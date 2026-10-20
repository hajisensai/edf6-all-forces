// The game's own dialog box items (HUiDialogBox::InitParam's items vector, EDF.dll TimeDateStamp 0x678CCB46; the RE is
// docs/room-native-dialog-re.md and docs/edf41-outfits.md): what room_dialog.cpp (the room / HQ menu's dialogs) and
// outfit41.cpp (the HQ outfit popup's EDF4.1 item) both do to them, done once.
//   - an item is 0x90 bytes {std::wstring text +0, std::function<void(ctx*)> +0x20 (its impl pointer +0x58), +0x60
//     enabled, +0x80 a tree (head +0x80, size +0x88)};
//   - the vector {+0x08 data, +0x10 capacity, +0x18 size} grows as the game's own loop grows it (0x8ABAE0 / 0x8ABAF3):
//     reserve(capacity * 2 + 2) by 0xB91D0 when full, the size first, then the item copy-constructed in place by
//     0x89950(vector, dst, src);
//   - an item is destroyed by the game's 0x8E170 (its tree, its std::function, its text);
//   - a std::function of ours is an MSVC _Func_impl whose vtable is a copy of a stock lambda's (its RTTI locator at [-1]
//     kept, _Target_type / _Delete_this / _Get the stock's) with _Copy / _Move / _Do_call ours.
// The calls go through ItemOps (the game's functions; tests/game_dialog_test.cpp hands fakes) so the growth and the
// vtable are tested offline. UI thread only, as the dialogs are.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::gamedialog {
// EDF.dll 0x678CCB46 RVAs (checked by tools/room_dialog_sigs.py / tools/outfit41_sigs.py before any call).
inline constexpr std::size_t kReserveRva=0xB91D0,kItemCopyRva=0x89950,kItemDestroyRva=0x8E170;
// An item.
inline constexpr std::size_t kItemSize=0x90,kItemText=0x0,kItemFunc=0x20,kItemImpl=0x58,kItemEnabled=0x60,
    kItemTree=0x80,kItemTreeSize=0x88;
// The items vector (relative to the vector object: InitParam +0x150, the outfit request +0x150).
inline constexpr std::size_t kVecData=0x8,kVecCapacity=0x10,kVecSize=0x18;
// std::_Func_impl's vtable (MSVC): _Copy, _Move, _Do_call, _Target_type, _Delete_this, _Get.
inline constexpr int kFuncSlots=6,kSlotCopy=0,kSlotMove=1,kSlotCall=2;

// The vector's growth when full, as the game's own loop does it (0x8ABAF3).
constexpr std::size_t Grown(std::size_t capacity) noexcept { return capacity*2+2; }

using ReserveFn=void(__fastcall*)(void*,std::size_t);
using ItemCopyFn=void*(__fastcall*)(void*,void*,const void*);
using ItemDestroyFn=void(__fastcall*)(void*);
struct ItemOps { ReserveFn reserve=nullptr; ItemCopyFn copy=nullptr; ItemDestroyFn destroy=nullptr; };
inline ItemOps GameOps(unsigned char* base) noexcept {
    return ItemOps{reinterpret_cast<ReserveFn>(base+kReserveRva),reinterpret_cast<ItemCopyFn>(base+kItemCopyRva),
                   reinterpret_cast<ItemDestroyFn>(base+kItemDestroyRva)};
}

template<class T> T Field(const void* p,std::size_t at) noexcept { T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+at,sizeof(T));return v; }
template<class T> void SetField(void* p,std::size_t at,T v) noexcept { std::memcpy(static_cast<unsigned char*>(p)+at,&v,sizeof(T)); }

inline std::size_t Count(const unsigned char* vec) noexcept { return Field<std::size_t>(vec,kVecSize); }
inline unsigned char* ItemAt(const unsigned char* vec,std::size_t i) noexcept { return Field<unsigned char*>(vec,kVecData)+i*kItemSize; }

namespace detail {
// Room for one more (the game's growth), then the size first: the slot the copy is made in.
inline unsigned char* Grow(const ItemOps& ops,unsigned char* vec) noexcept {
    std::size_t size=Count(vec);
    if(size==Field<std::size_t>(vec,kVecCapacity))ops.reserve(vec,Grown(size));
    size=Count(vec);
    SetField<std::size_t>(vec,kVecSize,size+1);
    return ItemAt(vec,size);
}
}  // namespace detail
// push_back(item): `item` outside the vector (a temporary the caller then destroys). The new item.
inline unsigned char* PushItem(const ItemOps& ops,unsigned char* vec,const unsigned char* item) noexcept {
    unsigned char* slot=detail::Grow(ops,vec);
    ops.copy(vec,slot,item);
    return slot;
}
// push_back(vector[index]): the source looked up after the growth (a reserve moves every item).
inline unsigned char* PushCopyOf(const ItemOps& ops,unsigned char* vec,std::size_t index) noexcept {
    unsigned char* slot=detail::Grow(ops,vec);
    ops.copy(vec,slot,ItemAt(vec,index));
    return slot;
}
// The vector cut to `keep` items, the ones past it destroyed in place by the game's item destructor.
inline void Truncate(const ItemOps& ops,unsigned char* vec,std::size_t keep) noexcept {
    const std::size_t size=Count(vec);
    for(std::size_t i=keep;i<size;++i)ops.destroy(ItemAt(vec,i));
    if(keep<size)SetField<std::size_t>(vec,kVecSize,keep);
}

// Our _Func_impl vtable: `out[0]` the stock vtable's [-1] (its RTTI locator), out[1..6] its six slots with _Copy /
// _Move / _Do_call ours. Objects get &out[1].
using FuncVtable=void*[1+kFuncSlots];
inline void CloneFuncVtable(FuncVtable& out,void* const* stock,void* copy,void* move,void* call) noexcept {
    out[0]=stock[-1];
    for(int i=0;i<kFuncSlots;++i)out[1+i]=stock[i];
    out[1+kSlotCopy]=copy;
    out[1+kSlotMove]=move;
    out[1+kSlotCall]=call;
}
}  // namespace crew::gamedialog
