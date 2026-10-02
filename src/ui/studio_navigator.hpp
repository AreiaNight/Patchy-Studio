#pragma once

// The Patchy Studio navigator: a small floating panel in a bottom corner of the
// canvas area (the side-bar side, so it follows the handedness setting). It shows
// the whole artwork with the visible part outlined; a press or drag there pans
// the view, and its rows zoom and rotate the view like Photoshop's Navigator
// panel. It drives the active CanvasWidget directly through its view API; view
// changes never touch the document or its undo history.

#include <QImage>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QTimer;

namespace patchy::ui {

class CanvasWidget;
class StudioShell;
class StudioNavigatorView;
class StudioNavigatorSlider;

class StudioNavigator final : public QWidget {
  Q_OBJECT

public:
  StudioNavigator(StudioShell& shell, QWidget* parent);

  // Re-reads the canvas view (zoom, viewport outline). Cheap; called on every
  // pan and zoom step.
  void sync_from_canvas();
  // Rebuilds the overview thumbnail on the next poll when the render changed.
  void schedule_overview();
  [[nodiscard]] QSize sizeHint() const override;

  // Zoom slider position (0..kZoomSteps) <-> zoom factor, log-scaled between the
  // canvas limits so every doubling gets the same travel.
  static constexpr int kZoomSteps = 1000;
  [[nodiscard]] static int slider_position_for_zoom(double zoom);
  [[nodiscard]] static double zoom_for_slider_position(int position);

signals:
  void close_requested();
  // A drag on the panel's background moved it to `top_left` (parent
  // coordinates, not yet clamped); a double-click there asks for the default spot.
  void move_requested(QPoint top_left);
  void move_finished();
  void reset_position_requested();

protected:
  void paintEvent(QPaintEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  [[nodiscard]] CanvasWidget* canvas() const;
  void poll_overview();
  void zoom_by(double factor);
  void rotate_by(double degrees);
  void set_rotation(double degrees);

  StudioShell& shell_;
  StudioNavigatorView* view_{nullptr};
  StudioNavigatorSlider* zoom_slider_{nullptr};
  QLabel* zoom_label_{nullptr};
  StudioNavigatorSlider* rotation_slider_{nullptr};
  QLabel* rotation_label_{nullptr};
  QAbstractButton* flip_horizontal_button_{nullptr};
  QAbstractButton* flip_vertical_button_{nullptr};
  QTimer* poll_timer_{nullptr};
  qint64 overview_key_{0};
  QSize overview_document_size_;
  bool overview_dirty_{true};
  bool syncing_{false};
  bool moving_{false};
  QPoint move_grab_offset_;
};

}  // namespace patchy::ui
