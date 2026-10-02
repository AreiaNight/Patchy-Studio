#include "ui/color_wheel_panel.hpp"

#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/color_wheel_widget.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <functional>

#include <algorithm>

namespace patchy::ui {

// New (top) over previous (bottom) foreground color. The fills are the colors themselves
// (content); the frame is a theme role.
class ColorCompareSwatch final : public QWidget {
public:
  explicit ColorCompareSwatch(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("colorWheelCompareSwatch"));
    setCursor(Qt::PointingHandCursor);
  }

  void set_colors(QColor current, QColor previous) {
    current_ = current;
    previous_ = previous;
    update();
  }

  std::function<void()> previous_clicked;

  [[nodiscard]] QSize sizeHint() const override { return {58, 46}; }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath outline;
    outline.addRoundedRect(area, 4.0, 4.0);
    const auto half = area.height() / 2.0;
    painter.save();
    painter.setClipPath(outline);
    painter.fillRect(QRectF(area.left(), area.top(), area.width(), half), current_);
    painter.fillRect(QRectF(area.left(), area.top() + half, area.width(), area.height() - half), previous_);
    painter.restore();
    painter.setPen(theme().field_inset_border);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(area.left(), area.top() + half), QPointF(area.right(), area.top() + half));
    painter.drawPath(outline);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && event->position().y() >= height() / 2.0 && previous_clicked) {
      previous_clicked();
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

private:
  QColor current_{Qt::black};
  QColor previous_{Qt::black};
};

// A row of color cells: the harmony colors (read-only) or the saved-color slots (invalid =
// empty, drawn as an outlined well). Fills are content colors; outlines are theme roles.
class ColorSwatchStrip final : public QWidget {
public:
  ColorSwatchStrip(int cells, QWidget* parent) : QWidget(parent), colors_(static_cast<std::size_t>(cells)) {
    setMinimumHeight(20);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
  }

  void set_colors(std::vector<QColor> colors) {
    colors_ = std::move(colors);
    update();
  }
  // The cell drawn with the accent ring (the saved slot holding the current color), or -1.
  void set_marked(int index) {
    if (index != marked_) {
      marked_ = index;
      update();
    }
  }
  [[nodiscard]] int marked() const noexcept { return marked_; }
  [[nodiscard]] const std::vector<QColor>& colors() const noexcept { return colors_; }
  [[nodiscard]] QRect cell_rect(int index) const {
    const int count = std::max<int>(1, static_cast<int>(colors_.size()));
    const int gap = 3;
    const int cell = std::max(8, (width() - gap * (count - 1)) / count);
    return {index * (cell + gap), 0, cell, height() - 1};
  }
  [[nodiscard]] int cell_at(QPoint position) const {
    for (int index = 0; index < static_cast<int>(colors_.size()); ++index) {
      if (cell_rect(index).contains(position)) {
        return index;
      }
    }
    return -1;
  }

  std::function<void(int)> clicked;
  std::function<void(int, QPoint)> context_requested;

  [[nodiscard]] QSize sizeHint() const override { return {160, 22}; }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto& colors = theme();
    for (int index = 0; index < static_cast<int>(colors_.size()); ++index) {
      const QRectF box = QRectF(cell_rect(index)).adjusted(0.5, 0.5, -0.5, -0.5);
      const auto& color = colors_[static_cast<std::size_t>(index)];
      if (color.isValid()) {
        painter.setPen(colors.field_inset_border);
        painter.setBrush(color);
        painter.drawRoundedRect(box, 3.0, 3.0);
      } else {
        // An empty slot is a recessed well with a dashed edge, so it never reads as a dark color.
        painter.setPen(QPen(colors.field_inset_border, 1.0, Qt::DashLine));
        painter.setBrush(colors.field_bg);
        painter.drawRoundedRect(box, 3.0, 3.0);
      }
      if (index == marked_ || index == hovered_) {
        painter.setPen(QPen(index == marked_ ? colors.accent : colors.button_hover_border, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box.adjusted(1.0, 1.0, -1.0, -1.0), 2.5, 2.5);
      }
    }
  }
  void mouseMoveEvent(QMouseEvent* event) override {
    set_hovered(cell_at(event->position().toPoint()));
    QWidget::mouseMoveEvent(event);
  }
  void leaveEvent(QEvent* event) override {
    set_hovered(-1);
    QWidget::leaveEvent(event);
  }
  void mousePressEvent(QMouseEvent* event) override {
    const auto index = cell_at(event->position().toPoint());
    if (index < 0) {
      QWidget::mousePressEvent(event);
      return;
    }
    if (event->button() == Qt::LeftButton && clicked) {
      clicked(index);
    } else if (event->button() == Qt::RightButton && context_requested) {
      context_requested(index, event->globalPosition().toPoint());
    }
    event->accept();
  }

private:
  void set_hovered(int index) {
    if (index != hovered_) {
      hovered_ = index;
      update();
    }
  }

