#include "ui/studio_widgets.hpp"

#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace patchy::ui {
namespace {

// Icons are drawn on a 24-unit grid; the painter scales the grid into the target rect.
constexpr qreal kIconGrid = 24.0;
constexpr qreal kIconStroke = 1.6;

void begin_icon(QPainter& painter, const QRectF& rect, const QColor& color) {
  const qreal scale = std::min(rect.width(), rect.height()) / kIconGrid;
  painter.translate(rect.center());
  painter.scale(scale, scale);
  painter.translate(-kIconGrid / 2.0, -kIconGrid / 2.0);
  QPen pen(color, kIconStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
}

QPainterPath polyline(std::initializer_list<QPointF> points, bool closed = false) {
  QPainterPath path;
  bool first = true;
  for (const auto& point : points) {
    if (first) {
      path.moveTo(point);
      first = false;
    } else {
      path.lineTo(point);
    }
  }
  if (closed) {
    path.closeSubpath();
  }
  return path;
}

void draw_arrow_head(QPainter& painter, QPointF tip, QPointF from, qreal length) {
  const QPointF direction = tip - from;
  const qreal norm = std::hypot(direction.x(), direction.y());
  if (norm <= 0.0) {
    return;
  }
  const QPointF unit = direction / norm;
  const QPointF normal(-unit.y(), unit.x());
  painter.drawPath(polyline({tip - unit * length + normal * length * 0.8, tip, tip - unit * length - normal * length * 0.8}));
}

}  // namespace

void paint_studio_icon(QPainter& painter, StudioIcon icon, const QRectF& rect, const QColor& color) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  begin_icon(painter, rect, color);
  switch (icon) {
    case StudioIcon::Wrench: {
      QPen thick = painter.pen();
      thick.setWidthF(2.4);
      painter.setPen(thick);
      painter.drawLine(QPointF(5.0, 19.0), QPointF(12.6, 11.4));
      painter.setPen(QPen(color, kIconStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      QPainterPath head;
      head.arcMoveTo(QRectF(11.0, 3.0, 10.0, 10.0), 200.0);
      head.arcTo(QRectF(11.0, 3.0, 10.0, 10.0), 200.0, 250.0);
      painter.drawPath(head);
      painter.drawLine(QPointF(16.0, 8.0), QPointF(19.6, 4.4));
      break;
    }
    case StudioIcon::Wand: {
      painter.drawLine(QPointF(4.5, 19.5), QPointF(14.5, 9.5));
      painter.drawLine(QPointF(17.5, 3.0), QPointF(17.5, 8.0));
      painter.drawLine(QPointF(15.0, 5.5), QPointF(20.0, 5.5));
      painter.drawLine(QPointF(20.5, 10.5), QPointF(20.5, 12.5));
      painter.drawLine(QPointF(19.5, 11.5), QPointF(21.5, 11.5));
      painter.drawLine(QPointF(11.0, 3.5), QPointF(11.0, 5.5));
      painter.drawLine(QPointF(10.0, 4.5), QPointF(12.0, 4.5));
      break;
    }
    case StudioIcon::Selection: {
      QPainterPath path;
      path.moveTo(16.8, 6.4);
      path.cubicTo(14.4, 4.2, 7.4, 4.6, 7.8, 8.8);
      path.cubicTo(8.2, 12.6, 16.2, 11.2, 16.4, 15.4);
      path.cubicTo(16.6, 19.6, 9.6, 20.0, 7.0, 17.4);
      painter.drawPath(path);
      break;
    }
    case StudioIcon::Transform: {
      painter.drawPath(polyline({{7.0, 3.5}, {7.0, 18.5}, {10.8, 14.8}, {13.6, 20.8}, {16.0, 19.8}, {13.2, 13.8},
                                 {18.6, 13.8}},
                                true));
      break;
    }
    case StudioIcon::Brush: {
      painter.drawLine(QPointF(12.8, 12.0), QPointF(20.0, 4.0));
      QPainterPath tip;
      tip.moveTo(11.2, 11.2);
      tip.cubicTo(7.6, 11.0, 5.8, 13.6, 5.8, 16.4);
      tip.cubicTo(5.8, 18.2, 4.8, 19.4, 3.6, 20.2);
      tip.cubicTo(8.0, 20.8, 12.4, 19.2, 12.8, 14.6);
      tip.closeSubpath();
      painter.drawPath(tip);
      break;
    }
    case StudioIcon::Smudge: {
      QPainterPath finger;
      finger.addRoundedRect(QRectF(8.6, 2.8, 4.8, 11.0), 2.4, 2.4);
      painter.drawPath(finger);
      QPainterPath hand;
      hand.moveTo(8.6, 11.0);
      hand.cubicTo(6.4, 10.6, 5.4, 12.6, 6.4, 14.8);
      hand.lineTo(8.6, 20.8);
      hand.lineTo(16.8, 20.8);
      hand.cubicTo(18.2, 17.8, 18.8, 14.8, 18.2, 12.4);
      hand.cubicTo(17.8, 10.8, 15.6, 10.6, 15.0, 12.0);
      hand.cubicTo(14.6, 10.4, 13.6, 10.2, 13.4, 10.6);
      painter.drawPath(hand);
      break;
    }
    case StudioIcon::Eraser: {
      painter.drawPath(polyline({{3.5, 14.5}, {12.0, 6.0}, {19.0, 13.0}, {10.5, 21.5}}, true));
      painter.drawLine(QPointF(7.8, 10.2), QPointF(14.8, 17.2));
      painter.drawLine(QPointF(13.5, 21.5), QPointF(20.5, 21.5));
      break;
    }
    case StudioIcon::Layers: {
      painter.drawRoundedRect(QRectF(4.0, 9.0, 11.0, 11.0), 1.5, 1.5);
      painter.drawPath(polyline({{8.5, 9.0}, {8.5, 4.5}, {19.5, 4.5}, {19.5, 15.5}, {15.0, 15.5}}));
      break;
    }
    case StudioIcon::Undo:
    case StudioIcon::Redo: {
      if (icon == StudioIcon::Redo) {
        painter.translate(kIconGrid, 0.0);
        painter.scale(-1.0, 1.0);
      }
      QPainterPath path;
      path.moveTo(19.0, 18.5);
      path.cubicTo(19.0, 11.5, 15.0, 9.0, 6.5, 9.0);
      painter.drawPath(path);
      painter.drawPath(polyline({{10.5, 5.0}, {6.5, 9.0}, {10.5, 13.0}}));
      break;
    }
    case StudioIcon::Plus:
      painter.drawLine(QPointF(12.0, 5.0), QPointF(12.0, 19.0));
      painter.drawLine(QPointF(5.0, 12.0), QPointF(19.0, 12.0));
      break;
    case StudioIcon::Close:
      painter.drawLine(QPointF(6.5, 6.5), QPointF(17.5, 17.5));
      painter.drawLine(QPointF(17.5, 6.5), QPointF(6.5, 17.5));
      break;
    case StudioIcon::Eyedropper: {
      painter.drawPath(polyline({{4.5, 19.5}, {5.0, 16.5}, {12.5, 9.0}, {15.0, 11.5}, {7.5, 19.0}}, true));
      painter.drawLine(QPointF(11.0, 7.5), QPointF(16.5, 13.0));
      painter.setBrush(color);
      painter.drawEllipse(QPointF(16.6, 7.4), 3.0, 3.0);
      break;
    }
    case StudioIcon::Canvas:
      painter.drawPath(polyline({{7.0, 3.0}, {7.0, 17.0}, {21.0, 17.0}}));
      painter.drawPath(polyline({{3.0, 7.0}, {17.0, 7.0}, {17.0, 21.0}}));
      break;
    case StudioIcon::Share:
      painter.drawPath(polyline({{8.5, 10.0}, {5.0, 10.0}, {5.0, 20.5}, {19.0, 20.5}, {19.0, 10.0}, {15.5, 10.0}}));
      painter.drawLine(QPointF(12.0, 3.5), QPointF(12.0, 14.5));
      painter.drawPath(polyline({{8.5, 7.0}, {12.0, 3.5}, {15.5, 7.0}}));
      break;
    case StudioIcon::Prefs:
      painter.drawRoundedRect(QRectF(3.0, 7.5, 18.0, 9.0), 4.5, 4.5);
      painter.setBrush(color);
      painter.drawEllipse(QPointF(16.5, 12.0), 2.6, 2.6);
      break;
    case StudioIcon::Help: {
      painter.drawEllipse(QPointF(12.0, 12.0), 8.5, 8.5);
      QPainterPath mark;
      mark.moveTo(9.4, 9.6);
      mark.cubicTo(9.4, 6.6, 14.6, 6.6, 14.6, 9.6);
      mark.cubicTo(14.6, 11.6, 12.0, 11.6, 12.0, 14.0);
      painter.drawPath(mark);
      painter.setBrush(color);
      painter.drawEllipse(QPointF(12.0, 16.9), 0.6, 0.6);
      break;
    }
    case StudioIcon::Freehand: {
      QPainterPath loop;
      loop.moveTo(8.5, 15.0);
      loop.cubicTo(3.0, 13.0, 3.5, 5.0, 12.0, 4.5);
      loop.cubicTo(20.5, 4.0, 22.0, 13.0, 13.0, 14.5);
      loop.cubicTo(10.0, 15.0, 8.0, 15.5, 8.5, 18.0);
      loop.cubicTo(9.0, 20.0, 7.0, 21.0, 6.0, 20.5);
      painter.drawPath(loop);
      break;
    }
    case StudioIcon::Rectangle:
      painter.drawRect(QRectF(4.0, 6.0, 16.0, 12.0));
      break;
    case StudioIcon::Ellipse:
      painter.drawEllipse(QRectF(3.5, 5.5, 17.0, 13.0));
      break;
    case StudioIcon::Invert: {
      painter.drawEllipse(QPointF(12.0, 12.0), 7.5, 7.5);
      QPainterPath half;
      half.moveTo(12.0, 4.5);
      half.arcTo(QRectF(4.5, 4.5, 15.0, 15.0), 90.0, 180.0);
      half.closeSubpath();
      painter.fillPath(half, color);
      break;
    }
    case StudioIcon::Feather: {
      QPainterPath vane;
      vane.moveTo(6.0, 18.0);
      vane.cubicTo(8.0, 11.0, 12.0, 6.5, 19.5, 4.5);
      vane.cubicTo(19.0, 11.5, 14.0, 16.5, 6.0, 18.0);
      painter.drawPath(vane);
      painter.drawLine(QPointF(4.0, 20.0), QPointF(14.0, 10.0));
      break;
    }
    case StudioIcon::Copy:
      painter.drawRoundedRect(QRectF(8.0, 8.0, 12.0, 12.0), 1.5, 1.5);
      painter.drawPath(polyline({{5.0, 15.5}, {4.0, 15.5}, {4.0, 4.0}, {15.5, 4.0}, {15.5, 5.0}}));
      break;
    case StudioIcon::FlipHorizontal:
    case StudioIcon::FlipVertical: {
      if (icon == StudioIcon::FlipVertical) {
        painter.translate(12.0, 12.0);
        painter.rotate(90.0);
        painter.translate(-12.0, -12.0);
      }
      painter.drawPath(polyline({{10.0, 6.0}, {10.0, 18.0}, {3.5, 18.0}}, true));
      painter.setBrush(color);
      painter.drawPath(polyline({{14.0, 6.0}, {14.0, 18.0}, {20.5, 18.0}}, true));
      break;
    }
    case StudioIcon::Rotate: {
      QPainterPath arc;
      arc.arcMoveTo(QRectF(4.5, 4.5, 15.0, 15.0), 120.0);
      arc.arcTo(QRectF(4.5, 4.5, 15.0, 15.0), 120.0, -280.0);
      painter.drawPath(arc);
      draw_arrow_head(painter, QPointF(8.2, 5.6), QPointF(11.0, 4.6), 3.0);
      break;
    }
    case StudioIcon::Warp: {
      painter.drawRoundedRect(QRectF(4.0, 4.0, 16.0, 16.0), 2.0, 2.0);
      QPainterPath curves;
      curves.moveTo(4.0, 10.0);
      curves.cubicTo(9.0, 6.5, 15.0, 13.5, 20.0, 10.0);
      curves.moveTo(4.0, 15.0);
      curves.cubicTo(9.0, 11.5, 15.0, 18.5, 20.0, 15.0);
      curves.moveTo(10.0, 4.0);
      curves.cubicTo(8.0, 9.0, 12.0, 15.0, 10.0, 20.0);
      painter.drawPath(curves);
      break;
    }
    case StudioIcon::Check:
      painter.drawPath(polyline({{5.0, 12.5}, {10.0, 17.5}, {19.0, 7.0}}));
      break;
    case StudioIcon::Import:
      painter.drawPath(polyline({{4.5, 14.0}, {4.5, 20.0}, {19.5, 20.0}, {19.5, 14.0}}));
      painter.drawLine(QPointF(12.0, 3.5), QPointF(12.0, 15.0));
      painter.drawPath(polyline({{8.0, 11.0}, {12.0, 15.0}, {16.0, 11.0}}));
      break;
    case StudioIcon::More:
      painter.setBrush(color);
      for (const qreal x : {6.0, 12.0, 18.0}) {
        painter.drawEllipse(QPointF(x, 12.0), 1.2, 1.2);
      }
      break;
    case StudioIcon::Minus:
      painter.drawLine(QPointF(5.0, 12.0), QPointF(19.0, 12.0));
      break;
    case StudioIcon::RotateLeft: {
      // Rotate, mirrored about the vertical center line.
      QPainterPath arc;
      arc.arcMoveTo(QRectF(4.5, 4.5, 15.0, 15.0), 60.0);
      arc.arcTo(QRectF(4.5, 4.5, 15.0, 15.0), 60.0, 280.0);
      painter.drawPath(arc);
      draw_arrow_head(painter, QPointF(15.8, 5.6), QPointF(13.0, 4.6), 3.0);
      break;
    }
  }
  painter.restore();
}

QFont studio_scaled_font(QFont font, qreal factor, QFont::Weight weight) {
  if (font.pointSizeF() > 0) {
    font.setPointSizeF(font.pointSizeF() * factor);
  } else if (font.pixelSize() > 0) {
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(font.pixelSize() * factor))));
  }
  font.setWeight(weight);
  return font;
}

