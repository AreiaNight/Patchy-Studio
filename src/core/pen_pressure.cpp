#include "core/pen_pressure.hpp"

#include <algorithm>
#include <cmath>

namespace patchy {

float apply_pen_pressure_curve(float pressure, int curve) noexcept {
  if (!std::isfinite(pressure)) {
    return 1.0F;
  }
  pressure = std::clamp(pressure, 0.0F, 1.0F);
  curve = std::clamp(curve, kPenPressureCurveMin, kPenPressureCurveMax);
  if (curve == 0 || pressure <= 0.0F || pressure >= 1.0F) {
    return pressure;
  }
  const auto midpoint_output = 0.5 + static_cast<double>(curve) * 0.004;
  const auto gamma = std::log(midpoint_output) / std::log(0.5);
  return std::clamp(static_cast<float>(std::pow(static_cast<double>(pressure), gamma)), 0.0F, 1.0F);
}

}  // namespace patchy
