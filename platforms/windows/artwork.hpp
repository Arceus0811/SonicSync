#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sonicsync::windows {

struct ArtworkPixels {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> bgra;
};

using ArtworkPtr = std::shared_ptr<const ArtworkPixels>;

// Decodes supported Windows Imaging Component formats off the UI thread,
// center-crops to square album art and resizes to a bounded 512x512 BGRA image.
[[nodiscard]] ArtworkPtr decodeArtwork(const std::uint8_t* encoded, std::size_t size) noexcept;

} // namespace sonicsync::windows
