// The first person's muzzle flash size and brightness (firstperson.cpp FlashCreateHook; RE docs/fp-render-re.md §5.2), no
// game state: tools/fp_flash_check.cpp runs it on a document built offline.
// A flash class's constructor reads its look from the weapon's SGO list MuzzleFlash_CustomParameter, handed over in the
// flash's parameter block (weapon+0x488) at +0x40 (= weapon+0x4C8): a document handle {doc, node index} (variant tag 2,
// 0x68DBE5). Each element is read by index (0x52DE0) and turned into a double (0x52F50) inside the constructor: the
// sizes become the geometry of its beams (0x2C0AE0 / 0x2BD720) or sprite and the colour its tint. That geometry is drawn
// through the flash's world matrix (flash+0x60, copied from its borrowed matrix: in first person shot·T, the viewmodel's
// scale included), so it already shrinks with the gun as drawn and keeps the third person's proportion to it; the first
// pass, a light at the matrix's position row, takes only the colour. The first person's flash is smaller and dimmer
// only by these elements: the caller scales the sizes by FirstPersonFlashScale and the colour by FirstPersonFlashBright
// for the one construction and puts the bytes back right after (Edit / Restore).
// The document (H, 0x52DE0 / 0x52E50 / 0x52F50): an override map [doc+0x08] (u32 per node, -1 none; count doc+0x18) into
// 40-byte override slots [doc+0x28]; the loaded file [doc+0x50]: nodes 16 bytes at file + i32 [file+0xC] + index*16
// {u64 value, u32 type} (pylib/dsgo.py: type 0 a double, 3 a container whose block at node + value holds the count at
// +0xC and the child indices at block + u32 [block+8]). An overridden node, any other type or a short list is not a
// layout this edits: the flash stays native (the caller logs it).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew {
namespace fpflash {
// Which elements are sizes (lengths and widths, m) and which is the colour (r, g, b, a; a kept).
struct Layout {
    const char* name;
    unsigned least;         // the list's elements at least
    int sizes[4];int sizeCount;
    int colour;
};
// MuzzleFlash01 (0x2BEC90), MuzzleFlash02 (0x2BFE60), LaserMuzzleFlash02 (0x2BBE90): [0] texture, [1] the beam's length,
// [2] its width, [3] the spokes' turn, [4] the spokes (an int), [5] a spoke's length, [6] its width, [7] an int, [8] the
// colour, [9].. not sizes.
constexpr Layout kBeams{"beams",10,{1,2,5,6},4,8};
// LaserMuzzleFlash01 (0x2BB300), LaserMuzzleFlash03 (0x2BD9B0): [0] texture, [1] the sprite's size, [2] the colour,
// [3] an int.
constexpr Layout kSprite{"sprite",4,{1,0,0,0},1,2};

constexpr std::size_t kVariantTag=0x10,kDocMap=0x08,kDocMapCount=0x18,kDocSlots=0x28,kDocFile=0x50,kFileNodes=0x0C;
constexpr std::uint16_t kTagDocument=2;
constexpr std::uint32_t kDouble=0,kContainer=3;

// What one construction changed: each double's place and its bytes before.
struct Edit { double* at[8]; std::uint64_t was[8]; int n; };

// `ok(pointer, bytes, writable)`: whether that memory may be read (written).
template<class Ok> const unsigned char* NodeOf(const unsigned char* doc,std::uint32_t index,Ok& ok) noexcept {
    if(!ok(doc,kDocFile+8,false))return nullptr;
    std::uint64_t mapped;std::memcpy(&mapped,doc+kDocMapCount,8);
    if(index<mapped) {
        const unsigned char* map;std::memcpy(&map,doc+kDocMap,8);
        const unsigned char* slots;std::memcpy(&slots,doc+kDocSlots,8);
        if(!ok(map+static_cast<std::size_t>(index)*4,4,false))return nullptr;
        std::uint32_t slot;std::memcpy(&slot,map+static_cast<std::size_t>(index)*4,4);
        if(slot!=0xFFFFFFFFu && slots)return nullptr;   // overridden: not the file's node (0x52DE0's first path)
    }
    const unsigned char* file;std::memcpy(&file,doc+kDocFile,8);
    if(!file || !ok(file,16,false))return nullptr;
    std::int32_t nodes;std::memcpy(&nodes,file+kFileNodes,4);
    const unsigned char* node=file+nodes+static_cast<std::size_t>(index)*16;
    return ok(node,16,false) ? node : nullptr;
}

inline std::uint32_t TypeOf(const unsigned char* node) noexcept { std::uint32_t t;std::memcpy(&t,node+8,4);return t; }

// Child `i` of the container `index` (its node index into `child`); false: not a container, or too short.
template<class Ok> bool ChildOf(const unsigned char* doc,std::uint32_t index,unsigned i,std::uint32_t* child,unsigned* count,
                                Ok& ok) noexcept {
    const unsigned char* node=NodeOf(doc,index,ok);
    if(!node || TypeOf(node)!=kContainer)return false;
    std::int32_t rel;std::memcpy(&rel,node,4);
    const unsigned char* block=node+rel;
    if(!ok(block,16,false))return false;
    std::uint32_t at,n;std::memcpy(&at,block+8,4);std::memcpy(&n,block+0xC,4);
    *count=n;
    if(i>=n || !ok(block+at+static_cast<std::size_t>(i)*4,4,false))return false;
    std::memcpy(child,block+at+static_cast<std::size_t>(i)*4,4);
    return true;
}

// The double of node `index`, writable; nullptr: not a plain double of the file.
template<class Ok> double* DoubleOf(const unsigned char* doc,std::uint32_t index,Ok& ok) noexcept {
    const unsigned char* node=NodeOf(doc,index,ok);
    if(!node || TypeOf(node)!=kDouble || !ok(node,8,true))return nullptr;
    return reinterpret_cast<double*>(const_cast<unsigned char*>(node));
}

// The list the handle `variant` (the flash parameter block's +0x40) names, its sizes times `size` and its colour's
// r, g, b times `bright`. Everything is found before anything is written: false (nothing written) when the handle or a
// node is not the layout. `out` takes what to put back.
template<class Ok> bool Scale(const unsigned char* variant,const Layout& l,double size,double bright,Edit* out,Ok ok) noexcept {
    out->n=0;
    if(!ok(variant,kVariantTag+2,false))return false;
    std::uint16_t tag;std::memcpy(&tag,variant+kVariantTag,2);
    const unsigned char* doc;std::memcpy(&doc,variant,8);
    std::uint32_t list;std::memcpy(&list,variant+8,4);
    if(tag!=kTagDocument || !doc)return false;
    double* at[8];int n=0;
    unsigned count=0;
    std::uint32_t child;
    for(int k=0;k<l.sizeCount;++k) {
        if(!ChildOf(doc,list,static_cast<unsigned>(l.sizes[k]),&child,&count,ok) || count<l.least)return false;
        if(!(at[n++]=DoubleOf(doc,child,ok)))return false;
    }
    std::uint32_t colour;
    unsigned channels=0;
    if(!ChildOf(doc,list,static_cast<unsigned>(l.colour),&colour,&count,ok) || count<l.least)return false;
    for(unsigned c=0;c<3;++c) {
        if(!ChildOf(doc,colour,c,&child,&channels,ok) || channels!=4)return false;
        if(!(at[n++]=DoubleOf(doc,child,ok)))return false;
    }
    for(int k=0;k<n;++k) {
        out->at[k]=at[k];std::memcpy(&out->was[k],at[k],8);
        *at[k]*=k<l.sizeCount ? size : bright;
    }
    out->n=n;
    return true;
}

// The bytes back as they were (the document is the weapon's SGO, shared by every weapon made from it).
inline void Restore(Edit& e) noexcept {
    for(int k=0;k<e.n;++k)std::memcpy(e.at[k],&e.was[k],8);
    e.n=0;
}
}  // namespace fpflash
}  // namespace crew
