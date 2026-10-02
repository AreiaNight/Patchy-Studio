#pragma once

namespace patchy {

// Global pen pressure response (Preferences > Pen, key input/pen/pressureCurve):
// -100 (firm) .. 0 (linear) .. 100 (soft). The curve is p^gamma through the point
// (0.5, 0.5 + curve * 0.004), so half pressure yields 10% at full firm and 90% at
// full soft. Monotone, maps 0 to 0 and 1 to 1, so "pen touching" (pressure > 0)
// and full pressure are preserved. Applied once to live tablet samples; scripted
// strokes pass their explicit pressure through unchanged.
inline constexpr int kPenPressureCurveMin = -100;
inline constexpr int kPenPressureCurveMax = 100;

[[nodiscard]] float apply_pen_pressure_curve(float pressure, int curve) noexcept;

}  // namespace patchy