void paint_studio_surface(QPainter& painter, const QRectF& rect, qreal radius, const QColor& fill,
                          const QColor& border) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  auto shadow = theme().studio_shadow;
  const int base_alpha = shadow.alpha();
  // Three widening layers approximate a soft blur without an offscreen pass.
  for (int step = 3; step >= 1; --step) {
    shadow.setAlpha(base_alpha / (step + 1));
    painter.setBrush(shadow);
    const qreal grow = step * 1.5;
    painter.drawRoundedRect(rect.adjusted(-grow, -grow + 2.0, grow, grow + 2.0), radius + grow, radius + grow);
  }
  painter.setBrush(fill);
  painter.setPen(border.alpha() > 0 ? QPen(border, 1.0) : QPen(Qt::NoPen));
  painter.drawRoundedRect(rect, radius, radius);
  painter.restore();
}

void paint_studio_checkerboard(QPainter& painter, const QRectF& rect, int cell) {
  // Transparency checkerboard: content, not chrome (theme_palette.hpp exemption).
  static const QColor kLight(0xf2, 0xf2, 0xf2);
  static const QColor kDark(0xcc, 0xcc, 0xcc);
  painter.save();
  painter.setClipRect(rect);
  painter.fillRect(rect, kLight);
  const int columns = static_cast<int>(std::ceil(rect.width() / cell));
  const int rows = static_cast<int>(std::ceil(rect.height() / cell));
  for (int row = 0; row < rows; ++row) {
    for (int column = row % 2; column < columns; column += 2) {
      painter.fillRect(QRectF(rect.left() + column * cell, rect.top() + row * cell, cell, cell), kDark);
    }
  }
  painter.restore();
}