  std::vector<QColor> colors_;
  int marked_{-1};
  int hovered_{-1};
};

namespace {

QString harmony_token(ColorHarmony harmony) {
  switch (harmony) {
    case ColorHarmony::Complementary: return QStringLiteral("complementary");
    case ColorHarmony::Analogous: return QStringLiteral("analogous");
    case ColorHarmony::Triadic: return QStringLiteral("triadic");
    case ColorHarmony::SplitComplementary: return QStringLiteral("splitComplementary");
    case ColorHarmony::Rectangle: return QStringLiteral("rectangle");
    case ColorHarmony::Square: return QStringLiteral("square");
    case ColorHarmony::None: break;
  }
  return QStringLiteral("none");
}

ColorHarmony harmony_from_token(const QString& token) {
  for (const auto harmony : {ColorHarmony::Complementary, ColorHarmony::Analogous, ColorHarmony::Triadic,
                             ColorHarmony::SplitComplementary, ColorHarmony::Rectangle, ColorHarmony::Square}) {
    if (token == harmony_token(harmony)) {
      return harmony;
    }
  }
  return ColorHarmony::None;
}

QString shape_token(ColorWheelShape shape) {
  switch (shape) {
    case ColorWheelShape::Triangle: return QStringLiteral("triangle");
    case ColorWheelShape::Diamond: return QStringLiteral("diamond");
    case ColorWheelShape::Square: break;
  }
  return QStringLiteral("square");
}

ColorWheelShape shape_from_token(const QString& token) {
  if (token == QStringLiteral("triangle")) return ColorWheelShape::Triangle;
  if (token == QStringLiteral("diamond")) return ColorWheelShape::Diamond;
  return ColorWheelShape::Square;
}

}  // namespace

ColorWheelSettings load_color_wheel_settings() {
  const auto settings = app_settings();
  ColorWheelSettings result;
  result.shape = shape_from_token(settings.value(QStringLiteral("colorWheel/shape")).toString());
  result.model = settings.value(QStringLiteral("colorWheel/model")).toString() == QStringLiteral("ryb")
                     ? ColorWheelModel::Ryb
                     : ColorWheelModel::Rgb;
  result.tone_lock = settings.value(QStringLiteral("colorWheel/toneLock"), false).toBool();
  result.harmony = harmony_from_token(settings.value(QStringLiteral("colorWheel/harmony")).toString());
  result.harmony_spread = std::clamp(settings.value(QStringLiteral("colorWheel/harmonySpread"), kHarmonySpreadDefault).toDouble(),
                                     kHarmonySpreadMin, kHarmonySpreadMax);
  return result;
}

std::vector<QColor> load_saved_wheel_colors() {
  const auto stored = app_settings().value(QStringLiteral("colorWheel/savedColors")).toStringList();
  std::vector<QColor> colors(kSavedColorSlots);
  for (int index = 0; index < kSavedColorSlots && index < stored.size(); ++index) {
    const QColor color(stored[index]);
    if (!stored[index].isEmpty() && color.isValid()) {
      colors[static_cast<std::size_t>(index)] = color;
    }
  }
  return colors;
}

