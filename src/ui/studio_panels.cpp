#include "ui/studio_panels.hpp"

#include "core/brush_tip.hpp"
#include "core/document.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_wheel_panel.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/studio_shell.hpp"
#include "ui/studio_widgets.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <optional>

namespace patchy::ui {
namespace {

QLabel* make_title(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setProperty("studioRole", QStringLiteral("title"));
  return label;
}

QLabel* make_section(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text.toUpper(), parent);
  label->setProperty("studioRole", QStringLiteral("section"));
  return label;
}

QScrollArea* make_scroll(QWidget* parent, QWidget* body) {
  auto* scroll = new QScrollArea(parent);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidgetResizable(true);
  scroll->setWidget(body);
  scroll->viewport()->setAutoFillBackground(false);
  body->setAutoFillBackground(false);
  return scroll;
}

// A rounded, translucent context menu matching the panels.
QMenu* make_studio_menu(QWidget* parent) {
  auto* menu = new QMenu(parent);
  menu->setObjectName(QStringLiteral("studioMenu"));
  menu->setAttribute(Qt::WA_DeleteOnClose, true);
  menu->setAttribute(Qt::WA_TranslucentBackground, true);
  menu->setWindowFlag(Qt::NoDropShadowWindowHint, true);
  set_themed_style(*menu, QStringLiteral(
                              "QMenu#studioMenu { background: @studio_row_bg; color: @studio_text;"
                              "  border: 1px solid @studio_row_hover_bg; border-radius: 10px; padding: 6px; }"
                              "QMenu#studioMenu::item { padding: 7px 22px 7px 12px; border-radius: 6px; }"
                              "QMenu#studioMenu::item:selected { background: @studio_row_selected_bg;"
                              "  color: @studio_row_selected_text; }"
                              "QMenu#studioMenu::item:disabled { color: @studio_text_muted; }"
                              "QMenu#studioMenu::separator { height: 1px; background: @studio_row_hover_bg;"
                              "  margin: 4px 8px; }"));
  return menu;
}

QPixmap icon_pixmap(StudioIcon icon, int extent, const QColor& color, qreal dpr) {
  QPixmap pixmap(QSize(extent, extent) * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  paint_studio_icon(painter, icon, QRectF(0, 0, extent, extent), color);
  return pixmap;
}

// Rows that run a registered command use the command's own (already translated)
// menu text, so the studio adds no second copy of those strings.
QPushButton* make_command_row(StudioShell& shell, QWidget* parent, const QString& id, const QString& label = {}) {
  auto* action = shell.command(id);
  const auto text = !label.isEmpty() ? label : clean_action_text(action);
  auto* button = new QPushButton(QString(text).replace(QLatin1Char('&'), QStringLiteral("&&")), parent);
  button->setCursor(Qt::PointingHandCursor);
  button->setFocusPolicy(Qt::NoFocus);
  button->setEnabled(action != nullptr && action->isEnabled());
  if (action != nullptr && action->isCheckable()) {
    button->setCheckable(true);
    button->setChecked(action->isChecked());
  }
  if (action != nullptr) {
    const auto shortcut = action->shortcut().toString(QKeySequence::NativeText);
    if (!shortcut.isEmpty()) {
      button->setToolTip(shortcut);
    }
  }
  QObject::connect(button, &QPushButton::clicked, parent, [&shell, id] {
    shell.close_panels();
    shell.run_command(id);
  });
  return button;
}

// --- Brush Library ------------------------------------------------------------------

struct StrokePreviewKey {
  QString id;
  QRgb ink;
  bool operator==(const StrokePreviewKey&) const = default;
};

size_t qHash(const StrokePreviewKey& key, size_t seed = 0) { return ::qHash(key.id, seed) ^ key.ink; }

// One dab of the tip at `extent` px, tinted `ink`, alpha = tip coverage.
QImage dab_image(const BrushTip* tip, const QString& id, int extent, const QColor& ink) {
  QImage coverage;
  if (tip != nullptr && !tip->empty()) {
    QImage source(tip->width, tip->height, QImage::Format_Alpha8);
    for (int y = 0; y < tip->height; ++y) {
      std::copy_n(tip->mask.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(tip->width),
                  tip->width, source.scanLine(y));
    }
    coverage = source.scaled(QSize(extent, extent), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  } else {
    coverage = QImage(extent, extent, QImage::Format_Alpha8);
    coverage.fill(0);
    QPainter painter(&coverage);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    if (id == builtin_square_brush_tip_id()) {
      painter.setBrush(QColor(0, 0, 0, 255));
      painter.drawRect(QRectF(extent * 0.15, extent * 0.15, extent * 0.7, extent * 0.7));
    } else {
      QRadialGradient gradient(QPointF(extent / 2.0, extent / 2.0), extent / 2.0);
      gradient.setColorAt(0.0, QColor(0, 0, 0, 255));
      gradient.setColorAt(0.7, QColor(0, 0, 0, 255));
      gradient.setColorAt(1.0, QColor(0, 0, 0, 0));
      painter.setBrush(gradient);
      painter.drawEllipse(QRectF(0, 0, extent, extent));
    }
  }
  QImage dab(coverage.size(), QImage::Format_ARGB32_Premultiplied);
  dab.fill(ink);
  QPainter painter(&dab);
  painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  painter.drawImage(0, 0, coverage);
  return dab;
}

// The library row's sample: the tip stamped along a soft S-curve with a pressure taper.
QImage stroke_preview(const BrushTipLibrary& library, const QString& id, QSize size, const QColor& ink) {
  static QHash<StrokePreviewKey, QImage> cache;
  const StrokePreviewKey key{id, ink.rgba()};
  if (const auto found = cache.constFind(key); found != cache.constEnd()) {
    return *found;
  }
  const auto tip = is_builtin_brush_tip_id(id) ? nullptr : library.tip(id);
  const auto* entry = library.find_entry(id);
  const double spacing = std::clamp(entry != nullptr ? entry->spacing : 0.12, 0.04, 1.0);
  const int extent = 22;
  const QImage dab = dab_image(tip.get(), id, extent, ink);
  QImage image(size * 2, QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(2.0);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  const double margin = 18.0;
  const double length = size.width() - 2 * margin;
  const double step = std::max(1.0, extent * spacing * 0.6);
  for (double x = 0.0; x <= length; x += step) {
    const double t = x / length;
    const double pressure = std::pow(std::sin(t * std::numbers::pi), 0.6);
    const double y = size.height() / 2.0 + std::sin(t * 2.0 * std::numbers::pi) * size.height() * 0.22;
    const double scale = 0.25 + 0.75 * pressure;
    const double side = extent * scale;
    painter.setOpacity(0.35 + 0.65 * pressure);
    painter.drawImage(QRectF(margin + x - side / 2.0, y - side / 2.0, side, side), dab);
  }
  painter.end();
  cache.insert(key, image);
  return image;
}

constexpr int kBrushRowHeight = 62;
constexpr int kBrushIdRole = Qt::UserRole + 1;

class BrushRowDelegate final : public QStyledItemDelegate {
public:
  BrushRowDelegate(const BrushTipLibrary& library, QObject* parent) : QStyledItemDelegate(parent), library_(library) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    const auto& palette = theme();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    const QRectF row = QRectF(option.rect).adjusted(2, 2, -2, -2);
    if (selected || hovered) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(selected ? palette.studio_row_selected_bg : palette.studio_row_hover_bg);
      painter->drawRoundedRect(row, 8, 8);
    }
    const QColor ink = selected ? palette.studio_row_selected_text : palette.studio_text;
    painter->setPen(ink);
    painter->setFont(studio_scaled_font(option.font, 0.92, QFont::Normal));
    painter->drawText(row.adjusted(12, 4, -8, 0).toRect(), Qt::AlignLeft | Qt::AlignTop, index.data().toString());
    const auto id = index.data(kBrushIdRole).toString();
    const QSize preview_size(static_cast<int>(row.width()) - 12, static_cast<int>(row.height()) - 18);
    const auto preview = stroke_preview(library_, id, preview_size, ink);
    painter->drawImage(QPointF(row.left() + 6, row.top() + 16), preview);
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    return {QStyledItemDelegate::sizeHint(option, index).width(), kBrushRowHeight};
  }

private:
  const BrushTipLibrary& library_;
};

class StudioBrushLibraryPanel final : public StudioPanel {
  Q_OBJECT

public:
  StudioBrushLibraryPanel(StudioShell& shell, QWidget* parent) : StudioPanel(shell, parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 4, 6, 4);
    column->setSpacing(8);
    auto* header = new QHBoxLayout;
    header->addWidget(make_title(tr("Brush Library"), this));
    header->addStretch(1);
    auto* import_button = new StudioIconButton(StudioIcon::Import, this);
    import_button->setToolTip(tr("Import brushes (.abr)"));
    import_button->set_extent(32);
    auto* settings_button = new StudioIconButton(StudioIcon::Prefs, this);
    settings_button->setToolTip(tr("Brush settings"));
    settings_button->set_extent(32);
    header->addWidget(import_button);
    header->addWidget(settings_button);
    column->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(10);
    sets_ = new QListWidget(this);
    sets_->setObjectName(QStringLiteral("studioBrushSets"));
    sets_->setFixedWidth(150);
    brushes_ = new QListWidget(this);
    brushes_->setObjectName(QStringLiteral("studioBrushList"));
    brushes_->setItemDelegate(new BrushRowDelegate(shell_.brush_library(), brushes_));
    brushes_->setMouseTracking(true);
    brushes_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    body->addWidget(sets_);
    body->addWidget(brushes_, 1);
    column->addLayout(body, 1);

    connect(sets_, &QListWidget::currentRowChanged, this, [this] { fill_brushes(); });
    connect(brushes_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
      shell_.select_brush_tip(item->data(kBrushIdRole).toString());
    });
    connect(import_button, &QAbstractButton::clicked, this, [this] { shell_.import_brushes(); });
    connect(settings_button, &QAbstractButton::clicked, this, [this] { shell_.open_brush_settings(); });
    fill_sets();
  }