// --- StudioIconButton ---------------------------------------------------------------

StudioIconButton::StudioIconButton(StudioIcon icon, QWidget* parent)
    : QAbstractButton(parent), has_icon_(true), icon_(icon) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_Hover, true);
}

StudioIconButton::StudioIconButton(QWidget* parent) : QAbstractButton(parent) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_Hover, true);
}

void StudioIconButton::set_icon(StudioIcon icon) {
  has_icon_ = true;
  icon_ = icon;
  update();
}

void StudioIconButton::set_extent(int extent) {
  extent_ = extent;
  updateGeometry();
  update();
}

QSize StudioIconButton::sizeHint() const {
  if (has_icon_) {
    return {extent_, extent_};
  }
  auto bold = font();
  bold.setWeight(QFont::DemiBold);
  return {QFontMetrics(bold).horizontalAdvance(text()) + 20, extent_};
}

void StudioIconButton::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto& palette = theme();
  QColor ink = palette.studio_icon;
  if (!isEnabled()) {
    ink = palette.studio_text_muted;
  } else if (isChecked()) {
    ink = palette.studio_accent;
  } else if (hovered_ || isDown()) {
    ink = palette.studio_icon_hover;
  }
  if (isDown() && isEnabled()) {
    auto press = palette.studio_icon_hover;
    press.setAlpha(28);
    painter.setPen(Qt::NoPen);
    painter.setBrush(press);
    painter.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 8, 8);
  }
  if (has_icon_) {
    const qreal side = std::min(width(), height()) * 0.62;
    paint_studio_icon(painter, icon_, QRectF(QPointF(0, 0), QSizeF(side, side)).translated(
                                          (width() - side) / 2.0, (height() - side) / 2.0),
                      ink);
    return;
  }
  auto bold = font();
  bold.setWeight(QFont::DemiBold);
  painter.setFont(bold);
  painter.setPen(isChecked() ? palette.studio_accent : (isEnabled() ? palette.studio_text : palette.studio_text_muted));
  painter.drawText(rect(), Qt::AlignCenter, text());
}

