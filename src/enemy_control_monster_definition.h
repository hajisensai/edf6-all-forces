// Stable native custom-parameter definitions. SGO references stay process local;
// only their typed, unevaluated values contribute to the accepted-shot digest.
#pragma once
#include "memory.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace crew::enemy_native::monster_definition {
namespace detail {
inline constexpr unsigned kMaxNodes=4096,kMaxDepth=32;
inline constexpr std::size_t kMaxBytes=4u*1024u*1024u,kMaxRawOffset=16u*1024u*1024u;
struct State {
    std::uint64_t hash=0;std::size_t bytes=0;unsigned visited=0,depth=0;
    const unsigned char* oldPath[kMaxDepth]{};
    unsigned dsgoPath[kMaxDepth]{};
    const unsigned char* tree=nullptr;const unsigned char* raw=nullptr;unsigned nodes=0;
};
inline const unsigned char* Address(const void* base,std::int64_t offset,std::size_t bytes) noexcept {
    if(!base)return nullptr;
    const auto address=reinterpret_cast<std::uintptr_t>(base);std::uintptr_t result=address;
    if(offset>=0) {
        const auto delta=static_cast<std::uint64_t>(offset);
        if(delta>(std::numeric_limits<std::uintptr_t>::max)()-address)return nullptr;
        result+=static_cast<std::uintptr_t>(delta);
    } else {
        const auto delta=static_cast<std::uint64_t>(-(offset+1))+1;
        if(delta>address)return nullptr;result-=static_cast<std::uintptr_t>(delta);
    }
    if(bytes>(std::numeric_limits<std::uintptr_t>::max)()-result)return nullptr;
    const auto* pointer=reinterpret_cast<const unsigned char*>(result);
    return !bytes || Readable(pointer,bytes) ? pointer : nullptr;
}
template<class T> inline bool Read(const void* base,std::size_t offset,T* out) noexcept {
    if(!out || offset>static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)()))return false;
    const auto* pointer=Address(base,static_cast<std::int64_t>(offset),sizeof(T));
    if(!pointer)return false;std::memcpy(out,pointer,sizeof(T));return true;
}
inline bool Mix(State& state,const void* bytes,std::size_t size) noexcept {
    if(size>kMaxBytes-state.bytes || (size && !Readable(bytes,size)))return false;
    const auto* data=static_cast<const unsigned char*>(bytes);
    for(std::size_t i=0;i<size;++i){state.hash^=data[i];state.hash*=1099511628211ull;}
    state.bytes+=size;return true;
}
inline bool Number(State& state,std::uint64_t value,unsigned bytes=4) noexcept {
    unsigned char encoded[8]{};for(unsigned i=0;i<bytes;++i)encoded[i]=static_cast<unsigned char>(value>>(i*8));
    return Mix(state,encoded,bytes);
}
inline const unsigned char* OldRelative(const void* node,std::int32_t offset,std::size_t bytes) noexcept {
    if(static_cast<std::int64_t>(offset)<-static_cast<std::int64_t>(kMaxRawOffset) ||
       static_cast<std::int64_t>(offset)>static_cast<std::int64_t>(kMaxRawOffset) || bytes>kMaxRawOffset)return nullptr;
    return Address(node,offset,bytes);
}
inline bool OldNode(State& state,const unsigned char* node) noexcept {
    if(++state.visited>kMaxNodes || state.depth>=kMaxDepth || !Readable(node,12))return false;
    for(unsigned i=0;i<state.depth;++i)if(state.oldPath[i]==node)return false;
    std::uint32_t type=0;std::int32_t count=0,relative=0;
    if(!Read(node,0,&type) || !Read(node,4,&count) || !Read(node,8,&relative) || type>4 || !Number(state,type))return false;
    if(type==1 || type==2)return Mix(state,node+8,4); // exact integer / float bits, including signed zero
    if(count<0 || !Number(state,static_cast<std::uint32_t>(count)))return false;
    if(type==3 || type==4) {
        const auto size=static_cast<std::size_t>(count)*(type==3 ? 2u : 1u);
        if(size>kMaxBytes-state.bytes)return false;if(!size)return true;
        const auto* data=OldRelative(node,relative,size);return data && Mix(state,data,size);
    }
    if(static_cast<unsigned>(count)>kMaxNodes)return false;
    if(!count)return true;
    const auto* children=OldRelative(node,relative,static_cast<std::size_t>(count)*12);
    if(!children)return false;state.oldPath[state.depth++]=node;bool valid=true;
    for(unsigned i=0;i<static_cast<unsigned>(count) && valid;++i)valid=OldNode(state,children+static_cast<std::size_t>(i)*12);
    --state.depth;return valid;
}
inline const unsigned char* RawAt(const State& state,std::int64_t offset,std::size_t bytes) noexcept {
    if(offset<0 || static_cast<std::uint64_t>(offset)>kMaxRawOffset ||
       bytes>kMaxRawOffset-static_cast<std::size_t>(offset))return nullptr;
    return Address(state.raw,offset,bytes);
}
inline bool NotOverridden(const State& state,unsigned index) noexcept {
    if(index>=state.nodes || !Readable(state.tree,0x58))return false;
    std::size_t count=0;if(!Read(state.tree,0x18,&count) || count>1048576)return false;
    if(index>=count)return true;
    const unsigned char* map=nullptr;std::uint32_t value=0;
    return Read(state.tree,8,&map) && Read(map,static_cast<std::size_t>(index)*4,&value) && value==UINT32_MAX;
}
inline const unsigned char* RawNode(const State& state,unsigned index) noexcept {
    return NotOverridden(state,index) ? RawAt(state,16+static_cast<std::int64_t>(index)*16,16) : nullptr;
}
inline bool RawText(State& state,std::int64_t offset) noexcept {
    unsigned count=0;
    for(;count<=1024;++count) {
        std::uint16_t word=0;const auto* at=RawAt(state,offset+static_cast<std::int64_t>(count)*2,2);
        if(!at || !Read(at,0,&word))return false;if(!word)break;
    }
    if(count>1024 || !Number(state,count))return false;
    const auto size=static_cast<std::size_t>(count)*2;const auto* text=RawAt(state,offset,size);
    return text && Mix(state,text,size);
}
inline bool DsgoNode(State& state,unsigned index) noexcept {
    if(++state.visited>kMaxNodes || state.depth>=kMaxDepth)return false;
    for(unsigned i=0;i<state.depth;++i)if(state.dsgoPath[i]==index)return false;
    const auto* node=RawNode(state,index);std::uint32_t type=0;std::int32_t relative=0;
    if(!node || !Read(node,8,&type) || type>4 || !Number(state,type))return false;
    if(type==0)return Mix(state,node,8); // literal double; never run a DSGO expression/VM
    if(!Read(node,0,&relative))return false;
    const auto blockOffset=16+static_cast<std::int64_t>(index)*16+relative;
    if(type==1)return RawText(state,blockOffset);
    if(type==2 || type==4) {
        const auto* block=RawAt(state,blockOffset,8);std::uint32_t bytes=0;std::int32_t dataRelative=0;
        if(!block || !Read(block,0,&bytes) || !Read(block,4,&dataRelative) || bytes>1024*1024 || !Number(state,bytes))return false;
        const auto* data=RawAt(state,blockOffset+dataRelative,bytes);return data && Mix(state,data,bytes);
    }
    const auto* block=RawAt(state,blockOffset,16);std::uint32_t count=0,names=0;std::int32_t childRelative=0,nameRelative=0;
    if(!block || !Read(block,12,&count) || !Read(block,4,&names) || !Read(block,8,&childRelative) || !Read(block,0,&nameRelative) ||
       count>1024 || names>count || !Number(state,count))return false;
    const auto childrenOffset=blockOffset+childRelative,nameOffset=blockOffset+nameRelative;
    const auto* children=RawAt(state,childrenOffset,static_cast<std::size_t>(count)*4);
    const auto* nameTable=RawAt(state,nameOffset,static_cast<std::size_t>(names)*8);
    if((count && !children) || (names && !nameTable))return false;
    for(unsigned n=0;n<names;++n){std::uint32_t child=0;if(!Read(nameTable,static_cast<std::size_t>(n)*8+4,&child) || child>=count)return false;}
    state.dsgoPath[state.depth++]=index;bool valid=true;
    for(unsigned child=0;child<count && valid;++child) {
        unsigned found=0;std::int64_t textOffset=0;
        for(unsigned n=0;n<names;++n) {
            std::uint32_t named=0;std::int32_t textRelative=0;
            if(!Read(nameTable,static_cast<std::size_t>(n)*8+4,&named)){valid=false;break;}
            if(named!=child)continue;
            if(++found>1 || !Read(nameTable,static_cast<std::size_t>(n)*8,&textRelative)){valid=false;break;}
            textOffset=nameOffset+static_cast<std::int64_t>(n)*8+textRelative;
        }
        if(!valid || !Number(state,found) || (found && !RawText(state,textOffset))){valid=false;break;}
        std::uint32_t selected=0;
        valid=Read(children,static_cast<std::size_t>(child)*4,&selected) && DsgoNode(state,selected);
    }
    --state.depth;return valid;
}
inline bool Dsgo(State& state,const void* nativeVariant) noexcept {
    std::uint32_t index=0,magic=0,version=0,stride=0;
    if(!Read(nativeVariant,0,&state.tree) || !Read(nativeVariant,8,&index) || !Readable(state.tree,0x58) ||
       !Read(state.tree,0x50,&state.raw) || !Readable(state.raw,16) || !Read(state.raw,0,&magic) ||
       !Read(state.raw,4,&version) || !Read(state.raw,8,&state.nodes) || !Read(state.raw,12,&stride) ||
       magic!=0x4F475344 || version!=16 || stride!=16 || !state.nodes || state.nodes>65536)return false;
    return DsgoNode(state,index);
}
} // namespace detail

// Transactional: invalid/overridden definitions leave the caller's digest intact.
inline bool HashVariant(const void* nativeVariant,std::uint64_t* hash) noexcept {
    __try {
        if(!hash || !Readable(hash,sizeof(*hash),true) || !Readable(nativeVariant,18))return false;
        detail::State state{};state.hash=*hash;std::uint16_t type=0;
        if(!detail::Read(nativeVariant,16,&type) || (type!=0xFFFF && type>2) || !detail::Number(state,type,2))return false;
        bool valid=type==0xFFFF || type==0;
        if(type==1){const unsigned char* node=nullptr;valid=detail::Read(nativeVariant,0,&node) && detail::OldNode(state,node);}
        else if(type==2)valid=detail::Dsgo(state,nativeVariant);
        if(!valid)return false;*hash=state.hash;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew::enemy_native::monster_definition
