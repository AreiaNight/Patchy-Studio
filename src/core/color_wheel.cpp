#include "core/color_wheel.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace patchy {
namespace {

constexpr double kSquareHalf = 0.70710678118654752440;  // square inscribed in the unit circle
constexpr double kSqrt3Half = 0.86602540378443864676;

// Triangle corners on the unit circle: pure hue right, white upper left, black lower left.
constexpr WheelPoint kTrianglePure{1.0, 0.0};
constexpr WheelPoint kTriangleWhite{-0.5, kSqrt3Half};
constexpr WheelPoint kTriangleBlack{-0.5, -kSqrt3Half};

double wrap_degrees(double angle) noexcept {
  if (!std::isfinite(angle)) {
    return 0.0;
  }
  angle = std::fmod(angle, 360.0);
  return angle < 0.0 ? angle + 360.0 : angle;
}

double clamp01(double value) noexcept {
  return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

// Painter's wheel anchors: RYB angle -> RGB hue. Red, orange, yellow, green, blue, violet.
constexpr std::array<double, 7> kRybAngles{0.0, 60.0, 120.0, 180.0, 240.0, 300.0, 360.0};
constexpr std::array<double, 7> kRgbHues{0.0, 30.0, 60.0, 120.0, 240.0, 280.0, 360.0};

double piecewise(double value, const std::array<double, 7>& from, const std::array<double, 7>& to) noexcept {
  for (std::size_t i = 1; i < from.size(); ++i) {
    if (value <= from[i]) {
      const auto t = (value - from[i - 1]) / (from[i] - from[i - 1]);
      return to[i - 1] + t * (to[i] - to[i - 1]);
    }
  }
  return to.back();
}

// Triangle coordinates: point = black + v * (white - black) + t * (pure - white), t = v * s.
void triangle_coordinates(WheelPoint point, double& v, double& t) noexcept {
  const auto d1x = kTriangleWhite.x - kTriangleBlack.x;
  const auto d1y = kTriangleWhite.y - kTriangleBlack.y;
  const auto d2x = kTrianglePure.x - kTriangleWhite.x;
  const auto d2y = kTrianglePure.y - kTriangleWhite.y;
  const auto qx = point.x - kTriangleBlack.x;
  const auto qy = point.y - kTriangleBlack.y;
  const auto det = d1x * d2y - d1y * d2x;
  v = (qx * d2y - qy * d2x) / det;
  t = (d1x * qy - d1y * qx) / det;
}

WheelPoint triangle_point(double v, double t) noexcept {
  return {kTriangleBlack.x + v * (kTriangleWhite.x - kTriangleBlack.x) + t * (kTrianglePure.x - kTriangleWhite.x),
          kTriangleBlack.y + v * (kTriangleWhite.y - kTriangleBlack.y) + t * (kTrianglePure.y - kTriangleWhite.y)};
}

WheelRgb hsl_to_rgb(double hue, double saturation, double lightness) noexcept {
  const auto chroma = (1.0 - std::abs(2.0 * lightness - 1.0)) * saturation;
  if (chroma <= 0.0) {
    return {lightness, lightness, lightness};
  }
  const auto value = lightness + chroma / 2.0;
  const auto hsv_saturation = value > 0.0 ? chroma / value : 0.0;
  return wheel_hsv_to_rgb({hue, hsv_saturation, value});
}

void rgb_to_hsl(WheelRgb color, double& saturation, double& lightness) noexcept {
  const auto maximum = std::max({color.r, color.g, color.b});
  const auto minimum = std::min({color.r, color.g, color.b});
  lightness = (maximum + minimum) / 2.0;
  const auto chroma = maximum - minimum;
  const auto denominator = 1.0 - std::abs(2.0 * lightness - 1.0);
  saturation = chroma <= 0.0 || denominator <= 0.0 ? 0.0 : std::clamp(chroma / denominator, 0.0, 1.0);
}

double srgb_to_linear(double value) noexcept {
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) noexcept {
  return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

struct Lab {
  double l{0.0};
  double a{0.0};
  double b{0.0};
};

// Björn Ottosson's OKLab (2020, public domain), linear sRGB in and out.
Lab to_oklab(WheelRgb color) noexcept {
  const auto r = srgb_to_linear(clamp01(color.r));
  const auto g = srgb_to_linear(clamp01(color.g));
  const auto b = srgb_to_linear(clamp01(color.b));
  const auto l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const auto m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const auto s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
          1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
          0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

// Linear sRGB (unclamped, so callers can test the gamut).
WheelRgb from_oklab_linear(Lab lab) noexcept {
  const auto l = lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
  const auto m = lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
  const auto s = lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
  const auto l3 = l * l * l;
  const auto m3 = m * m * m;
  const auto s3 = s * s * s;
  return {4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3,
          -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3,
          -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3};
}

bool in_gamut(WheelRgb linear) noexcept {
  constexpr double kEpsilon = 1e-7;
  return linear.r >= -kEpsilon && linear.r <= 1.0 + kEpsilon && linear.g >= -kEpsilon &&
         linear.g <= 1.0 + kEpsilon && linear.b >= -kEpsilon && linear.b <= 1.0 + kEpsilon;
}

}  // namespace

WheelHsv wheel_rgb_to_hsv(WheelRgb color) noexcept {
  const auto r = clamp01(color.r);
  const auto g = clamp01(color.g);
  const auto b = clamp01(color.b);
  const auto maximum = std::max({r, g, b});
  const auto minimum = std::min({r, g, b});
  const auto chroma = maximum - minimum;
  WheelHsv hsv{0.0, maximum > 0.0 ? chroma / maximum : 0.0, maximum};
  if (chroma > 0.0) {
    if (maximum == r) {
      hsv.h = 60.0 * std::fmod((g - b) / chroma, 6.0);
    } else if (maximum == g) {
      hsv.h = 60.0 * ((b - r) / chroma + 2.0);
    } else {
      hsv.h = 60.0 * ((r - g) / chroma + 4.0);
    }
    hsv.h = wrap_degrees(hsv.h);
  }
  return hsv;
}

WheelRgb wheel_hsv_to_rgb(WheelHsv color) noexcept {
  const auto h = wrap_degrees(color.h) / 60.0;
  const auto s = clamp01(color.s);
  const auto v = clamp01(color.v);
  const auto chroma = v * s;
  const auto x = chroma * (1.0 - std::abs(std::fmod(h, 2.0) - 1.0));
  const auto m = v - chroma;
  WheelRgb rgb;
  switch (static_cast<int>(std::floor(h)) % 6) {
    case 0: rgb = {chroma, x, 0.0}; break;
    case 1: rgb = {x, chroma, 0.0}; break;
    case 2: rgb = {0.0, chroma, x}; break;
    case 3: rgb = {0.0, x, chroma}; break;
    case 4: rgb = {x, 0.0, chroma}; break;
    default: rgb = {chroma, 0.0, x}; break;
  }
  return {rgb.r + m, rgb.g + m, rgb.b + m};
}

double wheel_angle_for_hue(double rgb_hue, ColorWheelModel model) noexcept {
  const auto hue = wrap_degrees(rgb_hue);
  return model == ColorWheelModel::Ryb ? wrap_degrees(piecewise(hue, kRgbHues, kRybAngles)) : hue;
}

double wheel_hue_for_angle(double angle, ColorWheelModel model) noexcept {
  const auto wrapped = wrap_degrees(angle);
  return model == ColorWheelModel::Ryb ? wrap_degrees(piecewise(wrapped, kRybAngles, kRgbHues)) : wrapped;
}

bool wheel_field_contains(ColorWheelShape shape, WheelPoint point) noexcept {
  constexpr double kEpsilon = 1e-9;
  switch (shape) {
    case ColorWheelShape::Square:
      return std::abs(point.x) <= kSquareHalf + kEpsilon && std::abs(point.y) <= kSquareHalf + kEpsilon;
    case ColorWheelShape::Triangle: {
      double v = 0.0;
      double t = 0.0;
      triangle_coordinates(point, v, t);
      return v >= -kEpsilon && v <= 1.0 + kEpsilon && t >= -kEpsilon && t <= v + kEpsilon;
    }
    case ColorWheelShape::Diamond:
      return std::abs(point.x) + std::abs(point.y) <= 1.0 + kEpsilon;
  }
  return false;
}

WheelPoint wheel_field_clamp(ColorWheelShape shape, WheelPoint point) noexcept {
  switch (shape) {
    case ColorWheelShape::Square:
      return {std::clamp(point.x, -kSquareHalf, kSquareHalf), std::clamp(point.y, -kSquareHalf, kSquareHalf)};
    case ColorWheelShape::Triangle: {
      double v = 0.0;
      double t = 0.0;
      triangle_coordinates(point, v, t);
      v = std::clamp(v, 0.0, 1.0);
      t = std::clamp(t, 0.0, v);
      return triangle_point(v, t);
    }
    case ColorWheelShape::Diamond: {
      const auto y = std::clamp(point.y, -1.0, 1.0);
      const auto reach = 1.0 - std::abs(y);
      return {std::clamp(point.x, -reach, reach), y};
    }
  }
  return point;
}

WheelRgb wheel_field_color(ColorWheelShape shape, double hue, WheelPoint point) noexcept {
  point = wheel_field_clamp(shape, point);
  switch (shape) {
    case ColorWheelShape::Square:
      return wheel_hsv_to_rgb({hue, (point.x + kSquareHalf) / (2.0 * kSquareHalf),
                               (point.y + kSquareHalf) / (2.0 * kSquareHalf)});
    case ColorWheelShape::Triangle: {
      double v = 0.0;
      double t = 0.0;
      triangle_coordinates(point, v, t);
      v = std::clamp(v, 0.0, 1.0);
      return wheel_hsv_to_rgb({hue, v > 0.0 ? std::clamp(t / v, 0.0, 1.0) : 0.0, v});
    }
    case ColorWheelShape::Diamond: {
      const auto lightness = (point.y + 1.0) / 2.0;
      const auto reach = 1.0 - std::abs(point.y);
      const auto saturation = reach > 0.0 ? std::clamp((point.x + reach) / (2.0 * reach), 0.0, 1.0) : 0.0;
      return hsl_to_rgb(hue, saturation, lightness);
    }
  }
  return {};
}

WheelPoint wheel_field_point(ColorWheelShape shape, WheelRgb color) noexcept {
  switch (shape) {
    case ColorWheelShape::Square: {
      const auto hsv = wheel_rgb_to_hsv(color);
      return {-kSquareHalf + 2.0 * kSquareHalf * hsv.s, -kSquareHalf + 2.0 * kSquareHalf * hsv.v};
    }
    case ColorWheelShape::Triangle: {
      const auto hsv = wheel_rgb_to_hsv(color);
      return triangle_point(hsv.v, hsv.v * hsv.s);
    }
    case ColorWheelShape::Diamond: {
      double saturation = 0.0;
      double lightness = 0.0;
      rgb_to_hsl({clamp01(color.r), clamp01(color.g), clamp01(color.b)}, saturation, lightness);
      const auto y = 2.0 * lightness - 1.0;
      return {(2.0 * saturation - 1.0) * (1.0 - std::abs(y)), y};
    }
  }
  return {};
}

bool wheel_harmony_has_spread(ColorHarmony harmony) noexcept {
  return harmony == ColorHarmony::Analogous || harmony == ColorHarmony::SplitComplementary ||
         harmony == ColorHarmony::Rectangle;
}

int wheel_harmony_count(ColorHarmony harmony) noexcept {
  switch (harmony) {
    case ColorHarmony::None: return 0;
    case ColorHarmony::Complementary: return 1;
    case ColorHarmony::Analogous:
    case ColorHarmony::Triadic:
    case ColorHarmony::SplitComplementary: return 2;
    case ColorHarmony::Rectangle:
    case ColorHarmony::Square: return 3;
  }
  return 0;
}

double wheel_harmony_angle(ColorHarmony harmony, double base_angle, double spread, int index) noexcept {
  spread = std::clamp(std::isfinite(spread) ? spread : kHarmonySpreadDefault, kHarmonySpreadMin, kHarmonySpreadMax);
  double offset = 0.0;
  switch (harmony) {
    case ColorHarmony::None: break;
    case ColorHarmony::Complementary: offset = 180.0; break;
    case ColorHarmony::Analogous: offset = index == 0 ? -spread : spread; break;
    case ColorHarmony::Triadic: offset = index == 0 ? 120.0 : 240.0; break;
    case ColorHarmony::SplitComplementary: offset = index == 0 ? 180.0 - spread : 180.0 + spread; break;
    case ColorHarmony::Rectangle: offset = index == 0 ? spread : index == 1 ? 180.0 : 180.0 + spread; break;
    case ColorHarmony::Square: offset = 90.0 * (index + 1); break;
  }
  return wrap_degrees(base_angle + offset);
}

double wheel_harmony_spread_for_angle(ColorHarmony harmony, double base_angle, int index, double angle) noexcept {
  // Signed distance from the base (or from the complement) in (-180, 180].
  const auto signed_from = [](double from, double to) {
    auto delta = wrap_degrees(to - from);
    return delta > 180.0 ? delta - 360.0 : delta;
  };
  double spread = kHarmonySpreadDefault;
  switch (harmony) {
    case ColorHarmony::Analogous: spread = std::abs(signed_from(base_angle, angle)); break;
    case ColorHarmony::SplitComplementary: spread = std::abs(signed_from(base_angle + 180.0, angle)); break;
    case ColorHarmony::Rectangle:
      spread = index == 2 ? signed_from(base_angle + 180.0, angle) : signed_from(base_angle, angle);
      spread = std::abs(spread);
      break;
    default: break;
  }
  return std::clamp(spread, kHarmonySpreadMin, kHarmonySpreadMax);
}

double wheel_perceptual_lightness(WheelRgb color) noexcept {
  return to_oklab(color).l;
}

WheelRgb wheel_tone_locked(WheelRgb candidate, WheelRgb reference) noexcept {
  const auto target = to_oklab(candidate);
  const Lab lab{to_oklab(reference).l, target.a, target.b};
  auto linear = from_oklab_linear(lab);
  if (!in_gamut(linear)) {
    // Keep the hue angle and lightness; find the largest chroma that sRGB can show.
    double low = 0.0;
    double high = 1.0;
    for (int i = 0; i < 40; ++i) {
      const auto mid = (low + high) / 2.0;
      if (in_gamut(from_oklab_linear({lab.l, lab.a * mid, lab.b * mid}))) {
        low = mid;
      } else {
        high = mid;
      }
    }
    linear = from_oklab_linear({lab.l, lab.a * low, lab.b * low});
  }
  return {clamp01(linear_to_srgb(std::clamp(linear.r, 0.0, 1.0))), clamp01(linear_to_srgb(std::clamp(linear.g, 0.0, 1.0))),
          clamp01(linear_to_srgb(std::clamp(linear.b, 0.0, 1.0)))};
}

}  // namespace patchy