void save_saved_wheel_colors(const std::vector<QColor>& colors) {
  QStringList stored;
  for (int index = 0; index < kSavedColorSlots; ++index) {
    const auto color = index < static_cast<int>(colors.size()) ? colors[static_cast<std::size_t>(index)] : QColor();
    stored << (color.isValid() ? color.name(QColor::HexRgb) : QString());
  }
  app_settings().setValue(QStringLiteral("colorWheel/savedColors"), stored);
}

void save_color_wheel_settings(const ColorWheelSettings& value) {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("colorWheel/shape"), shape_token(value.shape));
  settings.setValue(QStringLiteral("colorWheel/model"),
                    value.model == ColorWheelModel::Ryb ? QStringLiteral("ryb") : QStringLiteral("rgb"));
  settings.setValue(QStringLiteral("colorWheel/toneLock"), value.tone_lock);
  settings.setValue(QStringLiteral("colorWheel/harmony"), harmony_token(value.harmony));
  settings.setValue(QStringLiteral("colorWheel/harmonySpread"), value.harmony_spread);
}

ColorWheelPanel::ColorWheelPanel(bool compact, QWidget* parent) : QWidget(parent) {
  setObjectName(compact ? QStringLiteral("colorWheelHudPanel") : QStringLiteral("colorWheelPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(compact ? 0 : 6, compact ? 0 : 6, compact ? 0 : 6, compact ? 0 : 6);
  layout->setSpacing(4);

  shape_combo_ = new QComboBox(this);
  shape_combo_->setObjectName(QStringLiteral("colorWheelShapeCombo"));
  shape_combo_->addItem(QString(), static_cast<int>(ColorWheelShape::Square));
  shape_combo_->addItem(QString(), static_cast<int>(ColorWheelShape::Triangle));
  shape_combo_->addItem(QString(), static_cast<int>(ColorWheelShape::Diamond));
  model_combo_ = new QComboBox(this);
  model_combo_->setObjectName(QStringLiteral("colorWheelModelCombo"));
  model_combo_->addItem(QString(), static_cast<int>(ColorWheelModel::Rgb));
  model_combo_->addItem(QString(), static_cast<int>(ColorWheelModel::Ryb));
  tone_lock_button_ = new QToolButton(this);
  tone_lock_button_->setObjectName(QStringLiteral("colorWheelToneLockButton"));
  tone_lock_button_->setCheckable(true);
  tone_lock_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  tone_lock_button_->setIcon(themed_glyph_icon(QStringLiteral("colorWheelToneLock"), 16, &ThemePalette::text_primary,
                                               [](QPainter& painter, const QColor& ink) {
                                                 // A padlock: shackle over a body.
                                                 painter.setRenderHint(QPainter::Antialiasing, true);
                                                 painter.setPen(QPen(ink, 1.5));
                                                 painter.setBrush(Qt::NoBrush);
                                                 painter.drawArc(QRectF(5.0, 2.5, 6.0, 7.0), 0, 180 * 16);
                                                 painter.drawLine(QPointF(5.0, 6.0), QPointF(5.0, 7.5));
                                                 painter.drawLine(QPointF(11.0, 6.0), QPointF(11.0, 7.5));
                                                 painter.setPen(Qt::NoPen);
                                                 painter.setBrush(ink);
                                                 painter.drawRoundedRect(QRectF(3.5, 7.5, 9.0, 6.5), 1.2, 1.2);
                                               }));
  harmony_combo_ = new QComboBox(this);
  harmony_combo_->setObjectName(QStringLiteral("colorWheelHarmonyCombo"));
  for (const auto harmony : {ColorHarmony::None, ColorHarmony::Complementary, ColorHarmony::Analogous,
                             ColorHarmony::Triadic, ColorHarmony::SplitComplementary, ColorHarmony::Rectangle,
                             ColorHarmony::Square}) {
    harmony_combo_->addItem(QString(), static_cast<int>(harmony));
  }
  // Two balanced rows: what the wheel shows (field shape, ring layout), then how it picks
  // (harmony, Tone Lock).
  auto* view_row = new QHBoxLayout();
  view_row->setSpacing(4);
  view_row->addWidget(shape_combo_, 1);
  view_row->addWidget(model_combo_, 1);
  layout->addLayout(view_row);
  auto* pick_row = new QHBoxLayout();
  pick_row->setSpacing(4);
  pick_row->addWidget(harmony_combo_, 1);
  pick_row->addWidget(tone_lock_button_);
  layout->addLayout(pick_row);
  if (compact) {
    shape_combo_->hide();
    model_combo_->hide();
    tone_lock_button_->hide();
    harmony_combo_->hide();
  }

  wheel_ = new ColorWheelWidget(this);
  wheel_->setObjectName(compact ? QStringLiteral("colorWheelHudWheel") : QStringLiteral("colorWheelWidget"));
  swatch_ = new ColorCompareSwatch(this);
  hex_edit_ = new QLineEdit(this);
  hex_edit_->setObjectName(QStringLiteral("colorWheelHexEdit"));
  hex_edit_->setAlignment(Qt::AlignCenter);
  hex_edit_->setMaxLength(7);
  hex_edit_->setFixedWidth(swatch_->sizeHint().width());
  hex_edit_->setValidator(
      new QRegularExpressionValidator(QRegularExpression(QStringLiteral("#?[0-9A-Fa-f]{0,6}")), hex_edit_));
  swatch_->setFixedWidth(swatch_->sizeHint().width());
  auto* body = new QHBoxLayout();
  body->setSpacing(6);
  body->addWidget(wheel_, 1);
  auto* side = new QVBoxLayout();
  side->setSpacing(4);
  side->addWidget(swatch_);
  side->addWidget(hex_edit_);
  side->addStretch(1);
  body->addLayout(side);
  layout->addLayout(body, 1);
  if (compact) {
    hex_edit_->hide();
  }

  // The harmony's colors, then the saved colors with their save button.
  harmony_strip_ = new ColorSwatchStrip(3, this);
  harmony_strip_->setObjectName(QStringLiteral("colorWheelHarmonyStrip"));
  layout->addWidget(harmony_strip_);
  auto* saved_row = new QHBoxLayout();
  saved_row->setSpacing(4);
  save_color_button_ = new QToolButton(this);
  save_color_button_->setObjectName(QStringLiteral("colorWheelSaveColorButton"));
  save_color_button_->setAutoRaise(true);
  save_color_button_->setIcon(themed_glyph_icon(QStringLiteral("colorWheelSaveColor"), 16, &ThemePalette::text_primary,
                                                [](QPainter& painter, const QColor& ink) {
                                                  painter.setPen(QPen(ink, 1.8));
                                                  painter.drawLine(QPointF(8.0, 3.5), QPointF(8.0, 12.5));
                                                  painter.drawLine(QPointF(3.5, 8.0), QPointF(12.5, 8.0));
                                                }));
  saved_strip_ = new ColorSwatchStrip(kSavedColorSlots, this);
  saved_strip_->setObjectName(QStringLiteral("colorWheelSavedColorsStrip"));
  saved_row->addWidget(save_color_button_);
  saved_row->addWidget(saved_strip_, 1);
  layout->addLayout(saved_row);
  retranslate();

  apply_settings(load_color_wheel_settings());
  reload_saved_colors();

  const auto controls_changed = [this] {
    const auto value = settings();
    wheel_->set_shape(value.shape);
    wheel_->set_model(value.model);
    wheel_->set_tone_lock(value.tone_lock);
    wheel_->set_harmony(value.harmony);
    save_color_wheel_settings(value);
    refresh_swatch();
    emit settings_changed();
  };
  connect(harmony_combo_, &QComboBox::currentIndexChanged, this, controls_changed);
  connect(wheel_, &ColorWheelWidget::harmony_spread_edited, this, [this](double, bool finished) {
    refresh_swatch();
    if (finished) {
      save_color_wheel_settings(settings());
      emit settings_changed();
    }
  });
  harmony_strip_->clicked = [this](int index) {
    const auto& colors = harmony_strip_->colors();
    if (index < 0 || index >= static_cast<int>(colors.size()) || !colors[static_cast<std::size_t>(index)].isValid()) {
      return;
    }
    // A harmony color becomes the foreground; the wheel follows through set_foreground.
    emit color_picked(colors[static_cast<std::size_t>(index)], true);
  };
  connect(save_color_button_, &QToolButton::clicked, this, [this] { save_current_color(); });
  saved_strip_->clicked = [this](int index) {
    const auto color = saved_colors_[static_cast<std::size_t>(index)];
    if (color.isValid()) {
      emit color_picked(color, true);
    } else {
      set_saved_color(index, current_);
    }
  };
  saved_strip_->context_requested = [this](int index, QPoint global_position) {
    QMenu menu(this);
    auto* replace = menu.addAction(tr("Save Current Color Here"));
    auto* remove = menu.addAction(tr("Remove"));
    remove->setEnabled(saved_colors_[static_cast<std::size_t>(index)].isValid());
    const auto* chosen = menu.exec(global_position);
    if (chosen == replace) {
      set_saved_color(index, current_);
    } else if (chosen == remove) {
      set_saved_color(index, QColor());
    }
  };
  connect(shape_combo_, &QComboBox::currentIndexChanged, this, controls_changed);
  connect(model_combo_, &QComboBox::currentIndexChanged, this, controls_changed);
  connect(tone_lock_button_, &QToolButton::toggled, this, controls_changed);
  connect(wheel_, &ColorWheelWidget::edit_started, this, [this] {
    previous_ = current_;
    editing_ = true;
  });
  connect(wheel_, &ColorWheelWidget::color_edited, this, [this](QColor color, bool finished) {
    current_ = color;
    if (finished) {
      editing_ = false;
    }
    refresh_swatch();
    emit color_picked(color, finished);
  });
  connect(hex_edit_, &QLineEdit::editingFinished, this, [this] {
    auto text = hex_edit_->text().trimmed();
    if (!text.startsWith(QLatin1Char('#'))) {
      text.prepend(QLatin1Char('#'));
    }
    // Three digits expand like CSS (#f80 is #ff8800); anything else incomplete reverts.
    if (text.size() == 4) {
      text = QStringLiteral("#%1%1%2%2%3%3").arg(text[1]).arg(text[2]).arg(text[3]);
    }
    const QColor color(text);
    if (text.size() != 7 || !color.isValid() || color == current_) {
      hex_edit_->setText(current_.name(QColor::HexRgb).toUpper());
      return;
    }
    previous_ = current_;
    current_ = color;
    wheel_->set_color(color);
    refresh_swatch();
    hex_edit_->setText(current_.name(QColor::HexRgb).toUpper());
    emit color_picked(color, true);
  });
  swatch_->previous_clicked = [this] {
    const auto restored = previous_;
    previous_ = current_;
    current_ = restored;
    wheel_->set_color(restored);
    refresh_swatch();
    emit color_picked(restored, true);
  };
  refresh_swatch();
}

void ColorWheelPanel::retranslate() {
  const QSignalBlocker shape_blocker(shape_combo_);
  const QSignalBlocker model_blocker(model_combo_);
  shape_combo_->setItemText(0, tr("Square"));
  shape_combo_->setItemText(1, tr("Triangle"));
  shape_combo_->setItemText(2, tr("Diamond"));
  shape_combo_->setToolTip(tr("Shape of the field inside the ring"));
  model_combo_->setItemText(0, tr("RGB"));
  model_combo_->setItemText(1, tr("Painter's RYB"));
  model_combo_->setToolTip(tr("Wheel layout: RGB, or the painter's red, yellow and blue wheel"));
  {
    const QSignalBlocker harmony_blocker(harmony_combo_);
    harmony_combo_->setItemText(0, tr("No Harmony"));
    harmony_combo_->setItemText(1, tr("Complementary"));
    harmony_combo_->setItemText(2, tr("Analogous"));
    harmony_combo_->setItemText(3, tr("Triadic"));
    harmony_combo_->setItemText(4, tr("Split Complementary"));
    harmony_combo_->setItemText(5, tr("Rectangle"));
    harmony_combo_->setItemText(6, tr("Square"));
    harmony_combo_->setToolTip(tr("Color harmony: markers on the ring follow the main color; drag an outer "
                                  "marker to widen or narrow analogous, split and rectangle harmonies"));
  }
  harmony_strip_->setToolTip(tr("Harmony colors. Click one to use it."));
  save_color_button_->setToolTip(tr("Save the current color"));
  saved_strip_->setToolTip(tr("Saved colors. Click one to use it, click an empty slot to save the current "
                              "color there, right-click for more."));
  hex_edit_->setToolTip(tr("Hex code of the new color. Type a code and press Enter to use it."));
  tone_lock_button_->setText(tr("Tone Lock"));
  tone_lock_button_->setToolTip(tr("Keep the lightness while you change the hue, so shadows stay shadows"));
  swatch_->setToolTip(
      tr("New color (top) and previous color (bottom). Click the previous color to go back to it."));
}

void ColorWheelPanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    retranslate();
  }
}

