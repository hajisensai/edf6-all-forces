// Collect an ordinary GameObject's native raw-child list for one atomic PvP operation.
#pragma once
#include "crew.h"
#include "memory.h"

namespace crew { namespace enemy_pvp_native {
namespace tree_detail {
constexpr std::size_t kChildren=0x550,kChildCount=0x558,kDeletedFlags=0x18;
// EDF.dll 678CCB46: 1C36D0 recursively walks +550; 54ECAD/B2 allocates only 0x18 bytes.
// 54ECB7 writes a raw child pointer; 54ECBF-CE maintains the next/prev links.
constexpr std::size_t kNodeNext=0,kNodePrev=8,kNodeObject=0x10;
inline bool LiveIdentity(const ObjRef& ref) noexcept {
    // Check the control before touching a possibly freed object. No Vehicle ABI is involved.
    return ref.obj && ref.ctrl && Readable(ref.ctrl,16) && At<LONG>(ref.ctrl,8)>0 &&
        Readable(ref.obj,kChildCount+sizeof(std::size_t)) && At<const void*>(ref.obj,kSelfCtrl)==ref.ctrl &&
        !(At<unsigned char>(ref.obj,kDeletedFlags)&4u);
}
}
// Success contains root exactly once, then every descendant. Failure publishes count=0,
// including an exhausted output bound; partial output must never be emitted by the caller.
// Call only on the game thread, where native child lists cannot mutate concurrently.
inline bool CollectTree(const ObjRef& root,ObjRef* out,unsigned capacity,unsigned* count) noexcept {
    if(count)*count=0;
    if(!count || !out || !capacity)return false;
    __try {
        if(!tree_detail::LiveIdentity(root))return false;
        unsigned used=1;out[0]=root;
        for(unsigned current=0;current<used;++current) {
            if(!tree_detail::LiveIdentity(out[current]))return false;
            const auto* head=At<const unsigned char*>(out[current].obj,tree_detail::kChildren);
            if(!Readable(head,16))return false;
            const auto* node=At<const unsigned char*>(head,tree_detail::kNodeNext);
            const auto* previous=head;
            const auto expected=At<std::size_t>(out[current].obj,tree_detail::kChildCount);
            if(expected>capacity-used)return false;
            std::size_t walked=0;
            while(node!=head) {
                if(walked>=expected || !Readable(node,0x18) || At<const void*>(node,tree_detail::kNodePrev)!=previous)return false;
                const auto* object=At<const unsigned char*>(node,tree_detail::kNodeObject);
                if(!Readable(object,kSelfCtrl+sizeof(void*)))return false;
                const ObjRef child{object,At<const void*>(object,kSelfCtrl)};
                if(!tree_detail::LiveIdentity(child))return false;
                // A repeated address is invalid even when the list presents a different control block.
                // It also rejects object cycles and node cycles without an unbounded side allocation.
                for(unsigned i=0;i<used;++i)if(out[i].obj==child.obj)return false;
                if(used==capacity)return false;
                out[used++]=child;
                ++walked;previous=node;node=At<const unsigned char*>(node,tree_detail::kNodeNext);
            }
            if(walked!=expected || At<const void*>(head,tree_detail::kNodePrev)!=previous)return false;
        }
        *count=used;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} } // namespace crew::enemy_pvp_native