  [[nodiscard]] QSize preferred_size() const override { return {470, 520}; }

  void refresh_from_editor() override {
    const auto active = shell_.active_brush_tip_id();
    for (int row = 0; row < brushes_->count(); ++row) {
      auto* item = brushes_->item(row);
      if (item->data(kBrushIdRole).toString() == active && !item->isSelected()) {
        QSignalBlocker blocker(brushes_);
        brushes_->setCurrentItem(item);
      }
    }
  }

private:
  void fill_sets() {
    const auto& library = shell_.brush_library();
    QStringList folders;
    for (const auto& entry : library.entries()) {
      if (!folders.contains(entry.folder)) {
        folders.append(entry.folder);
      }
    }
    sets_->clear();
    auto* all = new QListWidgetItem(tr("All brushes"), sets_);
    all->setData(Qt::UserRole, QVariant());
    auto* basic = new QListWidgetItem(tr("Basic"), sets_);
    basic->setData(Qt::UserRole, QString());
    for (const auto& folder : folders) {
      if (folder.isEmpty()) {
        continue;
      }
      auto* item = new QListWidgetItem(folder, sets_);
      item->setData(Qt::UserRole, folder);
    }
    // Open on the set that holds the active brush.
    int initial = 0;
    const auto active = shell_.active_brush_tip_id();
    if (const auto* entry = library.find_entry(active); entry != nullptr) {
      for (int row = 1; row < sets_->count(); ++row) {
        if (sets_->item(row)->data(Qt::UserRole).toString() == entry->folder) {
          initial = row;
          break;
        }
      }
    } else if (is_builtin_brush_tip_id(active)) {
      initial = 1;
    }
    sets_->setCurrentRow(initial);
  }