void ColorWheelPanel::set_foreground(QColor color) {
  if (!color.isValid()) {
    return;
  }
  color = color.toRgb();
  if (color == current_) {
    return;
  }
  // Our own drag echoes back through the foreground: keep the color it started from.
  if (!editing_) {
    previous_ = current_;
  }
  current_ = color;
  wheel_->set_color(color);
  refresh_swatch();
}

void ColorWheelPanel::apply_settings(const ColorWheelSettings& value) {
  const QSignalBlocker shape_blocker(shape_combo_);
  const QSignalBlocker model_blocker(model_combo_);
  const QSignalBlocker tone_blocker(tone_lock_button_);
  shape_combo_->setCurrentIndex(std::max(0, shape_combo_->findData(static_cast<int>(value.shape))));
  model_combo_->setCurrentIndex(std::max(0, model_combo_->findData(static_cast<int>(value.model))));
  tone_lock_button_->setChecked(value.tone_lock);
  {
    const QSignalBlocker harmony_blocker(harmony_combo_);
    harmony_combo_->setCurrentIndex(std::max(0, harmony_combo_->findData(static_cast<int>(value.harmony))));
  }
  wheel_->set_shape(value.shape);
  wheel_->set_model(value.model);
  wheel_->set_tone_lock(value.tone_lock);
  wheel_->set_harmony(value.harmony);
  wheel_->set_harmony_spread(value.harmony_spread);
  refresh_swatch();
}

