# Patchy Studio (Procreate-style interface)

Read this before changing `src/ui/studio_*`, `src/ui/main_window_studio.cpp`, or the
MainWindow hooks that refresh the shell.

Patchy Studio is the `v2/` desktop build: the same editor engine and MainWindow, with a
Procreate-like face instead of the Photoshop-style chrome. Launching the app opens Studio;
`--classic` opens the classic interface. Automation runs (`--headless`, `--export`,
`--stress-test`) always build the classic window their scripts expect.

## Ownership

| File | Owns |
|---|---|
| `ui/studio_shell.{hpp,cpp}` | `StudioShell`: hides the classic chrome, builds the top bar, side bar and bottom bars, owns the popover and gallery, and is the only bridge into MainWindow (it is MainWindow's friend). |
| `ui/studio_panels.{hpp,cpp}` | The dropping panels: Brush Library, Layers, Colors, Actions, Adjustments. They reach the editor only through `StudioShell`'s public bridge. |
| `ui/studio_gallery.{hpp,cpp}` | The gallery of open and recent artwork, the New canvas presets, and the thumbnail cache. |
| `ui/studio_widgets.{hpp,cpp}` | Painted primitives: line icons, icon/color buttons, pill sliders, the value bubble, `StudioPopover`, and the shared panel QSS. |
| `ui/studio_navigator.{hpp,cpp}` | The navigator panel: overview thumbnail with the viewport outline, zoom and rotation rows, Fit, 100% and 0°. |
| `ui/main_window_studio.cpp` | `MainWindow::enable_studio_shell` and `notify_studio_shell`. |

## How the shell drives the editor

The shell never reimplements an edit. Every control goes through an entry point the
classic chrome already uses, so undo, history, settings persistence and tool state stay
identical:

- Commands run through the hotkey registry by id (`run_command("layer.new")`). Panel rows
  that run a command show the command's own translated text (`clean_action_text`).
- The side bar sliders write the hidden options-bar spin boxes `brushSizeSpin` and
  `brushOpacitySpin`; the layer opacity and blend editors write `opacity_spin_` and
  `blend_combo_`. Their existing signal paths do the rest.
- Tools switch through `MainWindow::activate_tool`; layers select through
  `select_layers_in_layer_list`; colors go through `apply_color_wheel_color`.
- The Adjustments panel lists the live `imageAdjustmentsMenu` and `filterMenu` actions, so
  new filters appear without studio changes.

The classic menu bar, toolbars, docks and status bar still exist, hidden. Every registered
command is also added to the window itself, which keeps shortcuts working with the menu
bar hidden. Docks that a command tries to show are hidden again.

## Refresh

MainWindow calls `notify_studio_shell()` at the start of `refresh_layer_list`,
`refresh_layer_thumbnails`, `refresh_layer_controls`, `refresh_color_buttons`,
`sync_brush_controls_from_canvas`, `activate_tool`, `activate_document_canvas`,
`update_undo_redo_actions`, `set_active_brush_tip` and `update_start_panel_visibility`.
The call only queues one zero-delay refresh, so it is safe in hot paths. A new editor
state that the shell shows needs a hook in the function that changes it. Panels rebuild
their rows only when a signature of what they show changes.

`~MainWindow` deletes the shell first: it holds raw pointers into the canvas area.

## Modes and gestures

- Tapping Brush, Smudge or Eraser selects the tool; tapping the active one opens the
  Brush Library.
- Selection activates Freehand (Lasso) and shows the selection bar; tapping it again
  returns to the previous paint tool. Transform starts Free Transform with the transform
  bar; Done and Cancel click the classic apply and cancel buttons.
- The side bar's square button is a one-shot eyedropper: it returns to the previous tool
  after the first release on the canvas.
- A press on the canvas closes an open panel. Esc closes it too.
- A newly opened document hides the gallery; closing the last one shows it.

## Input

The app takes no touch input. `main.cpp` turns off
`Qt::AA_SynthesizeMouseForUnhandledTouchEvents`, the canvas swallows touchpad gestures
(`QNativeGestureEvent`), and `CanvasWidget::should_drop_foreign_mouse_event` drops mouse
events from touch devices and every non-tablet mouse event while a pen stroke is open. A
stroke whose tablet release is lost ends at its last tip position on the next hover move or
press (`finish_pen_stroke_at_last_position`). Before this, a hover after a lost release, or
a pointer event injected mid-stroke, painted a straight line across the artwork. Test:
`ui_pen_stroke_ignores_foreign_pointer_events`.

## Navigator and handedness

The navigator floats in the bottom corner on the side-bar side; the side bar centers in
the space above it. It drives the active canvas through its view API only
(`visible_document_polygon`, `center_view_on_document_point`, `set_zoom_centered`,
`fit_to_view`), so it never touches the document or undo. MainWindow's
`handle_canvas_view_changed` calls `notify_studio_view_changed()` on every pan and zoom
step, which only repaints the navigator. The overview comes from the canvas render cache
(`overview_image`), polled every 400 ms while visible and rebuilt only when
`overview_image_key()` or the document size changed, never mid-stroke. The zoom slider
is log-scaled between the canvas zoom limits; a double-click resets to 100%. The
rotation row turns the view in 15 degree steps or freely from its slider (double-click or
0° straightens); the thumbnail stays upright and the viewport outline turns, as in
Photoshop. View rotation itself lives in the canvas: see "View rotation" in
[view-navigation.md](view-navigation.md).

Actions > Prefs offers Dominant hand as two explicit choices. Left-handed moves the side
bar and the navigator to the right edge. The old single "Right-hand interface" switch
read as "for right-handed people" in translation while it actually moved the controls
right, so it was replaced.

## Settings and coexistence

Studio stores preferences in `PatchyStudio.ini` beside classic Patchy's `Patchy.ini`
(`app_settings()`), so window layout, dock state and recent files never leak between the
two apps, while the brush library folder is shared. Studio keys (persisted identifiers):
`studio/rightHanded` (true = side bar on the right, the left-handed layout),
`studio/colorHistory`, `studio/navigatorVisible`. Its single-instance channel is
`PatchyStudio-SingleInstance-<user>`. On Windows the shell keeps the native window frame
(`use_custom_window_chrome` is off when Studio was requested), because the classic custom
frame lives in the menu bar Studio hides.

Gallery thumbnails of documents with a path are cached as PNG under
`<AppLocalData>/studio-thumbnails/<sha1 of the path>.png`, so recent PSDs show a picture
without reopening.

## Colors

All studio chrome reads the `studio_*` roles in `theme_palette.hpp` (painted, or as
`@tokens` in QSS). The token resolver writes `#rrggbb`, so translucent roles are only
usable when painted. Widgets that paint only part of their rect set the
`studioTransparent` property and a matching transparent rule, because the app sheet gives
plain widgets a window background.

## Tests

`tests/ui/studio_shell_tests.cpp` (filter `studio`) builds the window with
`request_studio_shell(true)` and restores the classic default afterwards. Artifacts:
`test-artifacts/studio-*.png`.