  void fill_brushes() {
    const auto& library = shell_.brush_library();
    auto* set_item = sets_->currentItem();
    const auto filter = set_item != nullptr ? set_item->data(Qt::UserRole) : QVariant();
    brushes_->clear();
    const auto add = [this](const QString& name, const QString& id) {
      auto* item = new QListWidgetItem(name, brushes_);
      item->setData(kBrushIdRole, id);
    };
    const bool all = !filter.isValid();
    const auto folder = filter.toString();
    if (all || folder.isEmpty()) {
      add(tr("Round"), builtin_round_brush_tip_id());
      add(tr("Square"), builtin_square_brush_tip_id());
    }
    for (const auto& entry : library.entries()) {
      if (all || entry.folder == folder) {
        add(entry.name, entry.id);
      }
    }
    refresh_from_editor();
  }

  QListWidget* sets_{nullptr};
  QListWidget* brushes_{nullptr};
};

// --- Layers -------------------------------------------------------------------------

QString blend_abbreviation(BlendMode mode) {
  if (mode == BlendMode::Normal) {
    return QStringLiteral("N");
  }
  const auto name = blend_mode_name(mode);
  QString letters;
  for (const auto& word : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
    letters += word.at(0).toUpper();
    if (letters.size() == 2) {
      break;
    }
  }
  return letters.isEmpty() ? name.left(1) : letters;
}

struct LayerRowInfo {
  LayerId id{};
  QString name;
  bool visible{true};
  bool group{false};
  bool collapsed{false};
  bool active{false};
  bool selected{false};
  int depth{0};
  BlendMode blend{BlendMode::Normal};
  int opacity{100};
};

class LayerRow final : public QWidget {
public:
  LayerRow(StudioShell& shell, const Layer& layer, LayerRowInfo info, QWidget* parent)
      : QWidget(parent), shell_(shell), info_(std::move(info)) {
    setFixedHeight(58);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    thumbnail_ = shell_.layer_thumbnail(layer);
  }

  std::function<void()> on_blend_clicked;
  std::function<void()> on_active_clicked;