void StudioIconButton::enterEvent(QEnterEvent* event) {
  hovered_ = true;
  update();
  QAbstractButton::enterEvent(event);
}

void StudioIconButton::leaveEvent(QEvent* event) {
  hovered_ = false;
  update();
  QAbstractButton::leaveEvent(event);
}

// --- StudioColorButton --------------------------------------------------------------

StudioColorButton::StudioColorButton(QWidget* parent) : QAbstractButton(parent) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
}

void StudioColorButton::set_color(QColor color) {
  if (color_ == color) {
    return;
  }
  color_ = color;
  update();
}

QSize StudioColorButton::sizeHint() const { return {40, 40}; }

void StudioColorButton::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const qreal diameter = std::min(width(), height()) * 0.66;
  const QRectF disc(QPointF((width() - diameter) / 2.0, (height() - diameter) / 2.0), QSizeF(diameter, diameter));
  painter.setPen(QPen(isChecked() ? theme().studio_accent : theme().studio_icon, 2.0));
  painter.setBrush(color_);
  painter.drawEllipse(disc);
}

// --- StudioSlider -------------------------------------------------------------------

namespace {
constexpr qreal kThumbHeight = 26.0;
constexpr qreal kThumbWidth = 24.0;
constexpr qreal kTrackWidth = 8.0;
}  // namespace

