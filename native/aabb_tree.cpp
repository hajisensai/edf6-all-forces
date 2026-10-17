// hkcms_build._section_tree (a compressed mesh section's Aabb4BytesCodec tree, depth first) and hkcms_build.simd_tree
// (an hkcdSimdTree's nodes) for the installer, loaded by pylib/hkcms_build.py via ctypes. Ports of them and of
// hkcms.encode_box / decode_box: the same float64 operations in the same order (numpy's and Python's floats are IEEE
// doubles; MSVC contracts nothing into FMA by default), the same stable median splits, the same float32 rounding, so
// the same bytes. tests/test_native_helpers.py compares both.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <numeric>
#include <vector>

namespace {

struct Box {
    double lo[3], hi[3];
};

// hkcms.encode_box(parent, box): the tightest code whose decoded box holds `box` grown by 1e-3
void encode(const Box& parent, const Box& raw, uint8_t xyz[3]) {
    const double margin = 1e-3;
    for (int k = 0; k < 3; ++k) {
        const double b0 = raw.lo[k] - margin, b1 = raw.hi[k] + margin;
        const double ext = parent.hi[k] - parent.lo[k];
        if (ext <= 0) {
            xyz[k] = 0;
            continue;
        }
        const double lo = std::max(0.0, (b0 - parent.lo[k]) / ext * 226.0);
        const double hi = std::max(0.0, (parent.hi[k] - b1) / ext * 226.0);
        const double fa = std::floor(std::sqrt(lo)), fc = std::floor(std::sqrt(hi));
        int a = fa >= 15.0 ? 15 : int(fa);
        int c = fc >= 15.0 ? 15 : int(fc);
        while (a && parent.lo[k] + double(a * a) / 226.0 * ext > b0) --a;
        while (c && parent.hi[k] - double(c * c) / 226.0 * ext < b1) --c;
        xyz[k] = uint8_t(a << 4 | c);
    }
}

// hkcms.decode_box(parent, xyz)
Box decode(const Box& parent, const uint8_t xyz[3]) {
    Box out{};
    for (int k = 0; k < 3; ++k) {
        const double ext = parent.hi[k] - parent.lo[k];
        const double a = double(xyz[k] >> 4), c = double(xyz[k] & 15);
        out.lo[k] = parent.lo[k] + a * a / 226.0 * ext;
        out.hi[k] = parent.hi[k] - c * c / 226.0 * ext;
    }
    return out;
}

struct Tree {
    const Box* boxes;
    std::vector<double> centres;   // (m, 3)
    std::vector<uint8_t> out;
    bool overflow = false;

    void rec(std::vector<int32_t> ids, const Box& parent) {
        Box box{};
        for (int k = 0; k < 3; ++k) {
            box.lo[k] = boxes[ids[0]].lo[k];
            box.hi[k] = boxes[ids[0]].hi[k];
        }
        for (size_t i = 1; i < ids.size(); ++i)
            for (int k = 0; k < 3; ++k) {
                box.lo[k] = std::min(box.lo[k], boxes[ids[i]].lo[k]);
                box.hi[k] = std::max(box.hi[k], boxes[ids[i]].hi[k]);
            }
        uint8_t xyz[3];
        encode(parent, box, xyz);
        const Box me = decode(parent, xyz);
        const size_t at = out.size();
        out.insert(out.end(), {xyz[0], xyz[1], xyz[2], 0});
        if (ids.size() == 1) {
            if (ids[0] > 127) overflow = true;   // the Python's bytearray refuses a byte over 255
            out[at + 3] = uint8_t(ids[0] << 1);
            return;
        }
        int axis = 0;   // numpy.argmax: the first of equal extents
        for (int k = 1; k < 3; ++k)
            if (box.hi[k] - box.lo[k] > box.hi[axis] - box.lo[axis]) axis = k;
        std::stable_sort(ids.begin(), ids.end(),
                         [&](int32_t x, int32_t y) { return centres[size_t(x) * 3 + axis] < centres[size_t(y) * 3 + axis]; });
        const size_t half = ids.size() / 2;
        rec(std::vector<int32_t>(ids.begin(), ids.begin() + ptrdiff_t(half)), me);
        if (half > 127) overflow = true;
        out[at + 3] = uint8_t((half << 1) | 1);   // the second child is 2 * first nodes on
        rec(std::vector<int32_t>(ids.begin() + ptrdiff_t(half), ids.end()), me);
    }
};

}  // namespace

