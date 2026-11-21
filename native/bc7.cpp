// BC7 (BC7_UNORM) block encoding for the installer's colour maps, loaded by pylib/texfile_accel.py via ctypes. A
// line-for-line port of pylib/bc7.py's encoder (integers only: modes 6, 1 and 3, the principal axis's line extremes,
// every p-bit choice, least-squares refits, the least error kept): the same bytes (tests/test_native_bc7.py compares
// them). bc7.py stays the reference; Python's floor division is floordiv() here (C++ truncates towards zero). Blocks
// are independent, so a level is split over threads by block rows (the bytes do not depend on the split).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace {

// bc7.py's constants (POWER, UNIT, REFITS, GOOD, CANDIDATES, SOLID_REACH, WEIGHTS, P2, A2)
constexpr int64_t POWER = 4, UNIT = 1024, REFITS = 2, GOOD = 16;
constexpr int CANDIDATES = 2, SOLID_REACH = 2;
constexpr int W2[4] = {0, 21, 43, 64};
constexpr int W3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr int W4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
constexpr uint16_t P2[64] = {
    0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80, 0xc800, 0xffec, 0xfe80, 0xe800, 0xffe8, 0xff00,
    0xfff0, 0xf000, 0xf710, 0x008e, 0x7100, 0x08ce, 0x008c, 0x7310, 0x3100, 0x8cce, 0x088c, 0x3110, 0x6666, 0x366c,
    0x17e8, 0x0ff0, 0x718e, 0x399c, 0xaaaa, 0xf0f0, 0x5a5a, 0x33cc, 0x3c3c, 0x55aa, 0x9696, 0xa55a, 0x73ce, 0x13c8,
    0x324c, 0x3bdc, 0x6996, 0xc33c, 0x9966, 0x0660, 0x0272, 0x04e4, 0x4e40, 0x2720, 0xc936, 0x936c, 0x39c6, 0x639c,
    0x9336, 0x9cc6, 0x817e, 0xe718, 0xccf0, 0x0fcc, 0x7744, 0xee22};
constexpr int A2[64] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2,  8, 2,  2, 8,
                        8,  15, 2,  8,  2,  2,  8,  8,  2,  2,  15, 15, 6,  8,  2,  8,  15, 15, 2, 8,  2, 2,
                        2,  15, 15, 6,  6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2, 15};

const int* weights(int ibits) { return ibits == 2 ? W2 : (ibits == 3 ? W3 : W4); }

int64_t floordiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int64_t round_div(int64_t a, int64_t b) { return floordiv(2 * a + b, 2 * b); }

int clamp255(int64_t v) { return v < 0 ? 0 : (v > 255 ? 255 : (int)v); }

int expand(int v, int bits) {
    v <<= 8 - bits;
    return v | (v >> bits);
}

// _QUANT: (colour bits 6 / 7, p-bit) -> 8-bit value -> code
struct Quant {
    uint8_t t[2][2][256];
    Quant() {
        for (int k = 0; k < 2; ++k) {
            const int cbits = 6 + k;
            for (int p = 0; p < 2; ++p)
                for (int v = 0; v < 256; ++v) {
                    int best = 1 << 30, code = 0;
                    for (int c = 0; c < (1 << cbits); ++c) {
                        int d = expand((c << 1) | p, cbits + 1) - v;
                        if (d < 0) d = -d;
                        if (d < best) { best = d; code = c; }
                    }
                    t[k][p][v] = (uint8_t)code;
                }
        }
    }
};
const Quant QUANT;

struct Kind {
    int cbits, how, ibits;  // how: 0 'one' (1, 1), 1 'shared' (0, 0) / (1, 1), 2 'each' all four
};
Kind kind_of(int mode) {
    if (mode == 6) return {7, 0, 4};
    if (mode == 1) return {6, 1, 3};
    return {7, 2, 2};
}

struct Fit {
    int64_t err;
    int q0[3], q1[3];
    int p0, p1;
    int idx[16];  // per member, in member order
};

using Px = int[16][3];

