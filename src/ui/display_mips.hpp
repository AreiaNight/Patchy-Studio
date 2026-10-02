#pragma once

#include <QImage>
#include <QRect>

namespace patchy::ui {

// The canvas's zoomed-out display mips: each level halves the previous one
// ((w + 1) / 2 x (h + 1) / 2) with an exact 2x2 box filter. A destination
// pixel depends only on its own source block (edge blocks average the one or
// two pixels that exist), so a dirty rect can be re-derived level by level and
// the result is byte-identical to halving the whole image again. Straight
// alpha formats average in premultiplied space so transparent pixels do not
// bleed their color into the mip. Integer math only: deterministic everywhere.

// One full halving. Formats other than RGBA8888 and RGBA8888_Premultiplied go
// through RGBA8888_Premultiplied and come back in the source format.
[[nodiscard]] QImage halve_display_image(const QImage& source);

// Successive halvings down to `level` (0 returns the image unchanged). Every
// mip chain and every patch drawn over one goes through this helper, so their
// pixels agree at mip-grid-aligned rects.
[[nodiscard]] QImage display_image_at_mip_level(QImage image, int level);

// Re-derives `half` (a halving of `source`) over the destination blocks that
// `source_rect` touches, and returns that destination rect. Both images must
// share a supported format (RGBA8888 or RGBA8888_Premultiplied) and `half`
// must be the size halve_display_image would produce; otherwise nothing
// happens and an empty rect comes back so the caller can rebuild instead.
QRect update_halved_display_region(const QImage& source, QImage& half, QRect source_rect);

}  // namespace patchy::ui
