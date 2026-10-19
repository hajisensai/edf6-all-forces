// Per-shot native owner derivation; no Vehicle ABI or speculative team-cache writes.
#include "enemy_control_pvp_native.h"
#include "enemy_control_pvp_tree.h"
#include "enemy_control_pvp_boss.h"
#include "enemy_control_pvp_transport.h"
#include "enemy_control_pvp_berserker.h"
#include "enemy_control_walking_parts.h"
#include <cstring>
#include <limits>

namespace crew {
bool EnemyControlOwnsTargetSelector(const void* root,const void* function) noexcept;
namespace native_pvp_detail {
using controlled_pvp::Emitter;using controlled_pvp::EmitterKind;using controlled_pvp::Life;
using enemy_native::NativeProfile;using enemy_native::NativeLease;using enemy_native::Kind;
constexpr NativeProfile kProfiles[]={
#include "enemy_control_native_profiles.inc"
};
struct Anchor { std::uint32_t rva;unsigned char bytes[16];unsigned size; };
constexpr Anchor kAnchors[]={
    {0x4A0239,{0x4C,0x8B,0x41,0x20},4},
    {0x4A0272,{0x83,0xBA,0x30,0x0C,0x00,0x00,0x01},7},
    {0x4A027B,{0x8B,0x82,0x34,0x0C,0x00,0x00},6},
    {0x4A028E,{0xFF,0x50,0x60},3},
    {0x4A0293,{0x48,0x8B,0x51,0x18},4},
    {0x4A1027,{0x48,0x8D,0x8F,0x60,0x03,0x00,0x00},7},
    {0x49D19E,{0x41,0x8D,0x4D,0x28},4},
    {0x49D30B,{0x4C,0x89,0x3B},3},
    {0x6D3190,{0x49,0x8B,0x95,0xB8,0x00,0x00,0x00},7},
    {0x6D31BD,{0xFF,0x50,0x60},3},
    {0x6D34BE,{0x83,0xBB,0x30,0x0C,0x00,0x00,0x01},7},
    {0x6D34E4,{0x49,0x89,0x8D,0xB0,0x00,0x00,0x00},7},
    {0x6D34F2,{0x49,0x89,0x85,0xB8,0x00,0x00,0x00},7},
    {0x232015,{0x8B,0x83,0x00,0x0A,0x00,0x00},6},
    {0x232023,{0x89,0x83,0x54,0x07,0x00,0x00},6},
    {0x3E72D5,{0x41,0x8B,0x80,0x18,0x03,0x00,0x00},7},
    {0x3E72DC,{0x89,0x85,0xA0,0x00,0x00,0x00},6},
    {0x39CFB9,{0x49,0x89,0x86,0x10,0x1C,0x00,0x00},7},
    {0x39CFC7,{0x49,0x89,0x9E,0x18,0x1C,0x00,0x00},7},
    {0x39D17C,{0x49,0x89,0x86,0x20,0x1C,0x00,0x00},7},
    {0x39D18A,{0x49,0x89,0x9E,0x28,0x1C,0x00,0x00},7},
    {0x3A93FD,{0xE8,0x8E,0x79,0x32,0x00},5},
    {0x3A940B,{0x48,0x89,0x03},3},
    {0x3A9412,{0x48,0x89,0x73,0x08},4},
    {0x3A9641,{0xE8,0x4A,0x77,0x32,0x00},5},
    {0x3A964E,{0x48,0x89,0x86,0xF0,0x00,0x00,0x00},7},
    {0x3A965C,{0x48,0x89,0xBE,0xF8,0x00,0x00,0x00},7},
    {0x3AA4A5,{0x48,0x8B,0x8B,0xF0,0x00,0x00,0x00},7},
    {0x3AA4AC,{0xE8,0x0F,0xAD,0x32,0x00},5},
    {0x3DE93A,{0x48,0x8B,0x99,0xA8,0x13,0x00,0x00},7},
    {0x3DFF18,{0x48,0x89,0x01},3},
    {0x3E6151,{0x48,0x89,0x11},3},
    {0x3E72D2,{0x4C,0x8B,0x03},3},
    {0x3E7322,{0x49,0x8B,0x40,0x30},4},
    {0x3E7347,{0x48,0x89,0x55,0x50},4},
    {0x3292BC,{0x49,0x89,0x87,0xA8,0x07,0x00,0x00},7},
    {0x3292C3,{0x49,0x89,0x9F,0xB0,0x07,0x00,0x00},7},
    {0x3295FA,{0x4D,0x8B,0xCF},3},
    {0x329603,{0x49,0x8B,0x8F,0xB0,0x07,0x00,0x00},7},
    {0x32960A,{0xE8,0x61,0x79,0x3C,0x00},5},
    {0x6F0F98,{0x4C,0x89,0x49,0x08},4},
    {0x6F119C,{0x49,0x89,0x4E,0x40},4},
    {0x6F11A4,{0x49,0x89,0x56,0x48},4},
    {0x3BA850,{0x48,0x89,0x87,0x50,0x15,0x00,0x00},7},
    {0x3C4DED,{0x49,0x89,0x86,0xE8,0x00,0x00,0x00},7},
    {0x3C4375,{0x48,0x89,0x03},3},
    {0x3D1691,{0x48,0x89,0x87,0xA8,0x11,0x00,0x00},7},
    {0x3DA401,{0x4C,0x89,0x7F,0x78},4},
    {0x3DA418,{0x48,0x8B,0x87,0x88,0x00,0x00,0x00},7},
    {0x3DA4F5,{0x48,0x89,0x04,0xDE},4},
    {0x6CF937,{0x48,0x8B,0x02},3},
    {0x6CF93A,{0x44,0x8B,0x88,0x14,0x03,0x00,0x00},7},
    {0x6CF941,{0x44,0x89,0x89,0xB8,0x02,0x00,0x00},7},
    {0x6D08DE,{0xFF,0x50,0x08},3},
    {0x32848C,{0x48,0x8B,0x8E,0xB0,0x07,0x00,0x00},7},
    {0x328493,{0xE8,0x18,0x91,0x3C,0x00},5},
    {0x32DAED,{0xE8,0x1E,0xA9,0xFF,0xFF},5},
    {0x32DBE1,{0x8B,0x83,0x14,0x03,0x00,0x00},6},
    {0x32DBE7,{0x89,0x83,0x54,0x0A,0x00,0x00},6},
    {0x6F0D89,{0x48,0x8D,0x05,0x98,0x72,0x0F,0x01},7},
    {0x32B210,{0x48,0x89,0x5C,0x24,0x08},5},
    {0x3248B6,{0x49,0x8B,0x56,0x08},4},
    {0x118AF35,{0x48,0x89,0x5F,0x38},4},
    {0x118AF48,{0x14,0x55},2},
    {0x118AF70,{0x48,0x89,0x39},3},
    {0x339298,{0x48,0x89,0x8F,0xE8,0x15,0x00,0x00},7},
    {0x3392A6,{0x48,0x89,0x97,0xF0,0x15,0x00,0x00},7},
    {0x40D02C,{0x48,0x89,0x8F,0xB8,0x16,0x00,0x00},7},
    {0x40D03A,{0x48,0x89,0x97,0xC0,0x16,0x00,0x00},7},
    {0x4228FB,{0x48,0x89,0xBB,0x08,0x17,0x00,0x00},7},
    {0x422909,{0x48,0x89,0x93,0x10,0x17,0x00,0x00},7},
    {0x43B469,{0x48,0x89,0x8F,0xD8,0x1A,0x00,0x00},7},
    {0x43B477,{0x48,0x89,0x97,0xE0,0x1A,0x00,0x00},7},
    {0x4E2780,{0x48,0x89,0x8B,0x08,0x11,0x00,0x00},7},
    {0x4E278E,{0x48,0x89,0x93,0x10,0x11,0x00,0x00},7},
    {0x33AF32,{0x8B,0x87,0x14,0x03,0x00,0x00},6},
    {0x33AF38,{0x89,0x85,0xF0,0x00,0x00,0x00},6},
    {0x40AFCE,{0x8B,0x87,0x14,0x03,0x00,0x00},6},
    {0x40AFDB,{0x89,0x87,0x10,0x17,0x00,0x00},6},
    {0x4246DC,{0x8B,0x86,0x14,0x03,0x00,0x00},6},
    {0x4246E2,{0x89,0x85,0xE0,0x00,0x00,0x00},6},
    {0x436269,{0x8B,0x83,0x14,0x03,0x00,0x00},6},
    {0x436276,{0x89,0x83,0x30,0x1B,0x00,0x00},6},
    {0x43D494,{0x8B,0x83,0x14,0x03,0x00,0x00},6},
    {0x43D4A1,{0x89,0x83,0x30,0x1B,0x00,0x00},6},
    {0x4E3686,{0x8B,0x81,0x14,0x03,0x00,0x00},6},
    {0x4E368C,{0x89,0x81,0x60,0x11,0x00,0x00},6},
    {0x4A8BDC,{0x8B,0x87,0x54,0xFD,0xFF,0xFF},6},
    {0x4A8BE2,{0x89,0x87,0x94,0x09,0x00,0x00},6},
    {0x4A802D,{0x48,0x89,0x8F,0x40,0x0F,0x00,0x00},7},
    {0x4A803B,{0x48,0x89,0x97,0x48,0x0F,0x00,0x00},7},
    {0x6F0EB2,{0x4C,0x89,0x41,0x08},4},
    {0x6F165A,{0x48,0x8B,0x41,0x08},4},
    {0x6F165E,{0x44,0x8B,0x90,0x14,0x03,0x00,0x00},7},
    {0x6F1665,{0x44,0x89,0x51,0x54},4},
    {0x6D107B,{0x48,0x89,0x8E,0xB0,0x02,0x00,0x00},7},
    {0x6D2A46,{0x8B,0x83,0x18,0x03,0x00,0x00},6},
    {0x6D2A4C,{0x89,0x85,0x10,0x02,0x00,0x00},6},
    {0x367E19,{0x41,0x8B,0x86,0x18,0x03,0x00,0x00},7},
    {0x367E20,{0x89,0x45,0x84},3},
    {0x429DA5,{0x41,0x8B,0x86,0x14,0x03,0x00,0x00},7},
    {0x429DAC,{0x89,0x46,0x24},3},
    {0x6C8183,{0x4C,0x89,0x01},3},
    {0x6C8AF0,{0x49,0x8B,0x06},3},
    {0x470F98,{0x48,0x8B,0xB7,0x20,0x11,0x00,0x00,0x4C,0x8B,0xBF,0x30,0x11,0x00,0x00},14},
    {0x4A4BC0,{0x48,0x89,0x91,0xB0,0x05,0x00,0x00},7},
    {0x4A0392,{0x48,0x8B,0x87,0xB0,0x05,0x00,0x00,0x8B,0x88,0x18,0x03,0x00,0x00},13},
    {0x4A039F,{0x89,0x8F,0x10,0x02,0x00,0x00},6},
    {0x45B491,{0x48,0x8B,0x83,0xB0,0x05,0x00,0x00,0x8B,0x90,0x18,0x03,0x00,0x00},13},
    {0x45B49E,{0x89,0x93,0x78,0x12,0x00,0x00},6},
    {0x4AAFA8,{0x48,0x89,0x8E,0x68,0x1B,0x00,0x00},7},
    {0x4AAFB6,{0x48,0x89,0x96,0x70,0x1B,0x00,0x00},7},
    {0x4ACE1D,{0x8B,0x83,0x14,0x03,0x00,0x00},6},
    {0x4ACE2A,{0x89,0x83,0xC0,0x1B,0x00,0x00},6},
    {0x4B1755,{0x49,0x89,0x95,0xA8,0x1A,0x00,0x00},7},
    {0x4B1763,{0x4D,0x89,0x85,0xB0,0x1A,0x00,0x00},7},
    {0x4B265B,{0x41,0x8B,0x87,0x14,0x03,0x00,0x00},7},
    {0x4B2662,{0x41,0x89,0x87,0x00,0x1B,0x00,0x00},7},
    {0x321BFD,{0xE8,0x0E,0xE3,0x3A,0x00},5},
    {0x31CD0A,{0x48,0x8D,0x8F,0x00,0x01,0x00,0x00},7},
    {0x31CD29,{0xE8,0x32,0x2F,0x3B,0x00},5},
    {0x313037,{0x48,0x89,0x11,0x4C,0x89,0x41,0x08},7},
    {0x30EB02,{0x48,0x8D,0x8B,0xD0,0x10,0x00,0x00,0x49,0x8B,0xD6,0xE8,0x7F,0x5B,0x00,0x00},15},
    {0x314730,{0x48,0x8B,0x06,0x8B,0x88,0x14,0x03,0x00,0x00,0x89,0x8B,0x68,0xFF,0xFF,0xFF},15},
    {0x52AF03,{0x48,0x8B,0x43,0x30},4},
    {0x52AF1D,{0x48,0x89,0x8B,0x38,0x07,0x00,0x00},7},
    {0x52AF2B,{0x48,0x89,0x93,0x40,0x07,0x00,0x00},7},
    {0x52C6E5,{0x8B,0x86,0x14,0x03,0x00,0x00},6},
    {0x52C6EE,{0x89,0x86,0x90,0x07,0x00,0x00},6},
    {0x52C8AF,{0x4C,0x8D,0x8E,0x00,0x07,0x00,0x00},7},
    {0x52C9B1,{0xE8,0xCA,0x78,0xC6,0x00},5},
    {0x535975,{0x48,0x69,0xB9,0xA8,0x12,0x00,0x00,0xB0,0x01,0x00,0x00},11},
    {0x535980,{0x48,0x8B,0x99,0x98,0x12,0x00,0x00},7},
    {0x539859,{0xE8,0xB2,0x66,0x19,0x00},5},
    {0x6CFF47,{0x48,0x89,0x4E,0x10},4},
    {0x6CFF4F,{0x48,0x89,0x56,0x18},4},
    {0x6D0602,{0x4C,0x8B,0x49,0x18},4},
    {0x6D067C,{0x8B,0x91,0x14,0x03,0x00,0x00},6},
    {0x6D0682,{0x89,0x90,0x90,0x00,0x00,0x00},6},
};
bool Ready() noexcept {
    if(!image)return false;
    for(const auto& a:kAnchors)
        if(!Readable(image+a.rva,a.size) || std::memcmp(image+a.rva,a.bytes,a.size)!=0)return false;
    return true;
}
bool Same(const ObjRef& a,const ObjRef& b) noexcept {return a.obj==b.obj && a.ctrl==b.ctrl;}
bool Team(int value) noexcept {return value>=-1 && value<7;}
bool Derived(EmitterKind kind) noexcept {return kind==EmitterKind::liveOwnerTeam || kind==EmitterKind::liveOwnerLastTeam;}
bool Cache(EmitterKind kind) noexcept {return kind==EmitterKind::ownerTeamCache || kind==EmitterKind::ownerLastTeamCache;}
bool Supported(const NativeProfile& p) noexcept {
    switch(p.vtableRva) {
    case 0x17CC2A8:case 0x17CC838:case 0x17A9650:case 0x17AA648:
    case 0x17C4D70:case 0x17C0BD0:case 0x17C24B0:case 0x17C5628:case 0x17C5A60:
    case 0x17ABA40:case 0x17BB798:case 0x17BC648:case 0x17BCEE0:case 0x17BD638:
    case 0x17BE120:case 0x17BE7E0:case 0x17BDAC0:case 0x17C84C8:
    case 0x17AFEE0:case 0x17B6270:case 0x17B6CA0:case 0x17B8100:case 0x17B5370:case 0x17B8898:
    case 0x17BEC68:case 0x17B93C0:case 0x17C94D8:case 0x17C9DC0:
    case 0x17ADA78:case 0x17AE088:case 0x17AE968:case 0x17AED50:case 0x17AF200:case 0x17AF9F8:return true;
    default:return false;
    }
}
bool ProofReady(const NativeProfile& p) noexcept {
    if(p.vtableRva==0x17BEC68)return EnemyControlBossPvpReady();
    if(p.inputRva==0x343DD0)return EnemyControlBerserkerPvpReady() && enemy_native::WalkingPartsControlReady();
    if(p.vtableRva==0x17B93C0 || p.vtableRva==0x17C94D8 || p.vtableRva==0x17C9DC0)return EnemyControlTransportPvpReady();
    return Supported(p);
}
// Core clones slots 4,5,7 and one exact verified selector at 14; COL is preserved. All other
// entries, exact RTTI name and the original input entry remain native evidence.
const NativeProfile* Class(const ObjRef& ref) noexcept {
    if(!enemy_pvp_native::tree_detail::LiveIdentity(ref) || At<const void*>(ref.obj,kSelf)!=ref.obj || !Ready())return nullptr;
    const auto* table=At<const unsigned char*>(ref.obj,0);
    if(reinterpret_cast<std::uintptr_t>(table)<sizeof(void*) || !Readable(table-8,8))return nullptr;
    const auto* col=At<const unsigned char*>(table-8,0);
    for(const auto& p:kProfiles) {
        const auto* original=image+p.vtableRva;
        if(!Readable(original-8,(static_cast<std::size_t>(p.slots)+1)*8) ||
           At<const void*>(original-8,0)!=col)continue;
        if(!Readable(col,24) || At<std::uint32_t>(col,0)!=1 || At<std::uint32_t>(col,4)!=0 ||
           At<std::uint32_t>(col,8)!=0 || image+At<std::uint32_t>(col,20)!=col)return nullptr;
        const auto* descriptor=image+At<std::uint32_t>(col,12);
        const auto length=std::strlen(p.name);
        if(!Readable(descriptor+16,length+7) || std::memcmp(descriptor+16,".?AV",4)!=0 ||
           std::memcmp(descriptor+20,p.name,length)!=0 || std::memcmp(descriptor+20+length,"@@",3)!=0 ||
           !Readable(table,static_cast<std::size_t>(p.slots)*8) ||
           At<const void*>(original,static_cast<std::size_t>(p.inputSlot)*8)!=image+p.inputRva)return nullptr;
        for(unsigned slot=0;slot<p.slots;++slot) {
            const auto* current=At<const void*>(table,static_cast<std::size_t>(slot)*8);
            if(slot!=4 && slot!=5 && slot!=7 && current!=At<const void*>(original,static_cast<std::size_t>(slot)*8) &&
               !(slot==14 && EnemyControlOwnsTargetSelector(ref.obj,current)))return nullptr;
        }
        return &p;
    }
    return nullptr;
}
// These native children are typed non-controllable parts, not input profiles.
struct FootType {std::uint32_t vtable,input;const char* name;std::size_t bytes;};
constexpr FootType kFeet[]={
    {0x17AAD48,0x328410,"DeiroiFootParts_Base",0x7C0},
    {0x17AB118,0x32B210,"DeiroiFootParts_Battery",0xD22},
    {0x17AB4B0,0x328410,"DeiroiFootParts_Joint",0x7C0},
    {0x17AB770,0x32DAE0,"DeiroiFootParts_Toe",0xA60},
};
const FootType* DeiroiFootType(const ObjRef& ref) noexcept {
    if(!enemy_pvp_native::tree_detail::LiveIdentity(ref) || At<const void*>(ref.obj,kSelf)!=ref.obj)return nullptr;
    for(const auto& type:kFeet) {
        if(At<const void*>(ref.obj,0)!=image+type.vtable)continue;
        if(!Readable(ref.obj,type.bytes) || !Readable(image+type.vtable-8,49*8) ||
           At<const void*>(image+type.vtable,5*8)!=image+type.input ||
           At<const void*>(image+type.vtable,7*8)!=image+0x54A000)return nullptr;
        const auto* col=At<const unsigned char*>(image+type.vtable-8,0);
        if(!Readable(col,24) || At<std::uint32_t>(col,0)!=1 || At<std::uint32_t>(col,4)!=0 ||
           At<std::uint32_t>(col,8)!=0 || image+At<std::uint32_t>(col,20)!=col)return nullptr;
        const auto* descriptor=image+At<std::uint32_t>(col,12);const auto length=std::strlen(type.name);
        if(!Readable(descriptor+16,length+7) || std::memcmp(descriptor+16,".?AV",4)!=0 ||
           std::memcmp(descriptor+20,type.name,length)!=0 || std::memcmp(descriptor+20+length,"@@",3)!=0)return nullptr;
        if(type.vtable==0x17AB118 && At<int>(ref.obj,0xB30)!=0 && At<int>(ref.obj,0xB30)!=1)return nullptr;
        return &type;
    }
    return nullptr;
}
bool DeiroiFoot(const ObjRef& ref) noexcept {return DeiroiFootType(ref)!=nullptr;}

bool SceneContains(const ObjRef& root,const void* child) noexcept {
    if(!Readable(root.obj,0x60))return false;
    const auto* entries=At<const unsigned char*>(root.obj,0x48);
    const auto count=At<std::size_t>(root.obj,0x58),capacity=At<std::size_t>(root.obj,0x50);
    if(count>4096 || count>capacity || (count && !Readable(entries,count*8)))return false;
    for(std::size_t i=0;i<count;++i)if(At<const void*>(entries,i*8)==child)return true;
    return false;
}
bool AddSubtree(const ObjRef& child,ObjRef* members,unsigned capacity,unsigned& count) noexcept {
    ObjRef tree[controlled_pvp::kMaxMembers]{};unsigned total=0;
    if(!enemy_pvp_native::CollectTree(child,tree,controlled_pvp::kMaxMembers,&total))return false;
    for(unsigned i=0;i<total;++i) {
        bool seen=false;
        for(unsigned j=0;j<count;++j)if(members[j].obj==tree[i].obj) {
            if(!Same(members[j],tree[i]))return false;seen=true;break;
        }
        if(!seen){if(count==capacity)return false;members[count++]=tree[i];}
    }
    return true;
}
bool DeiroiParts(const ObjRef& root,ObjRef* members,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(root.obj);
    if(!Readable(object+0xAE8,0x18))return false;
    const auto legs=At<std::size_t>(object,0xAF8),legCapacity=At<std::size_t>(object,0xAF0);
    const auto* rows=At<const unsigned char*>(object,0xAE8);
    if(legs>32 || legs>legCapacity || (legs && !Readable(rows,legs*0x40)))return false;
    unsigned found=0;
    for(std::size_t leg=0;leg<legs;++leg) {
        const auto* controller=At<const unsigned char*>(rows+leg*0x40,0x28);
        if(!Readable(controller,0x28) || At<const void*>(controller,0)!=root.obj)return false;
        const auto* entries=At<const unsigned char*>(controller,0x10);
        const auto parts=At<std::size_t>(controller,0x20),partCapacity=At<std::size_t>(controller,0x18);
        if(parts>128 || parts>partCapacity || (parts && !Readable(entries,parts*16)))return false;
        for(std::size_t part=0;part<parts;++part) {
            const auto* node=At<const unsigned char*>(entries+part*16,8);if(!Readable(node,0x28))return false;
            const auto* child=At<const unsigned char*>(node,8);if(!child)continue;
            if(!SceneContains(root,child))continue;
            if(!Readable(child,kSelfCtrl+8))return false;
            const ObjRef ref{child,At<const void*>(child,kSelfCtrl)};
            if(!Readable(ref.ctrl,16))return false;
            if(At<LONG>(ref.ctrl,8)<=0)continue;
            if(!DeiroiFoot(ref) || At<const void*>(child,0x38)!=root.obj || At<const void*>(child,0x5C0)!=root.obj ||
               ++found>256 || !AddSubtree(ref,members,capacity,count))return false;
        }
    }
    return true;
}
bool LeaseMatches(const NativeLease& lease,const ObjRef& ref) noexcept {
    if(lease.object!=ref.obj || lease.control!=ref.ctrl || !Readable(lease.profile,sizeof(NativeProfile)))return false;
    const auto* actual=Class(ref);if(!actual || !Supported(*actual) || !ProofReady(*actual))return false;
    const auto& p=*lease.profile;
    return p.vtableRva==actual->vtableRva && p.inputRva==actual->inputRva && p.aiRva==actual->aiRva &&
        p.profileHash==actual->profileHash && p.slots==actual->slots && p.buttonMask==actual->buttonMask &&
        p.kind==actual->kind && p.axes==actual->axes && p.inputSlot==actual->inputSlot &&
        Readable(p.name,std::strlen(actual->name)+1) && std::strcmp(p.name,actual->name)==0;
}
bool WeakOwner(const unsigned char* emitter,std::size_t at,const ObjRef& member) noexcept {
    return Readable(emitter+at,16) && At<const void*>(emitter,at)==member.obj &&
        At<const void*>(emitter,at+8)==member.ctrl;
}
bool Append(const ObjRef& owner,const void* object,EmitterKind kind,std::uint64_t token,
            Emitter* out,unsigned capacity,unsigned& count) noexcept {
    if(!object || !token || !Derived(kind))return false;
    for(unsigned i=0;i<count;++i)if(out[i].object==object)
        return out[i].kind==kind && Same(out[i].member,owner) && out[i].incarnation==token;
    if(count==capacity)return false;
    out[count++]={kind,owner,object,token};return true;
}
bool InlineInit(const ObjRef& member,std::size_t init,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);
    if(!Readable(object+init,0x1B0) || !Readable(object+init-0x10,8) || !WeakOwner(object,init+0x38,member))return false;
    const auto* resource=At<const void*>(object,init-0x10);
    return resource && Readable(resource,1) && Append(member,object+init,EmitterKind::liveOwnerTeam,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(resource)),out,capacity,count);
}
// Insect templates are not resource+Init pairs (IP-10 can be an int/float).
// This binding is the stable inline template's owner weak lifetime, not its data.
bool OwnedInit(const ObjRef& member,std::size_t init,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);
    return Readable(object+init,0x1B0) && WeakOwner(object,init+0x38,member) &&
        Append(member,object+init,EmitterKind::liveOwnerTeam,
               static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(member.ctrl)),out,capacity,count);
}
bool BodyMethod(const ObjRef& member,EmitterKind kind,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    return Append(member,member.obj,kind,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(member.ctrl)),out,capacity,count);
}
bool InsectContact(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);
    return Readable(object+0xF30,0x50) && WeakOwner(object,0xF40,member) &&
        Append(member,object+0xF30,EmitterKind::liveOwnerTeam,
               static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(member.ctrl)),out,capacity,count);
}
bool GenericContact(const ObjRef& member,const unsigned char* contact,std::uint64_t token,
                    Emitter* out,unsigned capacity,unsigned& count) noexcept {
    return Readable(contact,0x50) && At<const void*>(contact,0)==image+0x17E8028 &&
        At<const void*>(contact,8)==member.obj && WeakOwner(contact,0x40,member) &&
        Append(member,contact,EmitterKind::liveOwnerTeam,token,out,capacity,count);
}
bool SharedContact(const ObjRef& member,std::size_t at,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);if(!Readable(object+at,16))return false;
    const auto* contact=At<const unsigned char*>(object,at);const auto* control=At<const void*>(object,at+8);
    if(!contact && !control)return true;
    return contact && control && Readable(control,16) && At<LONG>(control,8)>0 &&
        GenericContact(member,contact,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control)),out,capacity,count);
}
bool BulletType(const unsigned char* bullet) noexcept {
    const auto* table=At<const unsigned char*>(bullet,0);
    if(reinterpret_cast<std::uintptr_t>(table)<8 || !Readable(table-8,8))return false;
    const auto* col=At<const unsigned char*>(table-8,0);
    if(!Readable(col,24) || At<std::uint32_t>(col,0)!=1 || At<std::uint32_t>(col,4)!=0 ||
       At<std::uint32_t>(col,8)!=0 || image+At<std::uint32_t>(col,20)!=col)return false;
    const auto* hierarchy=image+At<std::uint32_t>(col,16);if(!Readable(hierarchy,16))return false;
    const auto total=At<std::uint32_t>(hierarchy,8);const auto* array=image+At<std::uint32_t>(hierarchy,12);
    if(!total || total>128 || !Readable(array,static_cast<std::size_t>(total)*4))return false;
    for(unsigned i=0;i<total;++i) {
        const auto* base=image+At<std::uint32_t>(array,static_cast<std::size_t>(i)*4);
        if(!Readable(base,24))return false;
        if(At<std::uint32_t>(base,0)==0x2022E80 && At<std::int32_t>(base,8)==0 && At<std::int32_t>(base,12)==-1)return true;
    }
    return false;
}
bool OwnedBeam(const ObjRef& owner,const unsigned char* bullet,const void* control,Emitter* out,unsigned capacity,unsigned& count,bool activeWeapon=false) noexcept {
    if(!control)return true;
    if(!Readable(control,16))return false;
    if(At<LONG>(control,8)<=0)return true; // retained weak proves destruction without reading old bullet storage
    if(!bullet || !Readable(bullet,0xC35) || At<const void*>(bullet,kSelf)!=bullet || At<const void*>(bullet,kSelfCtrl)!=control || !BulletType(bullet))return false;
    // 6D34BE only puts mode-1 persistent bullets in these keepalive slots.
    if(At<int>(bullet,0xC30)!=1 || (activeWeapon && (At<std::uint32_t>(bullet,0xC34)&(1u<<14))))return true;
    if(!WeakOwner(bullet,0xAE8,owner) || !WeakOwner(bullet,0x880,owner))return false;
    for(const auto at:{0xB40u,0x894u}) {
        const auto* field=bullet+at;if(!Team(At<int>(field,0)))return false;
        const auto token=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control));bool duplicate=false;
        for(unsigned i=0;i<count;++i)if(out[i].object==field) {
            if(out[i].kind!=EmitterKind::ownerLastTeamCache || !Same(out[i].member,owner) || out[i].incarnation!=token || !Same(out[i].storage,{bullet,control}))return false;
            duplicate=true;break;
        }
        if(!duplicate){if(count==capacity)return false;out[count++]={EmitterKind::ownerLastTeamCache,owner,field,token,{bullet,control}};}
    }
    return true;
}
bool BeamCache(const ObjRef& owner,const unsigned char* slot,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    return OwnedBeam(owner,At<const unsigned char*>(slot,0xB0),At<const void*>(slot,0xB8),out,capacity,count);
}
// GunBattery2 is a different native type: +10 is a float, owner is +2B0/+2B8.
bool BigBattery(const ObjRef& member,const unsigned char* unit,const void* control,
                Emitter* out,unsigned capacity,unsigned& count) noexcept {
    if(!unit && !control)return true;
    if(!unit || !control || !Readable(control,16) || At<LONG>(control,8)<=0 ||
       !Readable(unit,0x2C0) || !WeakOwner(unit,0x2B0,member))return false;
    const auto total=At<std::size_t>(unit,0x288),reserved=At<std::size_t>(unit,0x280);
    const auto* muzzles=At<const void*>(unit,0x278);
    if(total>reserved || total>(std::numeric_limits<std::size_t>::max)()/0xD0 ||
       (total && (!muzzles || !Readable(muzzles,total*0xD0))))return false;
    if(!Append(member,unit,EmitterKind::liveOwnerLastTeam,
               static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control)),out,capacity,count))return false;
    const auto* data=static_cast<const unsigned char*>(muzzles);
    for(std::size_t i=0;i<total;++i)if(!BeamCache(member,data+i*0xD0,out,capacity,count))return false;
    return true;
}
bool SharedBigBattery(const ObjRef& member,std::size_t at,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);
    return Readable(object+at,16) && BigBattery(member,At<const unsigned char*>(object,at),At<const void*>(object,at+8),out,capacity,count);
}
bool BigBatteryArray(const ObjRef& member,std::size_t at,std::size_t stride,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);if(!Readable(object+at,0x18))return false;
    const auto total=At<std::size_t>(object,at+0x10);if(total>capacity-count)return false;
    const auto* data=At<const unsigned char*>(object,at);
    if(total && (!data || !Readable(data,total*stride)))return false;
    for(std::size_t i=0;i<total;++i)
        if(!BigBattery(member,At<const unsigned char*>(data,i*stride),At<const void*>(data,i*stride+8),out,capacity,count))return false;
    return true;
}
bool SharedHelper(const unsigned char* object,std::size_t at,std::size_t bytes,const unsigned char** out) noexcept {
    if(!out || !Readable(object+at,16))return false;
    const auto* helper=At<const unsigned char*>(object,at);const auto* control=At<const void*>(object,at+8);
    if(!helper || !control || !Readable(control,16) || At<LONG>(control,8)<=0 || !Readable(helper,bytes))return false;
    *out=helper;return true;
}
bool MermanBatteries(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const unsigned char* weapon=nullptr;const unsigned char* poison=nullptr;
    return SharedHelper(static_cast<const unsigned char*>(member.obj),0x1C10,0x10,&weapon) &&
        SharedHelper(static_cast<const unsigned char*>(member.obj),0x1C20,0x100,&poison) &&
        BigBattery(member,At<const unsigned char*>(weapon,0),At<const void*>(weapon,8),out,capacity,count) &&
        BigBattery(member,At<const unsigned char*>(poison,0xF0),At<const void*>(poison,0xF8),out,capacity,count);
}
bool SquidArms(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);if(!Readable(object+0x13A8,0x18))return false;
    const auto total=At<std::size_t>(object,0x13B8),reserved=At<std::size_t>(object,0x13B0);
    const auto* data=At<const unsigned char*>(object,0x13A8);
    if(total>reserved || total>capacity-count || (total && (!data || !Readable(data,total*16))))return false;
    for(std::size_t i=0;i<total;++i) {
        const auto* arm=At<const unsigned char*>(data,i*16);const auto* control=At<const void*>(data,i*16+8);
        if(!arm || !control || !Readable(control,16) || At<LONG>(control,8)<=0 || !Readable(arm,0x1A0) || At<const void*>(arm,0)!=member.obj ||
           !Append(member,arm,EmitterKind::liveOwnerLastTeam,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control)),out,capacity,count))return false;
    }
    // arm+190/+198 are already-emitted BulletBase weak; launch teams stay immutable.
    return true;
}
bool ShellBattery(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const unsigned char* helper=nullptr;const unsigned char* gunControl=nullptr;
    return SharedHelper(static_cast<const unsigned char*>(member.obj),0x1550,0xF8,&helper) &&
        SharedHelper(helper,0xE8,0x10,&gunControl) &&
        BigBattery(member,At<const unsigned char*>(gunControl,0),At<const void*>(gunControl,8),out,capacity,count);
}
bool SpinnerBatteries(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const unsigned char* helper=nullptr;
    if(!SharedHelper(static_cast<const unsigned char*>(member.obj),0x11A8,0x90,&helper))return false;
    const auto total=At<std::size_t>(helper,0x88),reserved=At<std::size_t>(helper,0x80);
    const auto* data=At<const unsigned char*>(helper,0x78);
    if(total>reserved || total>capacity-count || (total && (!data || !Readable(data,total*0x30))))return false;
    for(std::size_t i=0;i<total;++i)
        if(!BigBattery(member,At<const unsigned char*>(data,i*0x30),At<const void*>(data,i*0x30+8),out,capacity,count))return false;
    return true;
}
bool SpiderBigBatteries(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);if(!Readable(object+0x23D8,16))return false;
    const auto* head=At<const unsigned char*>(object,0x23D8);const auto expected=At<std::size_t>(object,0x23E0);
    if(expected>capacity-count || !Readable(head,16))return false;
    const auto* node=At<const unsigned char*>(head,0);const auto* previous=head;std::size_t walked=0;
    while(node!=head) {
        if(walked>=expected || !Readable(node,0x48) || At<const void*>(node,8)!=previous ||
           !BigBattery(member,At<const unsigned char*>(node,0x38),At<const void*>(node,0x40),out,capacity,count))return false;
        ++walked;previous=node;node=At<const unsigned char*>(node,0);
    }
    return walked==expected && At<const void*>(head,8)==previous;
}
bool SmallBattery(const ObjRef& member,const unsigned char* battery,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    if(!Readable(battery,0x30) || !WeakOwner(battery,0x10,member))return false;
    const auto* maker=At<const unsigned char*>(battery,0x20);const auto* control=At<const void*>(battery,0x28);
    if(!maker || !Readable(maker,8))return false;
    const auto* table=At<const unsigned char*>(maker,0);
    const std::uint32_t fire=table==image+0x17E6EE0 ? 0x6CF7D0u : table==image+0x17E6F08 ? 0x6CF910u : 0u;
    if(!fire || !Readable(table,0x18) || At<const void*>(table,8)!=image+fire ||
       At<const void*>(table,0x10)!=image+(fire==0x6CF7D0 ? 0x6CFEF0u : 0x6CFF00u))return false;
    // Entity maker's extra +2B8 is also refreshed in its per-shot virtual Create.
    return control && Readable(control,16) && At<LONG>(control,8)>0 &&
        Append(member,battery,EmitterKind::liveOwnerTeam,
               static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control)),out,capacity,count);
}
bool SmallBatteries(const ObjRef& member,std::size_t dataAt,std::size_t stride,std::size_t offset,
                    Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);
    if(!Readable(object+dataAt,0x18))return false;
    const auto total=At<std::size_t>(object,dataAt+0x10);
    if(total>capacity-count)return false;
    const auto* data=At<const unsigned char*>(object,dataAt);
    if(total && (!data || !Readable(data,total*stride)))return false;
    for(std::size_t i=0;i<total;++i) {
        const auto* battery=data+i*stride+offset;
        if(!SmallBattery(member,battery,out,capacity,count))return false;
    }
    return true;
}
bool HumanoidBeams(const ObjRef& owner,const unsigned char* weapon,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    if(!Readable(weapon+0x368,16))return false;
    const auto* head=At<const unsigned char*>(weapon,0x368);const auto expected=At<std::size_t>(weapon,0x370);
    if(expected>controlled_pvp::kMaxEmitters)return false;
    if(!head)return expected==0;
    if(!Readable(head,16))return false;
    const auto* node=At<const unsigned char*>(head,0);const auto* previous=head;std::size_t walked=0;
    while(node!=head) {
        if(walked>=expected || !Readable(node,0x28) || At<const void*>(node,8)!=previous ||
           !OwnedBeam(owner,At<const unsigned char*>(node,0x18),At<const void*>(node,0x20),out,capacity,count,true))return false;
        ++walked;previous=node;node=At<const unsigned char*>(node,0);
    }
    return walked==expected && At<const void*>(head,8)==previous;
}
bool HumanoidWeapons(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);
    if(!Readable(object+0x1120,0x18))return false;
    const auto total=At<std::size_t>(object,0x1130);
    if(total>controlled_pvp::kMaxEmitters)return false;
    const auto* data=At<const unsigned char*>(object,0x1120);
    if(total && (!data || !Readable(data,total*16)))return false;
    for(std::size_t i=0;i<total;++i) {
        const auto* control=At<const void*>(data,i*16+8);
        if(!control)continue; // 470FBC..DE likewise never uses an expired weak entry
        if(!Readable(control,16))return false;
        if(At<LONG>(control,8)<=0)continue;
        const auto* weapon=At<const unsigned char*>(data,i*16);
        if(!weapon)continue;
        if(!Readable(weapon,0x5B8) || At<const void*>(weapon,0x5B0)!=member.obj)return false;
        if(!Append(member,weapon,EmitterKind::liveOwnerLastTeam,
                   static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control)),out,capacity,count) ||
           !HumanoidBeams(member,weapon,out,capacity,count))return false;
    }
    return true;
}
bool Emitters(const ObjRef& member,const NativeProfile& p,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(member.obj);
    if(!Supported(p))return false;
    switch(p.vtableRva) {
    case 0x17C4D70:case 0x17C0BD0:case 0x17C24B0:return HumanoidWeapons(member,out,capacity,count);
    case 0x17C5628:return InlineInit(member,0x1B30,out,capacity,count);
    case 0x17C5A60:return InlineInit(member,0x1A70,out,capacity,count);
    case 0x17AA648:return SmallBatteries(member,0x1638,0x290,0x100,out,capacity,count);
    case 0x17ABA40:return OwnedInit(member,0x15B0,out,capacity,count) && InsectContact(member,out,capacity,count);
    case 0x17BB798:return OwnedInit(member,0x1680,out,capacity,count) && InsectContact(member,out,capacity,count);
    case 0x17BC648:return OwnedInit(member,0x1680,out,capacity,count) && InsectContact(member,out,capacity,count) && SharedContact(member,0x2140,out,capacity,count);
    case 0x17BCEE0:return OwnedInit(member,0x16D0,out,capacity,count) && InsectContact(member,out,capacity,count);
    case 0x17BD638:return OwnedInit(member,0x16D0,out,capacity,count) && InsectContact(member,out,capacity,count) && SharedContact(member,0x2240,out,capacity,count);
    case 0x17BE120:return OwnedInit(member,0x1AA0,out,capacity,count) && InsectContact(member,out,capacity,count);
    case 0x17BE7E0:return OwnedInit(member,0x1AA0,out,capacity,count) && InsectContact(member,out,capacity,count) && SharedContact(member,0x23C0,out,capacity,count) && SpiderBigBatteries(member,out,capacity,count);
    case 0x17BDAC0:return InsectContact(member,out,capacity,count) && BodyMethod(member,EmitterKind::liveOwnerTeam,out,capacity,count);
    case 0x17C84C8:return OwnedInit(member,0x10D0,out,capacity,count); // shared team-6 SpiderNet is not an owned child
    case 0x17B5370:return MermanBatteries(member,out,capacity,count);
    case 0x17B8898:return SquidArms(member,out,capacity,count);
    case 0x17B6CA0:return ShellBattery(member,out,capacity,count);
    case 0x17B8100:return SpinnerBatteries(member,out,capacity,count);
    case 0x17AFEE0:return GenericContact(member,object+0x11C0,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(member.ctrl)),out,capacity,count) && SharedBigBattery(member,0x1628,out,capacity,count) && BodyMethod(member,EmitterKind::liveOwnerLastTeam,out,capacity,count);
    case 0x17B6270:
        // Dyno's multi-body Contact has raw owner at +0, not generic +8.
        return Readable(object+0x12C0,0x20) && At<const void*>(object,0x12C0)==member.obj &&
            Append(member,object+0x12C0,EmitterKind::liveOwnerTeam,static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(member.ctrl)),out,capacity,count) &&
            BigBatteryArray(member,0x1460,0xA0,out,capacity,count) && SharedBigBattery(member,0x1498,out,capacity,count);
    default:break;
    }
    if(p.vtableRva==0x17A9650) {
        // 30A4D2 -> 313000 constructs the inline barrier. Its +8 is a model,
        // not a weak control. 314730 refreshes both BarrierInfo teams per step.
        if(!Readable(object+0x10C0,0x1510) || At<const void*>(object,0x10D0)!=member.obj)return false;
        const auto* model=At<const void*>(object,0x10C0);
        if(!model || !Readable(model,8) || At<const void*>(object,0x10D8)!=model)return false;
        return Append(member,object+0x10D0,EmitterKind::liveOwnerTeam,
                      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(model)),out,capacity,count);
    }
    if(p.vtableRva==0x17CC2A8)return InlineInit(member,0x700,out,capacity,count);
    // UFO515's small GunBattery is inline in stride-1B0 entries, not GB2.
    return p.vtableRva==0x17CC838 && SmallBatteries(member,0x1298,0x1B0,0x20,out,capacity,count);
}

