#include "ui/studio_shell.hpp"

#include "formats/document_flatten.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/filter_preview_proxy.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/main_window.hpp"
#include "ui/start_panel.hpp"
#include "ui/studio_gallery.hpp"
#include "ui/studio_navigator.hpp"
#include "ui/studio_panels.hpp"
#include "ui/studio_widgets.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace patchy::ui {
namespace {

// Persisted identifiers: never rename them (see AGENTS.md). studio/rightHanded is
// true when the side bar sits on the right edge, the left-handed layout.
QString right_handed_key() { return QStringLiteral("studio/rightHanded"); }
QString color_history_key() { return QStringLiteral("studio/colorHistory"); }
QString navigator_visible_key() { return QStringLiteral("studio/navigatorVisible"); }
QString navigator_position_key() { return QStringLiteral("studio/navigatorPosition"); }

constexpr int kTopBarHeight = 48;
constexpr int kSideBarWidth = 60;
constexpr int kSideBarHeight = 430;
constexpr int kSideBarInset = 10;
constexpr int kNavigatorInset = 6;
constexpr std::size_t kColorHistoryLength = 10;

// The translucent strip across the top of the canvas.
class StudioTopBar final : public QWidget {
public:
  explicit StudioTopBar(QWidget* parent) : QWidget(parent) { setObjectName(QStringLiteral("studioTopBar")); }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), theme().studio_bar_bg);
    painter.fillRect(QRect(0, height() - 1, width(), 1), theme().studio_bar_border);
  }
};

// The floating pill on the canvas edge holding the size and opacity sliders.
class StudioSideBar final : public QWidget {
public:
  explicit StudioSideBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("studioSideBar"));
    // The app sheet gives plain widgets a window background; only the pill paints here.
    setProperty("studioTransparent", true);
    setStyleSheet(QStringLiteral("QWidget[studioTransparent=\"true\"] { background: transparent; }"));
    setAutoFillBackground(false);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    // Inset far enough that the drop shadow stays inside the widget.
    paint_studio_surface(painter, QRectF(rect()).adjusted(7, 6, -7, -9), 14.0, theme().studio_bar_bg,
                         theme().studio_bar_border);
  }
};

bool is_selection_tool(CanvasTool tool) {
  switch (tool) {
    case CanvasTool::Marquee:
    case CanvasTool::EllipticalMarquee:
    case CanvasTool::Lasso:
    case CanvasTool::MagneticLasso:
    case CanvasTool::MagicWand:
    case CanvasTool::QuickSelect:
      return true;
    default:
      return false;
  }
}

StudioIconButton* make_icon_button(QWidget* parent, StudioIcon icon, const QString& tip, const char* name) {
  auto* button = new StudioIconButton(icon, parent);
  button->setObjectName(QLatin1String(name));
  button->setToolTip(tip);
  button->setCheckable(true);
  return button;
}

QPushButton* make_bar_button(QWidget* parent, StudioIcon icon, const QString& text, const char* name) {
  // A literal "&" (Copy & Paste) must not become a mnemonic underline.
  auto* button = new QPushButton(QString(text).replace(QLatin1Char('&'), QStringLiteral("&&")), parent);
  button->setObjectName(QLatin1String(name));
  button->setCursor(Qt::PointingHandCursor);
  button->setFocusPolicy(Qt::NoFocus);
  QPixmap pixmap(QSize(36, 36) * button->devicePixelRatioF());
  pixmap.setDevicePixelRatio(button->devicePixelRatioF());
  pixmap.fill(Qt::transparent);
  {
    QPainter painter(&pixmap);
    paint_studio_icon(painter, icon, QRectF(4, 4, 28, 28), theme().studio_text);
  }
  button->setIcon(QIcon(pixmap));
  button->setIconSize(QSize(20, 20));
  return button;
}

}  // namespace

StudioShell::StudioShell(MainWindow& window) : QObject(&window), window_(window) {
  host_ = window_.document_tabs_;
  {
    auto settings = app_settings();
    controls_on_right_ = settings.value(right_handed_key(), false).toBool();
    navigator_enabled_ = settings.value(navigator_visible_key(), true).toBool();
    if (const auto position = settings.value(navigator_position_key()); position.isValid()) {
      navigator_position_ = position.toPoint();
    }
    for (const auto& name : settings.value(color_history_key()).toStringList()) {
      const QColor color(name);
      if (color.isValid()) {
        color_history_.push_back(color);
      }
    }
  }
  refresh_timer_ = new QTimer(this);
  refresh_timer_->setSingleShot(true);
  refresh_timer_->setInterval(0);
  connect(refresh_timer_, &QTimer::timeout, this, [this] { refresh(); });
  bubble_timer_ = new QTimer(this);
  bubble_timer_->setSingleShot(true);
  bubble_timer_->setInterval(900);

  hide_classic_chrome();
  build_top_bar();
  build_side_bar();
  build_bottom_bars();
  navigator_ = new StudioNavigator(*this, host_);
  navigator_->hide();
  connect(navigator_, &StudioNavigator::close_requested, this, [this] { set_navigator_enabled(false); });
  connect(navigator_, &StudioNavigator::move_requested, this, [this](QPoint top_left) {
    navigator_position_ = top_left;
    layout_overlay();
  });
  // Saved when the drag ends, not on every step (each save writes the file).
  connect(navigator_, &StudioNavigator::move_finished, this, [this] {
    if (navigator_position_.has_value()) {
      app_settings().setValue(navigator_position_key(), navigator_->pos());
      navigator_position_ = navigator_->pos();
    }
  });
  connect(navigator_, &StudioNavigator::reset_position_requested, this, [this] {
    navigator_position_.reset();
    app_settings().remove(navigator_position_key());
    layout_overlay();
  });
  popover_ = new StudioPopover(host_);
  connect(popover_, &StudioPopover::closed, this, [this] {
    open_panel_ = Panel::None;
    refresh_tool_buttons();
  });
  gallery_ = new StudioGallery(*this, host_);
  gallery_->hide();
  host_->installEventFilter(this);
  window_.installEventFilter(this);
  connect(&ThemeManager::instance(), &ThemeManager::color_scheme_changed, this, [this] {
    host_->update();
    for (auto* child : host_->findChildren<QWidget*>()) {
      child->update();
    }
  });
  if (window_.canvas_ != nullptr) {
    last_primary_ = window_.canvas_->primary_color();
  }
  layout_overlay();
  refresh();
  if (window_.sessions_.empty()) {
    show_gallery();
  }
}