void line_ends(const Px& px, const int* members, int n, int* e0, int* e1) {
    // bc7.py's sum of (n p - s)(n p - s)^T over the members is n^2 sum(p p^T) - n s s^T: the same integers, one pass
    int32_t s32[3] = {0, 0, 0}, pp[6] = {0, 0, 0, 0, 0, 0}, mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
    for (int k = 0; k < n; ++k) {
        const int* q = px[members[k]];
        s32[0] += q[0];
        s32[1] += q[1];
        s32[2] += q[2];
        pp[0] += q[0] * q[0];
        pp[1] += q[0] * q[1];
        pp[2] += q[0] * q[2];
        pp[3] += q[1] * q[1];
        pp[4] += q[1] * q[2];
        pp[5] += q[2] * q[2];
        for (int c = 0; c < 3; ++c) {
            mn[c] = std::min(mn[c], q[c]);
            mx[c] = std::max(mx[c], q[c]);
        }
    }
    const int64_t s[3] = {s32[0], s32[1], s32[2]};
    static const int AT[3][3] = {{0, 1, 2}, {1, 3, 4}, {2, 4, 5}};
    int64_t cov[3][3];
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) cov[a][b] = (int64_t)n * n * pp[AT[a][b]] - n * s[a] * s[b];
    const int span[3] = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
    int axis = 0;
    for (int c = 1; c < 3; ++c)
        if (span[c] > span[axis]) axis = c;
    int64_t v[3] = {0, 0, 0};
    v[axis] = UNIT;
    for (int it = 0; it < POWER; ++it) {
        int64_t u[3];
        for (int a = 0; a < 3; ++a) u[a] = cov[a][0] * v[0] + cov[a][1] * v[1] + cov[a][2] * v[2];
        int64_t m = std::max({u[0] < 0 ? -u[0] : u[0], u[1] < 0 ? -u[1] : u[1], u[2] < 0 ? -u[2] : u[2]});
        if (m == 0) break;
        for (int a = 0; a < 3; ++a) v[a] = floordiv(u[a] * UNIT, m);
    }
    const int64_t vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    int64_t total = 0, lo = 0, hi = 0;
    for (int k = 0; k < n; ++k) {
        const int* p = px[members[k]];
        int64_t t = p[0] * v[0] + p[1] * v[1] + p[2] * v[2];
        total += t;
        if (k == 0 || t < lo) lo = t;
        if (k == 0 || t > hi) hi = t;
    }
    for (int c = 0; c < 3; ++c) {
        e0[c] = clamp255(round_div(s[c] * vv + (n * lo - total) * v[c], n * vv));
        e1[c] = clamp255(round_div(s[c] * vv + (n * hi - total) * v[c], n * vv));
    }
}

// The squared error of the nearest palette entry per member (the lower index of equals), the indices into `idx` when
// given. Stops once the error reaches `bound` (the partition ranking's cut: it can no longer place), returning it.
int64_t assign(const Px& px, const int* members, int n, const int* e0, const int* e1, int ibits, int* idx,
               int64_t bound = INT64_MAX) {
    const int* ws = weights(ibits);
    const int count = 1 << ibits;
    int pal[16][3];
    for (int j = 0; j < count; ++j)
        for (int c = 0; c < 3; ++c) pal[j][c] = ((64 - ws[j]) * e0[c] + ws[j] * e1[c] + 32) >> 6;
    int64_t err = 0;
    for (int k = 0; k < n; ++k) {
        const int* p = px[members[k]];
        int32_t best = INT32_MAX;  // at most 3 x 255^2
        int at = 0;
        for (int j = 0; j < count; ++j) {
            const int32_t d0 = p[0] - pal[j][0], d1 = p[1] - pal[j][1], d2 = p[2] - pal[j][2];
            const int32_t d = d0 * d0 + d1 * d1 + d2 * d2;
            if (d < best) {
                best = d;
                at = j;
                if (d == 0) break;  // nothing later is less
            }
        }
        err += best;
        if (idx) idx[k] = at;
        if (err >= bound) return err;
    }
    return err;
}

