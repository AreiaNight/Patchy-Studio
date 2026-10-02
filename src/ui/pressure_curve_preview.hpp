#pragma once

#include <QWidget>

namespace patchy::ui {

// Small graph of the pen pressure curve (Preferences > Pen): input pressure on
// the x axis, the pressure brushes receive on the y axis, with the linear
// diagonal for reference.
class PressureCurvePreview final : public QWidget {
  Q_OBJECT

public:
  explicit PressureCurvePreview(QWidget* parent = nullptr);

  void set_curve(int curve);
  [[nodiscard]] int curve() const noexcept { return curve_; }
  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  int curve_{0};
};

}  // namespace patchy::ui