StudioShell::~StudioShell() = default;

void StudioShell::hide_classic_chrome() {
  auto& window = window_;
  if (auto* bar = window.menuBar(); bar != nullptr && !bar->isNativeMenuBar()) {
    // Menu shortcuts survive through the window-level actions added below.
    bar->hide();
  }
  for (auto* toolbar : window.findChildren<QToolBar*>(Qt::FindDirectChildrenOnly)) {
    toolbar->hide();
    toolbar->toggleViewAction()->setEnabled(false);
  }
  for (auto* dock : window.findChildren<QDockWidget*>()) {
    dock->hide();
    // Window-menu commands (Color Wheel, Layers, ...) would otherwise bring the
    // classic panels back over the studio canvas.
    connect(dock, &QDockWidget::visibilityChanged, this, [dock](bool visible) {
      if (visible) {
        QTimer::singleShot(0, dock, [dock] { dock->hide(); });
      }
    });
  }
  window.statusBar()->hide();
  if (auto* tab_bar = window.document_tabs_->tabBar(); tab_bar != nullptr) {
    tab_bar->hide();
  }
  // The canvas starts under the top bar, so Fit and new canvases are never hidden
  // behind it; the side bar and panels still float over the artwork.
  append_themed_style(*window.document_tabs_,
                      QStringLiteral("QTabWidget#documentTabs::pane { margin-top: %1px; border: none; }")
                          .arg(kTopBarHeight));
  if (window.start_panel_ != nullptr) {
    window.start_panel_->hide();
  }
  // Commands that lived only on the now-hidden tool palette and options bar keep
  // their shortcuts by riding on the window itself.
  for (const auto& entry : window.hotkey_registry_.commands()) {
    if (entry.action != nullptr) {
      window.addAction(entry.action);
    }
  }
}

void StudioShell::build_top_bar() {
  top_bar_ = new StudioTopBar(host_);
  auto* row = new QHBoxLayout(top_bar_);
  row->setContentsMargins(10, 2, 10, 2);
  row->setSpacing(2);

  gallery_button_ = new StudioIconButton(top_bar_);
  gallery_button_->setObjectName(QStringLiteral("studioGalleryButton"));
  gallery_button_->setText(tr("Gallery"));
  gallery_button_->setToolTip(tr("Show the gallery of open and recent artwork"));
  connect(gallery_button_, &QAbstractButton::clicked, this, [this] { show_gallery(); });
  row->addWidget(gallery_button_);
  row->addSpacing(8);

  actions_button_ = make_icon_button(top_bar_, StudioIcon::Wrench, tr("Actions"), "studioActionsButton");
  adjustments_button_ = make_icon_button(top_bar_, StudioIcon::Wand, tr("Adjustments"), "studioAdjustmentsButton");
  selection_button_ = make_icon_button(top_bar_, StudioIcon::Selection, tr("Selection"), "studioSelectionButton");
  transform_button_ = make_icon_button(top_bar_, StudioIcon::Transform, tr("Transform"), "studioTransformButton");
  for (auto* button : {actions_button_, adjustments_button_, selection_button_, transform_button_}) {
    row->addWidget(button);
  }
  row->addStretch(1);
  brush_button_ = make_icon_button(top_bar_, StudioIcon::Brush, tr("Paint (tap again for the Brush Library)"),
                                   "studioBrushButton");
  smudge_button_ = make_icon_button(top_bar_, StudioIcon::Smudge, tr("Smudge (tap again for the Brush Library)"),
                                    "studioSmudgeButton");
  eraser_button_ = make_icon_button(top_bar_, StudioIcon::Eraser, tr("Erase (tap again for the Brush Library)"),
                                    "studioEraserButton");
  layers_button_ = make_icon_button(top_bar_, StudioIcon::Layers, tr("Layers"), "studioLayersButton");
  color_button_ = new StudioColorButton(top_bar_);
  color_button_->setObjectName(QStringLiteral("studioColorButton"));
  color_button_->setToolTip(tr("Colors"));
  color_button_->setCheckable(true);
  for (QWidget* button : std::initializer_list<QWidget*>{brush_button_, smudge_button_, eraser_button_, layers_button_,
                                                         color_button_}) {
    row->addWidget(button);
  }

  connect(actions_button_, &QAbstractButton::clicked, this, [this] { toggle_panel(Panel::Actions, actions_button_); });
  connect(adjustments_button_, &QAbstractButton::clicked, this,
          [this] { toggle_panel(Panel::Adjustments, adjustments_button_); });
  connect(selection_button_, &QAbstractButton::clicked, this, [this] { on_selection_clicked(); });
  connect(transform_button_, &QAbstractButton::clicked, this, [this] { on_transform_clicked(); });
  connect(brush_button_, &QAbstractButton::clicked, this, [this] { on_paint_tool_clicked(CanvasTool::Brush); });
  connect(smudge_button_, &QAbstractButton::clicked, this, [this] { on_paint_tool_clicked(CanvasTool::Smudge); });
  connect(eraser_button_, &QAbstractButton::clicked, this, [this] { on_paint_tool_clicked(CanvasTool::Eraser); });
  connect(layers_button_, &QAbstractButton::clicked, this, [this] { toggle_panel(Panel::Layers, layers_button_); });
  connect(color_button_, &QAbstractButton::clicked, this, [this] {
    if (open_panel_ == Panel::Color) {
      popover_->hide();
    } else {
      open_panel(Panel::Color, color_button_);
    }
  });
}

