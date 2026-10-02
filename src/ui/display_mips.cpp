#include "ui/display_mips.hpp"

#include <algorithm>
#include <cstdint>

namespace patchy::ui {
namespace {

bool supported_format(QImage::Format format) noexcept {
  return format == QImage::Format_RGBA8888 || format == QImage::Format_RGBA8888_Premultiplied;
}

QSize halved_size(QSize size) noexcept {
  return QSize(std::max(1, (size.width() + 1) / 2), std::max(1, (size.height() + 1) / 2));
}

// Writes destination rows [dst_rect] of `half` from `source`. Both images are
// RGBA8888 or RGBA8888_Premultiplied (bytes R, G, B, A) in the same format.
void halve_rows(const QImage& source, QImage& half, QRect dst_rect) {
  const bool premultiplied = source.format() == QImage::Format_RGBA8888_Premultiplied;
  const int src_w = source.width();
  const int src_h = source.height();
  for (int dy = dst_rect.top(); dy <= dst_rect.bottom(); ++dy) {
    const int sy0 = dy * 2;
    const int sy1 = std::min(sy0 + 1, src_h - 1);
    const int rows = sy1 == sy0 ? 1 : 2;
    const auto* row0 = source.constScanLine(sy0);
    const auto* row1 = source.constScanLine(sy1);
    auto* out = half.scanLine(dy);
    for (int dx = dst_rect.left(); dx <= dst_rect.right(); ++dx) {
      const int sx0 = dx * 2;
      const int sx1 = std::min(sx0 + 1, src_w - 1);
      const int cols = sx1 == sx0 ? 1 : 2;
      const std::uint8_t* samples[4] = {row0 + sx0 * 4, row0 + sx1 * 4, row1 + sx0 * 4, row1 + sx1 * 4};
      const int count = rows * cols;
      // Visit only the distinct pixels of an edge block.
      const int indices_full[4] = {0, 1, 2, 3};
      const int indices_col[2] = {0, 2};
      const int indices_row[2] = {0, 1};
      const int indices_one[1] = {0};
      const int* indices = count == 4 ? indices_full : cols == 1 && rows == 2 ? indices_col
                                                     : rows == 1 && cols == 2 ? indices_row
                                                                              : indices_one;
      auto* dst = out + dx * 4;
      if (premultiplied) {
        std::uint32_t sum[4] = {0, 0, 0, 0};
        for (int i = 0; i < count; ++i) {
          const auto* p = samples[indices[i]];
          sum[0] += p[0];
          sum[1] += p[1];
          sum[2] += p[2];
          sum[3] += p[3];
        }
        const auto half_count = static_cast<std::uint32_t>(count / 2);
        for (int c = 0; c < 4; ++c) {
          dst[c] = static_cast<std::uint8_t>((sum[c] + half_count) / static_cast<std::uint32_t>(count));
        }
        continue;
      }
      std::uint32_t alpha_sum = 0;
      std::uint32_t weighted[3] = {0, 0, 0};
      for (int i = 0; i < count; ++i) {
        const auto* p = samples[indices[i]];
        const std::uint32_t a = p[3];
        alpha_sum += a;
        weighted[0] += p[0] * a;
        weighted[1] += p[1] * a;
        weighted[2] += p[2] * a;
      }
      const auto half_count = static_cast<std::uint32_t>(count / 2);
      dst[3] = static_cast<std::uint8_t>((alpha_sum + half_count) / static_cast<std::uint32_t>(count));
      if (alpha_sum == 0) {
        dst[0] = dst[1] = dst[2] = 0;
        continue;
      }
      const auto half_alpha = alpha_sum / 2;
      for (int c = 0; c < 3; ++c) {
        dst[c] = static_cast<std::uint8_t>((weighted[c] + half_alpha) / alpha_sum);
      }
    }
  }
}

}  // namespace

QImage halve_display_image(const QImage& source) {
  if (source.isNull()) {
    return source;
  }
  const auto target = halved_size(source.size());
  if (target == source.size()) {
    return source;
  }
  if (!supported_format(source.format())) {
    const auto converted = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    return halve_display_image(converted).convertToFormat(source.format());
  }
  QImage half(target, source.format());
  if (half.isNull()) {
    return half;
  }
  halve_rows(source, half, half.rect());
  return half;
}

QImage display_image_at_mip_level(QImage image, int level) {
  for (int i = 0; i < level && !image.isNull(); ++i) {
    auto next = halve_display_image(image);
    if (next.size() == image.size()) {
      break;
    }
    image = std::move(next);
  }
  return image;
}

QRect update_halved_display_region(const QImage& source, QImage& half, QRect source_rect) {
  if (source.isNull() || half.isNull() || !supported_format(source.format()) ||
      half.format() != source.format() || half.size() != halved_size(source.size())) {
    return {};
  }
  source_rect = source_rect.intersected(source.rect());
  if (source_rect.isEmpty()) {
    return {};
  }
  const QRect dst_rect(QPoint(source_rect.left() / 2, source_rect.top() / 2),
                       QPoint(source_rect.right() / 2, source_rect.bottom() / 2));
  const auto clipped = dst_rect.intersected(half.rect());
  if (clipped.isEmpty()) {
    return {};
  }
  halve_rows(source, half, clipped);
  return clipped;
}

}  // namespace patchy::ui