StudioSlider::StudioSlider(QWidget* parent) : QWidget(parent) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
}

void StudioSlider::set_range(int minimum, int maximum) {
  minimum_ = minimum;
  maximum_ = std::max(minimum, maximum);
  value_ = std::clamp(value_, minimum_, maximum_);
  update();
}

void StudioSlider::set_curve(double exponent) {
  curve_ = std::max(0.1, exponent);
  update();
}

void StudioSlider::set_value(int value) {
  const int clamped = std::clamp(value, minimum_, maximum_);
  if (clamped == value_) {
    return;
  }
  value_ = clamped;
  update();
}

QSize StudioSlider::sizeHint() const { return {static_cast<int>(kThumbWidth) + 8, 150}; }

QRectF StudioSlider::track_rect() const {
  const qreal top = kThumbHeight / 2.0 + 2.0;
  const qreal bottom = height() - kThumbHeight / 2.0 - 2.0;
  return {(width() - kTrackWidth) / 2.0, top, kTrackWidth, std::max(1.0, bottom - top)};
}

double StudioSlider::position_for_value(int value) const {
  if (maximum_ == minimum_) {
    return 0.0;
  }
  const double fraction = static_cast<double>(value - minimum_) / static_cast<double>(maximum_ - minimum_);
  return std::pow(std::clamp(fraction, 0.0, 1.0), 1.0 / curve_);
}