void StudioShell::build_side_bar() {
  side_bar_ = new StudioSideBar(host_);
  auto* column = new QVBoxLayout(side_bar_);
  column->setContentsMargins(8, 16, 8, 16);
  column->setSpacing(6);

  size_slider_ = new StudioSlider(side_bar_);
  size_slider_->setObjectName(QStringLiteral("studioSizeSlider"));
  size_slider_->setToolTip(tr("Brush size"));
  size_slider_->set_curve(2.6);
  opacity_slider_ = new StudioSlider(side_bar_);
  opacity_slider_->setObjectName(QStringLiteral("studioOpacitySlider"));
  opacity_slider_->setToolTip(tr("Brush opacity"));
  opacity_slider_->set_range(1, 100);
  modify_button_ = new StudioIconButton(StudioIcon::Eyedropper, side_bar_);
  modify_button_->setObjectName(QStringLiteral("studioModifyButton"));
  modify_button_->setToolTip(tr("Eyedropper: pick a color from the canvas"));
  modify_button_->setCheckable(true);
  modify_button_->set_extent(38);
  undo_button_ = new StudioIconButton(StudioIcon::Undo, side_bar_);
  undo_button_->setObjectName(QStringLiteral("studioUndoButton"));
  undo_button_->setToolTip(tr("Undo"));
  undo_button_->set_extent(38);
  redo_button_ = new StudioIconButton(StudioIcon::Redo, side_bar_);
  redo_button_->setObjectName(QStringLiteral("studioRedoButton"));
  redo_button_->setToolTip(tr("Redo"));
  redo_button_->set_extent(38);

  column->addWidget(size_slider_, 1, Qt::AlignHCenter);
  column->addWidget(modify_button_, 0, Qt::AlignHCenter);
  column->addWidget(opacity_slider_, 1, Qt::AlignHCenter);
  column->addSpacing(6);
  column->addWidget(undo_button_, 0, Qt::AlignHCenter);
  column->addWidget(redo_button_, 0, Qt::AlignHCenter);

  bubble_ = new StudioBubble(host_);
  connect(bubble_timer_, &QTimer::timeout, bubble_, &QWidget::hide);

  const auto begin_drag = [this] { slider_dragging_ = true; };
  const auto end_drag = [this] {
    slider_dragging_ = false;
    bubble_timer_->start();
  };
  connect(size_slider_, &StudioSlider::drag_started, this, begin_drag);
  connect(size_slider_, &StudioSlider::drag_finished, this, end_drag);
  connect(opacity_slider_, &StudioSlider::drag_started, this, begin_drag);
  connect(opacity_slider_, &StudioSlider::drag_finished, this, end_drag);
  connect(size_slider_, &StudioSlider::value_changed, this, [this](int value) {
    if (auto* spin = window_.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin")); spin != nullptr) {
      spin->setValue(value);
    }
    show_slider_bubble(size_slider_, tr("%1 px").arg(value));
  });
  connect(opacity_slider_, &StudioSlider::value_changed, this, [this](int value) {
    if (auto* spin = window_.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin")); spin != nullptr) {
      spin->setValue(value);
    }
    show_slider_bubble(opacity_slider_, tr("%1%").arg(value));
  });
  connect(modify_button_, &QAbstractButton::clicked, this, [this] { on_modify_clicked(); });
  connect(undo_button_, &QAbstractButton::clicked, this, [this] { run_command(QStringLiteral("edit.undo")); });
  connect(redo_button_, &QAbstractButton::clicked, this, [this] { run_command(QStringLiteral("edit.redo")); });
}

