#pragma once

// The dropping panels of the Patchy Studio shell: Brush Library, Layers, Color,
// Actions, and Adjustments. Each is a StudioPanel the shell hosts in its
// StudioPopover; they talk to the editor only through StudioShell's bridge.

#include <QWidget>

namespace patchy::ui {

class StudioShell;

class StudioPanel : public QWidget {
  Q_OBJECT

public:
  StudioPanel(StudioShell& shell, QWidget* parent);

  // Re-reads editor state while the panel is open (the shell's coalesced refresh).
  virtual void refresh_from_editor() {}
  // Size the popover gives the panel's content.
  [[nodiscard]] virtual QSize preferred_size() const = 0;

protected:
  StudioShell& shell_;
};

enum class StudioPanelKind { Actions, Adjustments, Brushes, Layers, Color };

[[nodiscard]] StudioPanel* create_studio_panel(StudioPanelKind kind, StudioShell& shell, QWidget* parent);

}  // namespace patchy::ui