  [[nodiscard]] const LayerRowInfo& info() const noexcept { return info_; }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const auto& palette = theme();
    const QRectF row = QRectF(rect()).adjusted(1, 2, -1, -2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(info_.active ? palette.studio_row_selected_bg
                                  : (hovered_ ? palette.studio_row_hover_bg : palette.studio_row_bg));
    painter.drawRoundedRect(row, 9, 9);
    const QColor ink = info_.active ? palette.studio_row_selected_text : palette.studio_text;
    const QColor muted = info_.active ? palette.studio_row_selected_text : palette.studio_text_muted;
    const qreal indent = 10.0 + info_.depth * 16.0;
    qreal x = row.left() + indent;
    if (info_.group) {
      // Disclosure chevron.
      painter.setPen(QPen(muted, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      const QPointF center(x + 6, row.center().y());
      QPainterPath chevron;
      if (info_.collapsed) {
        chevron.moveTo(center + QPointF(-2, -4));
        chevron.lineTo(center + QPointF(2, 0));
        chevron.lineTo(center + QPointF(-2, 4));
      } else {
        chevron.moveTo(center + QPointF(-4, -2));
        chevron.lineTo(center + QPointF(0, 2));
        chevron.lineTo(center + QPointF(4, -2));
      }
      painter.drawPath(chevron);
      chevron_rect_ = QRectF(x - 4, row.top(), 22, row.height());
      x += 18;
    }
    const QRectF thumb(x, row.top() + 6, 54, row.height() - 12);
    if (info_.group) {
      painter.setPen(QPen(muted, 1.4));
      painter.setBrush(Qt::NoBrush);
      QPainterPath folder;
      const QRectF body = thumb.adjusted(10, 8, -10, -6);
      folder.moveTo(body.bottomLeft());
      folder.lineTo(body.left(), body.top());
      folder.lineTo(body.left() + body.width() * 0.4, body.top());
      folder.lineTo(body.left() + body.width() * 0.5, body.top() + 4);
      folder.lineTo(body.right(), body.top() + 4);
      folder.lineTo(body.bottomRight());
      folder.closeSubpath();
      painter.drawPath(folder);
    } else {
      QPainterPath clip;
      clip.addRoundedRect(thumb, 4, 4);
      painter.save();
      painter.setClipPath(clip);
      paint_studio_checkerboard(painter, thumb, 5);
      if (!thumbnail_.isNull()) {
        const QSizeF scaled = QSizeF(thumbnail_.deviceIndependentSize()).scaled(thumb.size(), Qt::KeepAspectRatio);
        painter.drawPixmap(QRectF(QPointF(thumb.center().x() - scaled.width() / 2.0,
                                          thumb.center().y() - scaled.height() / 2.0),
                                  scaled),
                           thumbnail_, QRectF(thumbnail_.rect()));
      }
      painter.restore();
    }
    x = thumb.right() + 10;

    // Right side: the blend-mode letter and the visibility check.
    check_rect_ = QRectF(row.right() - 34, row.center().y() - 11, 22, 22);
    blend_rect_ = QRectF(check_rect_.left() - 34, row.center().y() - 12, 28, 24);
    painter.setPen(QPen(info_.visible ? ink : muted, 1.6));
    painter.setBrush(info_.visible ? (info_.active ? palette.studio_row_selected_text : palette.studio_accent)
                                   : QBrush(Qt::NoBrush));
    painter.drawRoundedRect(check_rect_, 5, 5);
    if (info_.visible) {
      painter.setPen(QPen(info_.active ? palette.studio_row_selected_bg : palette.studio_row_selected_text, 2.0,
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      QPainterPath tick;
      tick.moveTo(check_rect_.left() + 5.5, check_rect_.center().y() + 0.5);
      tick.lineTo(check_rect_.left() + 9.5, check_rect_.bottom() - 6);
      tick.lineTo(check_rect_.right() - 5, check_rect_.top() + 6);
      painter.drawPath(tick);
    }
    auto bold = font();
    bold.setWeight(QFont::DemiBold);
    painter.setFont(bold);
    painter.setPen(ink);
    painter.drawText(blend_rect_, Qt::AlignCenter, blend_abbreviation(info_.blend));

    const QRectF text_rect(x, row.top() + 8, blend_rect_.left() - x - 6, row.height() - 16);
    painter.setFont(info_.active ? bold : font());
    painter.setPen(info_.visible ? ink : muted);
    const QFontMetrics metrics(painter.font());
    painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignTop,
                     metrics.elidedText(info_.name, Qt::ElideRight, static_cast<int>(text_rect.width())));
    if (info_.opacity < 100) {
      painter.setFont(font());
      painter.setPen(muted);
      painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignBottom, QStringLiteral("%1%").arg(info_.opacity));
    }
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      QWidget::mousePressEvent(event);
      return;
    }
    const auto position = event->position();
    if (check_rect_.adjusted(-6, -6, 6, 6).contains(position)) {
      shell_.set_layer_visible(info_.id, !info_.visible);
    } else if (blend_rect_.adjusted(-4, -6, 4, 6).contains(position)) {
      if (!info_.active) {
        shell_.select_layer(info_.id);
      }
      if (on_blend_clicked) {
        on_blend_clicked();
      }
    } else if (info_.group && chevron_rect_.contains(position)) {
      shell_.toggle_group_collapsed(info_.id);
    } else if (info_.active && on_active_clicked) {
      on_active_clicked();
    } else {
      shell_.select_layer(info_.id);
    }
    event->accept();
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (!check_rect_.contains(event->position()) && !blend_rect_.contains(event->position())) {
      shell_.rename_layer(info_.id);
    }
    event->accept();
  }

  void enterEvent(QEnterEvent* event) override {
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
  }

  void leaveEvent(QEvent* event) override {
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
  }

private:
  StudioShell& shell_;
  LayerRowInfo info_;
  QPixmap thumbnail_;
  bool hovered_{false};
  QRectF check_rect_;
  QRectF blend_rect_;
  QRectF chevron_rect_;
};

class StudioLayersPanel final : public StudioPanel {
  Q_OBJECT

public:
  StudioLayersPanel(StudioShell& shell, QWidget* parent) : StudioPanel(shell, parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 4, 6, 4);
    column->setSpacing(8);
    auto* header = new QHBoxLayout;
    header->addWidget(make_title(tr("Layers"), this));
    header->addStretch(1);
    auto* add_button = new StudioIconButton(StudioIcon::Plus, this);
    add_button->setObjectName(QStringLiteral("studioAddLayerButton"));
    add_button->setToolTip(tr("New layer"));
    add_button->set_extent(34);
    header->addWidget(add_button);
    column->addLayout(header);
    body_ = new QWidget;
    rows_ = new QVBoxLayout(body_);
    rows_->setContentsMargins(0, 0, 4, 0);
    rows_->setSpacing(2);
    rows_->addStretch(1);
    column->addWidget(make_scroll(this, body_), 1);
    connect(add_button, &QAbstractButton::clicked, this, [this] { shell_.add_layer(); });
    refresh_from_editor();
  }

