#include "ui/studio_gallery.hpp"

#include "ui/background_workers.hpp"
#include "ui/studio_shell.hpp"
#include "ui/studio_widgets.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEnterEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {
namespace {

constexpr int kCardWidth = 220;
constexpr int kThumbHeight = 165;
constexpr int kCardHeight = kThumbHeight + 52;
constexpr int kGap = 26;
constexpr int kSideMargin = 36;
constexpr int kHeaderHeight = 72;

struct CanvasPreset {
  const char* name;
  int width;
  int height;
  double ppi;
};

}  // namespace

QString studio_thumbnail_cache_path(const QString& document_path) {
  const auto key = QCryptographicHash::hash(QFileInfo(document_path).absoluteFilePath().toUtf8(),
                                            QCryptographicHash::Sha1)
                       .toHex();
  const auto directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/studio-thumbnails");
  return directory + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".png");
}

// --- StudioGalleryCard --------------------------------------------------------------

StudioGalleryCard::StudioGalleryCard(QString title, QString subtitle, QWidget* parent)
    : QAbstractButton(parent), title_(std::move(title)), subtitle_(std::move(subtitle)) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_Hover, true);
  setToolTip(title_);
  resize(sizeHint());
}

QSize StudioGalleryCard::sizeHint() const { return {kCardWidth, kCardHeight}; }

void StudioGalleryCard::set_image(const QImage& image) {
  image_ = image;
  update();
}

void StudioGalleryCard::set_active(bool active) {
  active_ = active;
  update();
}

void StudioGalleryCard::set_placeholder_text(const QString& text) {
  placeholder_ = text;
  update();
}

StudioIconButton* StudioGalleryCard::add_close_button() {
  close_button_ = new StudioIconButton(StudioIcon::Close, this);
  close_button_->set_extent(28);
  close_button_->resize(28, 28);
  close_button_->hide();
  return close_button_;
}

QRectF StudioGalleryCard::thumbnail_rect() const { return {0.0, 0.0, double(width()), double(kThumbHeight)}; }

void StudioGalleryCard::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  const auto& palette = theme();
  const auto frame = thumbnail_rect().adjusted(4, 4, -4, -4);
  QRectF picture = frame;
  if (!image_.isNull()) {
    const QSizeF scaled = QSizeF(image_.size()).scaled(frame.size(), Qt::KeepAspectRatio);
    picture = QRectF(QPointF(frame.center().x() - scaled.width() / 2.0, frame.center().y() - scaled.height() / 2.0),
                     scaled);
  }
  const QColor border = active_ ? palette.studio_accent
                                : (hovered_ ? palette.studio_card_hover_border : palette.studio_card_border);
  paint_studio_surface(painter, picture, 6.0, palette.studio_card_bg, QColor(Qt::transparent));
  if (!image_.isNull()) {
    QPainterPath clip;
    clip.addRoundedRect(picture, 6.0, 6.0);
    painter.save();
    painter.setClipPath(clip);
    paint_studio_checkerboard(painter, picture, 8);
    painter.drawImage(picture, image_);
    painter.restore();
  } else {
    painter.setFont(studio_scaled_font(font(), 1.6, QFont::DemiBold));
    painter.setPen(palette.studio_text_muted);
    painter.drawText(picture, Qt::AlignCenter, placeholder_);
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(border, active_ || hovered_ ? 2.0 : 1.0));
  painter.drawRoundedRect(picture, 6.0, 6.0);

  auto bold = font();
  bold.setWeight(QFont::DemiBold);
  painter.setFont(bold);
  painter.setPen(palette.studio_text);
  const QRect title_rect(0, kThumbHeight + 6, width(), QFontMetrics(bold).height());
  painter.drawText(title_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                   QFontMetrics(bold).elidedText(title_, Qt::ElideMiddle, width()));
  painter.setFont(font());
  painter.setPen(palette.studio_text_muted);
  const QRect subtitle_rect(0, title_rect.bottom() + 2, width(), fontMetrics().height());
  painter.drawText(subtitle_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                   fontMetrics().elidedText(subtitle_, Qt::ElideMiddle, width()));
}

