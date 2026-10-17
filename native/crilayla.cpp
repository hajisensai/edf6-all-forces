// CRILAYLA (the LZ of CRI's compressed CPK entries) for the installer, loaded by pylib/crilayla.py via ctypes.
// A port of crilayla.decompress_py: the same bit reader (backwards from the stream's end), the same back references
// (copied byte by byte, which is what the Python's doubling slices amount to), the same 0x100 byte verbatim prefix.
// It refuses (returns 0) every input on which the Python would leave the buffer's bounds (a cut or corrupt entry:
// Python's negative indexes wrap and its slices clip), so the caller runs the Python reference there and gets its
// bytes or its error. tests/test_native_crilayla.py compares both.
#include <cstddef>
#include <cstdint>
#include <cstring>

// `data` is the whole entry (`n` bytes, 'CRILAYLA' header included); `out` holds `size` + 0x100 bytes, `size` the
// header's. Returns 1 with `out` filled, 0 when the input is one the caller must hand to the Python reference.
extern "C" __declspec(dllexport) int edf6_crilayla_decompress(const uint8_t* data, size_t n, uint8_t* out,
                                                              size_t size) {
    if (n < 0x10) return 0;
    uint32_t hsize = 0, compressed = 0;
    std::memcpy(&hsize, data + 8, 4);
    std::memcpy(&compressed, data + 12, 4);
    if (hsize != size || compressed == 0) return 0;
    const size_t prefix = 0x10 + size_t(compressed);
    if (prefix + 0x100 > n) return 0;   // a short prefix would shrink the Python's buffer
    std::memcpy(out, data + prefix, 0x100);
    ptrdiff_t position = ptrdiff_t(prefix) - 1;
    uint64_t pool = 0;
    int left = 0;
    bool bad = false;
    auto bits = [&](int count) -> unsigned {
        while (left < count) {
            if (position < 0) {   // the Python reads data[-1] here
                bad = true;
                return 0;
            }
            pool = (pool << 8) | data[position--];
            left += 8;
        }
        left -= count;
        const unsigned value = unsigned(pool >> left);
        pool &= (uint64_t(1) << left) - 1;
        return value;
    };
    static const int widths[4] = {2, 3, 5, 8};
    const size_t end = 0x100 + size - 1;
    const size_t total = 0x100 + size;
    size_t written = 0;
    while (written < size) {
        const unsigned flag = bits(1);
        if (bad) return 0;
        if (flag) {
            const size_t offset = bits(13);
            size_t length = 3;
            bool more = true;
            for (int w : widths) {
                const unsigned level = bits(w);
                length += level;
                if (level != (1u << w) - 1) {
                    more = false;
                    break;
                }
            }
            if (more) {
                for (;;) {
                    const unsigned level = bits(8);
                    length += level;
                    if (bad || level != 255) break;
                }
            }
            if (bad) return 0;
            const size_t dest = end - written;
            const size_t source = dest + offset + 3;
            // the Python's slices clip past the buffer's end and go negative past its start: refuse both
            if (source >= total || length > size - written) return 0;
            for (size_t k = 0; k < length; ++k) out[dest - k] = out[source - k];
            written += length;
        } else {
            const unsigned value = bits(8);
            if (bad) return 0;
            out[end - written] = uint8_t(value);
            ++written;
        }
    }
    return 1;
}