  [[nodiscard]] QSize preferred_size() const override { return {340, 540}; }

  void refresh_from_editor() override {
    const auto* document = shell_.document();
    std::vector<std::pair<const Layer*, LayerRowInfo>> infos;
    if (document != nullptr) {
      const auto selected = shell_.selected_layer_ids();
      const auto active = document->active_layer_id();
      collect(document->layers(), 0, selected, active, infos);
    }
    // Rebuild only when something a row shows has changed (or a thumbnail may have).
    QString signature;
    for (const auto& [layer, info] : infos) {
      signature += QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8:%9;")
                       .arg(info.id)
                       .arg(info.name)
                       .arg(info.visible)
                       .arg(info.active)
                       .arg(info.collapsed)
                       .arg(static_cast<int>(info.blend))
                       .arg(info.opacity)
                       .arg(info.depth)
                       .arg(layer->render_revision());
    }
    signature += QString::number(expanded_blend_ ? *expanded_blend_ : 0);
    if (signature == signature_) {
      return;
    }
    signature_ = signature;
    while (rows_->count() > 1) {
      auto* item = rows_->takeAt(0);
      if (auto* widget = item->widget(); widget != nullptr) {
        widget->deleteLater();
      }
      delete item;
    }
    int insert_at = 0;
    for (const auto& [layer, info] : infos) {
      auto* row = new LayerRow(shell_, *layer, info, body_);
      const LayerId id = info.id;
      row->on_blend_clicked = [this, id] {
        expanded_blend_ = expanded_blend_ == id ? std::optional<LayerId>() : std::optional<LayerId>(id);
        signature_.clear();
        refresh_from_editor();
      };
      row->on_active_clicked = [this, row] { show_layer_menu(row); };
      rows_->insertWidget(insert_at++, row);
      if (expanded_blend_ == id && info.active) {
        rows_->insertWidget(insert_at++, make_blend_editor(info));
      }
    }
  }

private:
  void collect(const std::vector<Layer>& layers, int depth, const std::vector<LayerId>& selected,
               std::optional<LayerId> active, std::vector<std::pair<const Layer*, LayerRowInfo>>& out) {
    // Document order is bottom to top; the panel lists the top layer first.
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
      const auto& layer = *it;
      LayerRowInfo info;
      info.id = layer.id();
      info.name = QString::fromStdString(layer.name());
      info.visible = layer.visible();
      info.group = layer.kind() == LayerKind::Group;
      info.collapsed = info.group && shell_.group_collapsed(layer.id());
      info.active = active.has_value() && *active == layer.id();
      info.selected = std::find(selected.begin(), selected.end(), layer.id()) != selected.end();
      info.depth = depth;
      info.blend = layer.blend_mode();
      info.opacity = static_cast<int>(std::lround(layer.opacity() * 100.0f));
      out.emplace_back(&layer, info);
      if (info.group && !info.collapsed) {
        collect(layer.children(), depth + 1, selected, active, out);
      }
    }
  }

  QWidget* make_blend_editor(const LayerRowInfo& info) {
    auto* editor = new QWidget(body_);
    auto* grid = new QVBoxLayout(editor);
    grid->setContentsMargins(10, 4, 10, 8);
    grid->setSpacing(6);
    auto* opacity_row = new QHBoxLayout;
    auto* opacity_label = new QLabel(tr("Opacity"), editor);
    opacity_label->setProperty("studioRole", QStringLiteral("muted"));
    auto* slider = new QSlider(Qt::Horizontal, editor);
    slider->setRange(0, 100);
    slider->setValue(info.opacity);
    auto* value = new QLabel(QStringLiteral("%1%").arg(info.opacity), editor);
    value->setMinimumWidth(38);
    opacity_row->addWidget(opacity_label);
    opacity_row->addWidget(slider, 1);
    opacity_row->addWidget(value);
    grid->addLayout(opacity_row);
    auto* blend = new QComboBox(editor);
    add_blend_mode_items(blend);
    blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(info.blend))));
    grid->addWidget(blend);
    set_themed_style(*slider, QStringLiteral(
                                  "QSlider::groove:horizontal { height: 4px; background: @studio_row_hover_bg;"
                                  "  border-radius: 2px; }"
                                  "QSlider::sub-page:horizontal { background: @studio_accent; border-radius: 2px; }"
                                  "QSlider::handle:horizontal { width: 16px; height: 16px; margin: -6px 0px;"
                                  "  border-radius: 8px; background: @studio_slider_thumb; }"));
    connect(slider, &QSlider::valueChanged, this, [this, value](int percent) {
      value->setText(QStringLiteral("%1%").arg(percent));
      shell_.set_active_layer_opacity(percent);
    });
    connect(blend, &QComboBox::currentIndexChanged, this, [this, blend](int index) {
      const auto data = blend->itemData(index);
      if (data.isValid()) {
        shell_.set_active_layer_blend_mode(static_cast<BlendMode>(data.toInt()));
      }
    });
    return editor;
  }

  void show_layer_menu(QWidget* row) {
    auto* menu = make_studio_menu(this);
    const auto add_command = [this, menu](const QString& id) {
      if (auto* action = shell_.command(id); action != nullptr) {
        auto* item = menu->addAction(clean_action_text(action));
        item->setEnabled(action->isEnabled());
        connect(item, &QAction::triggered, this, [this, id] { shell_.run_command(id); });
      }
    };
    add_command(QStringLiteral("layer.rename"));
    add_command(QStringLiteral("select.layer_transparency"));
    add_command(QStringLiteral("edit.copy"));
    add_command(QStringLiteral("layer.fill"));
    add_command(QStringLiteral("layer.clear"));
    menu->addSeparator();
    add_command(QStringLiteral("layer.add_mask"));
    add_command(QStringLiteral("layer.toggle_clipping_mask"));
    add_command(QStringLiteral("layer.styles"));
    menu->addSeparator();
    add_command(QStringLiteral("layer.merge_down"));
    add_command(QStringLiteral("layer.duplicate"));
    add_command(QStringLiteral("layer.move_up"));
    add_command(QStringLiteral("layer.move_down"));
    add_command(QStringLiteral("layer.new_folder"));
    menu->addSeparator();
    add_command(QStringLiteral("layer.delete"));
    menu->popup(row->mapToGlobal(QPoint(row->width() - 8, 8)));
  }

  QWidget* body_{nullptr};
  QVBoxLayout* rows_{nullptr};
  QString signature_;
  std::optional<LayerId> expanded_blend_;
};

