#include "ui/pressure_curve_preview.hpp"

#include "core/pen_pressure.hpp"
#include "ui/theme_palette.hpp"

#include <QEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

PressureCurvePreview::PressureCurvePreview(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  setTabletTracking(true);
}

void PressureCurvePreview::tabletEvent(QTabletEvent* event) {
  if (event->type() != QEvent::TabletPress && event->type() != QEvent::TabletMove &&
      event->type() != QEvent::TabletRelease) {
    QWidget::tabletEvent(event);
    return;
  }
  const auto* device = event->pointingDevice();
  live_device_has_pressure_ =
      device != nullptr && device->capabilities().testFlag(QInputDevice::Capability::Pressure);
  // Hover moves report zero; keep the last touching value on screen until the
  // tip presses again so the reading stays legible after a lift.
  const bool touching = event->type() != QEvent::TabletRelease && (event->buttons() & Qt::LeftButton) != 0;
  if (touching || event->type() == QEvent::TabletPress) {
    live_pressure_ = std::clamp(static_cast<float>(event->pressure()), 0.0F, 1.0F);
  }
  update();
  event->accept();
}

void PressureCurvePreview::set_curve(int curve) {
  curve = std::clamp(curve, kPenPressureCurveMin, kPenPressureCurveMax);
  if (curve == curve_) {
    return;
  }
  curve_ = curve;
  update();
}

QSize PressureCurvePreview::sizeHint() const {
  return {120, 96};
}

void PressureCurvePreview::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::EnabledChange || event->type() == QEvent::PaletteChange) {
    update();
  }
}

void PressureCurvePreview::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto& colors = theme();
  const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  painter.setPen(colors.field_inset_border);
  painter.setBrush(colors.field_bg);
  painter.drawRect(frame);

  const auto plot = frame.adjusted(6.0, 6.0, -6.0, -6.0);
  const auto point_at = [&plot](double input, double output) {
    return QPointF(plot.left() + input * plot.width(), plot.bottom() - output * plot.height());
  };
  painter.setPen(QPen(colors.option_separator, 1.0, Qt::DashLine));
  painter.drawLine(point_at(0.0, 0.0), point_at(1.0, 1.0));

  QPainterPath path(point_at(0.0, 0.0));
  constexpr int kSteps = 48;
  for (int step = 1; step <= kSteps; ++step) {
    const auto input = static_cast<double>(step) / kSteps;
    path.lineTo(point_at(input, apply_pen_pressure_curve(static_cast<float>(input), curve_)));
  }
  painter.setPen(QPen(isEnabled() ? colors.accent : colors.text_disabled, 2.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);

  if (!live_device_has_pressure_) {
    painter.setPen(colors.text_primary);
    painter.drawText(plot, Qt::AlignLeft | Qt::AlignTop, tr("No pressure"));
  } else if (live_pressure_.has_value()) {
    const auto raw = *live_pressure_;
    const auto shaped = apply_pen_pressure_curve(raw, curve_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.accent);
    painter.drawEllipse(point_at(raw, shaped), 4.0, 4.0);
    painter.setPen(colors.text_primary);
    // Raw tablet pressure, then what brushes receive after the curve.
    const auto reading =
        QStringLiteral("%1% \u2192 %2%").arg(std::lround(raw * 100.0F)).arg(std::lround(shaped * 100.0F));
    painter.drawText(plot, Qt::AlignLeft | Qt::AlignTop, reading);
  }
}

}  // namespace patchy::ui
