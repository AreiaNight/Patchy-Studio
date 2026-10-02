#pragma once

// The Patchy Studio gallery: a full-window grid of artwork cards (the open
// documents first, then recent files) with Import and a "+" menu of canvas
// presets, like Procreate's gallery. Shown at startup and from the top bar's
// Gallery button.

#include <QAbstractButton>
#include <QImage>
#include <QPointer>
#include <QWidget>

#include <vector>

class QLabel;
class QScrollArea;

namespace patchy::ui {

class StudioIconButton;
class StudioPopover;
class StudioShell;

class StudioGalleryCard final : public QAbstractButton {
  Q_OBJECT

public:
  StudioGalleryCard(QString title, QString subtitle, QWidget* parent);

  void set_image(const QImage& image);
  void set_active(bool active);
  void set_placeholder_text(const QString& text);
  // Open documents get a close button in the corner.
  StudioIconButton* add_close_button();
  [[nodiscard]] QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  [[nodiscard]] QRectF thumbnail_rect() const;

  QString title_;
  QString subtitle_;
  QString placeholder_;
  QImage image_;
  bool active_{false};
  bool hovered_{false};
  StudioIconButton* close_button_{nullptr};
};

class StudioGallery final : public QWidget {
  Q_OBJECT

public:
  StudioGallery(StudioShell& shell, QWidget* parent);

  // Rebuilds the cards from the shell's open documents and recent files.
  void reload();

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  void relayout_cards();
  void show_new_canvas_menu();
  void load_recent_thumbnail(StudioGalleryCard* card, const QString& path);

  StudioShell& shell_;
  QLabel* title_{nullptr};
  StudioIconButton* import_button_{nullptr};
  StudioIconButton* new_button_{nullptr};
  QScrollArea* scroll_{nullptr};
  QWidget* grid_{nullptr};
  QLabel* open_label_{nullptr};
  QLabel* recent_label_{nullptr};
  QLabel* empty_label_{nullptr};
  std::vector<QPointer<StudioGalleryCard>> open_cards_;
  std::vector<QPointer<StudioGalleryCard>> recent_cards_;
  StudioPopover* new_canvas_popover_{nullptr};
  // reload() runs on every shell refresh while visible; this skips rebuilding
  // cards (and re-rendering thumbnails) when nothing they show has changed.
  QString signature_;
};

// Where the gallery keeps the small composites of documents it has shown, keyed
// by file path, so recent PSDs get a picture without reopening them.
[[nodiscard]] QString studio_thumbnail_cache_path(const QString& document_path);

}  // namespace patchy::ui