void StudioShell::build_bottom_bars() {
  const auto make_bar = [this](const char* name) {
    auto* bar = new StudioPopover(host_);
    bar->setObjectName(QLatin1String(name));
    bar->set_dismiss_on_outside_press(false);
    auto* content = new QWidget(bar);
    content->setObjectName(QStringLiteral("studioPanel"));
    set_themed_style(*content, studio_panel_qss());
    auto* row = new QHBoxLayout(content);
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(6);
    bar->set_content(content);
    return std::pair{bar, row};
  };

  {
    auto [bar, row] = make_bar("studioSelectionBar");
    selection_bar_ = bar;
    auto* content = bar->content();
    const auto add_tool = [this, content, row](StudioIcon icon, const QString& text, CanvasTool tool,
                                               const char* name) {
      auto* button = make_bar_button(content, icon, text, name);
      button->setCheckable(true);
      button->setProperty("studioTool", static_cast<int>(tool));
      connect(button, &QPushButton::clicked, this, [this, tool] { activate_tool(tool); });
      row->addWidget(button);
    };
    add_tool(StudioIcon::Wand, tr("Automatic"), CanvasTool::MagicWand, "studioSelectAutomatic");
    add_tool(StudioIcon::Freehand, tr("Freehand"), CanvasTool::Lasso, "studioSelectFreehand");
    add_tool(StudioIcon::Rectangle, tr("Rectangle"), CanvasTool::Marquee, "studioSelectRectangle");
    add_tool(StudioIcon::Ellipse, tr("Ellipse"), CanvasTool::EllipticalMarquee, "studioSelectEllipse");
    auto* separator = new QFrame(content);
    separator->setProperty("studioRole", QStringLiteral("separator"));
    separator->setFixedWidth(1);
    separator->setMinimumHeight(28);
    row->addWidget(separator);
    const auto add_mode = [this, content, row](StudioIcon icon, const QString& text, QAction* action, const char* name) {
      auto* button = make_bar_button(content, icon, text, name);
      button->setCheckable(true);
      connect(button, &QPushButton::clicked, this, [this, action] {
        if (action != nullptr) {
          action->trigger();
        }
        schedule_refresh();
      });
      button->setProperty("studioModeAction", QVariant::fromValue(static_cast<QObject*>(action)));
      row->addWidget(button);
    };
    add_mode(StudioIcon::Plus, tr("Add"), window_.selection_add_mode_action_, "studioSelectAdd");
    add_mode(StudioIcon::Close, tr("Subtract"), window_.selection_subtract_mode_action_, "studioSelectSubtract");
    const auto add_command = [this, content, row](StudioIcon icon, const QString& text, const QString& id,
                                                  const char* name) {
      auto* button = make_bar_button(content, icon, text, name);
      connect(button, &QPushButton::clicked, this, [this, id] { run_command(id); });
      row->addWidget(button);
    };
    add_command(StudioIcon::Invert, tr("Invert"), QStringLiteral("select.inverse"), "studioSelectInvert");
    add_command(StudioIcon::Copy, tr("Copy & Paste"), QStringLiteral("layer.via_copy"), "studioSelectCopyPaste");
    add_command(StudioIcon::Feather, tr("Expand"), QStringLiteral("select.expand"), "studioSelectExpand");
    add_command(StudioIcon::Close, tr("Clear"), QStringLiteral("select.deselect"), "studioSelectClear");
  }

  {
    auto [bar, row] = make_bar("studioTransformBar");
    transform_bar_ = bar;
    auto* content = bar->content();
    const auto add_button = [content, row](StudioIcon icon, const QString& text, const char* name) {
      auto* button = make_bar_button(content, icon, text, name);
      row->addWidget(button);
      return button;
    };
    auto* freeform = add_button(StudioIcon::Transform, tr("Freeform"), "studioTransformFreeform");
    connect(freeform, &QPushButton::clicked, this, [this] {
      if (window_.canvas_ != nullptr && !window_.canvas_->free_transform_active()) {
        run_command(QStringLiteral("edit.free_transform"));
      }
    });
    auto* warp = add_button(StudioIcon::Warp, tr("Warp"), "studioTransformWarp");
    connect(warp, &QPushButton::clicked, this, [this] {
      if (window_.transform_warp_mode_button_ != nullptr && window_.canvas_ != nullptr &&
          window_.canvas_->free_transform_active()) {
        window_.transform_warp_mode_button_->click();
      } else {
        run_command(QStringLiteral("edit.warp_transform"));
      }
    });
    auto* flip_h = add_button(StudioIcon::FlipHorizontal, tr("Flip Horizontal"), "studioTransformFlipH");
    connect(flip_h, &QPushButton::clicked, this, [this] { run_command(QStringLiteral("layer.flip_horizontal")); });
    auto* flip_v = add_button(StudioIcon::FlipVertical, tr("Flip Vertical"), "studioTransformFlipV");
    connect(flip_v, &QPushButton::clicked, this, [this] { run_command(QStringLiteral("layer.flip_vertical")); });
    auto* cancel = add_button(StudioIcon::Close, tr("Cancel"), "studioTransformCancel");
    connect(cancel, &QPushButton::clicked, this, [this] {
      if (window_.transform_cancel_button_ != nullptr) {
        window_.transform_cancel_button_->click();
      }
      schedule_refresh();
    });
    auto* done = add_button(StudioIcon::Check, tr("Done"), "studioTransformDone");
    connect(done, &QPushButton::clicked, this, [this] {
      if (window_.transform_apply_button_ != nullptr) {
        window_.transform_apply_button_->click();
      }
      schedule_refresh();
    });
  }
}

void StudioShell::layout_overlay() {
  if (host_ == nullptr) {
    return;
  }
  const auto area = host_->rect();
  top_bar_->setGeometry(0, 0, area.width(), kTopBarHeight);
  top_bar_->raise();
  // The navigator takes the bottom corner on the side-bar side; the side bar
  // centers in the space above it.
  const bool navigator_shown = navigator_ != nullptr && navigator_->isVisible();
  const auto navigator_size = navigator_ != nullptr ? navigator_->sizeHint() : QSize();
  int side_bottom = area.height();
  if (navigator_shown) {
    int navigator_x = controls_on_right_ ? area.width() - navigator_size.width() - kNavigatorInset : kNavigatorInset;
    int navigator_y = std::max(kTopBarHeight, area.height() - navigator_size.height() - kNavigatorInset);
    if (navigator_position_.has_value()) {
      // A dragged navigator floats wherever it was left, kept inside the canvas
      // area below the top bar; the side bar then uses the full height.
      navigator_x = std::clamp(navigator_position_->x(), 0, std::max(0, area.width() - navigator_size.width()));
      navigator_y = std::clamp(navigator_position_->y(), kTopBarHeight,
                               std::max(kTopBarHeight, area.height() - navigator_size.height()));
    } else {
      side_bottom = navigator_y;
    }
    navigator_->setGeometry(navigator_x, navigator_y, navigator_size.width(), navigator_size.height());
    navigator_->raise();
  }
  const int available = side_bottom - kTopBarHeight;
  const int side_height = std::min(kSideBarHeight, std::max(220, available - 20));
  const int side_y = kTopBarHeight + std::max(10, (available - side_height) / 2);
  const int side_x = controls_on_right_ ? area.width() - kSideBarWidth - kSideBarInset : kSideBarInset;
  side_bar_->setGeometry(side_x, side_y, kSideBarWidth, side_height);
  side_bar_->raise();
  for (auto* bar : {selection_bar_, transform_bar_}) {
    if (bar != nullptr && bar->isVisible()) {
      const auto hint = bar->content()->sizeHint();
      const int width = std::min(hint.width(), area.width() - 40);
      bar->show_at(QRect((area.width() - width) / 2, area.height() - hint.height() - 22, width, hint.height()));
    }
  }
  if (popover_ != nullptr && popover_->isVisible()) {
    popover_->raise();
  }
  if (gallery_ != nullptr) {
    gallery_->setGeometry(area);
    if (gallery_->isVisible()) {
      gallery_->raise();
    }
  }
}

