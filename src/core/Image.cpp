#include "core/Image.h"

#include <algorithm>
#include <cmath>

#include "core/FileSystem.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace it {

bool loadImage(const std::filesystem::path& path, ImageRGBA8& out) {
    auto data = fs::readBinaryFile(path);
    if (!data || data->empty()) return false;
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(data->data(), int(data->size()), &w, &h, &comp, 4);
    if (!px) return false;
    out.width = uint32_t(w);
    out.height = uint32_t(h);
    out.pixels.assign(px, px + size_t(w) * h * 4);
    stbi_image_free(px);
    return true;
}

namespace {
float toLinear(uint8_t c) {
    float v = c / 255.0f;
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
uint8_t toSrgb(float v) {
    v = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return uint8_t(std::lround(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255.0f));
}
}  // namespace

ImageRGBA8 downsample(const ImageRGBA8& img, bool srgb) {
    ImageRGBA8 out;
    out.width = std::max(1u, img.width / 2);
    out.height = std::max(1u, img.height / 2);
    out.pixels.resize(size_t(out.width) * out.height * 4);
    static float lut[256];
    static bool lutReady = false;
    if (!lutReady) {
        for (int i = 0; i < 256; ++i) lut[i] = toLinear(uint8_t(i));
        lutReady = true;
    }
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x)
            for (int c = 0; c < 4; ++c) {
                float s = 0.0f;
                for (uint32_t dy = 0; dy < 2; ++dy)
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        uint32_t sx = std::min(img.width - 1, x * 2 + dx), sy = std::min(img.height - 1, y * 2 + dy);
                        uint8_t v = img.pixels[(size_t(sy) * img.width + sx) * 4 + c];
                        s += (srgb && c < 3) ? lut[v] : float(v);
                    }
                s *= 0.25f;
                out.pixels[(size_t(y) * out.width + x) * 4 + c] = (srgb && c < 3) ? toSrgb(s) : uint8_t(std::lround(s));
            }
    return out;
}

ImageRGBA8 crop(const ImageRGBA8& img, uint32_t y0, uint32_t h) {
    ImageRGBA8 out;
    if (y0 >= img.height) return out;
    h = std::min(h, img.height - y0);
    out.width = img.width;
    out.height = h;
    out.pixels.assign(img.pixels.begin() + ptrdiff_t(size_t(y0) * img.width * 4), img.pixels.begin() + ptrdiff_t(size_t(y0 + h) * img.width * 4));
    return out;
}

}  // namespace it
