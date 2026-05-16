#include "tga_loader.h"

#include <fstream>
#include <string>

namespace Next {

namespace {

static uint16_t ReadU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

} // namespace

bool LoadTgaRGBA8(const std::string& path, ImageRGBA8& out, std::string& outError) {
    out = ImageRGBA8{};
    outError.clear();

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        outError = "Failed to open TGA: " + path;
        return false;
    }

    const std::streamsize fileSize = in.tellg();
    if (fileSize < 0) {
        outError = "Failed to determine TGA size: " + path;
        return false;
    }
    if (fileSize < 18) {
        outError = "TGA too small: " + path;
        return false;
    }
    in.seekg(0, std::ios::beg);
    if (!in) {
        outError = "Failed to seek TGA: " + path;
        return false;
    }

    std::vector<uint8_t> file(static_cast<size_t>(fileSize));
    if (!in.read(reinterpret_cast<char*>(file.data()), fileSize)) {
        outError = "Failed to read TGA: " + path;
        return false;
    }
    in.close();

    const uint8_t* h = file.data();
    const uint8_t idLength = h[0];
    const uint8_t colorMapType = h[1];
    const uint8_t imageType = h[2];
    const uint16_t colorMapLength = ReadU16LE(h + 5);
    const uint8_t colorMapDepth = h[7];

    const uint16_t width = ReadU16LE(h + 12);
    const uint16_t height = ReadU16LE(h + 14);
    const uint8_t pixelDepth = h[16];
    const uint8_t imageDescriptor = h[17];

    if (colorMapType != 0) {
        outError = "TGA colormap not supported: " + path;
        return false;
    }
    if (imageType != 2) {
        outError = "TGA imageType not supported (need 2): " + path;
        return false;
    }
    if (colorMapLength != 0 || colorMapDepth != 0) {
        outError = "TGA colormap fields unexpected: " + path;
        return false;
    }
    if (width == 0 || height == 0) {
        outError = "TGA invalid dimensions: " + path;
        return false;
    }
    if (pixelDepth != 24 && pixelDepth != 32) {
        outError = "TGA pixelDepth not supported (need 24 or 32): " + path;
        return false;
    }

    size_t offset = 18;
    offset += static_cast<size_t>(idLength);
    if (offset > file.size()) {
        outError = "TGA invalid ID length: " + path;
        return false;
    }

    const size_t srcBpp = (pixelDepth == 24) ? 3 : 4;
    const size_t srcSize = static_cast<size_t>(width) * static_cast<size_t>(height) * srcBpp;
    if (srcSize > file.size() - offset) {
        outError = "TGA truncated pixel data: " + path;
        return false;
    }

    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

    const bool originTop = (imageDescriptor & 0x20) != 0; // bit 5: top-left origin if set
    const uint8_t* src = file.data() + offset;

    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t srcY = originTop ? y : (height - 1 - y);
        for (uint32_t x = 0; x < width; ++x) {
            const size_t si = (static_cast<size_t>(srcY) * width + x) * srcBpp;
            const size_t di = (static_cast<size_t>(y) * width + x) * 4;

            const uint8_t b = src[si + 0];
            const uint8_t g = src[si + 1];
            const uint8_t r = src[si + 2];
            const uint8_t a = (srcBpp == 4) ? src[si + 3] : 255;

            out.pixels[di + 0] = r;
            out.pixels[di + 1] = g;
            out.pixels[di + 2] = b;
            out.pixels[di + 3] = a;
        }
    }

    return true;
}

} // namespace Next