ColorWheelSettings ColorWheelPanel::settings() const {
  ColorWheelSettings value;
  value.shape = static_cast<ColorWheelShape>(shape_combo_->currentData().toInt());
  value.model = static_cast<ColorWheelModel>(model_combo_->currentData().toInt());
  value.tone_lock = tone_lock_button_->isChecked();
  value.harmony = static_cast<ColorHarmony>(harmony_combo_->currentData().toInt());
  value.harmony_spread = wheel_->harmony_spread();
  return value;
}

void ColorWheelPanel::refresh_swatch() {
  swatch_->set_colors(current_, previous_);
  if (!hex_edit_->hasFocus()) {
    hex_edit_->setText(current_.name(QColor::HexRgb).toUpper());
  }
  refresh_saved_marker();
  if (harmony_strip_ != nullptr) {
    auto colors = wheel_->harmony_colors();
    harmony_strip_->setVisible(!colors.empty());
    harmony_strip_->set_colors(std::move(colors));
  }
}

void ColorWheelPanel::reload_saved_colors() {
  saved_colors_ = load_saved_wheel_colors();
  saved_strip_->set_colors(saved_colors_);
  refresh_saved_marker();
}

void ColorWheelPanel::refresh_saved_marker() {
  if (saved_strip_ == nullptr) {
    return;
  }
  const auto match = std::find(saved_colors_.begin(), saved_colors_.end(), current_);
  saved_strip_->set_marked(match != saved_colors_.end() ? static_cast<int>(match - saved_colors_.begin()) : -1);
}

