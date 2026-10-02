// Patchy Studio, the Procreate-style shell over MainWindow (studio_shell.hpp):
// it hides the classic chrome, its top-bar tools switch the canvas tool and
// open the panels, the side bar drives the brush controls, the Layers panel
// adds layers through the normal command, and the gallery lists open work.
// Each test saves an artifact of the state it checks (test-artifacts/studio-*).

#include "core/document.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/color_wheel_panel.hpp"
#include "ui/main_window.hpp"
#include "ui/studio_widgets.hpp"

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QDockWidget>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QToolBar>

namespace {

using patchy::test::ui::drag;
using patchy::test::ui::process_events_for;
using patchy::test::ui::require_hotkey_action;
using patchy::test::ui::save_widget_artifact;
using patchy::test::ui::send_mouse;
using patchy::test::ui::show_window;
using patchy::ui::CanvasTool;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;

// The request is static (main.cpp sets it before the window exists); restore the
// classic default so later tests in the suite build the classic window.
auto request_studio() {
  MainWindow::request_studio_shell(true);
  return qScopeGuard([] { MainWindow::request_studio_shell(false); });
}

QAbstractButton* studio_button(MainWindow& window, const char* name) {
  auto* button = window.findChild<QAbstractButton*>(QLatin1String(name));
  CHECK(button != nullptr);
  return button;
}

void click(QAbstractButton* button) {
  button->click();
  process_events_for(30);
}

void ui_studio_shell_hides_classic_chrome_and_drives_tools() {
  const auto studio = request_studio();
  MainWindow window;
  window.enable_studio_shell();
  show_window(window);
  process_events_for(60);

  CHECK(!window.menuBar()->isVisible());
  for (auto* dock : window.findChildren<QDockWidget*>()) {
    CHECK(!dock->isVisible());
  }
  for (auto* toolbar : window.findChildren<QToolBar*>(Qt::FindDirectChildrenOnly)) {
    CHECK(!toolbar->isVisible());
  }
  auto* top_bar = window.findChild<QWidget*>(QStringLiteral("studioTopBar"));
  CHECK(top_bar != nullptr && top_bar->isVisible());
  auto* side_bar = window.findChild<QWidget*>(QStringLiteral("studioSideBar"));
  CHECK(side_bar != nullptr && side_bar->isVisible());
  // A shortcut that lived on the hidden menu bar still reaches its action.
  CHECK(window.actions().contains(require_hotkey_action(window, QStringLiteral("edit.undo"))));
  save_widget_artifact("studio-shell", window);

  auto* canvas = MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  auto* eraser = studio_button(window, "studioEraserButton");
  click(eraser);
  CHECK(canvas->tool() == CanvasTool::Eraser);
  CHECK(eraser->isChecked());
  CHECK(!studio_button(window, "studioBrushButton")->isChecked());

  // Tapping the active tool again opens the Brush Library on its brushes.
  click(eraser);
  auto* brushes = window.findChild<QListWidget*>(QStringLiteral("studioBrushList"));
  CHECK(brushes != nullptr && brushes->isVisible());
  CHECK(brushes->count() >= 2);
  save_widget_artifact("studio-brush-library", window);
  click(eraser);
  CHECK(!brushes->isVisible());

  click(studio_button(window, "studioBrushButton"));
  CHECK(canvas->tool() == CanvasTool::Brush);

  // Dragging the side bar's size slider to its top sets the options-bar size.
  auto* size_slider = window.findChild<patchy::ui::StudioSlider*>(QStringLiteral("studioSizeSlider"));
  auto* size_spin = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  CHECK(size_slider != nullptr && size_spin != nullptr);
  const QPoint top(size_slider->width() / 2, 2);
  send_mouse(*size_slider, QEvent::MouseButtonPress, top, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*size_slider, QEvent::MouseButtonRelease, top, Qt::LeftButton, Qt::NoButton);
  process_events_for(30);
  CHECK(size_slider->value() == size_slider->maximum());
  CHECK(size_spin->value() == size_slider->value());
  CHECK(canvas->brush_size() == size_slider->value());

  // The modify button is a one-shot eyedropper.
  click(studio_button(window, "studioModifyButton"));
  CHECK(canvas->tool() == CanvasTool::Eyedropper);
  click(studio_button(window, "studioModifyButton"));
  CHECK(canvas->tool() == CanvasTool::Brush);
}

void ui_studio_shell_panels_open_and_edit() {
  const auto studio = request_studio();
  MainWindow window;
  window.enable_studio_shell();
  show_window(window);
  process_events_for(60);
  auto* canvas = MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  auto* document = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(document != nullptr);

  click(studio_button(window, "studioLayersButton"));
  auto* add_layer = studio_button(window, "studioAddLayerButton");
  CHECK(add_layer->isVisible());
  const auto layers_before = document->layers().size();
  click(add_layer);
  process_events_for(60);
  CHECK(document->layers().size() == layers_before + 1);
  save_widget_artifact("studio-layers", window);

  click(studio_button(window, "studioColorButton"));
  auto* wheel = window.findChild<patchy::ui::ColorWheelPanel*>(QStringLiteral("studioColorWheel"));
  CHECK(wheel != nullptr && wheel->isVisible());
  save_widget_artifact("studio-color", window);

  click(studio_button(window, "studioActionsButton"));
  save_widget_artifact("studio-actions", window);

  click(studio_button(window, "studioAdjustmentsButton"));
  save_widget_artifact("studio-adjustments", window);
  click(studio_button(window, "studioAdjustmentsButton"));

  click(studio_button(window, "studioSelectionButton"));
  CHECK(canvas->tool() == CanvasTool::Lasso);
  auto* selection_bar = window.findChild<QWidget*>(QStringLiteral("studioSelectionBar"));
  CHECK(selection_bar != nullptr && selection_bar->isVisible());
  click(studio_button(window, "studioSelectRectangle"));
  CHECK(canvas->tool() == CanvasTool::Marquee);
  save_widget_artifact("studio-selection", window);
  click(studio_button(window, "studioSelectionButton"));
  CHECK(canvas->tool() == CanvasTool::Brush);
  CHECK(!selection_bar->isVisible());

  click(studio_button(window, "studioGalleryButton"));
  auto* gallery = window.findChild<QWidget*>(QStringLiteral("studioGallery"));
  CHECK(gallery != nullptr && gallery->isVisible());
  process_events_for(200);
  save_widget_artifact("studio-gallery", window);
}

void ui_studio_shell_paints_undoes_and_transforms() {
  const auto studio = request_studio();
  MainWindow window;
  window.enable_studio_shell();
  show_window(window);
  process_events_for(60);
  auto* canvas = MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);

