#include "ui/studio_navigator.hpp"

#include "core/document.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/studio_shell.hpp"
#include "ui/studio_widgets.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

namespace patchy::ui {

namespace {

constexpr int kOverviewPollMs = 400;
constexpr double kZoomButtonFactor = 1.25;
constexpr double kWheelZoomFactor = 1.1;

}  // namespace

// The thumbnail with the viewport outline. A press or drag centers the view on
// the document point under the pointer; the wheel zooms.
class StudioNavigatorView final : public QWidget {
  Q_OBJECT

public:
  StudioNavigatorView(std::function<CanvasWidget*()> canvas, QWidget* parent)
      : QWidget(parent), canvas_(std::move(canvas)) {
    setObjectName(QStringLiteral("studioNavigatorView"));
    setCursor(Qt::OpenHandCursor);
    setMinimumSize(120, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  void set_overview(QImage image) {
    overview_ = std::move(image);
    update();
  }

  // Where the document sits inside the view, letterboxed to its aspect ratio.
  [[nodiscard]] QRectF image_rect() const {
    const auto* canvas = canvas_();
    const auto* document = canvas != nullptr ? canvas->document() : nullptr;
    const QRectF frame = QRectF(rect()).adjusted(6.0, 6.0, -6.0, -6.0);
    if (document == nullptr || document->width() <= 0 || document->height() <= 0 || frame.isEmpty()) {
      return {};
    }
    const double scale = std::min(frame.width() / document->width(), frame.height() / document->height());
    const QSizeF size(document->width() * scale, document->height() * scale);
    return QRectF(frame.center() - QPointF(size.width() / 2.0, size.height() / 2.0), size);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme().studio_row_bg);
    painter.drawRoundedRect(QRectF(rect()), 8.0, 8.0);

    const auto* canvas = canvas_();
    const auto* document = canvas != nullptr ? canvas->document() : nullptr;
    const auto target = image_rect();
    if (document == nullptr || target.isEmpty()) {
      return;
    }
    paint_studio_checkerboard(painter, target, 5);
    if (!overview_.isNull()) {
      painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
      painter.drawImage(target, overview_);
    }

    // The viewport, mapped from document space into the thumbnail. The part of
    // the frame outside it is dimmed so the visible region reads at a glance.
    const double scale = target.width() / document->width();
    QPolygonF viewport;
    for (const auto& point : canvas->visible_document_polygon()) {
      viewport << target.topLeft() + point * scale;
    }
    painter.save();
    painter.setClipRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0));
    QPainterPath outside;
    outside.addRect(target);
    QPainterPath inside;
    inside.addPolygon(viewport);
    inside.closeSubpath();
    painter.setBrush(theme().studio_shadow);
    painter.drawPath(outside.subtracted(inside));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(theme().studio_accent, 2.0));
    painter.drawPolygon(viewport);
    painter.restore();
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      QWidget::mousePressEvent(event);
      return;
    }
    dragging_ = true;
    setCursor(Qt::ClosedHandCursor);
    center_at(event->position());
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (dragging_ && (event->buttons() & Qt::LeftButton) != 0) {
      center_at(event->position());
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (dragging_ && event->button() == Qt::LeftButton) {
      dragging_ = false;
      setCursor(Qt::OpenHandCursor);
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void wheelEvent(QWheelEvent* event) override {
    auto* canvas = canvas_();
    const auto delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y();
    if (canvas != nullptr && canvas->document() != nullptr && delta != 0) {
      canvas->set_zoom_centered(canvas->zoom() * (delta > 0 ? kWheelZoomFactor : 1.0 / kWheelZoomFactor));
    }
    event->accept();
  }

private:
  void center_at(QPointF position) {
    auto* canvas = canvas_();
    const auto* document = canvas != nullptr ? canvas->document() : nullptr;
    const auto target = image_rect();
    if (document == nullptr || target.isEmpty()) {
      return;
    }
    const double scale = document->width() / target.width();
    const QPointF document_point = (position - target.topLeft()) * scale;
    canvas->center_view_on_document_point(
        QPointF(std::clamp(document_point.x(), 0.0, static_cast<double>(document->width())),
                std::clamp(document_point.y(), 0.0, static_cast<double>(document->height()))));
  }

  std::function<CanvasWidget*()> canvas_;
  QImage overview_;
  bool dragging_{false};
};

// A thin horizontal pill slider in the side bar's colors. The fill runs from
// `origin` (the minimum, or the center value of a two-sided range) to the thumb.
// A double-click asks for the reset value.
class StudioNavigatorSlider final : public QWidget {
  Q_OBJECT

public:
  explicit StudioNavigatorSlider(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(22);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void set_range(int minimum, int maximum, int origin) {
    minimum_ = minimum;
    maximum_ = std::max(minimum + 1, maximum);
    origin_ = std::clamp(origin, minimum_, maximum_);
    value_ = std::clamp(value_, minimum_, maximum_);
    update();
  }

  void set_value(int value) {  // no signal
    value = std::clamp(value, minimum_, maximum_);
    if (value != value_) {
      value_ = value;
      update();
    }
  }

  [[nodiscard]] int value() const noexcept { return value_; }

signals:
  void value_changed(int value);
  void reset_requested();

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto track = track_rect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme().studio_slider_track);
    painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
    const double thumb_x = x_for_value(value_);
    const double origin_x = x_for_value(origin_);
    painter.setBrush(theme().studio_slider_fill);
    painter.drawRoundedRect(QRectF(QPointF(std::min(thumb_x, origin_x), track.top()),
                                   QPointF(std::max(thumb_x, origin_x), track.bottom())),
                            track.height() / 2.0, track.height() / 2.0);
    painter.setBrush(theme().studio_slider_thumb);
    painter.setPen(QPen(theme().studio_bar_border, 1.0));
    painter.drawEllipse(QPointF(thumb_x, track.center().y()), 7.0, 7.0);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = true;
      drag_to(event->position().x());
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (dragging_) {
      drag_to(event->position().x());
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      dragging_ = false;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    emit reset_requested();
    event->accept();
  }

  void wheelEvent(QWheelEvent* event) override {
    const auto delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y();
    if (delta != 0) {
      const int step = std::max(1, (maximum_ - minimum_) / 50);
      apply(value_ + (delta > 0 ? step : -step));
    }
    event->accept();
  }

private:
  [[nodiscard]] QRectF track_rect() const {
    return QRectF(8.0, height() / 2.0 - 2.0, std::max(1.0, width() - 16.0), 4.0);
  }

  [[nodiscard]] double x_for_value(int value) const {
    const auto track = track_rect();
    return track.left() + track.width() * (value - minimum_) / static_cast<double>(maximum_ - minimum_);
  }

  void drag_to(double x) {
    const auto track = track_rect();
    const double t = std::clamp((x - track.left()) / track.width(), 0.0, 1.0);
    apply(minimum_ + static_cast<int>(std::lround(t * (maximum_ - minimum_))));
  }

  void apply(int value) {
    value = std::clamp(value, minimum_, maximum_);
    if (value == value_) {
      return;
    }
    value_ = value;
    update();
    emit value_changed(value_);
  }

  int minimum_{0};
  int maximum_{100};
  int origin_{0};
  int value_{0};
  bool dragging_{false};
};

namespace {

StudioIconButton* make_small_button(QWidget* parent, StudioIcon icon, const QString& tip, const char* name) {
  auto* button = new StudioIconButton(icon, parent);
  button->setObjectName(QLatin1String(name));
  button->setToolTip(tip);
  button->set_extent(26);
  return button;
}

}  // namespace

StudioNavigator::StudioNavigator(StudioShell& shell, QWidget* parent) : QWidget(parent), shell_(shell) {
  setObjectName(QStringLiteral("studioNavigator"));
  // Only the rounded surface paints; the shadow margin shows the canvas.
  setProperty("studioTransparent", true);
  setStyleSheet(QStringLiteral("QWidget[studioTransparent=\"true\"] { background: transparent; }"));

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(9, 8, 9, 11);
  auto* content = new QWidget(this);
  content->setObjectName(QStringLiteral("studioPanel"));
  set_themed_style(*content, studio_panel_qss() +
                                 QStringLiteral("QWidget#studioPanel QPushButton { text-align: center;"
                                                "  padding: 4px 8px; }"));
  outer->addWidget(content);
  auto* column = new QVBoxLayout(content);
  column->setContentsMargins(8, 4, 8, 6);
  column->setSpacing(4);

  auto* header = new QHBoxLayout;
  header->setSpacing(4);
  auto* title = new QLabel(tr("Navigator"), content);
  title->setProperty("studioRole", QStringLiteral("muted"));
  header->addWidget(title, 1);
  auto* close = make_small_button(content, StudioIcon::Close, tr("Hide the navigator"), "studioNavigatorClose");
  close->set_extent(22);
  connect(close, &QAbstractButton::clicked, this, [this] { emit close_requested(); });
  header->addWidget(close);
  column->addLayout(header);

  view_ = new StudioNavigatorView([this] { return canvas(); }, content);
  column->addWidget(view_, 1);

  auto* zoom_row = new QHBoxLayout;
  zoom_row->setSpacing(2);
  auto* zoom_out = make_small_button(content, StudioIcon::Minus, tr("Zoom out"), "studioNavigatorZoomOut");
  connect(zoom_out, &QAbstractButton::clicked, this, [this] { zoom_by(1.0 / kZoomButtonFactor); });
  zoom_row->addWidget(zoom_out);
  zoom_slider_ = new StudioNavigatorSlider(content);
  zoom_slider_->setObjectName(QStringLiteral("studioNavigatorZoomSlider"));
  zoom_slider_->setToolTip(tr("Zoom (double-click for 100%)"));
  zoom_slider_->set_range(0, kZoomSteps, 0);
  connect(zoom_slider_, &StudioNavigatorSlider::value_changed, this, [this](int position) {
    if (auto* canvas_widget = canvas(); canvas_widget != nullptr && !syncing_) {
      canvas_widget->set_zoom_centered(zoom_for_slider_position(position));
    }
  });
  connect(zoom_slider_, &StudioNavigatorSlider::reset_requested, this, [this] {
    if (auto* canvas_widget = canvas(); canvas_widget != nullptr) {
      canvas_widget->set_zoom_centered(1.0);
    }
  });
  zoom_row->addWidget(zoom_slider_, 1);
  auto* zoom_in = make_small_button(content, StudioIcon::Plus, tr("Zoom in"), "studioNavigatorZoomIn");
  connect(zoom_in, &QAbstractButton::clicked, this, [this] { zoom_by(kZoomButtonFactor); });
  zoom_row->addWidget(zoom_in);
  zoom_label_ = new QLabel(content);
  zoom_label_->setObjectName(QStringLiteral("studioNavigatorZoomLabel"));
  zoom_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  zoom_label_->setMinimumWidth(46);
  zoom_row->addWidget(zoom_label_);
  column->addLayout(zoom_row);

  auto* buttons = new QHBoxLayout;
  buttons->setSpacing(6);
  auto* fit = new QPushButton(tr("Fit"), content);
  fit->setObjectName(QStringLiteral("studioNavigatorFit"));
  fit->setToolTip(tr("Fit the artwork in the window"));
  fit->setCursor(Qt::PointingHandCursor);
  fit->setFocusPolicy(Qt::NoFocus);
  connect(fit, &QPushButton::clicked, this, [this] {
    if (auto* canvas_widget = canvas(); canvas_widget != nullptr) {
      canvas_widget->fit_to_view();
    }
  });
  buttons->addWidget(fit, 1);
  auto* actual = new QPushButton(tr("100%"), content);
  actual->setObjectName(QStringLiteral("studioNavigatorActualPixels"));
  actual->setToolTip(tr("Show the artwork at actual pixels"));
  actual->setCursor(Qt::PointingHandCursor);
  actual->setFocusPolicy(Qt::NoFocus);
  connect(actual, &QPushButton::clicked, this, [this] {
    if (auto* canvas_widget = canvas(); canvas_widget != nullptr) {
      canvas_widget->set_zoom_centered(1.0);
    }
  });
  buttons->addWidget(actual, 1);
  column->addLayout(buttons);

  poll_timer_ = new QTimer(this);
  poll_timer_->setInterval(kOverviewPollMs);
  connect(poll_timer_, &QTimer::timeout, this, [this] { poll_overview(); });
}

QSize StudioNavigator::sizeHint() const { return {244, 238}; }

int StudioNavigator::slider_position_for_zoom(double zoom) {
  const double minimum = CanvasWidget::minimum_zoom();
  const double maximum = CanvasWidget::maximum_zoom();
  const double t = std::log(std::clamp(zoom, minimum, maximum) / minimum) / std::log(maximum / minimum);
  return static_cast<int>(std::lround(t * kZoomSteps));
}

double StudioNavigator::zoom_for_slider_position(int position) {
  const double minimum = CanvasWidget::minimum_zoom();
  const double maximum = CanvasWidget::maximum_zoom();
  const double t = std::clamp(position, 0, kZoomSteps) / static_cast<double>(kZoomSteps);
  return minimum * std::pow(maximum / minimum, t);
}

CanvasWidget* StudioNavigator::canvas() const {
  auto* canvas_widget = shell_.canvas();
  return canvas_widget != nullptr && canvas_widget->document() != nullptr ? canvas_widget : nullptr;
}

void StudioNavigator::sync_from_canvas() {
  auto* canvas_widget = canvas();
  syncing_ = true;
  if (canvas_widget != nullptr) {
    zoom_slider_->set_value(slider_position_for_zoom(canvas_widget->zoom()));
    zoom_label_->setText(tr("%1%").arg(std::lround(canvas_widget->zoom() * 100.0)));
  } else {
    zoom_label_->clear();
  }
  syncing_ = false;
  view_->update();
}

void StudioNavigator::schedule_overview() {
  overview_dirty_ = true;
  if (isVisible()) {
    poll_overview();
  }
}

void StudioNavigator::poll_overview() {
  auto* canvas_widget = canvas();
  if (canvas_widget == nullptr) {
    if (overview_key_ != 0) {
      overview_key_ = 0;
      view_->set_overview({});
    }
    return;
  }
  // Never rescale the composite mid-stroke; the stroke's release refreshes it.
  if (canvas_widget->pointer_gesture_active()) {
    return;
  }
  const auto key = canvas_widget->overview_image_key();
  const auto* document = canvas_widget->document();
  const QSize document_size(document->width(), document->height());
  if (!overview_dirty_ && key == overview_key_ && document_size == overview_document_size_) {
    return;
  }
  overview_dirty_ = false;
  overview_key_ = key;
  overview_document_size_ = document_size;
  const auto bound = (view_->image_rect().size() * view_->devicePixelRatioF()).toSize();
  view_->set_overview(canvas_widget->overview_image(bound));
}

void StudioNavigator::zoom_by(double factor) {
  if (auto* canvas_widget = canvas(); canvas_widget != nullptr) {
    canvas_widget->set_zoom_centered(canvas_widget->zoom() * factor);
  }
}

void StudioNavigator::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  // Inset far enough that the drop shadow stays inside the widget.
  paint_studio_surface(painter, QRectF(rect()).adjusted(4, 3, -4, -6), 14.0, theme().studio_bar_bg,
                       theme().studio_bar_border);
}

void StudioNavigator::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  overview_dirty_ = true;
  sync_from_canvas();
  poll_overview();
  poll_timer_->start();
}

void StudioNavigator::hideEvent(QHideEvent* event) {
  QWidget::hideEvent(event);
  poll_timer_->stop();
}

}  // namespace patchy::ui

#include "studio_navigator.moc"
