#pragma once

// Patchy Studio: a Procreate-style face for MainWindow.
//
// The shell hides MainWindow's Photoshop-style chrome (menu bar, tool palette,
// options bar, docks, status bar, document tabs) and lays its own controls over
// the canvas: a thin top bar (Gallery, Actions, Adjustments, Selection,
// Transform on the left; Brush, Smudge, Eraser, Layers, Color on the right), a
// floating side bar (brush size, eyedropper, opacity, undo/redo), dropping
// panels, bottom bars for the selection and transform modes, and a gallery of
// open and recent artwork.
//
// It drives the editor through the same entry points the classic chrome uses
// (registered hotkey commands, MainWindow's layer and color members, the hidden
// options-bar spin boxes), so every edit keeps its undo, history, and settings
// behavior. StudioShell is MainWindow's friend; the panels in studio_panels.hpp
// reach the window only through the bridge methods below.

#include "core/layer.hpp"

#include <QColor>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

class QAction;
class QMenu;
class QTimer;
class QWidget;

namespace patchy {
class Document;
}

namespace patchy::ui {

class BrushTipLibrary;
class CanvasWidget;
class MainWindow;
class StudioBubble;
class StudioColorButton;
class StudioGallery;
class StudioIconButton;
class StudioPopover;
class StudioSlider;
enum class CanvasTool;

class StudioShell final : public QObject {
  Q_OBJECT

public:
  explicit StudioShell(MainWindow& window);
  ~StudioShell() override;

  // Coalesces any number of editor-state changes into one refresh on the next
  // event-loop pass.
  void schedule_refresh();

  // --- Bridge for the panels -------------------------------------------------
  [[nodiscard]] QWidget* host() const noexcept { return host_; }
  [[nodiscard]] CanvasWidget* canvas() const;
  [[nodiscard]] const Document* document() const;
  // The registered action for a hotkey command id ("layer.new"), or null.
  [[nodiscard]] QAction* command(const QString& id) const;
  void run_command(const QString& id);
  [[nodiscard]] QMenu* window_menu(const QString& object_name) const;

  [[nodiscard]] QPixmap layer_thumbnail(const Layer& layer);
  [[nodiscard]] std::vector<LayerId> selected_layer_ids() const;
  void select_layer(LayerId id);
  void set_layer_visible(LayerId id, bool visible);
  void set_active_layer_opacity(int percent);
  void set_active_layer_blend_mode(BlendMode mode);
  void rename_layer(LayerId id);
  void add_layer();
  void toggle_group_collapsed(LayerId id);
  [[nodiscard]] bool group_collapsed(LayerId id) const;

  [[nodiscard]] BrushTipLibrary& brush_library() const;
  [[nodiscard]] QString active_brush_tip_id() const;
  void select_brush_tip(const QString& id);
  void open_brush_settings();
  void import_brushes();

  [[nodiscard]] QColor primary_color() const;
  [[nodiscard]] QColor secondary_color() const;
  void set_primary_color(QColor color, bool finished);
  [[nodiscard]] const std::vector<QColor>& color_history() const noexcept { return color_history_; }

  // Gallery entries.
  struct OpenDocument {
    int tab_index{0};
    QString title;
    QString path;
    QSize size;
    bool modified{false};
    bool active{false};
  };
  [[nodiscard]] std::vector<OpenDocument> open_documents() const;
  [[nodiscard]] QStringList recent_files() const;
  // Small composite of an open document, rendered off the GUI thread; `ready`
  // runs on the GUI thread with the image.
  void render_document_thumbnail(int tab_index, QSize bound, std::function<void(QImage)> ready);
  void activate_document(int tab_index);
  void close_document(int tab_index);
  void open_recent(const QString& path);
  void open_file_dialog();
  void create_canvas(int width, int height, double ppi);
  void create_canvas_with_dialog();
  void show_gallery();
  void hide_gallery();

  [[nodiscard]] bool right_handed() const noexcept { return right_handed_; }
  void set_right_handed(bool enabled);
  void set_light_interface(bool enabled);
  [[nodiscard]] bool light_interface() const;
  void toggle_full_screen();
  [[nodiscard]] bool full_screen() const;
  void show_about();
  void close_panels();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  enum class Panel { None, Actions, Adjustments, Brushes, Layers, Color };
  enum class Mode { Paint, Selection, Transform };

  void hide_classic_chrome();
  void build_top_bar();
  void build_side_bar();
  void build_bottom_bars();
  void layout_overlay();
  void refresh();
  void refresh_tool_buttons();
  void refresh_side_bar();
  void refresh_mode_bars();

  void toggle_panel(Panel panel, StudioIconButton* anchor);
  void open_panel(Panel panel, QWidget* anchor);
  void on_paint_tool_clicked(CanvasTool tool);
  void on_selection_clicked();
  void on_transform_clicked();
  void on_modify_clicked();
  void activate_tool(CanvasTool tool);
  [[nodiscard]] CanvasTool current_tool() const;
  [[nodiscard]] Mode current_mode() const;
  void remember_color(QColor color);
  void show_slider_bubble(StudioSlider* slider, const QString& text);

  MainWindow& window_;
  QWidget* host_{nullptr};
  QTimer* refresh_timer_{nullptr};

  QWidget* top_bar_{nullptr};
  StudioIconButton* gallery_button_{nullptr};
  StudioIconButton* actions_button_{nullptr};
  StudioIconButton* adjustments_button_{nullptr};
  StudioIconButton* selection_button_{nullptr};
  StudioIconButton* transform_button_{nullptr};
  StudioIconButton* brush_button_{nullptr};
  StudioIconButton* smudge_button_{nullptr};
  StudioIconButton* eraser_button_{nullptr};
  StudioIconButton* layers_button_{nullptr};
  StudioColorButton* color_button_{nullptr};

  QWidget* side_bar_{nullptr};
  StudioSlider* size_slider_{nullptr};
  StudioSlider* opacity_slider_{nullptr};
  StudioIconButton* modify_button_{nullptr};
  StudioIconButton* undo_button_{nullptr};
  StudioIconButton* redo_button_{nullptr};
  StudioBubble* bubble_{nullptr};
  QTimer* bubble_timer_{nullptr};
  bool slider_dragging_{false};

  StudioPopover* popover_{nullptr};
  Panel open_panel_{Panel::None};
  StudioPopover* selection_bar_{nullptr};
  StudioPopover* transform_bar_{nullptr};
  StudioGallery* gallery_{nullptr};

  // The tool to return to after a one-shot eyedropper pick or leaving Selection.
  std::optional<CanvasTool> return_tool_;
  bool eyedropper_one_shot_{false};
  QColor last_primary_;
  std::vector<QColor> color_history_;
  bool right_handed_{false};
  bool refreshing_{false};
  std::size_t last_session_count_{0};
};

}  // namespace patchy::ui
