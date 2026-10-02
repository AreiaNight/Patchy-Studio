#include "ui/pressure_curve_preview.hpp"

#include "core/pen_pressure.hpp"
#include "ui/theme_palette.hpp"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace patchy::ui {

PressureCurvePreview::PressureCurvePreview(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
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
  return {88, 88};
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
}

}  // namespace patchy::ui