// --- Color --------------------------------------------------------------------------

class HistorySwatch final : public QAbstractButton {
public:
  HistorySwatch(QColor color, QWidget* parent) : QAbstractButton(parent), color_(color) {
    setFixedSize(26, 26);
    setCursor(Qt::PointingHandCursor);
    setToolTip(color.name(QColor::HexRgb).toUpper());
  }
  [[nodiscard]] QColor color() const noexcept { return color_; }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(theme().studio_panel_border, 1.0));
    painter.setBrush(color_);
    painter.drawEllipse(QRectF(rect()).adjusted(2, 2, -2, -2));
  }

private:
  QColor color_;
};

class StudioColorPanel final : public StudioPanel {
  Q_OBJECT

public:
  StudioColorPanel(StudioShell& shell, QWidget* parent) : StudioPanel(shell, parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 4, 6, 4);
    column->setSpacing(8);
    column->addWidget(make_title(tr("Colors"), this));
    wheel_ = new ColorWheelPanel(false, this);
    wheel_->setObjectName(QStringLiteral("studioColorWheel"));
    wheel_->set_foreground(shell_.primary_color());
    column->addWidget(wheel_, 1);
    column->addWidget(make_section(tr("History"), this));
    history_row_ = new QHBoxLayout;
    history_row_->setSpacing(4);
    column->addLayout(history_row_);
    connect(wheel_, &ColorWheelPanel::color_picked, this,
            [this](QColor color, bool finished) { shell_.set_primary_color(color, finished); });
    rebuild_history();
  }

  [[nodiscard]] QSize preferred_size() const override { return {360, 560}; }

  void refresh_from_editor() override {
    const auto color = shell_.primary_color();
    if (color != wheel_->current_color()) {
      wheel_->set_foreground(color);
    }
    rebuild_history();
  }

