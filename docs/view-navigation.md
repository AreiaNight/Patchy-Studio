# View navigation: the Zoom tool, Scrubby Zoom, and drag zoom

Read this before changing the Zoom tool, the pen ZoomCanvas drag, or wheel zoom. The tool palette and the Zoom button's double-click live in [tools.md](tools.md); the status-bar zoom box is `ZoomPercentEdit` (src/ui/zoom_status_bar.hpp).

## Zoom model

- Zoom is a continuous `double` (`CanvasWidget::zoom_`), clamped to `kMinZoom` 0.05 and `kMaxZoom` 128 (src/ui/canvas_widget_view.cpp). There is no preset-level table; menu Zoom In/Out multiply by 1.25 and 0.8 through `set_zoom_centered`, which UI presets must use (header comment in canvas_widget.hpp).
- `zoom_at_widget_point(widget_position, factor)` is the anchored helper: it keeps the document point under `widget_position` fixed, clamps, recomputes the pan, and calls `update_tool_cursor()`, `update()`, and `notify_view_changed()` on every step. A gesture cursor set before calling it is overwritten immediately, so drag-zoom gestures keep the tool cursor (the pen gesture's `SizeVerCursor` never sticks for this reason; cosmetic, unfixed).
- Wheel: Alt+wheel and the `input/wheelZooms` preference zoom at the cursor by 1.1 / 0.9 (canvas_widget_events.cpp); a macOS pinch uses `1 + value()`.

## Zoom tool (Z, `tools.zoom`)

Handled in src/ui/canvas_widget_events.cpp; the tool works while a preview dialog has editing locked (`edit_locked_ && !zooming_` guards).

- **Press** sets `zooming_` and records `zoom_start_` / `zoom_current_` clamped to the document frame (`clamped_document_point`), so a marquee begun in the grey margin stays on the canvas edge.
- **Click** (release with under `kZoomClickSlopPx` = 8 px of Manhattan travel, or a drag whose rect is at most 1 px on both axes): `zoom_at_widget_point(zoom_click_anchor(pos), 2.0)`, or 0.5 with Alt. `zoom_click_anchor` clamps the press position onto the document frame, so a margin click zooms toward the nearest document edge, never toward empty space.
- **Marquee** (Scrubby Zoom off, a real drag, Alt not held): `zoom_to_document_rect` with an 80 px margin. `draw_zoom_preview` (canvas_widget_view.cpp) paints the rect; the Alt badge on the cursor comes from `apply_zoom_cursor` (canvas_widget_cursors.cpp) via the `eventFilter` Alt watch. The info panel reports the rect as "Zoom".
- **Scrubby Zoom** (GitHub issue 51, Photoshop's gesture; `tools/zoomScrubby`, default off). The options-bar checkbox `zoomScrubbyCheck` ("Scrubby Zoom") shows for the Zoom tool only and, unlike the other tool rows, stays enabled under a preview-dialog edit lock (objectName exception in `refresh_options_bar`). It follows the Fill tool's Contiguous wiring: `CanvasWidget::set_zoom_scrubby`, the `current_zoom_scrubby_` mirror in MainWindow, `load_tool_settings` / `save_tool_settings`, the checkbox re-sync in `refresh_options_bar` and `sync_tool_option_controls_from_canvas`, and the push into every activated canvas in `activate_document_canvas`, so the option is one global preference across documents. Gesture: the press arms `zoom_scrubbing_` (a sub-state of `zooming_`, never the pen flag) with `zoom_drag_anchor_widget_ = zoom_click_anchor(press)` and `zoom_drag_last_pos_ = press`. Movement under the click slop keeps the release a click (2x, Alt 0.5x, exactly the branch above). The first move at or past the slop sets `zoom_scrub_started_` and applies the accumulated horizontal delta at once (no dead zone); every move after that calls `apply_zoom_drag_step(dx)`: `zoom_at_widget_point(anchor, kZoomDragFactorPerPixel ^ dx)` with right = in, left = out, vertical travel ignored. Release after a started scrub does nothing more. Alt only matters to a click (an Alt drag scrubs). While scrubbing there is no marquee preview, no "Zoom" info rect, and the marching-ants timer does not repaint for the gesture. Focus loss and `cancel_pointer_gestures` clear both scrub flags.
- Cursor and hotkey behavior are unchanged by the option: the magnifier stays, Alt still flips the badge.

## Pen ZoomCanvas drag

The pen button action `PenButtonAction::ZoomCanvas` (docs/tools.md pen section; `canvas_widget_pen.cpp` for tablet events, the pen-button-as-mouse path in `canvas_widget_events.cpp`) uses `begin_zoom_drag` / `update_zoom_drag` / `end_zoom_drag` (canvas_widget_view.cpp): vertical delta, up = in, anchored at the press, gated by `pen_zoom_dragging_`. It shares `zoom_drag_anchor_widget_`, `zoom_drag_last_pos_`, and `apply_zoom_drag_step` with Scrubby Zoom; the two gestures cannot overlap because a left press ends a pen-button gesture first.

`kZoomDragFactorPerPixel` = 1.01 is the one sensitivity constant for both gestures: about 70 px doubles the zoom, 100 px is 2.7x.

## Coverage

- `ui_zoom_tool_scrubby_option_persists_and_reaches_canvas` (checkbox default, visibility, `tools/zoomScrubby`, second document), `ui_zoom_tool_scrubby_drag_zooms_live_around_press_point` (1.01 per pixel, anchor invariance, click and Alt-click unchanged, Alt drag scrubs, vertical travel inert, option off restores the marquee), `ui_shape_flyout_and_zoom_tool_work` (marquee, button double-click), `ui_options_bar_tracks_active_tool` (the checkbox hides for other tools), all in tests/ui/canvas_view_tools_tests.cpp.
- `ui_pen_zoom_button_drag_changes_zoom_without_painting` (tests/ui/pen_tablet_input_tests.cpp) pins the pen gesture through the shared helper.
- `ui_zoom_tool_double_click_keeps_view_centered_at_actual_pixels`, `ui_image_resize_recenters_view_and_zoom_double_click_shows_document`, `ui_zoom_preset_recovers_parked_view`, `ui_canvas_wheel_zoom_mode_zooms_at_cursor`, `ui_status_bar_zoom_percent_box_edits_zoom` cover the rest of the zoom surface.
