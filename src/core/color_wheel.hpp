#pragma once

namespace patchy {

// Color math behind the Color Wheel panel and its on-canvas HUD. Pure functions of their
// arguments (no Qt), deterministic doubles; see docs/color-wheel.md.

struct WheelRgb {
  double r{0.0};  // sRGB-encoded 0..1
  double g{0.0};
  double b{0.0};
};

struct WheelPoint {
  double x{0.0};  // unit space: the field shape is inscribed in the unit circle, y up
  double y{0.0};
};

// Hue ring layout. Rgb is the ordinary additive wheel; Ryb is the painter's (Itten) wheel,
// where red, yellow and blue sit a third of the circle apart and the complements are the
// painter's ones (red/green, yellow/violet, blue/orange).
enum class ColorWheelModel { Rgb = 0, Ryb };

// The field inside the ring. Square: saturation right, value up (HSV). Triangle: pure hue,
// white and black corners (HSV). Diamond: white top, black bottom, gray left, pure hue right
// (HSL). Persisted as tokens; append only.
enum class ColorWheelShape { Square = 0, Triangle, Diamond };

struct WheelHsv {
  double h{0.0};  // 0..360
  double s{0.0};
  double v{0.0};
};

[[nodiscard]] WheelHsv wheel_rgb_to_hsv(WheelRgb color) noexcept;
[[nodiscard]] WheelRgb wheel_hsv_to_rgb(WheelHsv color) noexcept;

// Ring angle (degrees, 0 = right, counterclockwise) for an RGB hue and back. Rgb is the
// identity; Ryb is a monotone piecewise-linear remap anchored at the painter's primaries.
[[nodiscard]] double wheel_angle_for_hue(double rgb_hue, ColorWheelModel model) noexcept;
[[nodiscard]] double wheel_hue_for_angle(double angle, ColorWheelModel model) noexcept;

// Field geometry. field_color maps a point (clamped into the shape) to a color of `hue`;
// field_point is its inverse for a color (its own hue is ignored, `hue` places it).
[[nodiscard]] bool wheel_field_contains(ColorWheelShape shape, WheelPoint point) noexcept;
[[nodiscard]] WheelPoint wheel_field_clamp(ColorWheelShape shape, WheelPoint point) noexcept;
[[nodiscard]] WheelRgb wheel_field_color(ColorWheelShape shape, double hue, WheelPoint point) noexcept;
[[nodiscard]] WheelPoint wheel_field_point(ColorWheelShape shape, WheelRgb color) noexcept;

// Color harmonies laid on the ring. Angles are in ring space (wheel_angle_for_hue), so on the
// RYB ring the complements are the painter's. Persisted as tokens; append only.
enum class ColorHarmony { None = 0, Complementary, Analogous, Triadic, SplitComplementary, Rectangle, Square };

// Harmonies whose shape has a user spread (the angle between neighbors or to the complement's
// sides): Analogous, SplitComplementary and Rectangle.
[[nodiscard]] bool wheel_harmony_has_spread(ColorHarmony harmony) noexcept;
inline constexpr double kHarmonySpreadMin = 5.0;
inline constexpr double kHarmonySpreadMax = 90.0;
inline constexpr double kHarmonySpreadDefault = 30.0;

// Ring angles of the harmony's other colors (the base itself excluded), in a fixed order:
// Complementary {180}, Analogous {-s, +s}, Triadic {120, 240}, SplitComplementary {180-s, 180+s},
// Rectangle {s, 180, 180+s}, Square {90, 180, 270}; each added to base_angle and wrapped.
[[nodiscard]] int wheel_harmony_count(ColorHarmony harmony) noexcept;
[[nodiscard]] double wheel_harmony_angle(ColorHarmony harmony, double base_angle, double spread, int index) noexcept;

// The spread that puts harmony color `index` at `angle` (the inverse used when a secondary
// marker is dragged), clamped to [kHarmonySpreadMin, kHarmonySpreadMax].
[[nodiscard]] double wheel_harmony_spread_for_angle(ColorHarmony harmony, double base_angle, int index,
                                                    double angle) noexcept;

// OKLab perceptual lightness (0..1) of an sRGB color.
[[nodiscard]] double wheel_perceptual_lightness(WheelRgb color) noexcept;

// Tone lock: `candidate`'s hue and chroma at `reference`'s OKLab lightness, pulling chroma in
// until it fits sRGB. Moving through hue this way keeps the value structure of a painting.
[[nodiscard]] WheelRgb wheel_tone_locked(WheelRgb candidate, WheelRgb reference) noexcept;

}  // namespace patchy