int StudioSlider::value_for_position(double position) const {
  const double fraction = std::pow(std::clamp(position, 0.0, 1.0), curve_);
  return minimum_ + static_cast<int>(std::lround(fraction * (maximum_ - minimum_)));
}

QPoint StudioSlider::thumb_center() const {
  const auto track = track_rect();
  const double y = track.bottom() - position_for_value(value_) * track.height();
  return {width() / 2, static_cast<int>(std::lround(y))};
}

void StudioSlider::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto& palette = theme();
  const auto track = track_rect();
  painter.setPen(Qt::NoPen);
  painter.setBrush(palette.studio_slider_track);
  painter.drawRoundedRect(track, kTrackWidth / 2.0, kTrackWidth / 2.0);
  const auto thumb = thumb_center();
  QRectF filled(track.left(), thumb.y(), track.width(), track.bottom() - thumb.y());
  painter.setBrush(palette.studio_slider_fill);
  painter.drawRoundedRect(filled, kTrackWidth / 2.0, kTrackWidth / 2.0);
  const QRectF thumb_rect(thumb.x() - kThumbWidth / 2.0, thumb.y() - kThumbHeight / 2.0, kThumbWidth, kThumbHeight);
  auto shadow = palette.studio_shadow;
  painter.setBrush(shadow);
  painter.drawRoundedRect(thumb_rect.translated(0, 1.5), 7, 7);
  painter.setBrush(palette.studio_slider_thumb);
  painter.drawRoundedRect(thumb_rect, 7, 7);
}

void StudioSlider::drag_to(double y) {
  const auto track = track_rect();
  const double position = (track.bottom() - (y - grab_offset_)) / track.height();
  const int value = value_for_position(position);
  if (value != value_) {
    value_ = value;
    update();
    emit value_changed(value_);
  }
}

void StudioSlider::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  const auto thumb = thumb_center();
  const qreal y = event->position().y();
  // Grabbing the thumb keeps its offset (no jump); a press on the track jumps there.
  grab_offset_ = std::abs(y - thumb.y()) <= kThumbHeight / 2.0 ? y - thumb.y() : 0.0;
  dragging_ = true;
  emit drag_started();
  drag_to(y);
  event->accept();
}

void StudioSlider::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_) {
    drag_to(event->position().y());
    event->accept();
  }
}

void StudioSlider::mouseReleaseEvent(QMouseEvent* event) {
  if (dragging_ && event->button() == Qt::LeftButton) {
    dragging_ = false;
    emit drag_finished();
    event->accept();
  }
}

void StudioSlider::wheelEvent(QWheelEvent* event) {
  const int steps = event->angleDelta().y() / 120;
  if (steps == 0) {
    return;
  }
  const double position = position_for_value(value_) + steps * 0.02;
  int value = value_for_position(position);
  if (value == value_) {
    value = std::clamp(value_ + steps, minimum_, maximum_);
  }
  if (value != value_) {
    value_ = value;
    update();
    emit drag_started();
    emit value_changed(value_);
    emit drag_finished();
  }
  event->accept();
}

// --- StudioBubble -------------------------------------------------------------------

StudioBubble::StudioBubble(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  hide();
}

void StudioBubble::show_text(const QString& text, QPoint anchor_left_center) {
  text_ = text;
  const auto bold = studio_scaled_font(font(), 1.1, QFont::DemiBold);
  setFont(bold);
  const QFontMetrics metrics(bold);
  const QSize size(metrics.horizontalAdvance(text_) + 24, metrics.height() + 12);
  setGeometry(QRect(QPoint(anchor_left_center.x(), anchor_left_center.y() - size.height() / 2), size));
  raise();
  show();
  update();
}