void StudioGalleryCard::enterEvent(QEnterEvent* event) {
  hovered_ = true;
  if (close_button_ != nullptr) {
    close_button_->show();
    close_button_->raise();
  }
  update();
  QAbstractButton::enterEvent(event);
}

void StudioGalleryCard::leaveEvent(QEvent* event) {
  hovered_ = false;
  if (close_button_ != nullptr) {
    close_button_->hide();
  }
  update();
  QAbstractButton::leaveEvent(event);
}

void StudioGalleryCard::resizeEvent(QResizeEvent* event) {
  if (close_button_ != nullptr) {
    close_button_->move(width() - close_button_->width() - 8, 8);
  }
  QAbstractButton::resizeEvent(event);
}

// --- StudioGallery ------------------------------------------------------------------

StudioGallery::StudioGallery(StudioShell& shell, QWidget* parent) : QWidget(parent), shell_(shell) {
  setObjectName(QStringLiteral("studioGallery"));
  setFocusPolicy(Qt::StrongFocus);

  auto* column = new QVBoxLayout(this);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);

  auto* header = new QWidget(this);
  header->setObjectName(QStringLiteral("studioPanel"));
  set_themed_style(*header, studio_panel_qss());
  header->setFixedHeight(kHeaderHeight);
  auto* header_row = new QHBoxLayout(header);
  header_row->setContentsMargins(kSideMargin, 12, kSideMargin - 8, 0);
  title_ = new QLabel(tr("Patchy Studio"), header);
  title_->setProperty("studioRole", QStringLiteral("hero"));
  header_row->addWidget(title_);
  header_row->addStretch(1);
  import_button_ = new StudioIconButton(header);
  import_button_->setObjectName(QStringLiteral("studioGalleryImport"));
  import_button_->setText(tr("Import"));
  import_button_->setToolTip(tr("Open a file from disk"));
  header_row->addWidget(import_button_);
  new_button_ = new StudioIconButton(StudioIcon::Plus, header);
  new_button_->setObjectName(QStringLiteral("studioGalleryNew"));
  new_button_->setToolTip(tr("New canvas"));
  new_button_->set_extent(44);
  header_row->addWidget(new_button_);
  column->addWidget(header);

  scroll_ = new QScrollArea(this);
  scroll_->setObjectName(QStringLiteral("studioPanel"));
  set_themed_style(*scroll_, studio_panel_qss());
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setWidgetResizable(false);
  grid_ = new QWidget(scroll_);
  grid_->setAttribute(Qt::WA_TranslucentBackground, true);
  scroll_->setWidget(grid_);
  scroll_->viewport()->setAutoFillBackground(false);
  column->addWidget(scroll_, 1);

  const auto make_section = [this](const QString& text) {
    auto* label = new QLabel(text, grid_);
    label->setProperty("studioRole", QStringLiteral("section"));
    set_themed_style(*label, QStringLiteral("QLabel { color: @studio_text_muted; font-weight: 600; }"));
    return label;
  };
  open_label_ = make_section(tr("OPEN"));
  recent_label_ = make_section(tr("RECENT"));
  empty_label_ = new QLabel(tr("Tap + to start a new canvas, or Import to open a file."), grid_);
  set_themed_style(*empty_label_, QStringLiteral("QLabel { color: @studio_text_muted; }"));

  new_canvas_popover_ = new StudioPopover(this);
  new_canvas_popover_->set_dismiss_exempt(new_button_);

  connect(import_button_, &QAbstractButton::clicked, this, [this] { shell_.open_file_dialog(); });
  connect(new_button_, &QAbstractButton::clicked, this, [this] {
    if (new_canvas_popover_->isVisible()) {
      new_canvas_popover_->hide();
    } else {
      show_new_canvas_menu();
    }
  });
}

void StudioGallery::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), theme().studio_gallery_bg);
}

void StudioGallery::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  relayout_cards();
}

