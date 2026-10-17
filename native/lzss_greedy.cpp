// fmb_build.cmpl_fast's encoder (greedy LZSS over the game's CMPL format, at most `tries` earlier occurrences of each
// 3 byte key looked at) for the installer, loaded by pylib/fmb_build.py via ctypes. A port of fmb_build._lzss_items +
// fmb._emit: the same items, so the same bytes. Python finds the occurrences with bytes.rfind (the nearest start
// before the current one, then the nearest before that ...); here each position's previous start of the same key is
// precomputed (a chain), which is the same sequence. tests/test_native_lzss.py compares both.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

constexpr ptrdiff_t WINDOW = 4095, MAX_MATCH = 18, MIN_MATCH = 3;

}  // namespace

// Worst case after the 8-byte header: every byte a literal, a flag byte per eight items.
extern "C" __declspec(dllexport) size_t edf6_lzss_greedy_bound(size_t n) {
    return 8 + n + (n + 7) / 8;
}

// `out` holds edf6_lzss_greedy_bound(n) bytes; returns the stream's length ('CMPL', big-endian size, then the groups).
extern "C" __declspec(dllexport) size_t edf6_lzss_greedy(const uint8_t* data, size_t n, int tries, uint8_t* out) {
    size_t o = 0;
    out[o++] = 'C'; out[o++] = 'M'; out[o++] = 'P'; out[o++] = 'L';
    const uint32_t size = uint32_t(n);
    out[o++] = uint8_t(size >> 24); out[o++] = uint8_t(size >> 16); out[o++] = uint8_t(size >> 8); out[o++] = uint8_t(size);
    const ptrdiff_t len = ptrdiff_t(n);
    // prev[i]: the largest j < i whose 3 bytes equal those at i (-1: none); positions that start a full key only
    std::vector<int32_t> prev(n >= 3 ? n - 2 : 0, -1);
    if (n >= 3) {
        // every 3 byte key's latest start; kept between calls (64 MB once) and put back to -1 where this call wrote
        thread_local std::vector<int32_t> head(size_t(1) << 24, -1);
        for (ptrdiff_t i = 0; i + 3 <= len; ++i) {
            const uint32_t key = uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8 | data[i + 2];
            prev[size_t(i)] = head[key];
            head[key] = int32_t(i);
        }
        for (ptrdiff_t i = 0; i + 3 <= len; ++i)
            head[uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8 | data[i + 2]] = -1;
    }
    size_t flag_at = 0;
    int bit = 8;
    ptrdiff_t pos = 0;
    while (pos < len) {
        if (bit == 8) {
            flag_at = o++;
            out[flag_at] = 0;
            bit = 0;
        }
        ptrdiff_t best_len = 0, best_src = -1;
        const ptrdiff_t want = std::min(MAX_MATCH, len - pos);
        if (want >= MIN_MATCH) {
            const ptrdiff_t lo = std::max<ptrdiff_t>(0, pos - WINDOW);
            ptrdiff_t s = prev[size_t(pos)];
            int left = tries;
            while (s >= lo && left) {
                ptrdiff_t ln = 3;
                while (ln < want && data[s + ln] == data[pos + ln]) ++ln;
                if (ln > best_len) {
                    best_len = ln;
                    best_src = s;
                    if (ln == want) break;
                }
                s = s > lo ? prev[size_t(s)] : -1;
                --left;
            }
        }
        if (best_len >= MIN_MATCH) {
            const unsigned off = unsigned(0xFEE + best_src) & 0xFFF;
            out[o++] = uint8_t(off >> 4);
            out[o++] = uint8_t(((off & 0xF) << 4) | unsigned(best_len - 3));
            pos += best_len;
        } else {
            out[flag_at] = uint8_t(out[flag_at] | (1u << bit));
            out[o++] = data[pos];
            pos += 1;
        }
        ++bit;
    }
    return o;
}