void StudioBubble::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(theme().studio_bubble_bg);
  painter.drawRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
  painter.setPen(theme().studio_bubble_text);
  painter.drawText(rect(), Qt::AlignCenter, text_);
}

// --- StudioPopover ------------------------------------------------------------------

StudioPopover::StudioPopover(QWidget* host) : QWidget(host) {
  setObjectName(QStringLiteral("studioPopover"));
  // Only the rounded panel paints; the corners and shadow margin show the canvas.
  setProperty("studioTransparent", true);
  setStyleSheet(QStringLiteral("QWidget[studioTransparent=\"true\"] { background: transparent; }"));
  layout_ = new QVBoxLayout(this);
  layout_->setSpacing(0);
  hide();
}

void StudioPopover::set_content(QWidget* content) {
  if (content_ != nullptr) {
    layout_->removeWidget(content_);
    content_->deleteLater();
  }
  content_ = content;
  if (content_ != nullptr) {
    content_->setParent(this);
    layout_->addWidget(content_);
  }
}

void StudioPopover::set_dismiss_exempt(QWidget* widget) { dismiss_exempt_ = widget; }

void StudioPopover::show_below(const QRect& anchor, QSize content_size, bool arrow) {
  arrow_ = arrow;
  const int top_extra = arrow_ ? kArrow : 0;
  layout_->setContentsMargins(kMargin, kMargin + top_extra, kMargin, kMargin);
  auto* host = parentWidget();
  const QSize size(content_size.width() + 2 * kMargin, content_size.height() + 2 * kMargin + top_extra);
  int x = anchor.center().x() - size.width() / 2;
  const int max_x = host != nullptr ? host->width() - size.width() - 4 : x;
  x = std::clamp(x, 4, std::max(4, max_x));
  int height = size.height();
  if (host != nullptr) {
    height = std::min(height, host->height() - anchor.bottom() - 8);
  }
  setGeometry(x, anchor.bottom() + 2, size.width(), std::max(80, height));
  arrow_x_ = std::clamp(anchor.center().x() - x, kMargin + 18, size.width() - kMargin - 18);
  raise();
  show();
  qApp->installEventFilter(this);
  update();
}

void StudioPopover::show_at(const QRect& rect) {
  arrow_ = false;
  layout_->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
  setGeometry(rect.adjusted(-kMargin, -kMargin, kMargin, kMargin));
  raise();
  show();
  qApp->installEventFilter(this);
  update();
}

void StudioPopover::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto& palette = theme();
  const int top_extra = arrow_ ? kArrow : 0;
  const QRectF panel = QRectF(rect()).adjusted(kMargin - 6, kMargin - 6 + top_extra, -(kMargin - 6), -(kMargin - 4));
  paint_studio_surface(painter, panel, 14.0, palette.studio_panel_bg, palette.studio_panel_border);
  if (arrow_) {
    QPainterPath arrow;
    arrow.moveTo(arrow_x_ - kArrow, panel.top() + 0.5);
    arrow.lineTo(arrow_x_, panel.top() - kArrow + 1.0);
    arrow.lineTo(arrow_x_ + kArrow, panel.top() + 0.5);
    arrow.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette.studio_panel_bg);
    painter.drawPath(arrow);
  }
}

void StudioPopover::hideEvent(QHideEvent* event) {
  qApp->removeEventFilter(this);
  QWidget::hideEvent(event);
  emit closed();
}

bool StudioPopover::eventFilter(QObject* watched, QEvent* event) {
  if (!isVisible() || !dismiss_on_outside_press_) {
    return false;
  }
  if (event->type() == QEvent::KeyPress) {
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape && QApplication::activeModalWidget() == nullptr &&
        QApplication::activePopupWidget() == nullptr) {
      hide();
      return true;
    }
    return false;
  }
  if (event->type() != QEvent::MouseButtonPress) {
    return false;
  }
  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget == nullptr) {
    return false;
  }
  // Popups and dialogs the content opened (combo lists, menus, color dialogs) live
  // in other windows; a press there never dismisses the panel.
  if (widget->window() != window()) {
    return false;
  }
  if (widget == this || isAncestorOf(widget)) {
    return false;
  }
  if (dismiss_exempt_ != nullptr && (widget == dismiss_exempt_ || dismiss_exempt_->isAncestorOf(widget))) {
    return false;
  }
  hide();
  return false;
}

