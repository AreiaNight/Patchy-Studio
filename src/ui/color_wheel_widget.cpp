#include "ui/color_wheel_widget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace patchy::ui {
namespace {

constexpr double kRingInnerFraction = 0.80;  // ring thickness: 20% of the outer radius
constexpr double kFieldFraction = 0.76;      // field inscribed just inside the ring
constexpr double kDegrees = 180.0 / std::numbers::pi;

QRgb premultiplied(WheelRgb color, double alpha) {
  const auto a = std::clamp(alpha, 0.0, 1.0);
  const auto channel = [a](double value) { return static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * a * 255.0)); };
  return qRgba(channel(color.r), channel(color.g), channel(color.b), static_cast<int>(std::lround(a * 255.0)));
}

// Markers sit over arbitrary colors, so they are a black-plus-white pair like marching ants
// (the documented content-marker exemption in docs/ui-conventions.md), not theme roles.
void draw_marker(QPainter& painter, QPointF position, double radius) {
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(Qt::black, 3.0));
  painter.drawEllipse(position, radius, radius);
  painter.setPen(QPen(Qt::white, 1.5));
  painter.drawEllipse(position, radius, radius);
}

}  // namespace

WheelRgb to_wheel_rgb(QColor color) {
  const auto rgb = color.toRgb();
  return {rgb.red() / 255.0, rgb.green() / 255.0, rgb.blue() / 255.0};
}

QColor from_wheel_rgb(WheelRgb color) {
  const auto channel = [](double value) { return static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0)); };
  return QColor(channel(color.r), channel(color.g), channel(color.b));
}

ColorWheelWidget::ColorWheelWidget(QWidget* parent) : QWidget(parent) {
  QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  policy.setHeightForWidth(true);
  setSizePolicy(policy);
  setMouseTracking(false);
  setCursor(Qt::CrossCursor);
  set_color(color_);
}

QSize ColorWheelWidget::sizeHint() const {
  return {220, 220};
}

QSize ColorWheelWidget::minimumSizeHint() const {
  return {120, 120};
}

QPointF ColorWheelWidget::center() const {
  return QPointF(width() / 2.0, height() / 2.0);
}

double ColorWheelWidget::outer_radius() const {
  return std::max(8.0, std::min(width(), height()) / 2.0 - 4.0);
}

double ColorWheelWidget::inner_radius() const {
  return outer_radius() * kRingInnerFraction;
}

double ColorWheelWidget::field_radius() const {
  return outer_radius() * kFieldFraction;
}

WheelPoint ColorWheelWidget::unit_point(QPointF position) const {
  const auto c = center();
  const auto radius = field_radius();
  return {(position.x() - c.x()) / radius, -(position.y() - c.y()) / radius};
}

QPointF ColorWheelWidget::ring_position_for_hue(double rgb_hue) const {
  const auto angle = wheel_angle_for_hue(rgb_hue, model_) / kDegrees;
  const auto radius = (outer_radius() + inner_radius()) / 2.0;
  const auto c = center();
  return {c.x() + radius * std::cos(angle), c.y() - radius * std::sin(angle)};
}

QPointF ColorWheelWidget::field_position_for_color(QColor color) const {
  const auto point = wheel_field_point(shape_, to_wheel_rgb(color));
  const auto c = center();
  return {c.x() + point.x * field_radius(), c.y() - point.y * field_radius()};
}

void ColorWheelWidget::set_harmony(ColorHarmony harmony) {
  harmony_ = harmony;
  update();
}

void ColorWheelWidget::set_harmony_spread(double spread) {
  harmony_spread_ = std::clamp(spread, kHarmonySpreadMin, kHarmonySpreadMax);
  update();
}

QPointF ColorWheelWidget::harmony_marker_position(int index) const {
  const auto angle = wheel_harmony_angle(harmony_, wheel_angle_for_hue(hue_, model_), harmony_spread_, index) / kDegrees;
  const auto radius = (outer_radius() + inner_radius()) / 2.0;
  const auto c = center();
  return {c.x() + radius * std::cos(angle), c.y() - radius * std::sin(angle)};
}