void StudioShell::schedule_refresh() {
  if (refresh_timer_ != nullptr && !refresh_timer_->isActive()) {
    refresh_timer_->start();
  }
}

void StudioShell::refresh() {
  if (refreshing_) {
    return;
  }
  refreshing_ = true;
  if (window_.start_panel_ != nullptr && window_.start_panel_->isVisible()) {
    window_.start_panel_->hide();
  }
  if (auto* bar = window_.menuBar(); bar != nullptr && !bar->isNativeMenuBar() && bar->isVisible()) {
    bar->hide();
  }
  // A document that just opened (from the gallery, a drop, or the command line)
  // takes the screen, as on iPad.
  const auto session_count = window_.sessions_.size();
  if (session_count > last_session_count_ && gallery_->isVisible()) {
    gallery_->hide();
  }
  last_session_count_ = session_count;
  if (auto* tab_bar = window_.document_tabs_->tabBar(); tab_bar != nullptr && tab_bar->isVisible()) {
    tab_bar->hide();
  }
  if (auto* canvas = window_.canvas_; canvas != nullptr && !canvas->property("studioFiltered").toBool()) {
    canvas->setProperty("studioFiltered", true);
    canvas->installEventFilter(this);
    canvas->set_scroll_bars_hidden(true);
  }
  const auto primary = primary_color();
  if (primary.isValid() && primary != last_primary_) {
    if (last_primary_.isValid()) {
      remember_color(last_primary_);
    }
    last_primary_ = primary;
  }
  refresh_tool_buttons();
  refresh_side_bar();
  refresh_mode_bars();
  refresh_navigator();
  if (auto* panel = qobject_cast<StudioPanel*>(popover_->content()); panel != nullptr && popover_->isVisible()) {
    panel->refresh_from_editor();
  }
  if (window_.sessions_.empty() && !gallery_->isVisible()) {
    show_gallery();
  } else if (gallery_->isVisible()) {
    gallery_->reload();
  }
  refreshing_ = false;
}

CanvasTool StudioShell::current_tool() const {
  return window_.canvas_ != nullptr ? window_.canvas_->tool() : CanvasTool::Brush;
}

StudioShell::Mode StudioShell::current_mode() const {
  if (window_.canvas_ == nullptr) {
    return Mode::Paint;
  }
  if (window_.canvas_->free_transform_active() || window_.canvas_->warp_transform_active()) {
    return Mode::Transform;
  }
  if (is_selection_tool(current_tool())) {
    return Mode::Selection;
  }
  if (current_tool() == CanvasTool::Move) {
    return Mode::Transform;
  }
  return Mode::Paint;
}

void StudioShell::refresh_tool_buttons() {
  const bool has_document = window_.canvas_ != nullptr && window_.has_active_document();
  const auto tool = current_tool();
  const auto mode = current_mode();
  const auto set = [](QAbstractButton* button, bool checked) {
    if (button != nullptr && button->isChecked() != checked) {
      button->setChecked(checked);
    }
  };
  set(actions_button_, open_panel_ == Panel::Actions);
  set(adjustments_button_, open_panel_ == Panel::Adjustments);
  set(selection_button_, mode == Mode::Selection);
  set(transform_button_, mode == Mode::Transform);
  set(brush_button_, tool == CanvasTool::Brush || tool == CanvasTool::MixerBrush);
  set(smudge_button_, tool == CanvasTool::Smudge);
  set(eraser_button_, tool == CanvasTool::Eraser);
  set(layers_button_, open_panel_ == Panel::Layers);
  set(color_button_, open_panel_ == Panel::Color);
  set(modify_button_, tool == CanvasTool::Eyedropper);
  for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{
           actions_button_, adjustments_button_, selection_button_, transform_button_, brush_button_,
           smudge_button_, eraser_button_, layers_button_, color_button_}) {
    button->setEnabled(has_document || button == actions_button_);
  }
  color_button_->set_color(primary_color());
}

void StudioShell::refresh_side_bar() {
  side_bar_->setVisible(window_.canvas_ != nullptr && window_.has_active_document());
  auto* size_spin = window_.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* opacity_spin = window_.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  if (!slider_dragging_) {
    if (size_spin != nullptr) {
      size_slider_->set_range(size_spin->minimum(), std::min(size_spin->maximum(), 1000));
      size_slider_->set_value(size_spin->value());
    }
    if (opacity_spin != nullptr) {
      opacity_slider_->set_range(opacity_spin->minimum(), opacity_spin->maximum());
      opacity_slider_->set_value(opacity_spin->value());
    }
  }
  if (window_.undo_action_ != nullptr) {
    undo_button_->setEnabled(window_.undo_action_->isEnabled());
  }
  if (window_.redo_action_ != nullptr) {
    redo_button_->setEnabled(window_.redo_action_->isEnabled());
  }
}