void StudioPopover::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    hide();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

QString studio_panel_qss() {
  return QStringLiteral(
      "QWidget#studioPanel { background: transparent; color: @studio_text; }"
      "QWidget#studioPanel QLabel { color: @studio_text; background: transparent; }"
      "QWidget#studioPanel QLabel[studioRole=\"title\"] { font-weight: 600; font-size: 15px; }"
      "QWidget#studioPanel QLabel[studioRole=\"muted\"] { color: @studio_text_muted; }"
      "QWidget#studioPanel QLabel[studioRole=\"section\"] { color: @studio_text_muted; font-size: 11px;"
      "  font-weight: 600; padding-top: 6px; }"
      "QWidget#studioPanel QScrollArea, QWidget#studioPanel QScrollArea > QWidget > QWidget,"
      "QWidget#studioPanel QStackedWidget, QWidget#studioPanel QStackedWidget > QWidget"
      "  { background: transparent; border: none; }"
      "QWidget#studioPanel QLabel[studioRole=\"hero\"] { font-size: 26px; font-weight: 700; }"
      "QWidget#studioPanel QListWidget { background: transparent; border: none; outline: none;"
      "  color: @studio_text; }"
      "QWidget#studioPanel QListWidget::item { border-radius: 8px; padding: 6px 8px; margin: 1px 0px; }"
      "QWidget#studioPanel QListWidget::item:hover { background: @studio_row_hover_bg; }"
      "QWidget#studioPanel QListWidget::item:selected { background: @studio_row_selected_bg;"
      "  color: @studio_row_selected_text; }"
      "QWidget#studioPanel QPushButton { background: @studio_row_bg; color: @studio_text; border: none;"
      "  border-radius: 8px; padding: 7px 12px; text-align: left; }"
      "QWidget#studioPanel QPushButton:hover { background: @studio_row_hover_bg; }"
      "QWidget#studioPanel QPushButton:checked { background: @studio_row_selected_bg;"
      "  color: @studio_row_selected_text; }"
      "QWidget#studioPanel QPushButton:disabled { color: @studio_text_muted; }"
      "QWidget#studioPanel QPushButton[studioRole=\"tab\"] { background: transparent; text-align: center;"
      "  padding: 6px 4px; }"
      "QWidget#studioPanel QPushButton[studioRole=\"tab\"]:checked { color: @studio_accent; }"
      "QWidget#studioPanel QLineEdit { background: @studio_row_bg; color: @studio_text; border: none;"
      "  border-radius: 6px; padding: 4px 8px; selection-background-color: @studio_row_selected_bg; }"
      "QWidget#studioPanel QComboBox { background: @studio_row_bg; color: @studio_text; border: none;"
      "  border-radius: 6px; padding: 4px 8px; }"
      "QWidget#studioPanel QComboBox QAbstractItemView { background: @studio_row_bg; color: @studio_text;"
      "  selection-background-color: @studio_row_selected_bg; border: none; }"
      "QWidget#studioPanel QScrollBar:vertical { background: transparent; width: 6px; margin: 2px; }"
      "QWidget#studioPanel QScrollBar::handle:vertical { background: @studio_row_hover_bg; border-radius: 3px;"
      "  min-height: 24px; }"
      "QWidget#studioPanel QScrollBar::add-line, QWidget#studioPanel QScrollBar::sub-line { height: 0px; }"
      "QWidget#studioPanel QScrollBar::add-page, QWidget#studioPanel QScrollBar::sub-page"
      "  { background: transparent; }"
      "QWidget#studioPanel QFrame[studioRole=\"separator\"] { background: @studio_row_hover_bg;"
      "  max-height: 1px; min-height: 1px; border: none; }");
}

}  // namespace patchy::ui