std::vector<QColor> ColorWheelWidget::harmony_colors() const {
  std::vector<QColor> colors;
  const auto base_angle = wheel_angle_for_hue(hue_, model_);
  for (int index = 0; index < wheel_harmony_count(harmony_); ++index) {
    const auto hue = wheel_hue_for_angle(wheel_harmony_angle(harmony_, base_angle, harmony_spread_, index), model_);
    auto color = wheel_field_color(shape_, hue, point_);
    if (tone_lock_) {
      color = wheel_tone_locked(color, to_wheel_rgb(color_));
    }
    colors.push_back(from_wheel_rgb(color));
  }
  return colors;
}

void ColorWheelWidget::set_color(QColor color) {
  if (!color.isValid()) {
    return;
  }
  color_ = color.toRgb();
  const auto hsv = wheel_rgb_to_hsv(to_wheel_rgb(color_));
  if (hsv.s > 0.0 && hsv.v > 0.0) {
    hue_ = hsv.h;
  }
  point_ = wheel_field_point(shape_, to_wheel_rgb(color_));
  update();
}

void ColorWheelWidget::set_model(ColorWheelModel model) {
  if (model == model_) {
    return;
  }
  model_ = model;
  ring_image_ = QImage();
  update();
}

void ColorWheelWidget::set_shape(ColorWheelShape shape) {
  if (shape == shape_) {
    return;
  }
  shape_ = shape;
  point_ = wheel_field_point(shape_, to_wheel_rgb(color_));
  field_image_ = QImage();
  update();
}

void ColorWheelWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  ring_image_ = QImage();
  field_image_ = QImage();
}

void ColorWheelWidget::rebuild_ring_image() {
  const auto dpr = devicePixelRatioF();
  const auto size = static_cast<int>(std::ceil(outer_radius() * 2.0 * dpr)) + 2;
  ring_image_ = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
  ring_image_.fill(Qt::transparent);
  const auto half = size / 2.0;
  const auto outer = outer_radius() * dpr;
  const auto inner = inner_radius() * dpr;
  for (int y = 0; y < size; ++y) {
    auto* row = reinterpret_cast<QRgb*>(ring_image_.scanLine(y));
    for (int x = 0; x < size; ++x) {
      const auto dx = x + 0.5 - half;
      const auto dy = half - (y + 0.5);
      const auto distance = std::hypot(dx, dy);
      // One-pixel antialiased edges on both sides of the ring.
      const auto coverage = std::min(std::clamp(outer - distance + 0.5, 0.0, 1.0),
                                     std::clamp(distance - inner + 0.5, 0.0, 1.0));
      if (coverage <= 0.0) {
        continue;
      }
      const auto hue = wheel_hue_for_angle(std::atan2(dy, dx) * kDegrees, model_);
      row[x] = premultiplied(wheel_hsv_to_rgb({hue, 1.0, 1.0}), coverage);
    }
  }
  ring_image_.setDevicePixelRatio(dpr);
}

void ColorWheelWidget::rebuild_field_image() {
  const auto dpr = devicePixelRatioF();
  const auto size = static_cast<int>(std::ceil(field_radius() * 2.0 * dpr)) + 2;
  field_image_ = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
  field_image_.fill(Qt::transparent);
  const auto half = size / 2.0;
  const auto radius = field_radius() * dpr;
  for (int y = 0; y < size; ++y) {
    auto* row = reinterpret_cast<QRgb*>(field_image_.scanLine(y));
    for (int x = 0; x < size; ++x) {
      // 2x2 supersampled coverage gives the field smooth edges without a mask pass.
      int inside = 0;
      for (const auto [ox, oy] : {std::pair{0.25, 0.25}, {0.75, 0.25}, {0.25, 0.75}, {0.75, 0.75}}) {
        inside += wheel_field_contains(shape_, {(x + ox - half) / radius, (half - (y + oy)) / radius}) ? 1 : 0;
      }
      if (inside == 0) {
        continue;
      }
      const WheelPoint point{(x + 0.5 - half) / radius, (half - (y + 0.5)) / radius};
      row[x] = premultiplied(wheel_field_color(shape_, hue_, point), inside / 4.0);
    }
  }
  field_image_.setDevicePixelRatio(dpr);
  field_image_hue_ = hue_;
}