  // Choosing a brush in the library sets the canvas tip and closes nothing else.
  click(studio_button(window, "studioBrushButton"));
  click(studio_button(window, "studioBrushButton"));
  auto* brushes = window.findChild<QListWidget*>(QStringLiteral("studioBrushList"));
  CHECK(brushes != nullptr && brushes->count() >= 2);
  auto* square = brushes->item(1);
  emit brushes->itemClicked(square);
  process_events_for(30);
  // The built-in Square is a procedural shape, not a library tip id.
  CHECK(square->data(Qt::UserRole + 1).toString() == patchy::ui::builtin_square_brush_tip_id());
  CHECK(canvas->brush_shape() == patchy::BrushShape::Square);

  // A press on the canvas closes the open panel, then a stroke paints and the side
  // bar's Undo takes it back.
  const QPoint center = canvas->rect().center();
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  process_events_for(30);
  CHECK(!brushes->isVisible());
  const auto depth_before = MainWindowTestAccess::active_session_undo_depth(window);
  drag(*canvas, center + QPoint(-120, -40), center + QPoint(140, 60));
  process_events_for(60);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) > depth_before);
  auto* undo = studio_button(window, "studioUndoButton");
  CHECK(undo->isEnabled());
  save_widget_artifact("studio-stroke", window);
  const auto depth_after_stroke = MainWindowTestAccess::active_session_undo_depth(window);
  click(undo);
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) < depth_after_stroke);
  click(studio_button(window, "studioRedoButton"));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth_after_stroke);

  // Transform opens a free-transform session with its bottom bar; Done commits it.
  click(studio_button(window, "studioTransformButton"));
  process_events_for(60);
  CHECK(canvas->free_transform_active());
  auto* transform_bar = window.findChild<QWidget*>(QStringLiteral("studioTransformBar"));
  CHECK(transform_bar != nullptr && transform_bar->isVisible());
  save_widget_artifact("studio-transform", window);
  click(studio_button(window, "studioTransformDone"));
  process_events_for(60);
  CHECK(!canvas->free_transform_active());
  click(studio_button(window, "studioTransformButton"));
  CHECK(canvas->tool() != CanvasTool::Move);
  CHECK(!transform_bar->isVisible());
}

}  // namespace

std::vector<patchy::test::TestCase> studio_shell_tests() {
  return {
      {"ui_studio_shell_hides_classic_chrome_and_drives_tools", ui_studio_shell_hides_classic_chrome_and_drives_tools},
      {"ui_studio_shell_panels_open_and_edit", ui_studio_shell_panels_open_and_edit},
      {"ui_studio_shell_paints_undoes_and_transforms", ui_studio_shell_paints_undoes_and_transforms},
  };
}