void StudioGallery::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape && !shell_.open_documents().empty()) {
    shell_.hide_gallery();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void StudioGallery::reload() {
  const auto documents = shell_.open_documents();
  auto recent = shell_.recent_files();
  QStringList open_paths;
  QString signature;
  for (const auto& entry : documents) {
    signature += QStringLiteral("%1|%2|%3|%4|%5x%6;")
                     .arg(entry.tab_index)
                     .arg(entry.title)
                     .arg(entry.modified)
                     .arg(entry.active)
                     .arg(entry.size.width())
                     .arg(entry.size.height());
    if (!entry.path.isEmpty()) {
      open_paths.append(QFileInfo(entry.path).absoluteFilePath());
    }
  }
  // Open documents already have a card; listing them again under Recent is noise.
  recent.erase(std::remove_if(recent.begin(), recent.end(),
                              [&open_paths](const QString& path) {
                                return open_paths.contains(QFileInfo(path).absoluteFilePath());
                              }),
               recent.end());
  constexpr qsizetype kMaxRecent = 24;
  if (recent.size() > kMaxRecent) {
    recent = recent.mid(0, kMaxRecent);
  }
  signature += recent.join(QLatin1Char(';'));
  if (signature == signature_ && (!open_cards_.empty() || !recent_cards_.empty())) {
    return;
  }
  signature_ = signature;

  for (auto& card : open_cards_) {
    if (card != nullptr) {
      card->deleteLater();
    }
  }
  for (auto& card : recent_cards_) {
    if (card != nullptr) {
      card->deleteLater();
    }
  }
  open_cards_.clear();
  recent_cards_.clear();

  for (const auto& entry : documents) {
    const QString subtitle = QStringLiteral("%1 × %2").arg(entry.size.width()).arg(entry.size.height());
    const QString title = entry.modified ? entry.title + QStringLiteral(" •") : entry.title;
    auto* card = new StudioGalleryCard(title, subtitle, grid_);
    card->set_active(entry.active);
    card->set_placeholder_text(QStringLiteral("…"));
    const int tab_index = entry.tab_index;
    connect(card, &QAbstractButton::clicked, this, [this, tab_index] { shell_.activate_document(tab_index); });
    auto* close = card->add_close_button();
    close->setToolTip(tr("Close"));
    connect(close, &QAbstractButton::clicked, this, [this, tab_index] { shell_.close_document(tab_index); });
    QPointer<StudioGalleryCard> guard(card);
    const QString path = entry.path;
    shell_.render_document_thumbnail(tab_index, QSize(kCardWidth * 2, kThumbHeight * 2), [guard, path](QImage image) {
      if (guard != nullptr) {
        guard->set_image(image);
      }
      if (!path.isEmpty() && !image.isNull()) {
        const auto cache = studio_thumbnail_cache_path(path);
        QDir().mkpath(QFileInfo(cache).absolutePath());
        image.save(cache, "PNG");
      }
    });
    card->show();
    open_cards_.push_back(card);
  }
  for (const auto& path : recent) {
    const QFileInfo info(path);
    auto* card = new StudioGalleryCard(info.fileName(), QDir::toNativeSeparators(info.absolutePath()), grid_);
    card->set_placeholder_text(info.suffix().toUpper());
    connect(card, &QAbstractButton::clicked, this, [this, path] { shell_.open_recent(path); });
    load_recent_thumbnail(card, path);
    card->show();
    recent_cards_.push_back(card);
  }
  relayout_cards();
}

void StudioGallery::load_recent_thumbnail(StudioGalleryCard* card, const QString& path) {
  const auto cache = studio_thumbnail_cache_path(path);
  if (QFileInfo::exists(cache)) {
    card->set_image(QImage(cache));
    return;
  }
  const auto suffix = QFileInfo(path).suffix().toLower();
  static const QStringList kDirect = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                      QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
                                      QStringLiteral("tif"), QStringLiteral("tiff")};
  if (!kDirect.contains(suffix) || !QFileInfo::exists(path)) {
    return;
  }
  QPointer<StudioGalleryCard> guard(card);
  run_tracked_background_worker([guard, path] {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto full = reader.size();
    if (full.isValid()) {
      reader.setScaledSize(full.scaled(QSize(kCardWidth * 2, kThumbHeight * 2), Qt::KeepAspectRatio));
    }
    auto image = reader.read();
    QMetaObject::invokeMethod(
        qApp,
        [guard, image = std::move(image)] {
          if (guard != nullptr && !image.isNull()) {
            guard->set_image(image);
          }
        },
        Qt::QueuedConnection);
  });
}