private:
  void rebuild_history() {
    const auto& history = shell_.color_history();
    QString signature;
    for (const auto& color : history) {
      signature += color.name();
    }
    if (signature == history_signature_ && history_row_->count() > 0) {
      return;
    }
    history_signature_ = signature;
    while (auto* item = history_row_->takeAt(0)) {
      if (auto* widget = item->widget(); widget != nullptr) {
        widget->deleteLater();
      }
      delete item;
    }
    for (const auto& color : history) {
      auto* swatch = new HistorySwatch(color, this);
      connect(swatch, &QAbstractButton::clicked, this, [this, color] {
        shell_.set_primary_color(color, true);
        wheel_->set_foreground(color);
      });
      history_row_->addWidget(swatch);
    }
    if (history.empty()) {
      auto* hint = new QLabel(tr("Colors you paint with appear here."), this);
      hint->setProperty("studioRole", QStringLiteral("muted"));
      history_row_->addWidget(hint);
    }
    history_row_->addStretch(1);
  }

  ColorWheelPanel* wheel_{nullptr};
  QHBoxLayout* history_row_{nullptr};
  QString history_signature_;
};

// --- Actions ------------------------------------------------------------------------

class StudioActionsPanel final : public StudioPanel {
  Q_OBJECT

public:
  StudioActionsPanel(StudioShell& shell, QWidget* parent) : StudioPanel(shell, parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 4, 6, 4);
    column->setSpacing(8);
    column->addWidget(make_title(tr("Actions"), this));
    auto* tabs = new QHBoxLayout;
    tabs->setSpacing(2);
    column->addLayout(tabs);
    pages_ = new QStackedWidget(this);
    column->addWidget(pages_, 1);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);

    struct Tab {
      StudioIcon icon;
      QString label;
      std::function<void(QVBoxLayout*, QWidget*)> fill;
    };
    const std::vector<Tab> tab_specs = {
        {StudioIcon::Plus, tr("Add"),
         [this](QVBoxLayout* list, QWidget* page) {
           for (const auto* id : {"file.place_embedded", "file.import_files_as_layers", "edit.cut", "edit.copy",
                                  "edit.copy_merged", "edit.paste", "edit.paste_in_place"}) {
             list->addWidget(make_command_row(shell_, page, QLatin1String(id)));
           }
           list->addWidget(make_command_row(shell_, page, QStringLiteral("tools.type"), tr("Add text")));
         }},
        {StudioIcon::Canvas, tr("Canvas"),
         [this](QVBoxLayout* list, QWidget* page) {
           for (const auto* id : {"tools.crop", "image.size", "image.canvas_size", "image.rotate_cw", "image.rotate_ccw",
                                  "view.grid", "view.rulers", "view.fit_on_screen", "view.actual_pixels"}) {
             list->addWidget(make_command_row(shell_, page, QLatin1String(id)));
           }
         }},
        {StudioIcon::Share, tr("Share"),
         [this](QVBoxLayout* list, QWidget* page) {
           for (const auto* id : {"file.save", "file.save_as", "file.export_flat", "file.export_multipage_pdf",
                                  "file.export_animated_gif", "file.print"}) {
             list->addWidget(make_command_row(shell_, page, QLatin1String(id)));
           }
         }},
        {StudioIcon::Prefs, tr("Prefs"),
         [this](QVBoxLayout* list, QWidget* page) {
           const auto add_toggle = [this, list, page](const QString& text, bool checked, std::function<void(bool)> apply) {
             auto* button = new QPushButton(text, page);
             button->setCheckable(true);
             button->setChecked(checked);
             button->setCursor(Qt::PointingHandCursor);
             connect(button, &QPushButton::toggled, this, [apply = std::move(apply)](bool on) { apply(on); });
             list->addWidget(button);
           };
           add_toggle(tr("Light interface"), shell_.light_interface(),
                      [this](bool on) { shell_.set_light_interface(on); });
           add_toggle(tr("Right-hand interface"), shell_.right_handed(),
                      [this](bool on) { shell_.set_right_handed(on); });
           add_toggle(tr("Full screen"), shell_.full_screen(), [this](bool) { shell_.toggle_full_screen(); });
           list->addWidget(make_command_row(shell_, page, QStringLiteral("file.preferences")));
         }},
        {StudioIcon::Help, tr("Help"),
         [this](QVBoxLayout* list, QWidget* page) {
           auto* about = new QPushButton(tr("About Patchy Studio"), page);
           about->setCursor(Qt::PointingHandCursor);
           connect(about, &QPushButton::clicked, this, [this] { shell_.show_about(); });
           list->addWidget(about);
           list->addWidget(make_command_row(shell_, page, QStringLiteral("help.scripting_guide")));
           auto* tips = new QLabel(tr("Tips: B, S and E pick Brush, Smudge and Eraser. [ and ] change the brush "
                                      "size. Ctrl+Z undoes, Ctrl+Shift+Z redoes. Space+drag pans, Alt+click picks a "
                                      "color."),
                                   page);
           tips->setWordWrap(true);
           tips->setProperty("studioRole", QStringLiteral("muted"));
           list->addWidget(tips);
         }},
    };
    for (std::size_t index = 0; index < tab_specs.size(); ++index) {
      const auto& spec = tab_specs[index];
      auto* tab = new QPushButton(spec.label, this);
      tab->setProperty("studioRole", QStringLiteral("tab"));
      tab->setCheckable(true);
      tab->setCursor(Qt::PointingHandCursor);
      tab->setIcon(QIcon(icon_pixmap(spec.icon, 22, theme().studio_text, devicePixelRatioF())));
      tab->setIconSize(QSize(18, 18));
      group->addButton(tab, static_cast<int>(index));
      tabs->addWidget(tab, 1);
      auto* page = new QWidget;
      auto* list = new QVBoxLayout(page);
      list->setContentsMargins(0, 4, 0, 0);
      list->setSpacing(4);
      spec.fill(list, page);
      list->addStretch(1);
      pages_->addWidget(make_scroll(pages_, page));
    }
    connect(group, &QButtonGroup::idClicked, pages_, &QStackedWidget::setCurrentIndex);
    if (auto* first = group->button(0); first != nullptr) {
      first->setChecked(true);
    }
  }

  [[nodiscard]] QSize preferred_size() const override { return {380, 470}; }

