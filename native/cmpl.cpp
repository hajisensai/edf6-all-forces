// CMPL (Okumura LZSS, N=4096, F=18, THRESHOLD=2) for the installer's generators, loaded by pylib/mdb.py via ctypes.
// A line-for-line port of mdb.cmpl_compress / mdb.cmpl_decompress: the same binary-search-tree encoder, so the same
// bytes (tests/test_native_cmpl.py compares both on every input it builds). The pure Python versions stay the
// reference and the fallback when this DLL is not built (a dev checkout without build.cmd).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

constexpr int N = 4096, F = 18, TH = 2, NIL = N;

struct Encoder {
    uint8_t text[N + F - 1] = {};
    std::vector<int> lson = std::vector<int>(N + 257, NIL);
    std::vector<int> rson = std::vector<int>(N + 257, NIL);
    std::vector<int> dad = std::vector<int>(N + 257, NIL);
    int mpos = 0, mlen = 0;

    void insert(int r) {
        int cmp = 1;
        const int key = r;
        int p = N + 1 + text[key];
        rson[r] = lson[r] = NIL;
        mlen = 0;
        for (;;) {
            if (cmp >= 0) {
                if (rson[p] != NIL) {
                    p = rson[p];
                } else {
                    rson[p] = r;
                    dad[r] = p;
                    return;
                }
            } else {
                if (lson[p] != NIL) {
                    p = lson[p];
                } else {
                    lson[p] = r;
                    dad[r] = p;
                    return;
                }
            }
            int i = 1;
            for (; i < F; ++i) {
                cmp = int(text[key + i]) - int(text[p + i]);
                if (cmp != 0) break;
            }
            if (i > mlen) {
                mpos = p;
                mlen = i;
                if (mlen >= F) break;
            }
        }
        dad[r] = dad[p];
        lson[r] = lson[p];
        rson[r] = rson[p];
        dad[lson[p]] = r;
        dad[rson[p]] = r;
        if (rson[dad[p]] == p) rson[dad[p]] = r;
        else lson[dad[p]] = r;
        dad[p] = NIL;
    }

    void remove(int p) {
        if (dad[p] == NIL) return;
        int q;
        if (rson[p] == NIL) {
            q = lson[p];
        } else if (lson[p] == NIL) {
            q = rson[p];
        } else {
            q = lson[p];
            if (rson[q] != NIL) {
                while (rson[q] != NIL) q = rson[q];
                rson[dad[q]] = lson[q];
                dad[lson[q]] = dad[q];
                lson[q] = lson[p];
                dad[lson[p]] = q;
            }
            rson[q] = rson[p];
            dad[rson[p]] = q;
        }
        dad[q] = dad[p];
        if (rson[dad[p]] == p) rson[dad[p]] = q;
        else lson[dad[p]] = q;
        dad[p] = NIL;
    }
};

}  // namespace

// The worst case of the stream after the 8-byte header: every byte a literal, one flag byte per eight.
extern "C" __declspec(dllexport) size_t edf6_cmpl_bound(size_t n) {
    return 8 + n + (n + 7) / 8;
}

// `out` holds edf6_cmpl_bound(n) bytes; returns the stream's length (header included).
extern "C" __declspec(dllexport) size_t edf6_cmpl_compress(const uint8_t* data, size_t n, uint8_t* out) {
    size_t o = 0;
    out[o++] = 'C'; out[o++] = 'M'; out[o++] = 'P'; out[o++] = 'L';
    const uint32_t size = uint32_t(n);
    out[o++] = uint8_t(size >> 24); out[o++] = uint8_t(size >> 16); out[o++] = uint8_t(size >> 8); out[o++] = uint8_t(size);
    if (n == 0) return o;
    Encoder* e = new Encoder();
    uint8_t code[17] = {};
    int cptr = 1;
    unsigned mask = 1;
    int s = 0, r = N - F;
    size_t src = 0;
    int length = 0;
    while (length < F && src < n) e->text[r + length++] = data[src++];
    for (int i = 1; i <= F; ++i) e->insert(r - i);
    e->insert(r);
    for (;;) {
        if (e->mlen > length) e->mlen = length;
        if (e->mlen <= TH) {
            e->mlen = 1;
            code[0] |= uint8_t(mask);
            code[cptr++] = e->text[r];
        } else {
            code[cptr] = uint8_t((e->mpos >> 4) & 0xFF);   // the game stores the position high byte first
            code[cptr + 1] = uint8_t(((e->mpos & 0xF) << 4) | (e->mlen - (TH + 1)));
            cptr += 2;
        }
        mask = (mask << 1) & 0xFF;
        if (mask == 0) {
            for (int k = 0; k < cptr; ++k) out[o++] = code[k];
            code[0] = 0;
            cptr = 1;
            mask = 1;
        }
        const int last = e->mlen;
        int i = 0;
        while (i < last && src < n) {
            const uint8_t c = data[src++];
            e->remove(s);
            e->text[s] = c;
            if (s < F - 1) e->text[s + N] = c;
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            e->insert(r);
            ++i;
        }
        while (i < last) {
            ++i;
            e->remove(s);
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            if (--length) e->insert(r);
        }
        if (length <= 0) break;
    }
    if (cptr > 1)
        for (int k = 0; k < cptr; ++k) out[o++] = code[k];
    delete e;
    return o;
}

// The stream after the 8-byte header into `out` (`size` bytes, the header's); returns how many bytes it gave, less
// than `size` when the stream ends first (the caller says so as mdb.cmpl_decompress does).
extern "C" __declspec(dllexport) size_t edf6_cmpl_decompress(const uint8_t* data, size_t n, uint8_t* out, size_t size) {
    uint8_t win[N] = {};
    unsigned r = 0xFEE;
    size_t produced = 0, i = 0;
    unsigned flags = 0;
    while (produced < size && i < n) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            flags = data[i++] | 0xFF00u;
        }
        if (flags & 1) {
            if (i >= n) break;
            const uint8_t c = data[i++];
            out[produced++] = c;
            win[r] = c;
            r = (r + 1) & 0xFFF;
            continue;
        }
        if (i + 2 > n) break;
        const unsigned b0 = data[i], b1 = data[i + 1];
        i += 2;
        const unsigned off = (b0 << 4) | (b1 >> 4);
        for (unsigned k = 0; k < (b1 & 0xF) + 3u; ++k) {
            const uint8_t c = win[(off + k) & 0xFFF];
            if (produced < size) out[produced] = c;   // past `size` the Python version trims what it appended
            ++produced;
            win[r] = c;
            r = (r + 1) & 0xFFF;
        }
    }
    return produced < size ? produced : size;
}
