// The firing weapon's model in a DemoIndirectFire's round (jet_bay.cpp EmcFire; docs/feedback-2026-10-10-proteus-shield-crash.md).
// Static RE on EDF.dll TimeDateStamp 0x678CCB46 (H unless said):
//  - A bullet is made from an InitParam (the IFC's at IFC +0x40: the factory call 0x2B9F55 passes r9 = IFC + 0x40; a
//    weapon's at weapon +0x800). Its +0x1B0 is an sgs::ut variant (16 bytes of value, its alternative at +0x10, 0xFFFF
//    valueless) that the InitParam ctor leaves valueless (0x100321 mov word [rbx+0x1C0],di) and only a weapon fills:
//    the weapon init copies its SGO's `animation_model` there (0x68DAD5 lea rcx,[rsi+0x9B0] = 0x800 + 0x1B0, then
//    0x244CB0). The IFC's config 0x2B5F40 never writes IFC +0x1F0 / +0x200.
//  - BarrierBullet01's ctor builds its device model from it (0x28FCEC lea r8,[rsi+0x1B0]; 0x28FD02 call 0x6BB890), and
//    0x6BB890 throws sgs::ut::InvalidVariantException (0x6BB927 -> 0x6BF03F _CxxThrowException) on a valueless one.
//    Nothing catches it: an IFC firing a BarrierBullet01 kills the game (test hub report #6: the throw at EDF+6BF044,
//    returning to EDF+28FD07 / 28F758 / 11942C3 / 2B9F6D (the IFC's fire) / 5B5D67 (the DemoIndirectFire's update)).
//  - A DemoIndirectFire keeps its SGO's root at +0x100 (its ctor reads indirect_fire_param through it: 0x5B56F9 lea
//    rbx,[rdi+0x100]) and looks members up with the variant's own visitors, a table entry per alternative: find
//    (image +0x179EBA8, request {name, index out}: 0x5B5717..0x5B5736) and get (image +0x179EAF0, request {variant out,
//    index}: 0x5B573F..0x5B5777). For a node reference (alternative 2) get writes {document, member} as alternative 2
//    into the out variant, freeing what was there (0x2390D0); alternative 2 needs no destructor (0x89940 ret).
// CarryModel does for the round what the weapon init does for a weapon's rounds: the DemoIndirectFire's own SGO's
// `animation_model` into its IFC's InitParam +0x1B0, before the IFC fires (its step, later in the frame).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::ifcmodel {
constexpr unsigned kVariantFind=0x179EBA8,kVariantGet=0x179EAF0;   // visitor tables, one entry per alternative
constexpr unsigned kVariantAlternatives=3;                         // the tables' code entries (the 4th is data)
constexpr std::size_t kVariantAt=0x10;                             // the alternative (std::uint16_t)
constexpr std::uint16_t kValueless=0xFFFF;
constexpr std::size_t kDemoSgo=0x100;                              // a DemoIndirectFire's SGO root (a variant)
constexpr std::size_t kIfcModel=0x40+0x1B0;                        // IFC: its InitParam's model (a variant)
constexpr wchar_t kModelMember[]=L"animation_model";

struct Find { const wchar_t* name; std::int32_t index; };
struct Get { void* out; std::int32_t index; };
using VisitFn=void(__fastcall*)(const void* variant,void* request);

enum class Carry { done, noSgo, noMember, slotTaken, notMade };

inline std::uint16_t AlternativeOf(const unsigned char* variant) noexcept {
    std::uint16_t a;
    std::memcpy(&a,variant+kVariantAt,sizeof(a));
    return a;
}
inline VisitFn Visitor(const unsigned char* base,unsigned table,std::uint16_t alternative) noexcept {
    VisitFn f;
    std::memcpy(&f,base+table+static_cast<std::size_t>(alternative)*8,sizeof(f));
    return f;
}

// `sgo`: the DemoIndirectFire's SGO root; `slot`: its IFC's InitParam +0x1B0 (still valueless: the IFC has not fired).
inline Carry CarryModel(const unsigned char* base,const unsigned char* sgo,unsigned char* slot) noexcept {
    const std::uint16_t root=AlternativeOf(sgo);
    if(root>=kVariantAlternatives)return Carry::noSgo;
    if(AlternativeOf(slot)!=kValueless)return Carry::slotTaken;
    Find find{kModelMember,-1};
    Visitor(base,kVariantFind,root)(sgo,&find);
    if(find.index<0)return Carry::noMember;
    Get get{slot,find.index};
    Visitor(base,kVariantGet,root)(sgo,&get);
    return AlternativeOf(slot)<kVariantAlternatives ? Carry::done : Carry::notMade;
}

inline const char* CarryText(Carry c) noexcept {
    switch(c) {
        case Carry::done: return "carried";
        case Carry::noSgo: return "the round's SGO is not readable";
        case Carry::noMember: return "its SGO has no animation_model (an older install: run the installer)";
        case Carry::slotTaken: return "its IFC already holds a model";
        case Carry::notMade: return "the model was not copied";
    }
    return "?";
}
}  // namespace crew::ifcmodel