private:
  QStackedWidget* pages_{nullptr};
};

// --- Adjustments --------------------------------------------------------------------

class StudioAdjustmentsPanel final : public StudioPanel {
  Q_OBJECT

public:
  StudioAdjustmentsPanel(StudioShell& shell, QWidget* parent) : StudioPanel(shell, parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 4, 6, 4);
    column->setSpacing(8);
    column->addWidget(make_title(tr("Adjustments"), this));
    auto* body = new QWidget;
    list_ = new QVBoxLayout(body);
    list_->setContentsMargins(0, 0, 4, 0);
    list_->setSpacing(3);
    if (auto* menu = shell_.window_menu(QStringLiteral("imageAdjustmentsMenu")); menu != nullptr) {
      add_menu(menu, clean_action_text(menu->menuAction()), body);
    }
    if (auto* menu = shell_.window_menu(QStringLiteral("filterMenu")); menu != nullptr) {
      add_menu(menu, clean_action_text(menu->menuAction()), body);
    }
    list_->addStretch(1);
    column->addWidget(make_scroll(this, body), 1);
  }

  [[nodiscard]] QSize preferred_size() const override { return {320, 560}; }

private:
  // Lists `menu`'s commands under a heading; each submenu becomes its own section.
  void add_menu(QMenu* menu, const QString& heading, QWidget* body) {
    bool heading_added = false;
    const auto ensure_heading = [&] {
      if (!heading_added) {
        list_->addWidget(make_section(heading, body));
        heading_added = true;
      }
    };
    std::vector<QMenu*> submenus;
    for (auto* action : menu->actions()) {
      if (action->isSeparator() || !action->isVisible()) {
        continue;
      }
      if (auto* submenu = action->menu(); submenu != nullptr) {
        submenus.push_back(submenu);
        continue;
      }
      ensure_heading();
      auto* button = new QPushButton(clean_action_text(action), body);
      button->setCursor(Qt::PointingHandCursor);
      button->setFocusPolicy(Qt::NoFocus);
      button->setEnabled(action->isEnabled());
      QPointer<QAction> guard(action);
      connect(button, &QPushButton::clicked, this, [this, guard] {
        shell_.close_panels();
        if (guard != nullptr && guard->isEnabled()) {
          guard->trigger();
        }
        shell_.schedule_refresh();
      });
      list_->addWidget(button);
    }
    for (auto* submenu : submenus) {
      add_menu(submenu, clean_action_text(submenu->menuAction()), body);
    }
  }

  QVBoxLayout* list_{nullptr};
};

}  // namespace

StudioPanel::StudioPanel(StudioShell& shell, QWidget* parent) : QWidget(parent), shell_(shell) {
  setObjectName(QStringLiteral("studioPanel"));
  set_themed_style(*this, studio_panel_qss());
}

StudioPanel* create_studio_panel(StudioPanelKind kind, StudioShell& shell, QWidget* parent) {
  switch (kind) {
    case StudioPanelKind::Actions:
      return new StudioActionsPanel(shell, parent);
    case StudioPanelKind::Adjustments:
      return new StudioAdjustmentsPanel(shell, parent);
    case StudioPanelKind::Brushes:
      return new StudioBrushLibraryPanel(shell, parent);
    case StudioPanelKind::Layers:
      return new StudioLayersPanel(shell, parent);
    case StudioPanelKind::Color:
      return new StudioColorPanel(shell, parent);
  }
  return new StudioActionsPanel(shell, parent);
}

}  // namespace patchy::ui

#include "studio_panels.moc"