// `boxes`: m boxes as (lo x, y, z, hi x, y, z) doubles, `domain` likewise; `out` holds 4 * (2m - 1) bytes. Returns 1,
// or 0 (nothing usable in `out`) for an input the caller must hand to the Python: m == 0, a NaN, or a
// byte over 255 (which the Python refuses).
extern "C" __declspec(dllexport) int edf6_section_tree(const double* boxes, size_t m, const double* domain,
                                                       uint8_t* out) {
    if (m == 0) return 0;
    for (size_t i = 0; i < m * 6; ++i)
        if (std::isnan(boxes[i])) return 0;   // numpy's min / max propagate a NaN, std::min / max do not
    for (int k = 0; k < 6; ++k)
        if (std::isnan(domain[k])) return 0;
    Tree t;
    t.boxes = reinterpret_cast<const Box*>(boxes);
    t.centres.resize(m * 3);
    for (size_t i = 0; i < m; ++i)
        for (int k = 0; k < 3; ++k) t.centres[i * 3 + k] = (t.boxes[i].lo[k] + t.boxes[i].hi[k]) * 0.5;
    std::vector<int32_t> ids(m);
    std::iota(ids.begin(), ids.end(), 0);
    Box dom{};
    for (int k = 0; k < 3; ++k) {
        dom.lo[k] = domain[k];
        dom.hi[k] = domain[3 + k];
    }
    t.out.reserve(4 * (2 * m - 1));
    t.rec(std::move(ids), dom);
    if (t.overflow || t.out.size() != 4 * (2 * m - 1)) return 0;
    std::copy(t.out.begin(), t.out.end(), out);
    return 1;
}