void StudioShell::refresh_mode_bars() {
  const auto mode = current_mode();
  const bool has_document = window_.canvas_ != nullptr && window_.has_active_document();
  const bool show_selection = has_document && mode == Mode::Selection && !gallery_->isVisible();
  const bool show_transform = has_document && mode == Mode::Transform && !gallery_->isVisible();
  if (show_selection) {
    const auto tool = current_tool();
    for (auto* button : selection_bar_->content()->findChildren<QPushButton*>()) {
      const auto tool_property = button->property("studioTool");
      if (tool_property.isValid()) {
        const auto button_tool = static_cast<CanvasTool>(tool_property.toInt());
        const bool checked = button_tool == tool || (button_tool == CanvasTool::MagicWand && tool == CanvasTool::QuickSelect) ||
                             (button_tool == CanvasTool::Lasso && tool == CanvasTool::MagneticLasso);
        button->setChecked(checked);
      }
      if (auto* action = qobject_cast<QAction*>(button->property("studioModeAction").value<QObject*>());
          action != nullptr) {
        button->setChecked(action->isChecked());
      }
    }
  }
  const auto update_bar = [this](StudioPopover* bar, bool show) {
    if (show && !bar->isVisible()) {
      bar->show();  // sized by layout_overlay
      layout_overlay();
    } else if (!show && bar->isVisible()) {
      bar->hide();
    }
  };
  update_bar(selection_bar_, show_selection);
  update_bar(transform_bar_, show_transform);
}

void StudioShell::refresh_navigator() {
  const bool show = navigator_enabled_ && window_.canvas_ != nullptr && window_.has_active_document() &&
                    !gallery_->isVisible();
  if (show != navigator_->isVisible()) {
    navigator_->setVisible(show);
    layout_overlay();
  }
  if (show) {
    navigator_->sync_from_canvas();
    navigator_->schedule_overview();
  }
}

void StudioShell::canvas_view_changed() {
  if (navigator_ != nullptr && navigator_->isVisible()) {
    navigator_->sync_from_canvas();
  }
}

void StudioShell::set_navigator_enabled(bool enabled) {
  navigator_enabled_ = enabled;
  app_settings().setValue(navigator_visible_key(), enabled);
  refresh_navigator();
}

void StudioShell::show_slider_bubble(StudioSlider* slider, const QString& text) {
  const auto thumb = slider->mapTo(host_, slider->thumb_center());
  const int gap = 22;
  if (controls_on_right_) {
    bubble_->show_text(text, QPoint(side_bar_->geometry().left() - gap - 80, thumb.y()));
  } else {
    bubble_->show_text(text, QPoint(side_bar_->geometry().right() + gap, thumb.y()));
  }
  bubble_timer_->start();
}

void StudioShell::toggle_panel(Panel panel, StudioIconButton* anchor) {
  if (open_panel_ == panel && popover_->isVisible()) {
    popover_->hide();
    return;
  }
  open_panel(panel, anchor);
}

void StudioShell::open_panel(Panel panel, QWidget* anchor) {
  StudioPanelKind kind = StudioPanelKind::Actions;
  switch (panel) {
    case Panel::Actions:
      kind = StudioPanelKind::Actions;
      break;
    case Panel::Adjustments:
      kind = StudioPanelKind::Adjustments;
      break;
    case Panel::Brushes:
      kind = StudioPanelKind::Brushes;
      break;
    case Panel::Layers:
      kind = StudioPanelKind::Layers;
      break;
    case Panel::Color:
      kind = StudioPanelKind::Color;
      break;
    case Panel::None:
      popover_->hide();
      return;
  }
  if (popover_->isVisible()) {
    // Switching panels: close the old one first so its closed() bookkeeping runs.
    popover_->hide();
  }
  auto* content = create_studio_panel(kind, *this, popover_);
  popover_->set_content(content);
  popover_->set_dismiss_exempt(anchor);
  open_panel_ = panel;
  const QRect anchor_rect(anchor->mapTo(host_, QPoint(0, 0)), anchor->size());
  popover_->show_below(anchor_rect, content->preferred_size());
  refresh_tool_buttons();
}

void StudioShell::close_panels() {
  if (popover_ != nullptr) {
    popover_->hide();
  }
}

void StudioShell::activate_tool(CanvasTool tool) {
  if (window_.canvas_ == nullptr) {
    return;
  }
  window_.activate_tool(tool);
  schedule_refresh();
}

void StudioShell::on_paint_tool_clicked(CanvasTool tool) {
  const auto current = current_tool();
  const bool already = current == tool || (tool == CanvasTool::Brush && current == CanvasTool::MixerBrush);
  eyedropper_one_shot_ = false;
  if (already) {
    if (open_panel_ == Panel::Brushes && popover_->isVisible()) {
      popover_->hide();
    } else {
      auto* anchor = tool == CanvasTool::Smudge ? smudge_button_ : (tool == CanvasTool::Eraser ? eraser_button_ : brush_button_);
      open_panel(Panel::Brushes, anchor);
    }
  } else {
    if (open_panel_ == Panel::Brushes) {
      popover_->hide();
    }
    if (window_.canvas_ != nullptr && (window_.canvas_->free_transform_active() || window_.canvas_->warp_transform_active()) &&
        window_.transform_apply_button_ != nullptr) {
      window_.transform_apply_button_->click();
    }
    activate_tool(tool);
  }
  refresh_tool_buttons();
}

void StudioShell::on_selection_clicked() {
  if (current_mode() == Mode::Selection) {
    activate_tool(return_tool_.value_or(CanvasTool::Brush));
    return_tool_.reset();
    return;
  }
  const auto current = current_tool();
  if (!is_selection_tool(current) && current != CanvasTool::Move) {
    return_tool_ = current;
  }
  activate_tool(CanvasTool::Lasso);
}

void StudioShell::on_transform_clicked() {
  auto* canvas = window_.canvas_;
  if (canvas == nullptr) {
    return;
  }
  if (canvas->free_transform_active() || canvas->warp_transform_active()) {
    if (window_.transform_apply_button_ != nullptr) {
      window_.transform_apply_button_->click();
    }
    activate_tool(return_tool_.value_or(CanvasTool::Brush));
    return_tool_.reset();
    return;
  }
  if (current_tool() == CanvasTool::Move) {
    activate_tool(return_tool_.value_or(CanvasTool::Brush));
    return_tool_.reset();
    return;
  }
  if (!is_selection_tool(current_tool())) {
    return_tool_ = current_tool();
  }
  activate_tool(CanvasTool::Move);
  run_command(QStringLiteral("edit.free_transform"));
  schedule_refresh();
}