void ColorWheelPanel::set_saved_color(int index, QColor color) {
  if (index < 0 || index >= kSavedColorSlots) {
    return;
  }
  saved_colors_[static_cast<std::size_t>(index)] = color.isValid() ? color.toRgb() : QColor();
  save_saved_wheel_colors(saved_colors_);
  saved_strip_->set_colors(saved_colors_);
  refresh_saved_marker();
  emit saved_colors_changed();
}

void ColorWheelPanel::save_current_color() {
  const auto empty = std::find_if(saved_colors_.begin(), saved_colors_.end(),
                                  [](const QColor& color) { return !color.isValid(); });
  if (empty != saved_colors_.end()) {
    set_saved_color(static_cast<int>(empty - saved_colors_.begin()), current_);
    return;
  }
  std::rotate(saved_colors_.begin(), saved_colors_.begin() + 1, saved_colors_.end());
  set_saved_color(kSavedColorSlots - 1, current_);
}

ColorWheelHud::ColorWheelHud(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("colorWheelHud"));
  setAttribute(Qt::WA_TranslucentBackground, true);
  setFocusPolicy(Qt::StrongFocus);
  auto settings = app_settings();
  size_ = std::clamp(settings.value(QStringLiteral("colorWheel/hudSize"), size_).toInt(), kMinimumSize, kMaximumSize);
  persistent_ = settings.value(QStringLiteral("colorWheel/hudPersistent"), false).toBool();

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 6, 10, 10);
  layout->setSpacing(2);
  auto* header = new QHBoxLayout();
  header->addStretch(1);
  pin_button_ = new QToolButton(this);
  pin_button_->setObjectName(QStringLiteral("colorWheelHudPinButton"));
  pin_button_->setCheckable(true);
  pin_button_->setChecked(persistent_);
  pin_button_->setAutoRaise(true);
  pin_button_->setIcon(themed_glyph_icon(QStringLiteral("colorWheelHudPin"), 16, &ThemePalette::text_primary,
                                         [](QPainter& painter, const QColor& ink) {
                                           painter.setPen(QPen(ink, 1.6));
                                           painter.setBrush(Qt::NoBrush);
                                           painter.drawEllipse(QPointF(8.0, 6.0), 3.2, 3.2);
                                           painter.drawLine(QPointF(8.0, 9.2), QPointF(8.0, 14.0));
                                         }));
  close_button_ = new QToolButton(this);
  close_button_->setObjectName(QStringLiteral("colorWheelHudCloseButton"));
  close_button_->setAutoRaise(true);
  close_button_->setIcon(themed_glyph_icon(QStringLiteral("colorWheelHudClose"), 16, &ThemePalette::text_primary,
                                           [](QPainter& painter, const QColor& ink) {
                                             painter.setPen(QPen(ink, 1.6));
                                             painter.drawLine(QPointF(4.5, 4.5), QPointF(11.5, 11.5));
                                             painter.drawLine(QPointF(11.5, 4.5), QPointF(4.5, 11.5));
                                           }));
  header->addWidget(pin_button_);
  header->addWidget(close_button_);
  layout->addLayout(header);
  panel_ = new ColorWheelPanel(true, this);
  layout->addWidget(panel_, 1);
  retranslate();

  connect(pin_button_, &QToolButton::toggled, this, [this](bool checked) { set_persistent(checked); });
  connect(close_button_, &QToolButton::clicked, this, &QWidget::hide);
  connect(panel_, &ColorWheelPanel::color_picked, this, [this](QColor, bool finished) {
    if (finished && !persistent_) {
      hide();
    }
  });
  resize(size_, size_);
  hide();
}

