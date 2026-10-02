#pragma once

// Painted building blocks for the Patchy Studio shell (studio_shell.hpp): the
// line icons, the round icon buttons of the top bar, the pill sliders of the
// side bar, the value bubble, and the rounded popover that hosts every panel.
// Everything paints from the studio_* roles in theme_palette.hpp, so the shell
// follows Dark/Light like the rest of the app.

#include <QAbstractButton>
#include <QColor>
#include <QFont>
#include <QPointer>
#include <QWidget>

#include <functional>

class QPainter;
class QVBoxLayout;

namespace patchy::ui {

enum class StudioIcon {
  Wrench,
  Wand,
  Selection,
  Transform,
  Brush,
  Smudge,
  Eraser,
  Layers,
  Undo,
  Redo,
  Plus,
  Close,
  Eyedropper,
  Canvas,
  Share,
  Prefs,
  Help,
  Freehand,
  Rectangle,
  Ellipse,
  Invert,
  Feather,
  Copy,
  FlipHorizontal,
  FlipVertical,
  Rotate,
  Warp,
  Check,
  Import,
  More,
};

// Draws `icon` as 1.6px-at-24px line art centered in `rect`.
void paint_studio_icon(QPainter& painter, StudioIcon icon, const QRectF& rect, const QColor& color);

// `font` scaled by `factor`, whether it was set in points or (the app default) pixels.
[[nodiscard]] QFont studio_scaled_font(QFont font, qreal factor, QFont::Weight weight);

// Fills a rounded rectangle with a soft drop shadow underneath (studio_shadow).
void paint_studio_surface(QPainter& painter, const QRectF& rect, qreal radius, const QColor& fill,
                          const QColor& border);

// The transparency checkerboard behind thumbnails. Content, not chrome, so its
// two grays are fixed (see the exemption in theme_palette.hpp).
void paint_studio_checkerboard(QPainter& painter, const QRectF& rect, int cell = 6);

// A round, flat icon button: studio_icon at rest, studio_icon_hover under the
// mouse, studio_accent while checked. With text and no icon it is a text button.
class StudioIconButton : public QAbstractButton {
  Q_OBJECT

public:
  explicit StudioIconButton(StudioIcon icon, QWidget* parent = nullptr);
  explicit StudioIconButton(QWidget* parent = nullptr);  // text-only

  void set_icon(StudioIcon icon);
  void set_extent(int extent);
  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  bool has_icon_{false};
  StudioIcon icon_{StudioIcon::Plus};
  int extent_{40};
  bool hovered_{false};
};

// The current-color disc at the right end of the top bar.
class StudioColorButton : public QAbstractButton {
  Q_OBJECT

public:
  explicit StudioColorButton(QWidget* parent = nullptr);
  void set_color(QColor color);
  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QColor color_{Qt::black};
};

// A vertical pill slider (Procreate's side bar). Value runs bottom (minimum) to
// top (maximum); `curve` maps the 0..1 track position to 0..1 of the range so
// the size slider can spend more travel on small brushes.
class StudioSlider : public QWidget {
  Q_OBJECT

public:
  explicit StudioSlider(QWidget* parent = nullptr);

  void set_range(int minimum, int maximum);
  void set_curve(double exponent);
  void set_value(int value);  // no signal
  [[nodiscard]] int value() const noexcept { return value_; }
  [[nodiscard]] int minimum() const noexcept { return minimum_; }
  [[nodiscard]] int maximum() const noexcept { return maximum_; }
  // Widget-space center of the thumb, for placing the value bubble.
  [[nodiscard]] QPoint thumb_center() const;
  [[nodiscard]] QSize sizeHint() const override;

signals:
  void value_changed(int value);
  void drag_started();
  void drag_finished();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

private:
  [[nodiscard]] QRectF track_rect() const;
  [[nodiscard]] double position_for_value(int value) const;
  [[nodiscard]] int value_for_position(double position) const;
  void drag_to(double y);

  int minimum_{1};
  int maximum_{100};
  int value_{50};
  double curve_{1.0};
  bool dragging_{false};
  double grab_offset_{0.0};
};

// A short-lived rounded label ("42 px", "75%") shown beside a slider while it moves.
class StudioBubble : public QWidget {
  Q_OBJECT

public:
  explicit StudioBubble(QWidget* parent = nullptr);
  void show_text(const QString& text, QPoint anchor_left_center);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QString text_;
};

// A rounded panel that drops from a top-bar button with a small arrow pointing
// at it. It is a child of the canvas area, not a window, so it composes over the
// canvas without a compositor round trip. A press anywhere outside it (other
// than inside a popup the content opened, such as a combo list) closes it.
class StudioPopover : public QWidget {
  Q_OBJECT

public:
  explicit StudioPopover(QWidget* host);

  // Takes ownership of `content`.
  void set_content(QWidget* content);
  [[nodiscard]] QWidget* content() const noexcept { return content_; }
  // Anchors under `anchor` (a rect in host coordinates) with the content's
  // preferred size, clamped into the host. `arrow` false draws a plain panel.
  void show_below(const QRect& anchor, QSize content_size, bool arrow = true);
  // Places the panel at an explicit host-space rect (bottom bars).
  void show_at(const QRect& rect);
  [[nodiscard]] bool is_open() const { return isVisible(); }
  // A press on these widgets does not dismiss the popover (its own anchor button
  // toggles instead of close-and-reopen).
  void set_dismiss_exempt(QWidget* widget);
  // Bottom tool bars stay up until their tool is left, so outside presses (the
  // canvas strokes that make a selection) must not close them.
  void set_dismiss_on_outside_press(bool dismiss) noexcept { dismiss_on_outside_press_ = dismiss; }

signals:
  void closed();

protected:
  void paintEvent(QPaintEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  static constexpr int kMargin = 12;  // shadow + padding around the content
  static constexpr int kArrow = 10;

  QWidget* content_{nullptr};
  QVBoxLayout* layout_{nullptr};
  bool arrow_{true};
  int arrow_x_{0};
  QPointer<QWidget> dismiss_exempt_;
  bool dismiss_on_outside_press_{true};
};

// Studio QSS shared by panel content (lists, labels, line edits, combos), as an
// @token template for set_themed_style.
[[nodiscard]] QString studio_panel_qss();

}  // namespace patchy::ui