Fit quantize(const Px& px, const int* members, int n, const int* e0, const int* e1, int mode) {
    static const int PB[3][4][2] = {{{1, 1}}, {{0, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 0}, {1, 1}}};
    static const int PN[3] = {1, 2, 4};
    const Kind kd = kind_of(mode);
    Fit best{};
    bool have = false;
    for (int k = 0; k < PN[kd.how]; ++k) {
        Fit got{};
        got.p0 = PB[kd.how][k][0];
        got.p1 = PB[kd.how][k][1];
        int x0[3], x1[3];
        for (int c = 0; c < 3; ++c) {
            got.q0[c] = QUANT.t[kd.cbits - 6][got.p0][e0[c]];
            got.q1[c] = QUANT.t[kd.cbits - 6][got.p1][e1[c]];
            x0[c] = expand((got.q0[c] << 1) | got.p0, kd.cbits + 1);
            x1[c] = expand((got.q1[c] << 1) | got.p1, kd.cbits + 1);
        }
        got.err = assign(px, members, n, x0, x1, kd.ibits, got.idx);
        if (!have || got.err < best.err) {
            best = got;
            have = true;
        }
    }
    return best;
}

bool refit(const Px& px, const int* members, int n, const int* idx, int ibits, int* e0, int* e1) {
    const int* ws = weights(ibits);
    int64_t a = 0, b = 0, c = 0, x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
    for (int k = 0; k < n; ++k) {
        const int64_t w = ws[idx[k]];
        a += (64 - w) * (64 - w);
        b += (64 - w) * w;
        c += w * w;
        for (int ch = 0; ch < 3; ++ch) {
            x[ch] += (64 - w) * 64 * px[members[k]][ch];
            y[ch] += w * 64 * px[members[k]][ch];
        }
    }
    const int64_t det = a * c - b * b;
    if (det == 0) return false;
    for (int ch = 0; ch < 3; ++ch) {
        e0[ch] = clamp255(round_div(c * x[ch] - b * y[ch], det));
        e1[ch] = clamp255(round_div(a * y[ch] - b * x[ch], det));
    }
    return true;
}

// bc7._solid: a subset of one colour
Fit solid(const Px& px, const int* members, int n, int mode) {
    static const int PB[3][4][2] = {{{1, 1}}, {{0, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 0}, {1, 1}}};
    static const int PN[3] = {1, 2, 4};
    const Kind kd = kind_of(mode);
    const int* t = px[members[0]];
    const int top = (1 << kd.cbits) - 1;
    const int* ws = weights(kd.ibits);
    Fit best{};
    bool have = false;
    for (int k = 0; k < PN[kd.how]; ++k) {
        const int p0 = PB[kd.how][k][0], p1 = PB[kd.how][k][1];
        for (int j = 0; j < (1 << kd.ibits); ++j) {
            const int w = ws[j];
            Fit got{};
            got.p0 = p0;
            got.p1 = p1;
            for (int c = 0; c < 3; ++c) {
                const int b0 = QUANT.t[kd.cbits - 6][p0][t[c]], b1 = QUANT.t[kd.cbits - 6][p1][t[c]];
                int dbest = -1, c0 = 0, c1 = 0;
                for (int a0 = std::max(0, b0 - SOLID_REACH); a0 <= std::min(top, b0 + SOLID_REACH); ++a0) {
                    const int x0 = expand((a0 << 1) | p0, kd.cbits + 1);
                    for (int a1 = std::max(0, b1 - SOLID_REACH); a1 <= std::min(top, b1 + SOLID_REACH); ++a1) {
                        const int x1 = expand((a1 << 1) | p1, kd.cbits + 1);
                        int d = (((64 - w) * x0 + w * x1 + 32) >> 6) - t[c];
                        if (d < 0) d = -d;
                        if (dbest < 0 || d < dbest) { dbest = d; c0 = a0; c1 = a1; }
                    }
                }
                got.err += (int64_t)dbest * dbest;
                got.q0[c] = c0;
                got.q1[c] = c1;
            }
            got.err *= n;
            for (int m = 0; m < n; ++m) got.idx[m] = j;
            if (!have || got.err < best.err) {
                best = got;
                have = true;
            }
        }
    }
    return best;
}

