// SGO node blocks built in memory (pure logic; src/soldier_classes.cpp, tests/soldier_classes_test.cpp).
//
// A node is 12 bytes {int32 type, int32 a, int32 rel} (pylib/sgo.py; docs/newclass-re/inject.md 1.2): 0 array (a
// children of 12 bytes at node + rel), 1 int (the value at +8), 2 float (at +8), 3 UTF-16 string (a characters at
// node + rel, NUL-terminated), 4 raw block (a bytes at node + rel). The game reads rel signed (movsxd) and always from
// the node's own address, so a block written here is position independent: it is built into a vector and can be copied
// anywhere (the plugin copies it into memory it never frees, because the game keeps pointers into the strings).
//
// Read() copies a node tree out of memory (the game's loaded CONFIG.SGO); Write() lays a tree out with the root node at
// offset 0, arrays breadth first, then raw blocks (16-aligned), then the strings (deduplicated, each NUL-terminated).
// An empty string gets a = 0 and a rel pointing at a real NUL, so a reader that follows rel anyway reads "".
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace crew::sgob {
static_assert(sizeof(wchar_t)==2,"SGO strings are UTF-16; wchar_t must be 16-bit (MSVC)");

constexpr std::int32_t kArray=0,kInt=1,kFloat=2,kString=3,kRaw=4;
constexpr std::size_t kNodeSize=12;

struct Value {
    std::int32_t type=kInt;
    std::uint32_t bits=0;              // kInt: the value; kFloat: its IEEE bits
    std::wstring text;                 // kString
    std::vector<unsigned char> raw;    // kRaw
    std::vector<Value> items;          // kArray
};

inline Value Int(std::int32_t v) {
    Value out;out.type=kInt;out.bits=static_cast<std::uint32_t>(v);
    return out;
}
inline Value Float(float f) {
    Value out;out.type=kFloat;std::memcpy(&out.bits,&f,4);
    return out;
}
inline Value Str(std::wstring s) {
    Value out;out.type=kString;out.text=std::move(s);
    return out;
}
inline Value Array(std::vector<Value> items) {
    Value out;out.type=kArray;out.items=std::move(items);
    return out;
}
inline Value Raw(std::vector<unsigned char> bytes) {
    Value out;out.type=kRaw;out.raw=std::move(bytes);
    return out;
}

inline bool operator==(const Value& a,const Value& b) {
    if(a.type!=b.type)return false;
    switch(a.type) {
    case kInt:case kFloat:return a.bits==b.bits;
    case kString:return a.text==b.text;
    case kRaw:return a.raw==b.raw;
    default:return a.items==b.items;
    }
}
inline bool operator!=(const Value& a,const Value& b) { return !(a==b); }

// Bounds a read of memory the plugin did not write: a deeper tree, a longer array or string than any CONFIG.SGO has
// means the node is not what it is taken for, and Read() fails instead of walking off.
struct Limits {
    unsigned depth=16;
    std::size_t nodes=1u<<16;
    std::size_t chars=1u<<12;
    std::size_t rawBytes=1u<<20;
};

namespace detail {
inline std::int32_t I32(const unsigned char* p) {
    std::int32_t v;std::memcpy(&v,p,4);
    return v;
}
inline bool ReadNode(const unsigned char* node,Value& out,const Limits& lim,unsigned depth,std::size_t& nodes) {
    if(depth>lim.depth || ++nodes>lim.nodes)return false;
    const std::int32_t type=I32(node),a=I32(node+4),rel=I32(node+8);
    out=Value{};
    out.type=type;
    switch(type) {
    case kInt:case kFloat:
        std::memcpy(&out.bits,node+8,4);
        return true;
    case kString: {
        if(a<0 || static_cast<std::size_t>(a)>lim.chars)return false;
        if(a==0)return true;
        const unsigned char* s=node+rel;
        out.text.resize(static_cast<std::size_t>(a));
        std::memcpy(out.text.data(),s,static_cast<std::size_t>(a)*2);
        return true;
    }
    case kRaw:
        if(a<0 || static_cast<std::size_t>(a)>lim.rawBytes)return false;
        out.raw.assign(node+rel,node+rel+a);
        return true;
    case kArray: {
        if(a<0 || static_cast<std::size_t>(a)>lim.nodes)return false;
        out.items.resize(static_cast<std::size_t>(a));
        const unsigned char* first=node+rel;
        for(std::int32_t i=0;i<a;++i)
            if(!ReadNode(first+static_cast<std::ptrdiff_t>(i)*kNodeSize,out.items[static_cast<std::size_t>(i)],lim,depth+1,nodes))
                return false;
        return true;
    }
    default:
        return false;   // not a node type the format has
    }
}
inline void Put(std::vector<unsigned char>& buf,std::size_t at,std::int32_t type,std::int32_t a,std::int32_t rel) {
    std::memcpy(buf.data()+at,&type,4);
    std::memcpy(buf.data()+at+4,&a,4);
    std::memcpy(buf.data()+at+8,&rel,4);
}
}  // namespace detail

// The tree under the node at `node`. False (out unspecified): an unknown node type or past the limits.
inline bool Read(const unsigned char* node,Value& out,const Limits& lim=Limits{}) {
    std::size_t nodes=0;
    return detail::ReadNode(node,out,lim,0,nodes);
}

// The block for `root`: the root node at offset 0, everything it holds after it. Empty when a value cannot be laid out
// (a type outside 0..4, an array or string longer than an int32 can count).
inline std::vector<unsigned char> Write(const Value& root) {
    using detail::Put;
    std::vector<unsigned char> buf(kNodeSize);
    std::vector<std::pair<std::size_t,const Value*>> strings,raws;
    std::vector<std::pair<std::size_t,const Value*>> level{{0,&root}};
    while(!level.empty()) {
        std::vector<std::pair<std::size_t,const Value*>> next;
        for(const auto& [pos,v]:level) {
            switch(v->type) {
            case kInt:case kFloat:
                Put(buf,pos,v->type,4,static_cast<std::int32_t>(v->bits));
                break;
            case kString:strings.emplace_back(pos,v);break;
            case kRaw:raws.emplace_back(pos,v);break;
            case kArray: {
                if(v->items.size()>0x7FFFFFFu)return {};
                const std::size_t at=buf.size();
                buf.resize(at+v->items.size()*kNodeSize);
                Put(buf,pos,kArray,static_cast<std::int32_t>(v->items.size()),v->items.empty()?0:static_cast<std::int32_t>(at-pos));
                for(std::size_t i=0;i<v->items.size();++i)next.emplace_back(at+i*kNodeSize,&v->items[i]);
                break;
            }
            default:return {};
            }
        }
        level=std::move(next);
    }
    for(const auto& [pos,v]:raws) {
        if(v->raw.size()>0x7FFFFFFFu)return {};
        while(buf.size()%16)buf.push_back(0);
        const std::size_t at=buf.size();
        buf.insert(buf.end(),v->raw.begin(),v->raw.end());
        Put(buf,pos,kRaw,static_cast<std::int32_t>(v->raw.size()),static_cast<std::int32_t>(at-pos));
    }
    if(buf.size()%2)buf.push_back(0);
    std::map<std::wstring,std::size_t> pool;
    for(const auto& [pos,v]:strings) {
        if(v->text.size()>0x7FFFFFFu)return {};
        auto it=pool.find(v->text);
        if(it==pool.end()) {
            const std::size_t at=buf.size();
            buf.resize(at+(v->text.size()+1)*2);
            if(!v->text.empty())std::memcpy(buf.data()+at,v->text.data(),v->text.size()*2);
            it=pool.emplace(v->text,at).first;
        }
        Put(buf,pos,kString,static_cast<std::int32_t>(v->text.size()),static_cast<std::int32_t>(it->second-pos));
    }
    while(buf.size()%16)buf.push_back(0);
    if(buf.size()>0x7FFFFFFFu)return {};
    return buf;
}
}  // namespace crew::sgob
