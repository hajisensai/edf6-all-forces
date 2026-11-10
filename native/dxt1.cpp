// DXT1 block encoding for the installer's textures, loaded by pylib/texfile_accel.py via ctypes. A line-for-line port
// of texfile._dxt1_block (integers only: the range fit, the principal axis's extremes, least-squares refits, the least
// error kept): the same bytes (tests/test_native_dxt1.py compares it with texfile's pure Python encoder). texfile stays
// the reference, texfile_accel's numpy version the fallback when this DLL is not built (a dev checkout without
// build.cmd). Python's floor division is floordiv() here (C++ truncates towards zero).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

// texfile's constants (DXT1_GOOD, DXT1_POWER, DXT1_UNIT, DXT1_REFITS, _DXT1_WEIGHT, _DXT1_INDEX)
constexpr int64_t GOOD = 1296, POWER = 4, UNIT = 1024, REFITS = 2;
constexpr int WEIGHT[4] = {3, 0, 2, 1};
constexpr int INDEX[4] = {1, 3, 2, 0};

int64_t floordiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int rgb565(const int* p) {
    return ((p[0] * 31 + 127) / 255) << 11 | ((p[1] * 63 + 127) / 255) << 5 | ((p[2] * 31 + 127) / 255);
}

void rgb888(int c, int* e) {
    int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    e[0] = (r << 3) | (r >> 2);
    e[1] = (g << 2) | (g >> 4);
    e[2] = (b << 3) | (b >> 2);
}

struct Fit {
    int64_t err;
    int c0, c1;
    uint32_t idx;
};

Fit fit(const int (*px)[3], int c0, int c1) {
    if (c0 < c1) { int t = c0; c0 = c1; c1 = t; }
    int e0[3], e1[3];
    rgb888(c0, e0);
    rgb888(c1, e1);
    Fit out{0, c0, c1, 0};
    if (c0 == c1) {
        for (int i = 0; i < 16; ++i)
            for (int c = 0; c < 3; ++c) {
                int64_t d = 3 * px[i][c] - 3 * e0[c];
                out.err += d * d;
            }
        return out;
    }
    int d[3] = {e0[0] - e1[0], e0[1] - e1[1], e0[2] - e1[2]};
    int64_t dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    for (int i = 0; i < 16; ++i) {
        int64_t s = 6 * ((int64_t)(px[i][0] - e1[0]) * d[0] + (int64_t)(px[i][1] - e1[1]) * d[1] +
                         (int64_t)(px[i][2] - e1[2]) * d[2]);
        int w = s < dd ? 0 : (s < 3 * dd ? 1 : (s < 5 * dd ? 2 : 3));
        for (int c = 0; c < 3; ++c) {
            int64_t r = 3 * px[i][c] - w * e0[c] - (3 - w) * e1[c];
            out.err += r * r;
        }
        out.idx |= (uint32_t)INDEX[w] << (2 * i);
    }
    return out;
}

bool refit(const int (*px)[3], uint32_t idx, int* ends) {
    int64_t xx = 0, xy = 0, yy = 0, xp[3] = {0, 0, 0}, yp[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) {
        int64_t x = WEIGHT[(idx >> (2 * i)) & 3], y = 3 - x;
        xx += x * x;
        xy += x * y;
        yy += y * y;
        for (int c = 0; c < 3; ++c) {
            xp[c] += x * px[i][c];
            yp[c] += y * px[i][c];
        }
    }
    int64_t det = xx * yy - xy * xy;
    if (det == 0) return false;
    const int64_t rows[2][2] = {{yy, -xy}, {-xy, xx}};
    const int64_t top[3] = {31, 63, 31};
    for (int k = 0; k < 2; ++k) {
        int64_t q[3];
        for (int c = 0; c < 3; ++c) {
            int64_t num = 3 * (rows[k][0] * xp[c] + rows[k][1] * yp[c]) * top[c], den = det * 255;
            int64_t v = floordiv(2 * num + den, 2 * den);
            q[c] = v < 0 ? 0 : (v > top[c] ? top[c] : v);
        }
        ends[k] = (int)(q[0] << 11 | q[1] << 5 | q[2]);
    }
    return true;
}

Fit block(const int (*px)[3]) {
    int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c) {
            if (px[i][c] < lo[c]) lo[c] = px[i][c];
            if (px[i][c] > hi[c]) hi[c] = px[i][c];
        }
    int ax = 0;
    for (int c = 1; c < 3; ++c)
        if (hi[c] - lo[c] > hi[ax] - lo[ax]) ax = c;   // the first of equal spans (Python's max)
    int imax = 0, imin = 0;
    for (int i = 1; i < 16; ++i) {
        if (px[i][ax] > px[imax][ax]) imax = i;
        if (px[i][ax] < px[imin][ax]) imin = i;
    }
    Fit best = fit(px, rgb565(px[imax]), rgb565(px[imin]));
    if (best.err <= GOOD) return best;
    int64_t s[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c) s[c] += px[i][c];
    int64_t d[16][3];
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c) d[i][c] = 16 * px[i][c] - s[c];
    int64_t cov[3][3] = {};
    for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 3; ++k)
            for (int i = 0; i < 16; ++i) cov[j][k] += d[i][j] * d[i][k];
    int64_t v[3] = {0, 0, 0};
    v[ax] = UNIT;
    for (int it = 0; it < POWER; ++it) {
        int64_t w[3], m = 0;
        for (int j = 0; j < 3; ++j) {
            w[j] = cov[j][0] * v[0] + cov[j][1] * v[1] + cov[j][2] * v[2];
            int64_t a = w[j] < 0 ? -w[j] : w[j];
            if (a > m) m = a;
        }
        if (m == 0) break;
        for (int j = 0; j < 3; ++j) v[j] = floordiv(w[j] * UNIT, m);
    }
    int64_t t[16];
    for (int i = 0; i < 16; ++i) t[i] = d[i][0] * v[0] + d[i][1] * v[1] + d[i][2] * v[2];
    int thi = 0, tlo = 0;
    for (int i = 1; i < 16; ++i) {
        if (t[i] > t[thi]) thi = i;
        if (t[i] < t[tlo]) tlo = i;
    }
    Fit got = fit(px, rgb565(px[thi]), rgb565(px[tlo]));
    if (got.err < best.err) best = got;
    for (int r = 0; r < REFITS; ++r) {
        if (best.c0 == best.c1) break;
        int ends[2];
        if (!refit(px, best.idx, ends)) break;
        got = fit(px, ends[0], ends[1]);
        if (got.err >= best.err) break;
        best = got;
    }
    return best;
}

}  // namespace

// One mip level: `rgba` (width x height, 4 bytes a pixel, rows top to bottom) into its DXT1 blocks in `out` (8 bytes
// per 4 x 4 block, blocks row by row, edge pixels repeated past the image: texfile._dxt1_level). Returns the bytes
// written: ceil(width / 4) x ceil(height / 4) x 8.
extern "C" __declspec(dllexport) size_t edf6_dxt1_level(const uint8_t* rgba, int width, int height, uint8_t* out) {
    if (width <= 0 || height <= 0) return 0;
    const int bw = (width + 3) / 4, bh = (height + 3) / 4;
    size_t at = 0;
    int px[16][3];
    for (int by = 0; by < bh; ++by)
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
            Fit f = block(px);
            uint16_t c0 = (uint16_t)f.c0, c1 = (uint16_t)f.c1;
            std::memcpy(out + at, &c0, 2);
            std::memcpy(out + at + 2, &c1, 2);
            std::memcpy(out + at + 4, &f.idx, 4);
            at += 8;
        }
    return at;
}