Fit fit(const Px& px, const int* members, int n, int mode) {
    bool flat = true;
    for (int k = 1; k < n && flat; ++k)
        for (int c = 0; c < 3; ++c)
            if (px[members[k]][c] != px[members[0]][c]) flat = false;
    if (flat) return solid(px, members, n, mode);
    int e0[3], e1[3];
    line_ends(px, members, n, e0, e1);
    Fit best = quantize(px, members, n, e0, e1, mode);
    for (int it = 0; it < REFITS; ++it) {
        int r0[3], r1[3];
        if (!refit(px, members, n, best.idx, kind_of(mode).ibits, r0, r1)) {
            if (best.err) {
                int lo[3], hi[3];
                for (int c = 0; c < 3; ++c) {
                    lo[c] = clamp255(std::min(e0[c], e1[c]) - 1);
                    hi[c] = clamp255(std::max(e0[c], e1[c]) + 1);
                }
                Fit got = quantize(px, members, n, lo, hi, mode);
                if (got.err < best.err) best = got;
            }
            break;
        }
        Fit got = quantize(px, members, n, r0, r1, mode);
        if (got.err >= best.err) break;
        best = got;
    }
    return best;
}

struct Bits {
    uint64_t lo = 0, hi = 0;
    int n = 0;
    void put(uint64_t v, int bits) {
        for (int i = 0; i < bits; ++i, ++n) {
            uint64_t bit = (v >> i) & 1;
            if (n < 64) lo |= bit << n;
            else hi |= bit << (n - 64);
        }
    }
};

// bc7._pack for modes 6 (ns 1), 1 and 3 (ns 2)
void pack(int mode, int partition, const Fit* fits, uint8_t* out) {
    const int ns = mode == 6 ? 1 : 2;
    const Kind kd = kind_of(mode);
    const int ib = kd.ibits, top = 1 << (ib - 1);
    const int anchor[2] = {0, ns == 2 ? A2[partition] : 0};
    int idx[16] = {};
    int ends[4][3], pbits[4];
    for (int s = 0; s < ns; ++s) {
        Fit f = fits[s];
        int members[16], n = 0;
        for (int i = 0; i < 16; ++i)
            if ((ns == 1 ? 0 : (P2[partition] >> i) & 1) == s) members[n++] = i;
        for (int k = 0; k < n; ++k) idx[members[k]] = f.idx[k];
        if (idx[anchor[s]] & top) {
            for (int c = 0; c < 3; ++c) std::swap(f.q0[c], f.q1[c]);
            std::swap(f.p0, f.p1);
            for (int k = 0; k < n; ++k) idx[members[k]] = (1 << ib) - 1 - idx[members[k]];
        }
        std::memcpy(ends[2 * s], f.q0, sizeof f.q0);
        std::memcpy(ends[2 * s + 1], f.q1, sizeof f.q1);
        pbits[2 * s] = f.p0;
        pbits[2 * s + 1] = f.p1;
    }
    Bits b;
    b.put(1ull << mode, mode + 1);
    b.put((uint64_t)partition, mode == 6 ? 0 : 6);
    for (int c = 0; c < 3; ++c)
        for (int e = 0; e < 2 * ns; ++e) b.put((uint64_t)ends[e][c], kd.cbits);
    if (mode == 6)
        for (int e = 0; e < 2; ++e) b.put(127, 7);
    if (kd.how == 1) {
        for (int s = 0; s < ns; ++s) b.put((uint64_t)pbits[2 * s], 1);
    } else {
        for (int e = 0; e < 2 * ns; ++e) b.put((uint64_t)pbits[e], 1);
    }
    for (int i = 0; i < 16; ++i) {
        bool is_anchor = i == anchor[0] || (ns == 2 && i == anchor[1]);
        b.put((uint64_t)idx[i], is_anchor ? ib - 1 : ib);
    }
    std::memcpy(out, &b.lo, 8);
    std::memcpy(out + 8, &b.hi, 8);
}

