#include "ui/main_window.hpp"

#include "ui/studio_shell.hpp"

namespace patchy::ui {

void MainWindow::enable_studio_shell() {
  if (studio_shell_ != nullptr) {
    return;
  }
  studio_shell_ = new StudioShell(*this);
}

void MainWindow::notify_studio_shell() {
  if (studio_shell_ != nullptr && !shutting_down_) {
    studio_shell_->schedule_refresh();
  }
}

}  // namespace patchy::ui
