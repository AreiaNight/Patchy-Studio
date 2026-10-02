#pragma once

#include <QWidget>

#include <optional>

namespace patchy::ui {

// Small graph of the pen pressure curve (Preferences > Pen): input pressure on
// the x axis, the pressure brushes receive on the y axis, with the linear
// diagonal for reference. It doubles as a pen test pad: pressing on it with
// a tablet pen marks the reported pressure on the curve and prints the raw and
// shaped values, so a user can confirm pressure reaches the app.
class PressureCurvePreview final : public QWidget {
  Q_OBJECT

public:
  explicit PressureCurvePreview(QWidget* parent = nullptr);

  void set_curve(int curve);
  [[nodiscard]] int curve() const noexcept { return curve_; }
  [[nodiscard]] QSize sizeHint() const override;
  // The last pen sample, raw (as the tablet reported it); empty before any.
  [[nodiscard]] std::optional<float> live_pressure() const noexcept { return live_pressure_; }
  [[nodiscard]] bool live_device_has_pressure() const noexcept { return live_device_has_pressure_; }

protected:
  void paintEvent(QPaintEvent* event) override;
  void changeEvent(QEvent* event) override;
  void tabletEvent(QTabletEvent* event) override;

private:
  int curve_{0};
  std::optional<float> live_pressure_;
  bool live_device_has_pressure_{true};
};

}  // namespace patchy::ui
