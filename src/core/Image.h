#pragma once
// Image files (PNG and the other formats stb_image reads) as 8-bit RGBA, plus box-filtered mip chains.
#include <cstdint>
#include <filesystem>
#include <vector>

namespace it {

struct ImageRGBA8 {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;  // width * height * 4, rows top to bottom
    bool empty() const { return pixels.empty(); }
};

bool loadImage(const std::filesystem::path& path, ImageRGBA8& out);
/// Half-size image (2x2 box filter). `srgb`: average colour channels in linear space (alpha stays linear).
ImageRGBA8 downsample(const ImageRGBA8& img, bool srgb);
/// Rows [y0, y0 + h) of an image.
ImageRGBA8 crop(const ImageRGBA8& img, uint32_t y0, uint32_t h);

}  // namespace it
