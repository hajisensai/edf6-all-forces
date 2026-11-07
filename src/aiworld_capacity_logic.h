// ai::World's pathfinding-agent capacity, as data (docs/aiworld-re.md). No game access: aiworld_capacity.cpp installs
// it, tests/aiworld_capacity_test.cpp and tests/aiworld_capacity_audit.py check it.
//
// Stock: the World (RTTI .?AVWorld@ai@@, vtable 0x1768A48, 0x2180 bytes, made only by 0xEBE40) keeps its agents in an
// inline table [world+0x80+id*8] of 0x401 slots and hands out ids 1..0x400 from a free-id vector<uint16> (+0x2088).
// With the ids gone a new agent gets 0xFFFF, is not registered, and the slot readers / the recycle loop index the
// table with it, past the World's end (the stress:100 crash at 0xF3E9D, about 1024 soldiers).
// Patched: the table moves to the end of a larger World allocation, [world+0x2180+id*8], kTableSlots slots, and the
// ids are 1..kAgentCapacity, all an id's uint16 holds but 0xFFFF (the stock "no id"). Every change is an immediate or
// a displacement inside one instruction: the World is still made, zeroed, locked and freed by the game's own code.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew::aiworld {
constexpr std::uint32_t kStockWorldSize=0x2180,kStockSlots=0x80,kStockSlotCount=0x401,kStockIdBound=0x401;
// ids are uint16 (movzx word at every reader); 0xFFFF stays the stock "no id" and is never handed out.
constexpr std::uint32_t kIdBound=0xFFFF,kAgentCapacity=kIdBound-1;
// Slots 0..0x10000: every uint16 id, 0xFFFF included, indexes inside the table (a stray 0xFFFF reads null there
// instead of past the allocation). The init's zero loop clears 8 slots an iteration plus one after: 0x2000*8+1.
constexpr std::uint32_t kTableSlots=0x10001,kZeroIterations=(kTableSlots-1)/8;
constexpr std::uint32_t kTable=kStockWorldSize;   // the table right after the stock World
constexpr std::uint32_t kWorldSize=kTable+kTableSlots*8;
static_assert(kZeroIterations*8+1==kTableSlots,"the zero loop clears exactly the table");
static_assert(kTableSlots>kIdBound,"every uint16 id indexes inside the table");

constexpr std::uint8_t B0(std::uint32_t v) noexcept { return static_cast<std::uint8_t>(v); }
constexpr std::uint8_t B1(std::uint32_t v) noexcept { return static_cast<std::uint8_t>(v>>8); }
constexpr std::uint8_t B2(std::uint32_t v) noexcept { return static_cast<std::uint8_t>(v>>16); }
constexpr std::uint8_t B3(std::uint32_t v) noexcept { return static_cast<std::uint8_t>(v>>24); }

// One instruction: its stock bytes and the same with the 4-byte immediate / displacement at `field` rewritten.
enum class Kind : std::uint8_t { WorldSize, ZeroBase, ZeroCount, Slot, IdBound };
struct Site { std::uint32_t rva; std::uint8_t size,field; Kind kind; std::uint8_t stock[12]; std::uint8_t patched[12]; };

#define AIW_SLOT8(rva,b0,b1,b2,b3) \
    {rva,8,4,Kind::Slot,{b0,b1,b2,b3,0x80,0,0,0},{b0,b1,b2,b3,B0(kTable),B1(kTable),B2(kTable),B3(kTable)}}
#define AIW_SLOT12(rva,b0,b1,b2,b3) \
    {rva,12,4,Kind::Slot,{b0,b1,b2,b3,0x80,0,0,0,0,0,0,0},{b0,b1,b2,b3,B0(kTable),B1(kTable),B2(kTable),B3(kTable),0,0,0,0}}
