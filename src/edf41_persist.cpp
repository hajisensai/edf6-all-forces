// edf41c::Persist (src/edf41classes.h): a process-lifetime copy of an SGO subtree, for a bullet's custom parameter.
// Its own file so that tests/edf41dragon_control_fixture.cpp runs the real copy (the dragon's Configure -> CopyOf ->
// Persist) without the rest of the 4.1 class machinery.
#include "edf41classes.h"

#include "memory.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace crew::edf41c {
namespace {
// Persistent node copies: never freed, a bullet keeps a pointer into one. Kept in a deque (push_back never moves
// existing elements) and shared by content: a copy is immutable and laid out with relative offsets over a zero-filled
// buffer, so identical source trees give identical bytes, and callers that persist the same node again (a floatgun per
// colonist kill, every robo spawn) reuse one copy instead of adding another each time.
std::mutex persistLock;
std::deque<std::vector<char>> persisted;
std::unordered_multimap<std::uint64_t,std::size_t> persistedByHash;   // FNV-1a of the bytes -> index into `persisted`

// The limits of a copy: arrays of up to kMostChildren nodes, kDeepest levels below the root.
constexpr int kMostChildren=256,kDeepest=8;

std::uint64_t Fnv1a(const std::vector<char>& v) noexcept {
    std::uint64_t h=1469598103934665603ull;
    for(const char c:v) { h^=static_cast<unsigned char>(c); h*=1099511628211ull; }
    return h;
}
// Child i of an array node, unchecked: NodeBytes reads every node of the tree through Readable before CopyNode walks it.
const SgoNode* At(const SgoNode* array,int i) noexcept {
    return reinterpret_cast<const SgoNode*>(reinterpret_cast<const char*>(array)+array->b+static_cast<std::size_t>(i)*sizeof(SgoNode));
}
std::size_t NodeBytes(const SgoNode* n,int depth) noexcept {
    if(depth>kDeepest || !Readable(n,sizeof(SgoNode)))return 0;
    if(n->type==3)return n->a<0 ? 0 : sizeof(SgoNode)+static_cast<std::size_t>(n->a+1)*sizeof(wchar_t)+8;
    if(n->type!=0)return sizeof(SgoNode);
    if(n->a<0 || n->a>kMostChildren)return 0;
    std::size_t total=sizeof(SgoNode)*(1+static_cast<std::size_t>(n->a));
    for(int i=0;i<n->a;++i) {
        const std::size_t c=NodeBytes(At(n,i),depth+1);
        if(!c)return 0;
        total+=c;
    }
    return total;
}
// The node at `out + at` with its children laid out from `*end` (each array's children together); strings after.
bool CopyNode(const SgoNode* src,std::vector<char>& out,std::size_t at,std::size_t& end) noexcept {
    SgoNode copy=*src;
    if(src->type==3) {
        const std::size_t bytes=static_cast<std::size_t>(src->a)*sizeof(wchar_t);
        // An empty string skips the check: the real Readable takes zero bytes (as before the move), a test's may not.
        if(bytes && !Readable(reinterpret_cast<const char*>(src)+src->b,bytes))return false;
        if(bytes)std::memcpy(out.data()+end,reinterpret_cast<const char*>(src)+src->b,bytes);
        copy.b=static_cast<std::uint32_t>(end-at);
        end+=bytes+sizeof(wchar_t);
        end=(end+3)&~static_cast<std::size_t>(3);
    } else if(src->type==0) {
        const std::size_t first=end;
        end+=sizeof(SgoNode)*static_cast<std::size_t>(src->a);
        copy.b=static_cast<std::uint32_t>(first-at);
        for(int i=0;i<src->a;++i)
            if(!CopyNode(At(src,i),out,first+sizeof(SgoNode)*static_cast<std::size_t>(i),end))return false;
    }
    std::memcpy(out.data()+at,&copy,sizeof(copy));
    return true;
}
}  // namespace

const SgoNode* Persist(const SgoNode* n) noexcept {
    const std::size_t bytes=NodeBytes(n,0);
    if(!bytes)return nullptr;
    std::vector<char> out(bytes+16,'\0');
    std::size_t end=sizeof(SgoNode);
    if(!CopyNode(n,out,0,end))return nullptr;
    const std::uint64_t h=Fnv1a(out);
    std::lock_guard<std::mutex> hold(persistLock);
    const auto range=persistedByHash.equal_range(h);
    for(auto it=range.first;it!=range.second;++it) {
        const std::vector<char>& have=persisted[it->second];
        if(have.size()==out.size() && std::memcmp(have.data(),out.data(),out.size())==0)
            return reinterpret_cast<const SgoNode*>(have.data());
    }
    persistedByHash.emplace(h,persisted.size());
    persisted.push_back(std::move(out));
    return reinterpret_cast<const SgoNode*>(persisted.back().data());
}
}  // namespace crew::edf41c