bool FootContact(const ObjRef& part,const ObjRef*,unsigned,Emitter* out,unsigned capacity,unsigned& count) noexcept {
    const auto* object=static_cast<const unsigned char*>(part.obj);
    const auto* contact=At<const unsigned char*>(object,0x7B0);
    const auto* control=At<const unsigned char*>(object,0x7A8);
    // This reversed pair is NOT std::shared_ptr: count@0/4, object@8.
    return contact && control && Readable(control,16) && At<LONG>(control,0)>0 &&
        At<const void*>(control,8)==contact && GenericContact(part,contact,
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(control)),out,capacity,count);
}

bool EmitCurrent(const ObjRef& member,Emitter* out,unsigned capacity,unsigned& count,const ObjRef* scope=nullptr,unsigned scopeCount=0) noexcept {
    const auto* profile=Class(member);
    if(profile)return Emitters(member,*profile,out,capacity,count);
    const auto* foot=Ready() ? DeiroiFootType(member) : nullptr;
    if(!foot || !FootContact(member,scope,scopeCount,out,capacity,count))return false;
    if(foot->vtable==0x17AB118)return SmallBattery(member,static_cast<const unsigned char*>(member.obj)+0xB90,out,capacity,count);
    if(foot->vtable==0x17AB770)return BodyMethod(member,EmitterKind::liveOwnerTeam,out,capacity,count);
    return true;
}