// In install order. Each prefix is consistent on its own (no World exists while they are written): the allocation
// grows before anything uses the room, the table is zeroed before any access points at it, the id bound goes last.
constexpr Site kSites[]={
    // 0xEBE40, the only maker: mov ecx,0x2180; call 0x97A6B0 (the Havok heap's blockAlloc).
    {0xEBE5F,5,1,Kind::WorldSize,{0xB9,0x80,0x21,0,0},{0xB9,B0(kWorldSize),B1(kWorldSize),B2(kWorldSize),B3(kWorldSize)}},
    // 0xEED00, vtable slot 1 (the deleting destructor): the sized free 0x97A760 takes the size given (the World's
    // +8 is 0xFFFFFFFF, "use the caller's size"), and the flag-4 branch's 0x7DFA0 (a no-op), kept equal.
    {0xEED25,5,1,Kind::WorldSize,{0xBA,0x80,0x21,0,0},{0xBA,B0(kWorldSize),B1(kWorldSize),B2(kWorldSize),B3(kWorldSize)}},
    {0xEED41,5,1,Kind::WorldSize,{0xBA,0x80,0x21,0,0},{0xBA,B0(kWorldSize),B1(kWorldSize),B2(kWorldSize),B3(kWorldSize)}},
    // 0xEF180, the init: lea rax,[rdi+0x80]; mov ecx,0x80; the 8-store loop and one store after it.
    {0xEF5C8,7,3,Kind::ZeroBase,{0x48,0x8D,0x87,0x80,0,0,0},{0x48,0x8D,0x87,B0(kTable),B1(kTable),B2(kTable),B3(kTable)}},
    {0xEF5CF,5,1,Kind::ZeroCount,{0xB9,0x80,0,0,0},
        {0xB9,B0(kZeroIterations),B1(kZeroIterations),B2(kZeroIterations),B3(kZeroIterations)}},
    // Every slot access (tests/aiworld_capacity_audit.py proves there is no other).
    AIW_SLOT8(0xF1526,0x48,0x8B,0xB4,0xC8),    // mov rsi,[rax+rcx*8+0x80]: the cost modifier's agent (world = helper+0x18)
    AIW_SLOT8(0xF1F54,0x48,0x89,0x8C,0xDF),    // mov [rdi+rbx*8+0x80],rcx: register (0xF1EF0)
    AIW_SLOT8(0xF260C,0x48,0x89,0x8C,0xDE),    // mov [rsi+rbx*8+0x80],rcx: create and register (0xF2540)
    AIW_SLOT12(0xF3766,0x48,0xC7,0x84,0xDF),   // mov qword [rdi+rbx*8+0x80],0: unregister (0xF3740)
    AIW_SLOT12(0xF380E,0x48,0xC7,0x84,0xEE),   // mov qword [rsi+rbp*8+0x80],0: unregister (0xF37D0)
    AIW_SLOT8(0xF3B3D,0x4C,0x89,0xB4,0xEE),    // mov [rsi+rbp*8+0x80],r14: the recycle loop (0xF3AA0)
    AIW_SLOT8(0xF3E90,0x4C,0x8B,0x84,0xC1),    // mov r8,[rcx+rax*8+0x80]: vtable slot 5 (0xF3E60)
    // ai::World::AvoidanceFilter (vtable 0x1768A18, world = filter+0x18, made at 0xEF43E, kept at World+0x20D8):
    // slots 3 and 4 are leaf functions without .pdata, each reading both agents of a pair by their ids.
    AIW_SLOT8(0xF4168,0x49,0x8B,0x94,0xC1),    // mov rdx,[r9+rax*8+0x80]: slot 3 (0xF4160), first agent
    AIW_SLOT8(0xF4175,0x49,0x8B,0x8C,0xC1),    // mov rcx,[r9+rax*8+0x80]: slot 3, second agent
    AIW_SLOT8(0xF41C4,0x4C,0x8B,0x84,0xC1),    // mov r8,[rcx+rax*8+0x80]: slot 4 (0xF41B0), first agent
    AIW_SLOT8(0xF41EA,0x48,0x8B,0x8C,0xC1),    // mov rcx,[rcx+rax*8+0x80]: slot 4, second agent
    // 0xEF180: mov edi,0x401 bounds the free-id fill (and its reserve): ids 1..0xFFFE.
    {0xEF613,5,1,Kind::IdBound,{0xBF,0x01,0x04,0,0},{0xBF,B0(kIdBound),B1(kIdBound),B2(kIdBound),B3(kIdBound)}},
};
#undef AIW_SLOT8
#undef AIW_SLOT12
constexpr std::size_t kSiteCount=sizeof(kSites)/sizeof(kSites[0]);
}  // namespace crew::aiworld