void encode_block(const Px& px, uint8_t* out) {
    int all[16];
    for (int i = 0; i < 16; ++i) all[i] = i;
    Fit six = fit(px, all, 16, 6);
    int64_t best_err = six.err;
    int best_mode = 6, best_part = 0;
    Fit best_fits[2] = {six, Fit{}};
    if (six.err > GOOD) {
        // bc7.py scores all 64 and sorts by (score, partition); the first CANDIDATES of that are kept here as they
        // come (partitions in order: a later one of an equal score never displaces), and a partition's scoring stops
        // once it reaches the last kept score: the same partitions, in the same order.
        int64_t kept_score[CANDIDATES];
        int kept[CANDIDATES], nkept = 0;
        for (int p = 0; p < 64; ++p) {
            const int64_t bound = nkept == CANDIDATES ? kept_score[CANDIDATES - 1] : INT64_MAX;
            int64_t score = 0;
            for (int s = 0; s < 2 && score < bound; ++s) {
                int members[16], n = 0;
                for (int i = 0; i < 16; ++i)
                    if (((P2[p] >> i) & 1) == s) members[n++] = i;
                int e0[3], e1[3];
                line_ends(px, members, n, e0, e1);
                score += assign(px, members, n, e0, e1, 3, nullptr, bound == INT64_MAX ? INT64_MAX : bound - score);
            }
            if (score >= bound) continue;
            int at = nkept < CANDIDATES ? nkept++ : CANDIDATES - 1;
            while (at > 0 && kept_score[at - 1] > score) {
                kept_score[at] = kept_score[at - 1];
                kept[at] = kept[at - 1];
                --at;
            }
            kept_score[at] = score;
            kept[at] = p;
        }
        for (int r = 0; r < nkept; ++r) {
            const int p = kept[r];
            int groups[2][16], counts[2] = {0, 0};
            for (int i = 0; i < 16; ++i) {
                int s = (P2[p] >> i) & 1;
                groups[s][counts[s]++] = i;
            }
            for (int mode : {1, 3}) {
                Fit f0 = fit(px, groups[0], counts[0], mode);
                Fit f1 = fit(px, groups[1], counts[1], mode);
                if (f0.err + f1.err < best_err) {
                    best_err = f0.err + f1.err;
                    best_mode = mode;
                    best_part = p;
                    best_fits[0] = f0;
                    best_fits[1] = f1;
                }
            }
        }
    }
    pack(best_mode, best_part, best_fits, out);
}

void encode_rows(const uint8_t* rgba, int width, int height, int row0, int row1, uint8_t* out) {
    const int bw = (width + 3) / 4;
    Px px;
    for (int by = row0; by < row1; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            for (int y = 0; y < 4; ++y) {
                int row = by * 4 + y < height ? by * 4 + y : height - 1;
                for (int x = 0; x < 4; ++x) {
                    int col = bx * 4 + x < width ? bx * 4 + x : width - 1;
                    const uint8_t* p = rgba + ((size_t)row * width + col) * 4;
                    px[y * 4 + x][0] = p[0];
                    px[y * 4 + x][1] = p[1];
                    px[y * 4 + x][2] = p[2];
                }
            }
            encode_block(px, out + ((size_t)by * bw + bx) * 16);
        }
}

}  // namespace

// One mip level: `rgba` (width x height, 4 bytes a pixel, rows top to bottom; alpha ignored, the blocks decode to
// alpha 255) into its BC7 blocks in `out` (16 bytes per 4 x 4 block, blocks row by row, edge pixels repeated past the
// image: bc7.encode_level). `threads` <= 0: one per hardware thread. Returns the bytes written:
// ceil(width / 4) x ceil(height / 4) x 16.
extern "C" __declspec(dllexport) size_t edf6_bc7_level(const uint8_t* rgba, int width, int height, uint8_t* out,
                                                       int threads) {
    if (width <= 0 || height <= 0) return 0;
    const int bw = (width + 3) / 4, bh = (height + 3) / 4;
    int n = threads > 0 ? threads : (int)std::thread::hardware_concurrency();
    n = std::max(1, std::min(n, bh));
    if (n == 1) {
        encode_rows(rgba, width, height, 0, bh, out);
    } else {
        std::vector<std::thread> pool;
        for (int k = 0; k < n; ++k) {
            int r0 = (int)((int64_t)bh * k / n), r1 = (int)((int64_t)bh * (k + 1) / n);
            pool.emplace_back(encode_rows, rgba, width, height, r0, r1, out);
        }
        for (auto& t : pool) t.join();
    }
    return (size_t)bw * bh * 16;
}