void ColorWheelHud::retranslate() {
  pin_button_->setToolTip(tr("Keep the wheel on screen between strokes"));
  close_button_->setToolTip(tr("Close"));
}

void ColorWheelHud::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    retranslate();
  }
}

void ColorWheelHud::set_persistent(bool persistent) {
  persistent_ = persistent;
  if (pin_button_->isChecked() != persistent) {
    const QSignalBlocker blocker(pin_button_);
    pin_button_->setChecked(persistent);
  }
  app_settings().setValue(QStringLiteral("colorWheel/hudPersistent"), persistent_);
}

void ColorWheelHud::set_hud_size(int size) {
  size = std::clamp(size, kMinimumSize, kMaximumSize);
  if (size == size_) {
    return;
  }
  const auto middle = geometry().center();
  size_ = size;
  resize(size_, size_);
  if (auto* host = parentWidget(); host != nullptr) {
    auto top_left = middle - QPoint(size_ / 2, size_ / 2);
    top_left.setX(std::clamp(top_left.x(), 0, std::max(0, host->width() - size_)));
    top_left.setY(std::clamp(top_left.y(), 0, std::max(0, host->height() - size_)));
    move(top_left);
  }
  app_settings().setValue(QStringLiteral("colorWheel/hudSize"), size_);
}