namespace {

float u2f(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

struct SimdNode {
    double lo[4][3], hi[4][3];
    uint32_t data[4];
    uint8_t leaf;
    std::vector<size_t> kids;   // indexes into the node store
    uint32_t id = 0;
};

struct Simd {
    const Box* boxes;
    std::vector<double> centres;
    std::vector<int32_t> order;

    void kd(std::vector<int32_t> ids) {
        if (ids.size() <= 4) {
            order.insert(order.end(), ids.begin(), ids.end());
            return;
        }
        double mx[3], mn[3];
        for (int k = 0; k < 3; ++k) {
            mx[k] = boxes[ids[0]].hi[k];
            mn[k] = boxes[ids[0]].lo[k];
        }
        for (size_t i = 1; i < ids.size(); ++i)
            for (int k = 0; k < 3; ++k) {
                mx[k] = std::max(mx[k], boxes[ids[i]].hi[k]);
                mn[k] = std::min(mn[k], boxes[ids[i]].lo[k]);
            }
        int axis = 0;
        for (int k = 1; k < 3; ++k)
            if (mx[k] - mn[k] > mx[axis] - mn[axis]) axis = k;
        std::stable_sort(ids.begin(), ids.end(),
                         [&](int32_t x, int32_t y) { return centres[size_t(x) * 3 + axis] < centres[size_t(y) * 3 + axis]; });
        const size_t cut = 4 * ((ids.size() + 7) / 8);   // a multiple of 4: the left part fills whole leaf nodes
        kd(std::vector<int32_t>(ids.begin(), ids.begin() + ptrdiff_t(cut)));
        kd(std::vector<int32_t>(ids.begin() + ptrdiff_t(cut), ids.end()));
    }
};

void empty_lanes(SimdNode& node, double lo_value, double hi_value) {
    for (int lane = 0; lane < 4; ++lane)
        for (int k = 0; k < 3; ++k) {
            node.lo[lane][k] = lo_value;
            node.hi[lane][k] = hi_value;
        }
}

}  // namespace

// The node count simd_tree writes for n boxes (sentinel included); 0 for n == 0 (the Python's IndexError).
extern "C" __declspec(dllexport) size_t edf6_simd_tree_nodes(size_t n) {
    if (n == 0) return 0;
    size_t level = (n + 3) / 4, total = level;
    bool leaf = true;
    while (level > 1 || leaf) {
        level = (level + 3) / 4;
        total += level;
        leaf = false;
    }
    return total + 1;
}

// `boxes`: n boxes as (lo x, y, z, hi x, y, z) doubles; `data`: n lane values; `out` holds
// 128 * edf6_simd_tree_nodes(n) bytes. Returns 1, or 0 (nothing usable in `out`) for n == 0 or a NaN: the caller
// hands those to the Python.
extern "C" __declspec(dllexport) int edf6_simd_tree(const double* boxes, size_t n, const uint32_t* data, uint8_t* out) {
    if (n == 0) return 0;
    for (size_t i = 0; i < n * 6; ++i)
        if (std::isnan(boxes[i])) return 0;
    const double lo_empty = double(u2f(0x7F7FFFEEu)), hi_empty = double(u2f(0xFF7FFFEEu));   // hkcms_build.EMPTY_LO / HI
    Simd t;
    t.boxes = reinterpret_cast<const Box*>(boxes);
    t.centres.resize(n * 3);
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k) t.centres[i * 3 + k] = t.boxes[i].lo[k] + t.boxes[i].hi[k];
    std::vector<int32_t> ids(n);
    std::iota(ids.begin(), ids.end(), 0);
    t.kd(std::move(ids));
    std::vector<SimdNode> store;
    store.reserve(edf6_simd_tree_nodes(n));
    std::vector<size_t> level;
    for (size_t k = 0; k < n; k += 4) {
        SimdNode node{};
        empty_lanes(node, lo_empty, hi_empty);
        for (int lane = 0; lane < 4; ++lane) node.data[lane] = 0xFFFFFFFFu;
        for (size_t lane = 0; lane < 4 && k + lane < n; ++lane) {
            const Box& b = t.boxes[t.order[k + lane]];
            for (int c = 0; c < 3; ++c) {
                node.lo[lane][c] = b.lo[c];
                node.hi[lane][c] = b.hi[c];
            }
            node.data[lane] = data[t.order[k + lane]];
        }
        node.leaf = 1;
        store.push_back(std::move(node));
        level.push_back(store.size() - 1);
    }
    while (level.size() > 1 || store[level[0]].leaf) {
        std::vector<size_t> next;
        for (size_t k = 0; k < level.size(); k += 4) {
            SimdNode node{};
            empty_lanes(node, lo_empty, hi_empty);
            for (size_t lane = 0; lane < 4 && k + lane < level.size(); ++lane) {
                const SimdNode& c = store[level[k + lane]];
                bool any = false;
                for (int l = 0; l < 4; ++l) {
                    if (!(c.lo[l][0] <= c.hi[l][0])) continue;   // used = lo x <= hi x
                    for (int a = 0; a < 3; ++a) {
                        node.lo[lane][a] = any ? std::min(node.lo[lane][a], c.lo[l][a]) : c.lo[l][a];
                        node.hi[lane][a] = any ? std::max(node.hi[lane][a], c.hi[l][a]) : c.hi[l][a];
                    }
                    any = true;
                }
                if (!any) return 0;   // the Python's min of nothing raises
                node.kids.push_back(level[k + lane]);
            }
            node.leaf = 0;
            store.push_back(std::move(node));
            next.push_back(store.size() - 1);
        }
        level = std::move(next);
    }
    // breadth first from the root: node numbers 1, 2, 3 ...
    std::vector<size_t> flat;
    std::deque<size_t> queue{level[0]};
    while (!queue.empty()) {
        const size_t i = queue.front();
        queue.pop_front();
        store[i].id = uint32_t(flat.size() + 1);
        flat.push_back(i);
        for (size_t c : store[i].kids) queue.push_back(c);
    }
    for (size_t i : flat)
        for (size_t lane = 0; lane < store[i].kids.size(); ++lane) store[i].data[lane] = store[store[i].kids[lane]].id;
    if (flat.size() + 1 != edf6_simd_tree_nodes(n)) return 0;
    SimdNode sentinel{};
    empty_lanes(sentinel, lo_empty, hi_empty);
    sentinel.leaf = 0;
    size_t o = 0;
    auto emit = [&](const SimdNode& node) {
        float f[6][4];
        for (int lane = 0; lane < 4; ++lane) {
            const bool used = node.lo[lane][0] <= node.hi[lane][0];
            for (int a = 0; a < 3; ++a) {
                float lo32 = float(node.lo[lane][a]), hi32 = float(node.hi[lane][a]);
                if (used && double(lo32) > node.lo[lane][a]) lo32 = std::nextafter(lo32, -std::numeric_limits<float>::infinity());
                if (used && double(hi32) < node.hi[lane][a]) hi32 = std::nextafter(hi32, std::numeric_limits<float>::infinity());
                f[2 * a][lane] = lo32;
                f[2 * a + 1][lane] = hi32;
            }
        }
        std::memcpy(out + o, f, sizeof f);
        o += sizeof f;
        std::memcpy(out + o, node.data, 16);
        o += 16;
        out[o++] = node.leaf;
        std::memset(out + o, 0, 15);
        o += 15;
    };
    emit(sentinel);
    for (size_t i : flat) emit(store[i]);
    return 1;
}