void StudioShell::on_modify_clicked() {
  if (current_tool() == CanvasTool::Eyedropper) {
    activate_tool(return_tool_.value_or(CanvasTool::Brush));
    eyedropper_one_shot_ = false;
    return;
  }
  return_tool_ = current_tool();
  eyedropper_one_shot_ = true;
  activate_tool(CanvasTool::Eyedropper);
}

bool StudioShell::eventFilter(QObject* watched, QEvent* event) {
  if (watched == host_ && event->type() == QEvent::Resize) {
    layout_overlay();
  } else if (watched == &window_ && event->type() == QEvent::Show) {
    // Window-state restore can re-show docks and toolbars after construction.
    QTimer::singleShot(0, this, [this] {
      for (auto* toolbar : window_.findChildren<QToolBar*>(Qt::FindDirectChildrenOnly)) {
        toolbar->hide();
      }
      for (auto* dock : window_.findChildren<QDockWidget*>()) {
        dock->hide();
      }
      window_.statusBar()->hide();
      if (auto* bar = window_.menuBar(); bar != nullptr && !bar->isNativeMenuBar()) {
        bar->hide();
      }
      layout_overlay();
      schedule_refresh();
    });
  } else if (event->type() == QEvent::MouseButtonRelease && eyedropper_one_shot_ &&
             watched == window_.canvas_ && current_tool() == CanvasTool::Eyedropper) {
    eyedropper_one_shot_ = false;
    QTimer::singleShot(0, this, [this] {
      activate_tool(return_tool_.value_or(CanvasTool::Brush));
      return_tool_.reset();
    });
  } else if (event->type() == QEvent::MouseButtonPress && watched == window_.canvas_ && popover_->isVisible()) {
    // A stroke on the canvas closes the open panel, as tapping the canvas does on iPad.
    popover_->hide();
  }
  return QObject::eventFilter(watched, event);
}

void StudioShell::remember_color(QColor color) {
  if (!color.isValid()) {
    return;
  }
  color.setAlpha(255);
  color_history_.erase(std::remove(color_history_.begin(), color_history_.end(), color), color_history_.end());
  color_history_.insert(color_history_.begin(), color);
  if (color_history_.size() > kColorHistoryLength) {
    color_history_.resize(kColorHistoryLength);
  }
  QStringList names;
  for (const auto& entry : color_history_) {
    names.append(entry.name(QColor::HexRgb));
  }
  app_settings().setValue(color_history_key(), names);
}

// --- Bridge --------------------------------------------------------------------------

CanvasWidget* StudioShell::canvas() const { return window_.canvas_; }

const Document* StudioShell::document() const {
  if (window_.canvas_ == nullptr || !window_.has_active_document()) {
    return nullptr;
  }
  return &std::as_const(window_).document();
}

QAction* StudioShell::command(const QString& id) const {
  const auto* entry = window_.hotkey_registry_.find_command(id);
  return entry != nullptr ? entry->action.data() : nullptr;
}

void StudioShell::run_command(const QString& id) {
  if (auto* action = command(id); action != nullptr && action->isEnabled()) {
    action->trigger();
  }
  schedule_refresh();
}

QMenu* StudioShell::window_menu(const QString& object_name) const {
  return window_.findChild<QMenu*>(object_name);
}

QPixmap StudioShell::layer_thumbnail(const Layer& layer) {
  const auto* doc = document();
  if (doc == nullptr) {
    return {};
  }
  return window_.cached_layer_content_thumbnail(layer, doc->width(), doc->height());
}

std::vector<LayerId> StudioShell::selected_layer_ids() const {
  if (document() == nullptr) {
    return {};
  }
  return window_.selected_or_active_layer_ids();
}

void StudioShell::select_layer(LayerId id) {
  if (document() == nullptr) {
    return;
  }
  window_.select_layers_in_layer_list({id}, id);
  schedule_refresh();
}

void StudioShell::set_layer_visible(LayerId id, bool visible) {
  if (document() == nullptr) {
    return;
  }
  window_.set_layer_visibility(id, visible);
  schedule_refresh();
}

void StudioShell::set_active_layer_opacity(int percent) {
  if (window_.opacity_spin_ != nullptr) {
    window_.opacity_spin_->setValue(percent);
  }
}

void StudioShell::set_active_layer_blend_mode(BlendMode mode) {
  auto* combo = window_.blend_combo_;
  if (combo == nullptr) {
    return;
  }
  const int index = combo->findData(static_cast<int>(mode));
  if (index >= 0) {
    combo->setCurrentIndex(index);
  }
  schedule_refresh();
}

void StudioShell::rename_layer(LayerId id) {
  select_layer(id);
  window_.rename_active_layer();
  schedule_refresh();
}

void StudioShell::add_layer() {
  run_command(QStringLiteral("layer.new"));
}

void StudioShell::toggle_group_collapsed(LayerId id) {
  if (document() == nullptr) {
    return;
  }
  window_.toggle_layer_folder_expanded(id);
  schedule_refresh();
}

bool StudioShell::group_collapsed(LayerId id) const {
  if (document() == nullptr) {
    return false;
  }
  return window_.session().collapsed_layer_groups.contains(id);
}

BrushTipLibrary& StudioShell::brush_library() const { return window_.brush_tip_library(); }

QString StudioShell::active_brush_tip_id() const { return window_.active_brush_tip_id_; }

void StudioShell::select_brush_tip(const QString& id) {
  window_.set_active_brush_tip(id, true);
  schedule_refresh();
}