void ColorWheelWidget::paintEvent(QPaintEvent*) {
  if (ring_image_.isNull()) {
    rebuild_ring_image();
  }
  if (field_image_.isNull() || field_image_hue_ != hue_) {
    rebuild_field_image();
  }
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto c = center();
  const auto ring_half = ring_image_.width() / ring_image_.devicePixelRatio() / 2.0;
  painter.drawImage(QPointF(c.x() - ring_half, c.y() - ring_half), ring_image_);
  const auto field_half = field_image_.width() / field_image_.devicePixelRatio() / 2.0;
  painter.drawImage(QPointF(c.x() - field_half, c.y() - field_half), field_image_);
  const auto thickness = outer_radius() - inner_radius();
  // Harmony: a thin guide from the center to each harmony marker, then square markers (the
  // base keeps the round one). Black-plus-white like every marker on content.
  for (int index = 0; index < wheel_harmony_count(harmony_); ++index) {
    const auto position = harmony_marker_position(index);
    painter.setPen(QPen(Qt::white, 1.0, Qt::DashLine));
    painter.drawLine(c, position);
    const auto half = std::max(3.0, thickness * 0.26);
    const QRectF box(position.x() - half, position.y() - half, half * 2.0, half * 2.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 3.0));
    painter.drawRect(box);
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawRect(box);
  }
  draw_marker(painter, ring_position_for_hue(hue_), std::max(3.0, thickness * 0.32));
  draw_marker(painter, QPointF(c.x() + point_.x * field_radius(), c.y() - point_.y * field_radius()), 5.0);
}

void ColorWheelWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  const auto offset = event->position() - center();
  const auto distance = std::hypot(offset.x(), offset.y());
  // A harmony marker with a spread takes precedence over the ring under it.
  harmony_drag_index_ = -1;
  if (wheel_harmony_has_spread(harmony_)) {
    const auto grab = std::max(8.0, (outer_radius() - inner_radius()) * 0.6);
    for (int index = 0; index < wheel_harmony_count(harmony_); ++index) {
      const auto marker = harmony_marker_position(index) - event->position();
      if (std::hypot(marker.x(), marker.y()) <= grab) {
        harmony_drag_index_ = index;
      }
    }
  }
  if (harmony_drag_index_ >= 0) {
    drag_ = Drag::Harmony;
    drag_to(event->position(), false);
    event->accept();
    return;
  }
  if (distance >= inner_radius() - 2.0 && distance <= outer_radius() + 4.0) {
    drag_ = Drag::Ring;
  } else if (wheel_field_contains(shape_, unit_point(event->position())) || distance < inner_radius()) {
    drag_ = Drag::Field;
  } else {
    drag_ = Drag::None;
    event->ignore();
    return;
  }
  tone_reference_ = to_wheel_rgb(color_);
  emit edit_started();
  drag_to(event->position(), false);
  event->accept();
}

void ColorWheelWidget::mouseMoveEvent(QMouseEvent* event) {
  if (drag_ != Drag::None && (event->buttons() & Qt::LeftButton) != 0) {
    drag_to(event->position(), false);
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void ColorWheelWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (drag_ != Drag::None && event->button() == Qt::LeftButton) {
    drag_to(event->position(), true);
    drag_ = Drag::None;
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void ColorWheelWidget::drag_to(QPointF position, bool finished) {
  if (drag_ == Drag::Harmony) {
    const auto offset = position - center();
    const auto angle = std::atan2(-offset.y(), offset.x()) * kDegrees;
    harmony_spread_ = wheel_harmony_spread_for_angle(harmony_, wheel_angle_for_hue(hue_, model_), harmony_drag_index_, angle);
    update();
    emit harmony_spread_edited(harmony_spread_, finished);
    return;
  }
  if (drag_ == Drag::Ring) {
    const auto offset = position - center();
    hue_ = wheel_hue_for_angle(std::atan2(-offset.y(), offset.x()) * kDegrees, model_);
    auto candidate = wheel_field_color(shape_, hue_, point_);
    if (tone_lock_) {
      // Keep the value structure: the new hue at the lightness the drag started from.
      candidate = wheel_tone_locked(candidate, tone_reference_);
      point_ = wheel_field_point(shape_, candidate);
    }
    color_ = from_wheel_rgb(candidate);
  } else if (drag_ == Drag::Field) {
    point_ = wheel_field_clamp(shape_, unit_point(position));
    color_ = from_wheel_rgb(wheel_field_color(shape_, hue_, point_));
  }
  update();
  emit color_edited(color_, finished);
}

}  // namespace patchy::ui
