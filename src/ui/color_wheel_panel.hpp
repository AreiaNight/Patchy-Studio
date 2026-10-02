#pragma once

#include "core/color_wheel.hpp"

#include <QColor>
#include <QWidget>

#include <vector>

class QComboBox;
class QLineEdit;
class QToolButton;

namespace patchy::ui {

class ColorWheelWidget;
class ColorCompareSwatch;
class ColorSwatchStrip;

// Settings shared by the panel and the HUD (keys colorWheel/shape, colorWheel/model,
// colorWheel/toneLock, colorWheel/harmony, colorWheel/harmonySpread; tokens in
// docs/color-wheel.md).
struct ColorWheelSettings {
  ColorWheelShape shape{ColorWheelShape::Square};
  ColorWheelModel model{ColorWheelModel::Rgb};
  bool tone_lock{false};
  ColorHarmony harmony{ColorHarmony::None};
  double harmony_spread{kHarmonySpreadDefault};
};
[[nodiscard]] ColorWheelSettings load_color_wheel_settings();
void save_color_wheel_settings(const ColorWheelSettings& settings);

// Saved colors (colorWheel/savedColors, "#rrggbb" per slot, "" = empty), shared by the panel
// and the HUD. Always kSavedColorSlots entries; an invalid QColor is an empty slot.
inline constexpr int kSavedColorSlots = 8;
[[nodiscard]] std::vector<QColor> load_saved_wheel_colors();
void save_saved_wheel_colors(const std::vector<QColor>& colors);

// The Color Wheel panel body: field-shape and wheel-model choices, Tone Lock, the wheel, and a
// new/previous swatch (the previous half restores its color on click) with an editable hex
// code. Compact mode (the HUD) keeps only the wheel, the swatch and the color strips. set_foreground follows every foreground change, so an
// eyedropper sample lands beside the color it replaced.
class ColorWheelPanel final : public QWidget {
  Q_OBJECT

public:
  explicit ColorWheelPanel(bool compact, QWidget* parent = nullptr);

  void set_foreground(QColor color);
  void apply_settings(const ColorWheelSettings& settings);
  [[nodiscard]] ColorWheelSettings settings() const;
  [[nodiscard]] ColorWheelWidget* wheel() const noexcept { return wheel_; }
  [[nodiscard]] QColor current_color() const noexcept { return current_; }
  [[nodiscard]] QColor previous_color() const noexcept { return previous_; }
  [[nodiscard]] const std::vector<QColor>& saved_colors() const noexcept { return saved_colors_; }
  // Re-reads the saved colors from settings (after the other panel changed them).
  void reload_saved_colors();
  // Saves the current color into the first empty slot; when all are full the oldest (first)
  // slot drops out and the rest shift left.
  void save_current_color();

protected:
  void changeEvent(QEvent* event) override;

signals:
  // A color chosen here (wheel drag, or the previous swatch); finished on release.
  void color_picked(QColor color, bool finished);
  // The user changed shape, model, Tone Lock or the harmony (already saved).
  void settings_changed();
  // The saved colors changed (already written to settings).
  void saved_colors_changed();

private:
  void refresh_swatch();
  // Rings the saved slot that holds the current color, if any.
  void refresh_saved_marker();
  // Labels and tooltips; runs at construction and on every language switch.
  void retranslate();

  ColorWheelWidget* wheel_{nullptr};
  ColorCompareSwatch* swatch_{nullptr};
  QLineEdit* hex_edit_{nullptr};
  QComboBox* shape_combo_{nullptr};
  QComboBox* model_combo_{nullptr};
  QToolButton* tone_lock_button_{nullptr};
  QComboBox* harmony_combo_{nullptr};
  ColorSwatchStrip* harmony_strip_{nullptr};
  ColorSwatchStrip* saved_strip_{nullptr};
  QToolButton* save_color_button_{nullptr};
  std::vector<QColor> saved_colors_;
  void set_saved_color(int index, QColor color);
  QColor current_{Qt::black};
  QColor previous_{Qt::black};
  bool editing_{false};
};

// The wheel under the pointer: a translucent overlay inside the main window (so it behaves
// the same on every platform, wasm included). It opens at the pointer, hides after a pick or
// an outside click unless Keep Open is on, resizes with the mouse wheel (colorWheel/hudSize)
// and, while kept open, moves by dragging its rim (colorWheel/hudPersistent).
class ColorWheelHud final : public QWidget {
  Q_OBJECT

public:
  explicit ColorWheelHud(QWidget* parent);

  [[nodiscard]] ColorWheelPanel* panel() const noexcept { return panel_; }
  // Centers the HUD on `global_position`, clamped inside the parent.
  void show_at(QPoint global_position);
  void set_persistent(bool persistent);
  [[nodiscard]] bool persistent() const noexcept { return persistent_; }
  [[nodiscard]] int hud_size() const noexcept { return size_; }
  void set_hud_size(int size);

  static constexpr int kMinimumSize = 160;
  static constexpr int kMaximumSize = 520;

protected:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void changeEvent(QEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void retranslate();

  ColorWheelPanel* panel_{nullptr};
  QToolButton* pin_button_{nullptr};
  QToolButton* close_button_{nullptr};
  bool persistent_{false};
  int size_{260};
  QPoint drag_offset_{};
  bool moving_{false};
};

}  // namespace patchy::ui