Life MemberLife(const ObjRef& ref) noexcept {
    if(!ref.obj || !ref.ctrl)return Life::invalid;
    if(!Readable(ref.ctrl,16))return Life::invalid;
    if(At<LONG>(ref.ctrl,8)<=0)return Life::gone;
    if(!Readable(ref.obj,kSelfCtrl+sizeof(void*)))return Life::invalid;
    if(At<const void*>(ref.obj,kSelfCtrl)!=ref.ctrl)return Life::gone;
    return Life::live;
}
bool HookCollect(void* user,const ObjRef& root,ObjRef* members,unsigned memberCapacity,unsigned* memberCount,
                 Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept {
    if(memberCount)*memberCount=0;if(emitterCount)*emitterCount=0;
    __try {
        return user && LeaseMatches(*static_cast<const NativeLease*>(user),root) &&
            EnemyControlNativePvpCollect(root,members,memberCapacity,memberCount,emitters,emitterCapacity,emitterCount);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
Life HookRead(void*,const Emitter& emitter,int* team) noexcept {return EnemyControlNativePvpRead(emitter,team);}
bool HookWrite(void*,const Emitter& emitter,int team) noexcept {return EnemyControlNativePvpWrite(emitter,team);}
bool HookMemberBaseline(void*,const ObjRef& root,const ObjRef& member,int rootTeam,int rootLast,int* team,int* last) noexcept {
    return EnemyControlNativePvpBaseline(root,member,rootTeam,rootLast,team,last);
}
bool HookEmitterBaseline(void*,const Emitter& emitter,int ownerTeam,int ownerLast,int* team) noexcept {
    return EnemyControlNativePvpBaseline(emitter,ownerTeam,ownerLast,team);
}
} // namespace native_pvp_detail

bool EnemyControlNativePvpCollect(const ObjRef& root,ObjRef* members,unsigned memberCapacity,unsigned* memberCount,
                                 controlled_pvp::Emitter* emitters,unsigned emitterCapacity,unsigned* emitterCount) noexcept {
    using namespace native_pvp_detail;
    if(memberCount)*memberCount=0;if(emitterCount)*emitterCount=0;
    if(!memberCount || !emitterCount || !members || !memberCapacity || !emitters ||
       memberCapacity>controlled_pvp::kMaxMembers || emitterCapacity>controlled_pvp::kMaxEmitters)return false;
    __try {
        if(EnemyControlBossPvpSupports(root))return EnemyControlBossPvpCollect(root,members,memberCapacity,memberCount,emitters,emitterCapacity,emitterCount);
        if(EnemyControlTransportPvpSupports(root))return EnemyControlTransportPvpCollect(root,members,memberCapacity,memberCount,emitters,emitterCapacity,emitterCount);
        if(EnemyControlBerserkerPvpSupports(root))return enemy_native::WalkingPartsControlReady() &&
            EnemyControlBerserkerPvpCollect(root,members,memberCapacity,memberCount,emitters,emitterCapacity,emitterCount);
        unsigned used=0,emitted=0;bool covered[controlled_pvp::kMaxMembers]{};
        if(!enemy_pvp_native::CollectTree(root,members,memberCapacity,&used))return false;
        for(unsigned i=0;i<used;++i) {
            if(covered[i])continue;
            const bool boss=EnemyControlBossPvpSupports(members[i]),transport=EnemyControlTransportPvpSupports(members[i]),berserker=EnemyControlBerserkerPvpSupports(members[i]);
            if(berserker && !enemy_native::WalkingPartsControlReady())return false;
            if(boss || transport || berserker) {
                ObjRef group[controlled_pvp::kMaxMembers]{};Emitter weapons[controlled_pvp::kMaxEmitters]{};
                unsigned groupCount=0,weaponCount=0;
                const bool ok=boss ? EnemyControlBossPvpCollect(members[i],group,memberCapacity,&groupCount,weapons,emitterCapacity,&weaponCount) :
                    transport ? EnemyControlTransportPvpCollect(members[i],group,memberCapacity,&groupCount,weapons,emitterCapacity,&weaponCount) :
                    EnemyControlBerserkerPvpCollect(members[i],group,memberCapacity,&groupCount,weapons,emitterCapacity,&weaponCount);
                if(!ok || !groupCount || groupCount>memberCapacity || weaponCount>emitterCapacity)return false;
                bool ownsRoot=false;
                for(unsigned j=0;j<groupCount;++j) {
                    ownsRoot=ownsRoot || Same(group[j],members[i]);unsigned at=0;
                    for(;at<used;++at)if(members[at].obj==group[j].obj) {
                        if(!Same(members[at],group[j]))return false;break;
                    }
                    if(at==used){if(used==memberCapacity)return false;members[used++]=group[j];}
                    covered[at]=true;
                }
                if(!ownsRoot)return false;
                for(unsigned j=0;j<weaponCount;++j) {
                    const auto& e=weapons[j];bool known=false,duplicate=false;
                    for(unsigned n=0;n<used;++n)if(Same(e.member,members[n])){known=true;break;}
                    if(!known || !e.object || !e.incarnation)return false;
                    for(unsigned n=0;n<emitted;++n)if(emitters[n].object==e.object) {
                        if(emitters[n].kind!=e.kind || !Same(emitters[n].member,e.member) || emitters[n].incarnation!=e.incarnation || !Same(emitters[n].storage,e.storage))return false;
                        duplicate=true;break;
                    }
                    if(!duplicate){if(emitted==emitterCapacity)return false;emitters[emitted++]=e;}
                }
                continue;
            }
            const auto* p=Class(members[i]);
            if(p && p->vtableRva==0x17AA648 && !DeiroiParts(members[i],members,memberCapacity,used))return false;
        }
        for(unsigned i=0;i<used;++i)
            if(!covered[i] && !EmitCurrent(members[i],emitters,emitterCapacity,emitted,members,used))return false;
        *memberCount=used;*emitterCount=emitted;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlNativePvpCacheRetained(const controlled_pvp::Emitter& emitter) noexcept {
    int baseline=-1;return native_pvp_detail::Cache(emitter.kind) && emitter.storage.obj && emitter.storage.ctrl &&
        controlled_pvp::ReadEmitterBaseline(emitter,&baseline);
}
controlled_pvp::Life EnemyControlNativePvpReadRetainedCache(const controlled_pvp::Emitter& emitter,int* team) noexcept {
    using namespace native_pvp_detail;
    if(!team || !EnemyControlNativePvpCacheRetained(emitter))return Life::invalid;
    __try {
        const auto& storage=emitter.storage;
        if(!Readable(storage.ctrl,16))return Life::invalid;
        if(At<LONG>(storage.ctrl,8)<=0)return Life::gone;
        const auto* bullet=static_cast<const unsigned char*>(storage.obj);
        if(!Readable(bullet,0xC35))return Life::invalid;
        if(At<const void*>(bullet,kSelfCtrl)!=storage.ctrl || (At<unsigned char>(bullet,0x18)&4u))return Life::gone;
        if(At<const void*>(bullet,kSelf)!=storage.obj || !Ready() || !BulletType(bullet) ||
           !WeakOwner(bullet,0xAE8,emitter.member) || !WeakOwner(bullet,0x880,emitter.member) ||
           (emitter.object!=bullet+0xB40 && emitter.object!=bullet+0x894))return Life::invalid;
        const int value=At<int>(emitter.object,0);if(!Team(value))return Life::invalid;
        *team=value;return Life::live;
    } __except(EXCEPTION_EXECUTE_HANDLER){return Life::invalid;}
}
bool EnemyControlNativePvpWriteRetainedCache(const controlled_pvp::Emitter& emitter,int team) noexcept {
    int current=-1;
    __try {
        if(!native_pvp_detail::Team(team) || EnemyControlNativePvpReadRetainedCache(emitter,&current)!=controlled_pvp::Life::live)return false;
        if(current!=team) {
            if(!Readable(emitter.object,sizeof(int),true))return false;
            Put<int>(const_cast<void*>(emitter.object),0,team);
        }
        return EnemyControlNativePvpReadRetainedCache(emitter,&current)==controlled_pvp::Life::live && current==team;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
controlled_pvp::Life EnemyControlNativePvpRead(const controlled_pvp::Emitter& emitter,int* team) noexcept {
    using namespace native_pvp_detail;
    if(!team || !emitter.object || !emitter.incarnation)return Life::invalid;
    __try {
        if(EnemyControlNativePvpCacheRetained(emitter))return EnemyControlNativePvpReadRetainedCache(emitter,team);
        const auto life=MemberLife(emitter.member);
        if(life!=Life::live) {
            // A lost owner is not proof that an independently surviving cache
            // disappeared. Unsealed storage must stay unresolved until its own
            // lifetime ends or its original baseline becomes provable.
            if(Cache(emitter.kind) && emitter.storage.obj && emitter.storage.ctrl) {
                if(!Readable(emitter.storage.ctrl,16))return Life::invalid;
                return At<LONG>(emitter.storage.ctrl,8)<=0 ? Life::gone : Life::invalid;
            }
            return life;
        }
        if(EnemyControlBossPvpSupports(emitter.member))return EnemyControlBossPvpRead(emitter,team);
        if(EnemyControlTransportPvpSupports(emitter.member))return EnemyControlTransportPvpRead(emitter,team);
        if(EnemyControlBerserkerPvpSupports(emitter.member))return EnemyControlBerserkerPvpRead(emitter,team);
        if(!Derived(emitter.kind) && !Cache(emitter.kind))return Life::invalid;
        // Do not query the saved emitter pointer. Only enumerate the current native
        // typed container, then compare addresses/tokens; malformed containers are invalid.
        Emitter current[controlled_pvp::kMaxEmitters]{};unsigned count=0;
        const ObjRef enumerationRoot=emitter.member;
        const auto* foot=DeiroiFootType(emitter.member);
        const auto* memberType=Class(enumerationRoot);
        if(memberType && memberType->vtableRva==0x17AA648) {
            ObjRef scope[controlled_pvp::kMaxMembers]{};unsigned total=0;
            if(!EnemyControlNativePvpCollect(enumerationRoot,scope,controlled_pvp::kMaxMembers,&total,current,controlled_pvp::kMaxEmitters,&count))return Life::invalid;
        } else if(foot && foot->vtable==0x17AB118 && emitter.object==static_cast<const unsigned char*>(emitter.member.obj)+0xB90) {
            if(!SmallBattery(emitter.member,static_cast<const unsigned char*>(emitter.member.obj)+0xB90,current,controlled_pvp::kMaxEmitters,count))return Life::invalid;
        } else if(foot && foot->vtable==0x17AB770 && emitter.object==emitter.member.obj) {
            if(!BodyMethod(emitter.member,EmitterKind::liveOwnerTeam,current,controlled_pvp::kMaxEmitters,count))return Life::invalid;
        } else if(!EmitCurrent(emitter.member,current,controlled_pvp::kMaxEmitters,count,&emitter.member,1))return Life::invalid;
        for(unsigned i=0;i<count;++i)
            if(current[i].kind==emitter.kind && Same(current[i].member,emitter.member) &&
               current[i].object==emitter.object && current[i].incarnation==emitter.incarnation && Same(current[i].storage,emitter.storage)) {
                const int value=Cache(current[i].kind) ? At<int>(current[i].object,0) :
                    At<int>(current[i].member.obj,current[i].kind==EmitterKind::liveOwnerLastTeam ? 0x318 : kTeam);
                if(!Team(value))return Life::invalid;*team=value;return Life::live;
            }
        return Life::gone;
    } __except(EXCEPTION_EXECUTE_HANDLER){return Life::invalid;}
}
bool EnemyControlNativePvpWrite(const controlled_pvp::Emitter& emitter,int team) noexcept {
    __try {
        if(EnemyControlNativePvpCacheRetained(emitter))return EnemyControlNativePvpWriteRetainedCache(emitter,team);
        if(!native_pvp_detail::Team(team) || native_pvp_detail::MemberLife(emitter.member)!=controlled_pvp::Life::live)return false;
        if(EnemyControlBossPvpSupports(emitter.member))return EnemyControlBossPvpWrite(emitter,team);
        if(EnemyControlTransportPvpSupports(emitter.member))return EnemyControlTransportPvpWrite(emitter,team);
        if(EnemyControlBerserkerPvpSupports(emitter.member))return EnemyControlBerserkerPvpWrite(emitter,team);
        int current=-1;if(EnemyControlNativePvpRead(emitter,&current)!=controlled_pvp::Life::live)return false;
        if(!native_pvp_detail::Cache(emitter.kind))return current==team;
        if(current!=team) {
            if(!Readable(emitter.object,sizeof(int),true))return false;
            Put<int>(const_cast<void*>(emitter.object),0,team);
        }
        return EnemyControlNativePvpRead(emitter,&current)==controlled_pvp::Life::live && current==team;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool EnemyControlNativePvpBaseline(const ObjRef& root,const ObjRef& member,int rootTeam,int rootLast,int* team,int* last) noexcept {
    if(!team || !last || !native_pvp_detail::Team(rootTeam) || rootLast<0 || rootLast>=7)return false;
    __try {
        if(EnemyControlBossPvpSupports(root) || EnemyControlBossPvpSupports(member))return EnemyControlBossPvpBaseline(root,member,rootTeam,rootLast,team,last);
        if(EnemyControlTransportPvpSupports(root) || EnemyControlTransportPvpSupports(member))return EnemyControlTransportPvpBaseline(root,member,rootTeam,rootLast,team,last);
        if(EnemyControlBerserkerPvpSupports(root) || EnemyControlBerserkerPvpSupports(member))return EnemyControlBerserkerPvpBaseline(root,member,rootTeam,rootLast,team,last);
        if(!native_pvp_detail::Same(root,member))return false;
        const auto* p=native_pvp_detail::Class(member);if(!p || !native_pvp_detail::Supported(*p))return false;
        *team=rootTeam;*last=rootLast;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlNativePvpBaseline(const controlled_pvp::Emitter& emitter,int ownerTeam,int ownerLast,int* team) noexcept {
    if(!team || !native_pvp_detail::Team(ownerTeam) || ownerLast<0 || ownerLast>=7)return false;
    __try {
        if(EnemyControlNativePvpCacheRetained(emitter)) {
            int current=-1;
            return EnemyControlNativePvpReadRetainedCache(emitter,&current)==controlled_pvp::Life::live && controlled_pvp::ReadEmitterBaseline(emitter,team);
        }
        if(EnemyControlBossPvpSupports(emitter.member))return EnemyControlBossPvpBaseline(emitter,ownerTeam,ownerLast,team);
        if(EnemyControlTransportPvpSupports(emitter.member))return EnemyControlTransportPvpBaseline(emitter,ownerTeam,ownerLast,team);
        if(EnemyControlBerserkerPvpSupports(emitter.member))return EnemyControlBerserkerPvpBaseline(emitter,ownerTeam,ownerLast,team);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    int current=-1;if(EnemyControlNativePvpRead(emitter,&current)!=controlled_pvp::Life::live)return false;
    if(native_pvp_detail::Cache(emitter.kind) && !controlled_pvp::HasTemporaryTeam(emitter.member)) {
        *team=current;return true; // stable mode-off keeps the field's actual independent native baseline
    }
    *team=(emitter.kind==controlled_pvp::EmitterKind::liveOwnerLastTeam || emitter.kind==controlled_pvp::EmitterKind::ownerLastTeamCache) ? ownerLast : ownerTeam;return true;
}
bool EnemyControlNativePvpCollectHumanoidWeapons(const ObjRef& owner,controlled_pvp::Emitter* out,unsigned capacity,unsigned* count) noexcept {
    if(!out || !count || *count>capacity || capacity>controlled_pvp::kMaxEmitters)return false;
    const auto saved=*count;
    __try {
        if(!native_pvp_detail::Ready() || !enemy_pvp_native::tree_detail::LiveIdentity(owner) ||
           At<const void*>(owner.obj,kSelf)!=owner.obj || !native_pvp_detail::HumanoidWeapons(owner,out,capacity,*count)) {
            *count=saved;return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*count=saved;return false;}
}
bool EnemyControlNativePvpCollectBigBattery(const ObjRef& owner,const unsigned char* unit,const void* control,
                                           controlled_pvp::Emitter* out,unsigned capacity,unsigned* count) noexcept {
    if(!out || !count || *count>capacity || capacity>controlled_pvp::kMaxEmitters)return false;
    const auto saved=*count;
    __try {
        if(!native_pvp_detail::Ready() || !enemy_pvp_native::tree_detail::LiveIdentity(owner) ||
           At<const void*>(owner.obj,kSelf)!=owner.obj || !native_pvp_detail::BigBattery(owner,unit,control,out,capacity,*count)) {
            *count=saved;return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*count=saved;return false;}
}
bool EnemyControlNativePvpInstall() noexcept {
    __try {
        if(!native_pvp_detail::Ready())return false;
        const bool boss=EnemyControlBossPvpInstall();const bool transport=EnemyControlTransportPvpInstall();const bool berserker=EnemyControlBerserkerPvpInstall();
        // Core installs its general walking ABI first. This late aggregate is the sole installer
        // for the accepted detached-part entry, after the constructor/factory guards exist.
        const bool parts=berserker && enemy_native::InstallWalkingPartsAcceptedActions();
        return boss && transport && berserker && parts;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlNativePvpReset() noexcept {
    __try {
        if(controlled_pvp::HasLeases() || !EnemyControlBossPvpReset() || !EnemyControlBerserkerPvpReset())return false;
        EnemyControlTransportPvpReset();return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlNativePvpProfileReady(const char* className) noexcept {
    if(!className)return false;
    __try {
        if(!native_pvp_detail::Ready())return false;
        for(const auto& p:native_pvp_detail::kProfiles)
            if(native_pvp_detail::Supported(p) && native_pvp_detail::ProofReady(p) && std::strcmp(className,p.name)==0)return true;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlNativePvpHooks(const enemy_native::NativeLease& lease,controlled_pvp::Hooks* out) noexcept {
    if(!out)return false;*out={};
    __try {
        using namespace native_pvp_detail;
        const ObjRef root{lease.object,lease.control};if(!LeaseMatches(lease,root))return false;
        ObjRef members[controlled_pvp::kMaxMembers]{};Emitter emitters[controlled_pvp::kMaxEmitters]{};
        unsigned memberCount=0,emitterCount=0;
        if(!EnemyControlNativePvpCollect(root,members,controlled_pvp::kMaxMembers,&memberCount,
                                        emitters,controlled_pvp::kMaxEmitters,&emitterCount))return false;
        *out={const_cast<NativeLease*>(&lease),HookCollect,HookRead,HookWrite,HookMemberBaseline,HookEmitterBaseline};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
} // namespace crew
