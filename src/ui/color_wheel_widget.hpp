#pragma once

#include "core/color_wheel.hpp"

#include <QColor>
#include <QImage>
#include <QWidget>

#include <vector>

namespace patchy::ui {

// The hue ring plus its inner field (square, triangle or diamond) shared by the Color Wheel
// panel and the on-canvas HUD. Drags emit color_edited continuously and once more with
// finished = true on release; set_color never emits. Math lives in core/color_wheel.
class ColorWheelWidget final : public QWidget {
  Q_OBJECT

public:
  explicit ColorWheelWidget(QWidget* parent = nullptr);

  // External color changes (foreground set elsewhere). Grays keep the current hue so the
  // ring does not jump to red.
  void set_color(QColor color);
  [[nodiscard]] QColor color() const { return color_; }
  [[nodiscard]] double hue() const noexcept { return hue_; }

  void set_model(ColorWheelModel model);
  [[nodiscard]] ColorWheelModel model() const noexcept { return model_; }
  void set_shape(ColorWheelShape shape);
  [[nodiscard]] ColorWheelShape shape() const noexcept { return shape_; }
  // Ring drags keep the starting color's perceptual lightness (core wheel_tone_locked).
  void set_tone_lock(bool enabled) noexcept { tone_lock_ = enabled; }
  [[nodiscard]] bool tone_lock() const noexcept { return tone_lock_; }
  // Harmony markers on the ring. They follow the main hue; dragging a secondary marker of a
  // harmony with a spread (analogous, split complementary, rectangle) changes the spread.
  void set_harmony(ColorHarmony harmony);
  [[nodiscard]] ColorHarmony harmony() const noexcept { return harmony_; }
  void set_harmony_spread(double spread);
  [[nodiscard]] double harmony_spread() const noexcept { return harmony_spread_; }
  // The harmony's other colors: its hues at the current field position (and, with Tone Lock,
  // at the current color's lightness).
  [[nodiscard]] std::vector<QColor> harmony_colors() const;
  [[nodiscard]] QPointF harmony_marker_position(int index) const;

  // Widget-space positions of the ring and field markers (tests and HUD placement).
  [[nodiscard]] QPointF ring_position_for_hue(double rgb_hue) const;
  [[nodiscard]] QPointF field_position_for_color(QColor color) const;
  [[nodiscard]] QPointF center() const;

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;
  [[nodiscard]] bool hasHeightForWidth() const override { return true; }
  [[nodiscard]] int heightForWidth(int width) const override { return width; }

signals:
  void edit_started();
  void color_edited(QColor color, bool finished);
  // The user dragged a harmony marker to a new spread (finished on release).
  void harmony_spread_edited(double spread, bool finished);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  enum class Drag { None, Ring, Field, Harmony };

  [[nodiscard]] double outer_radius() const;
  [[nodiscard]] double inner_radius() const;
  [[nodiscard]] double field_radius() const;
  [[nodiscard]] WheelPoint unit_point(QPointF position) const;
  void drag_to(QPointF position, bool finished);
  void rebuild_ring_image();
  void rebuild_field_image();

  QColor color_{Qt::black};
  double hue_{0.0};                    // RGB hue of the field
  WheelPoint point_{};                 // marker in the field's unit space
  ColorWheelModel model_{ColorWheelModel::Rgb};
  ColorWheelShape shape_{ColorWheelShape::Square};
  bool tone_lock_{false};
  ColorHarmony harmony_{ColorHarmony::None};
  double harmony_spread_{kHarmonySpreadDefault};
  int harmony_drag_index_{-1};
  Drag drag_{Drag::None};
  WheelRgb tone_reference_{};          // color at the start of a ring drag
  QImage ring_image_;
  QImage field_image_;
  double field_image_hue_{-1.0};
};

// WheelRgb <-> QColor at 8 bits per channel.
[[nodiscard]] WheelRgb to_wheel_rgb(QColor color);
[[nodiscard]] QColor from_wheel_rgb(WheelRgb color);

}  // namespace patchy::ui
