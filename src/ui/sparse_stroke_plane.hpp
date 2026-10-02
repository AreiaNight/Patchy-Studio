#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace patchy::ui {

// Per-pixel float state for one brush stroke (accumulated alpha, coverage caps),
// stored as lazily allocated 64x64 tiles. Replaces an unordered_map keyed by pixel:
// a 300 px stroke touches over a million pixels, and two hashed lookups per pixel
// per dab dominated stroke time. Consecutive lookups along a row hit the cached
// tile. Keeps the map's semantics exactly: a pixel is absent until first touched
// (find returns nullptr), then starts at 0.
class SparseStrokePlane {
public:
  // The value at (x, y), inserting 0 on first touch (the map's operator[]).
  float& at(std::int32_t x, std::int32_t y) {
    auto& value = tile_for(x, y, true)->values[index(x, y)];
    if (value < 0.0F) {
      value = 0.0F;
    }
    return value;
  }

  // nullptr while (x, y) has never been touched.
  [[nodiscard]] const float* find(std::int32_t x, std::int32_t y) const {
    const auto* tile = tile_for(x, y, false);
    if (tile == nullptr) {
      return nullptr;
    }
    const auto& value = tile->values[index(x, y)];
    return value < 0.0F ? nullptr : &value;
  }

  void clear() noexcept {
    tiles_.clear();
    cached_tile_ = nullptr;
  }

private:
  static constexpr int kShift = 6;
  static constexpr int kSize = 1 << kShift;
  static constexpr float kAbsent = -1.0F;  // stored values are always in [0, 1]

  struct Tile {
    Tile() { values.fill(kAbsent); }
    std::array<float, kSize * kSize> values;
  };

  static std::size_t index(std::int32_t x, std::int32_t y) noexcept {
    return static_cast<std::size_t>(y & (kSize - 1)) * kSize + static_cast<std::size_t>(x & (kSize - 1));
  }

  // C++20 right shifts of negative values are arithmetic, so this floors.
  Tile* tile_for(std::int32_t x, std::int32_t y, bool create) const {
    const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y >> kShift)) << 32U) |
                     static_cast<std::uint32_t>(x >> kShift);
    if (cached_tile_ != nullptr && key == cached_key_) {
      return cached_tile_;
    }
    auto found = tiles_.find(key);
    if (found == tiles_.end()) {
      if (!create) {
        return nullptr;
      }
      found = tiles_.emplace(key, std::make_unique<Tile>()).first;
    }
    cached_key_ = key;
    cached_tile_ = found->second.get();
    return cached_tile_;
  }

  mutable std::unordered_map<std::uint64_t, std::unique_ptr<Tile>> tiles_;
  mutable std::uint64_t cached_key_{0};  // meaningful only while cached_tile_ is set
  mutable Tile* cached_tile_{nullptr};
};

}  // namespace patchy::ui
