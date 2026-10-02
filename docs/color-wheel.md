# Color Wheel panel and HUD

A painter's color wheel in the right dock column (`colorWheelDock`) plus the same wheel as a
translucent HUD at the pointer. Read before changing either. Binding legal boundary: the
"Color wheel" bullet in [legal-constraints.md](legal-constraints.md).

## Pieces

- `core/color_wheel.*`: all color math, Qt-free and deterministic doubles. HSV, the RGB/RYB
  ring remap, the three field shapes, OKLab lightness and Tone Lock.
- `ui/color_wheel_widget.*` (`ColorWheelWidget`): the ring and field, cached as images per size,
  model, shape and hue. Drags emit `color_edited(color, finished)`; `set_color` never emits, and
  a gray keeps the current hue so the ring does not jump to red.
- `ui/color_wheel_panel.*`: `ColorWheelPanel` (shape and model combos, then harmony and Tone
  Lock, the wheel, and the new/previous swatch over the hex field `colorWheelHexEdit`) and `ColorWheelHud` (compact panel, Keep Open pin, close).
- MainWindow: `create_color_wheel_dock`, `apply_color_wheel_color` (foreground, active text
  editor, open picker, `refresh_color_buttons`), and `show_color_wheel_hud`.
  `refresh_color_buttons` is the single place that pushes every foreground change (eyedropper,
  swatches, swap, the wheel's own echo) into the panel and the HUD.

## Behavior

- **Ring:** `ColorWheelModel::Rgb` is the additive wheel. `Ryb` is the painter's wheel: a
  monotone piecewise-linear remap anchored at red 0, orange 60, yellow 120, green 180, blue 240
  and violet 300 degrees (RGB hues 0, 30, 60, 120, 240 and 280), so painter's complements sit
  opposite each other.
- **Field:** each shape is inscribed in the unit circle, with y up.
  - Square: HSV, saturation right, value up.
  - Triangle: HSV with fixed corners: pure hue right, white upper left, black lower left.
  - Diamond: HSL, white top, black bottom, gray left, pure hue right.
  - `wheel_field_point` and `wheel_field_color` invert each other exactly.
- **Tone Lock:** a ring drag takes the new hue's color in OKLab, restores the lightness the
  drag started from, and pulls chroma in until the color fits sRGB. Field drags change value
  deliberately and are never locked.
- **Harmony** (`ColorHarmony`, combo `colorWheelHarmonyCombo`): Complementary, Analogous,
  Triadic, Split Complementary, Rectangle and Square add square markers on the ring, with
  dashed guides from the center.
  - The angles live in ring space (`wheel_harmony_angle`), so the RYB ring gives the painter's
    complements.
  - The markers follow the main hue.
  - Dragging a secondary marker of a spread harmony (analogous, split, rectangle) sets the
    spread through `wheel_harmony_spread_for_angle`, clamped to 5..90 degrees. The rectangle's
    complement is fixed.
  - The harmony strip shows each hue at the current field position (and, with Tone Lock, at the
    current lightness); a click uses that color.
- **Saved colors:** `kSavedColorSlots` (8) slots shared by the panel and the HUD.
  - "+" fills the first empty slot. When the row is full, the oldest drops out and the rest
    shift left.
  - A click uses a color; a click on an empty slot saves the current color there.
  - Right-click offers "Save Current Color Here" and "Remove".
- **Swatch:** "new" over "previous". An external foreground change moves new into previous;
  during the panel's own drag, previous stays at the color the drag started from. Clicking the
  previous half restores that color and swaps the two.
- **Hex field:** shows the new color as `#RRGGBB`. Enter with six digits (or three, expanded like
  CSS) picks that color; anything incomplete reverts. Hidden in the HUD.
- **Strips:** empty saved slots are dashed wells; the slot holding the current color gets an
  `accent` ring, and the hovered cell a `button_hover_border` ring.
- **HUD:** a child overlay of the main window, not a top-level window, so it behaves the same
  on every platform, wasm included, and is clamped inside the window.
  - Opens centered on the pointer from Window > Color Wheel at Pointer (`window.color_wheel_hud`,
    no default key) or a pen button set to Show color wheel (`PenButtonAction::ShowColorWheel`,
    token `colorWheel`).
  - Unless Keep Open is on, a finished pick, an outside press (application event filter, and
    the press still reaches its target) or Escape hides it.
  - The mouse wheel resizes it in 20 px steps between 160 and 520 px. While it is open, dragging
    its rim moves it.
- Window > Color Wheel (`window.color_wheel`, checkable, mirrors the dock) shows the dock expanded or hides it. The dock starts closed: its collapsed title alone pushed the all-expanded right column past the 950 px budget that `ui_right_dock_panels_expand_within_window_height` guards (a 1080p work area). Keep it closed by default unless that budget is re-planned. Once opened it survives a restart in the state it was left in: visible (`colorWheel/panelVisible`, written only by the menu path, since teardown hides docks) and expanded or collapsed (`colorWheel/panelExpanded`, written by the collapse toggle while docked). A dock that starts expanded drops its preferred-height demand at once, because `handle_right_dock_panel_toggled` skips the release before the window is shown.

## Settings (persisted identifiers)

- `colorWheel/shape` (`square` | `triangle` | `diamond`)
- `colorWheel/model` (`rgb` | `ryb`)
- `colorWheel/toneLock`
- `colorWheel/hudSize`
- `colorWheel/hudPersistent`
- `colorWheel/panelVisible` (default false)
- `colorWheel/panelExpanded` (default true)
- `colorWheel/harmony` (`none` | `complementary` | `analogous` | `triadic` | `splitComplementary` |
  `rectangle` | `square`)
- `colorWheel/harmonySpread` (degrees)
- `colorWheel/savedColors` (8 strings: `#rrggbb`, or empty for an empty slot)

The panel and the HUD share shape, model and Tone Lock: each saves on change and the window
mirrors it into the other.

## Colors and themes

The ring and field are content (the colors being chosen). Their markers are black-plus-white
pairs, the same exemption as marching ants in [ui-conventions.md](ui-conventions.md): they
must read over any color. Everything else is a theme role:
- the HUD glass is `panel_bg` at partial alpha;
- its outline is `panel_border_strong`;
- the swatch frame is `field_inset_border`;
- the pin and close glyphs are `themed_glyph_icon` with `text_primary`.

## Not implemented

- Munsell (its renotation data would need a licensing review).
- Temperature and perceptual sliders.
- Color history (the saved colors are explicit, not automatic).
- A HUD that extends outside the main window.

## Legal record (2026-09-30)

The design is original, and "MagicPicker", "ColorPicker" and "Coolorus" (a registered mark)
are never used; the UI says "Color Wheel".

- US 8089492 (hue strip plus saturation/brightness bar, Just2easy) expired 2020-01-03.
- Adobe US 11087503 (active to 2037) claims mixing color "blobs" on a palette canvas with
  parametric gradients between them; the wheel mixes nothing (not practiced).
- A color picker at the cursor is long prior art: Photoshop's HUD color picker (CS5, 2010),
  Krita's popup palette and Painter's temporal palette.
- RYB wheels (Itten, 1961) and OKLab (Ottosson, 2020, public domain) are free to use.

- Harmony rules on a wheel with draggable nodes are old public practice: Itten's schemes,
  Adobe Kuler (2006; any patent from that window has expired), Paletton and Color Scheme
  Designer.
- The Coolorus distributable was deliberately not opened. Its code and artwork are licensed
  and copyrighted, so everything here is written from color theory and Patchy's own UI.
  Never import a third-party panel's files to "match" it.

A keyword sweep cannot prove a negative. Give any new wheel gizmo (color harmonies, mixing
areas, gamut masks) its own check.

## Tests

- `color_wheel_math_round_trips_and_tone_lock_keeps_lightness` (core).
- `color_wheel_harmonies_place_markers_and_invert_spread` (core).
- `ui_color_wheel_harmony_and_saved_colors`.
- `ui_color_wheel_panel_sets_foreground_and_tracks_previous`.
- `ui_color_wheel_tone_lock_and_settings_persist`.
- `ui_color_wheel_hud_opens_picks_and_dismisses`.
