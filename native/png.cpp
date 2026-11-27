// PNG scanline filters undone for the installer's and the tests' texture reads, loaded by pylib/texfile.py via ctypes.
// A line-for-line port of texfile._unfilter_py (filters 0..4, Paeth as the PNG specification writes it): the same
// bytes (tests/test_native_helpers.py compares the two on every filter, pixel size and the repository's own PNGs).
// The pure Python one decoded the Dreadnought's 4096 x 4096 maps at about a second a megapixel (2026-11-27:
// 57 s of the menu preview test's 157 s).
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

}  // namespace

// `raw`: `height` lines of a filter byte and width * bpp bytes (n of them: at least height * (stride + 1), or -1).
// Returns 0, or 1 + the first unknown filter type (texfile's TextureError: PNG: unknown filter N).
extern "C" __declspec(dllexport) int edf6_png_unfilter(const uint8_t* raw, size_t n, int width, int height, int bpp,
                                                       uint8_t* out) {
    if (width < 0 || height < 0 || bpp < 1) return -1;
    const size_t stride = static_cast<size_t>(width) * static_cast<size_t>(bpp);
    if (n < static_cast<size_t>(height) * (stride + 1)) return -1;
    const size_t step = static_cast<size_t>(bpp);
    for (size_t y = 0; y < static_cast<size_t>(height); ++y) {
        const uint8_t* src = raw + y * (stride + 1);
        const int ft = src[0];
        ++src;
        uint8_t* line = out + y * stride;
        const uint8_t* prev = y ? line - stride : nullptr;   // the line above, already undone (zeros above the first)
        switch (ft) {
        case 0:
            for (size_t i = 0; i < stride; ++i) line[i] = src[i];
            break;
        case 1:
            for (size_t i = 0; i < stride; ++i)
                line[i] = static_cast<uint8_t>(src[i] + (i >= step ? line[i - step] : 0));
            break;
        case 2:
            for (size_t i = 0; i < stride; ++i) line[i] = static_cast<uint8_t>(src[i] + (prev ? prev[i] : 0));
            break;
        case 3:
            for (size_t i = 0; i < stride; ++i) {
                const int left = i >= step ? line[i - step] : 0, up = prev ? prev[i] : 0;
                line[i] = static_cast<uint8_t>(src[i] + ((left + up) >> 1));
            }
            break;
        case 4:
            for (size_t i = 0; i < stride; ++i) {
                const int left = i >= step ? line[i - step] : 0, up = prev ? prev[i] : 0;
                const int up_left = (prev && i >= step) ? prev[i - step] : 0;
                line[i] = static_cast<uint8_t>(src[i] + paeth(left, up, up_left));
            }
            break;
        default:
            return 1 + ft;
        }
    }
    return 0;
}