void StudioShell::open_brush_settings() {
  if (window_.brush_dynamics_button_ == nullptr) {
    return;
  }
  const auto anchor = popover_->isVisible() ? popover_->mapToGlobal(QPoint(popover_->width(), 0))
                                            : host_->mapToGlobal(QPoint(host_->width() / 2, kTopBarHeight));
  close_panels();
  window_.brush_dynamics_button_->show_popup_at(anchor - QPoint(360, 0));
}

void StudioShell::import_brushes() {
  close_panels();
  window_.import_brush_tips_from_abr();
  schedule_refresh();
}

QColor StudioShell::primary_color() const {
  return window_.canvas_ != nullptr ? window_.canvas_->primary_color() : QColor(Qt::black);
}

QColor StudioShell::secondary_color() const {
  return window_.canvas_ != nullptr ? window_.canvas_->secondary_color() : QColor(Qt::white);
}

void StudioShell::set_primary_color(QColor color, bool finished) {
  window_.apply_color_wheel_color(color, finished);
  if (finished) {
    schedule_refresh();
  } else {
    color_button_->set_color(color);
  }
}

std::vector<StudioShell::OpenDocument> StudioShell::open_documents() const {
  std::vector<OpenDocument> result;
  auto* tabs = window_.document_tabs_;
  for (int index = 0; index < tabs->count(); ++index) {
    auto* canvas = qobject_cast<CanvasWidget*>(tabs->widget(index));
    if (canvas == nullptr) {
      continue;
    }
    const auto* session = window_.session_for_canvas(canvas);
    if (session == nullptr) {
      continue;
    }
    OpenDocument entry;
    entry.tab_index = index;
    entry.title = session->title;
    entry.path = session->path;
    entry.size = QSize(session->document.width(), session->document.height());
    entry.modified = window_.session_is_modified(*session);
    entry.active = canvas == window_.canvas_;
    result.push_back(std::move(entry));
  }
  return result;
}

QStringList StudioShell::recent_files() const { return window_.recent_files_; }

void StudioShell::render_document_thumbnail(int tab_index, QSize bound, std::function<void(QImage)> ready) {
  auto* canvas = qobject_cast<CanvasWidget*>(window_.document_tabs_->widget(tab_index));
  const auto* session = canvas != nullptr ? window_.session_for_canvas(canvas) : nullptr;
  if (session == nullptr) {
    return;
  }
  // A Document copy shares its pixel storage copy-on-write, so the worker reads a
  // stable snapshot while the canvas keeps editing its own.
  auto snapshot = std::make_shared<const Document>(std::as_const(session->document));
  QPointer<StudioShell> guard(this);
  run_tracked_background_worker([snapshot, bound, guard, ready = std::move(ready)]() mutable {
    auto image = image_from_pixels(flatten_document_rgba8(*snapshot));
    if (!image.isNull()) {
      image = image.scaled(bound, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QMetaObject::invokeMethod(
        qApp,
        [guard, image = std::move(image), ready = std::move(ready)] {
          if (guard != nullptr) {
            ready(image);
          }
        },
        Qt::QueuedConnection);
  });
}

void StudioShell::activate_document(int tab_index) {
  hide_gallery();
  if (tab_index >= 0 && tab_index < window_.document_tabs_->count()) {
    window_.document_tabs_->setCurrentIndex(tab_index);
  }
  schedule_refresh();
}

void StudioShell::close_document(int tab_index) {
  window_.close_document_tab(tab_index);
  schedule_refresh();
}

void StudioShell::open_recent(const QString& path) {
  hide_gallery();
  window_.open_recent_document(path);
  if (window_.sessions_.empty()) {
    show_gallery();
  }
  schedule_refresh();
}

void StudioShell::open_file_dialog() {
  hide_gallery();
  window_.open_document();
  if (window_.sessions_.empty()) {
    show_gallery();
  }
  schedule_refresh();
}

void StudioShell::create_canvas(int width, int height, double ppi) {
  hide_gallery();
  window_.reset_document(width, height, QColor(Qt::white), tr("New canvas"), ppi);
  // Fit once the new canvas has its real size in the layout.
  QTimer::singleShot(0, this, [this] {
    if (window_.canvas_ != nullptr) {
      window_.canvas_->fit_to_view();
    }
  });
  schedule_refresh();
}

void StudioShell::create_canvas_with_dialog() {
  hide_gallery();
  window_.create_new_document();
  if (window_.sessions_.empty()) {
    show_gallery();
  }
  schedule_refresh();
}

void StudioShell::show_gallery() {
  close_panels();
  gallery_->setGeometry(host_->rect());
  gallery_->reload();
  gallery_->show();
  gallery_->raise();
  selection_bar_->hide();
  transform_bar_->hide();
  navigator_->hide();
}

void StudioShell::hide_gallery() {
  gallery_->hide();
  schedule_refresh();
}

void StudioShell::set_left_handed(bool left_handed) {
  controls_on_right_ = left_handed;
  app_settings().setValue(right_handed_key(), left_handed);
  // Switching hands sends a dragged navigator back to its corner on the new side.
  navigator_position_.reset();
  app_settings().remove(navigator_position_key());
  layout_overlay();
}

bool StudioShell::light_interface() const { return active_color_scheme() == ColorScheme::Light; }

void StudioShell::set_light_interface(bool enabled) {
  ThemeManager::instance().set_preference(enabled ? ColorSchemePreference::Light : ColorSchemePreference::Dark, true);
}

bool StudioShell::full_screen() const { return window_.isFullScreen(); }

void StudioShell::show_about() {
  close_panels();
  window_.show_about();
}

void StudioShell::toggle_full_screen() {
  if (window_.isFullScreen()) {
    window_.showNormal();
  } else {
    window_.showFullScreen();
  }
}

}  // namespace patchy::ui