void StudioGallery::relayout_cards() {
  if (grid_ == nullptr || scroll_ == nullptr) {
    return;
  }
  const int available = std::max(kCardWidth, scroll_->viewport()->width() - 2 * kSideMargin);
  const int columns = std::max(1, (available + kGap) / (kCardWidth + kGap));
  int y = 8;
  const auto place_section = [&](QLabel* label, const std::vector<QPointer<StudioGalleryCard>>& cards) {
    if (cards.empty()) {
      label->hide();
      return;
    }
    label->adjustSize();
    label->move(kSideMargin, y);
    label->show();
    y += label->height() + 12;
    int index = 0;
    for (const auto& card : cards) {
      if (card == nullptr) {
        continue;
      }
      const int column = index % columns;
      const int row = index / columns;
      card->setGeometry(kSideMargin + column * (kCardWidth + kGap), y + row * (kCardHeight + kGap), kCardWidth,
                        kCardHeight);
      ++index;
    }
    const int rows = (index + columns - 1) / columns;
    y += rows * (kCardHeight + kGap) + 12;
  };
  place_section(open_label_, open_cards_);
  place_section(recent_label_, recent_cards_);
  if (open_cards_.empty() && recent_cards_.empty()) {
    empty_label_->adjustSize();
    empty_label_->move(kSideMargin, y + 24);
    empty_label_->show();
    y += empty_label_->height() + 48;
  } else {
    empty_label_->hide();
  }
  grid_->resize(scroll_->viewport()->width(), y + kGap);
}

void StudioGallery::show_new_canvas_menu() {
  static const CanvasPreset kPresets[] = {
      {QT_TR_NOOP("Square"), 2048, 2048, 132.0},
      {QT_TR_NOOP("4K"), 3840, 2160, 132.0},
      {QT_TR_NOOP("A4 (300 PPI)"), 2480, 3508, 300.0},
      {QT_TR_NOOP("Comic page"), 1988, 3075, 300.0},
      {QT_TR_NOOP("Sketch"), 1600, 1200, 132.0},
  };
  auto* content = new QWidget;
  content->setObjectName(QStringLiteral("studioPanel"));
  set_themed_style(*content, studio_panel_qss());
  auto* column = new QVBoxLayout(content);
  column->setContentsMargins(4, 4, 4, 4);
  column->setSpacing(4);
  auto* heading = new QLabel(tr("New canvas"), content);
  heading->setProperty("studioRole", QStringLiteral("title"));
  column->addWidget(heading);

  const auto add_row = [this, content, column](const QString& name, int width, int height, double ppi) {
    auto* button = new QPushButton(content);
    button->setText(name + QStringLiteral("    ") + tr("%1 × %2 px").arg(width).arg(height));
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QPushButton::clicked, this, [this, width, height, ppi] {
      new_canvas_popover_->hide();
      shell_.create_canvas(width, height, ppi);
    });
    column->addWidget(button);
  };
  QSize screen_size(1920, 1080);
  if (const auto* screen = this->screen(); screen != nullptr) {
    screen_size = screen->size() * screen->devicePixelRatio();
  }
  add_row(tr("Screen size"), screen_size.width(), screen_size.height(), 132.0);
  for (const auto& preset : kPresets) {
    add_row(tr(preset.name), preset.width, preset.height, preset.ppi);
  }
  auto* custom = new QPushButton(tr("Custom size..."), content);
  custom->setCursor(Qt::PointingHandCursor);
  connect(custom, &QPushButton::clicked, this, [this] {
    new_canvas_popover_->hide();
    shell_.create_canvas_with_dialog();
  });
  column->addWidget(custom);
  new_canvas_popover_->set_content(content);
  const QRect anchor(new_button_->mapTo(this, QPoint(0, 0)), new_button_->size());
  new_canvas_popover_->show_below(anchor, QSize(300, content->sizeHint().height()));
}

}  // namespace patchy::ui