void ColorWheelHud::show_at(QPoint global_position) {
  auto* host = parentWidget();
  if (host == nullptr) {
    return;
  }
  resize(size_, size_);
  auto top_left = host->mapFromGlobal(global_position) - QPoint(size_ / 2, size_ / 2);
  top_left.setX(std::clamp(top_left.x(), 0, std::max(0, host->width() - size_)));
  top_left.setY(std::clamp(top_left.y(), 0, std::max(0, host->height() - size_)));
  move(top_left);
  show();
  raise();
  setFocus(Qt::PopupFocusReason);
}

void ColorWheelHud::paintEvent(QPaintEvent*) {
  // Glass: the panel surface at partial opacity so the artwork stays visible behind it.
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  auto fill = theme().panel_bg;
  fill.setAlpha(190);
  auto outline = theme().panel_border_strong;
  outline.setAlpha(220);
  painter.setPen(QPen(outline, 1.0));
  painter.setBrush(fill);
  painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14.0, 14.0);
}

void ColorWheelHud::wheelEvent(QWheelEvent* event) {
  const auto steps = event->angleDelta().y() / 120;
  if (steps != 0) {
    set_hud_size(size_ + steps * 20);
  }
  event->accept();
}

void ColorWheelHud::mousePressEvent(QMouseEvent* event) {
  // Clicks on the rim (outside the wheel and buttons) move the HUD.
  if (event->button() == Qt::LeftButton) {
    moving_ = true;
    drag_offset_ = event->position().toPoint();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void ColorWheelHud::mouseMoveEvent(QMouseEvent* event) {
  if (moving_ && parentWidget() != nullptr) {
    auto top_left = mapToParent(event->position().toPoint()) - drag_offset_;
    top_left.setX(std::clamp(top_left.x(), 0, std::max(0, parentWidget()->width() - width())));
    top_left.setY(std::clamp(top_left.y(), 0, std::max(0, parentWidget()->height() - height())));
    move(top_left);
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void ColorWheelHud::mouseReleaseEvent(QMouseEvent* event) {
  moving_ = false;
  QWidget::mouseReleaseEvent(event);
}

void ColorWheelHud::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    hide();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ColorWheelHud::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  qApp->installEventFilter(this);
}

void ColorWheelHud::hideEvent(QHideEvent* event) {
  qApp->removeEventFilter(this);
  moving_ = false;
  QWidget::hideEvent(event);
}

bool ColorWheelHud::eventFilter(QObject* watched, QEvent* event) {
  // Unless kept open, a press anywhere else dismisses the HUD and still reaches its target.
  if (!persistent_ && isVisible() &&
      (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TabletPress)) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr && widget != this && !isAncestorOf(widget)) {
      hide();
    }
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace patchy::ui
