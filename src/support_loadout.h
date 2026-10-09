// Out-of-game support loadouts (the user, 2026-10-09: "在游戏外可以编辑小组，还有载具、npc的挂载，npc的外观颜色等";
// corrected the same day: "我说的是战前配置载具的挂载分布。比如我飞机可以全带炸弹，或者全带导弹，或者带机炮"). Pure logic, no
// Windows or game: tests/support_loadout_test.cpp runs it offline, tools/support_loadout.py (the installer's editor and
// the SGO generator) mirrors it and tests/support_loadout_ini_test.py keeps the two equal. Feasibility and evidence:
// docs/feature-2026-10-09-loadout-editor.md.
//
// EDF6VehicleCrew.ini [VehicleCrew], the one source of truth (the plugin rereads it on every save):
//   SupportPreset_<KEY>=rifle@1E3A8A*2,rocket,sniper   a seated entry's soldiers in seat order (a leader every four):
//       kind      one of kSupportKindNames (the 14 stock AI templates: 5 Ranger, 5 Wing Diver, 4 Fencer weapons)
//       @RRGGBB   its primary colour (soldier_color change_color0); @RRGGBB:RRGGBB primary and secondary (change_color1);
//                 X a colour left stock (@X:FFFFFF). No @: the stock look.
//       *n        n soldiers of that spec (1-12)
//   SupportVehicle_<KEY>=...                            the vehicle's pylons, before the battle (kVehicleKeys):
//       a tank (TANK_CREWED / TANK_DELIVERY): its main gun HE or AP, then the rounds it carries besides, each a pylon:
//           HE,AP:25          the stock 105 mm howitzer's 25 HE and 25 APFSDS beside it (half and half)
//           AP,AP:20,GLM:4    the 90 mm smooth-bore's 30 AP, 20 more, 4 gun-launched missiles
//       a jet (STRIKE, FIGHTER, INTERCEPTOR, MULTIROLE, each guard and _F follow): its pylons, the guns always aboard:
//           MK82:6,MK82:6     all bombs        AAM_M:4,AAM_S:2   all missiles        GUNS   the guns alone
//     Empty or absent: the vehicle as it always came. A malformed value: refused whole, the stock vehicle, the problem named.
// A pylon is STORE:rounds, STORE one of kLoadStores (the stores the plugin already flies and fires: pylib/vcobjects.py
// STORES), rounds one of kRoundCounts. A coloured soldier and a loaded vehicle are generated SGOs whose names say what
// they are (SupportLookFile, VehicleVariantFile): the installer writes them, a peer missing one writes its name to its
// pending list and its installer makes it from the name alone. Online they are shared through each unit's 64-bit
// variant (support_net.h Unit::variant, sent in the unit message's unused `challenge`) and each peer's hello (an
// extension word and a Bloom filter of the files it has, in the hello's unused unit fields): support_protocol.h.
#pragma once
#include "support_call.h"
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cwctype>

namespace crew {
struct SupportKindName { SupportWeapon kind; const wchar_t* name; const wchar_t* label; };
// The ini spelling and the editor's label of every kind, SupportWeapon order. The Rangers' names are the ini weapons'
// (support_config.cpp kWeaponNames) so a preset reads like the older weapon keys.
inline constexpr SupportKindName kSupportKindNames[]={
    {SupportWeapon::rifle,L"rifle",L"游骑兵·步枪"},{SupportWeapon::flame,L"flame",L"游骑兵·火焰"},
    {SupportWeapon::rocket,L"rocket",L"游骑兵·火箭"},{SupportWeapon::shotgun,L"shotgun",L"游骑兵·霰弹"},
    {SupportWeapon::sniper,L"sniper",L"游骑兵·狙击"},
    {SupportWeapon::wingLance,L"lance",L"翼人·长矛"},{SupportWeapon::wingLaser,L"laser",L"翼人·激光"},
    {SupportWeapon::wingMonster,L"monster",L"翼人·狙击"},{SupportWeapon::wingIzuna,L"izuna",L"翼人·雷链"},
    {SupportWeapon::wingThunderBow,L"thunderbow",L"翼人·雷弓"},
    {SupportWeapon::fencerCannon,L"cannon",L"重装·重炮"},{SupportWeapon::fencerMiddleCannon,L"midcannon",L"重装·中炮"},
    {SupportWeapon::fencerPileBanker,L"pilebanker",L"重装·打桩"},{SupportWeapon::fencerShotgun,L"fshotgun",L"重装·霰弹"},
};
static_assert(sizeof(kSupportKindNames)/sizeof(kSupportKindNames[0])==static_cast<std::size_t>(kSupportWeaponCount),
              "one name per soldier kind");
constexpr const wchar_t* SupportKindIniName(SupportWeapon kind) noexcept {
    const auto i=static_cast<int>(kind);
    return i>=0 && i<kSupportWeaponCount ? kSupportKindNames[i].name : L"rifle";
}

// A soldier's colours, 0xRRGGBB each; kStockColour keeps the template's.
inline constexpr std::int32_t kStockColour=-1;
struct SupportLook {
    std::int32_t primary=kStockColour,secondary=kStockColour;
    constexpr bool On() const noexcept { return primary!=kStockColour || secondary!=kStockColour; }
    constexpr bool operator==(const SupportLook& o) const noexcept { return primary==o.primary && secondary==o.secondary; }
};
struct SupportPreset {
    int count=0;   // 0: none (the entry's own load)
    SupportWeapon kind[kSupportLoadoutMost]{};
    SupportLook look[kSupportLoadoutMost]{};
};

namespace loadout_detail {
inline bool Separator(wchar_t c) noexcept { return c==L',' || c==L'，' || c==L';' || c==L'、'; }
inline void Trim(const wchar_t*& a,const wchar_t*& b) noexcept {
    while(a<b && std::iswspace(*a))++a;
    while(b>a && std::iswspace(b[-1]))--b;
}
inline bool Same(const wchar_t* a,const wchar_t* b,const wchar_t* word) noexcept {
    for(;a<b;++a,++word)if(!*word || std::towlower(*a)!=std::towlower(*word))return false;
    return !*word;
}
inline int Hex(wchar_t c) noexcept {
    return c>=L'0' && c<=L'9' ? c-L'0' : c>=L'a' && c<=L'f' ? c-L'a'+10 : c>=L'A' && c<=L'F' ? c-L'A'+10 : -1;
}
// RRGGBB, or X / x (stock). False for anything else.
inline bool Colour(const wchar_t* a,const wchar_t* b,std::int32_t* out) noexcept {
    Trim(a,b);
    if(b-a==1 && (*a==L'X' || *a==L'x')){*out=kStockColour;return true;}
    if(b-a!=6)return false;
    std::int32_t v=0;
    for(;a<b;++a){const int h=Hex(*a);if(h<0)return false;v=v*16+h;}
    *out=v;return true;
}
// A whole number 1..most (no sign, no spaces inside).
inline bool Count(const wchar_t* a,const wchar_t* b,int most,int* out) noexcept {
    Trim(a,b);
    if(a==b || b-a>5)return false;
    int v=0;
    for(;a<b;++a){if(*a<L'0' || *a>L'9')return false;v=v*10+(*a-L'0');}
    if(v<1 || v>most)return false;
    *out=v;return true;
}
inline void Say(wchar_t* why,std::size_t capacity,const wchar_t* text,const wchar_t* a=nullptr,const wchar_t* b=nullptr) noexcept {
    if(!why || !capacity)return;
    std::size_t n=0;
    for(;*text && n+1<capacity;++text)why[n++]=*text;
    if(a)for(;a<b && n+1<capacity;++a)why[n++]=*a;
    why[n]=0;
}
inline const wchar_t* Find(const wchar_t* a,const wchar_t* b,wchar_t c) noexcept {
    for(;a<b;++a)if(*a==c)return a;
    return b;
}
// Calls each(a, b) for every non-empty trimmed token; stops (false) at the first each() refusing.
template<class Each> bool Tokens(const wchar_t* text,Each each) noexcept {
    for(const wchar_t* p=text;p && *p;) {
        const wchar_t* end=p;while(*end && !Separator(*end))++end;
        const wchar_t *a=p,*b=end;Trim(a,b);
        if(b>a && !each(a,b))return false;
        p=*end ? end+1 : end;
    }
    return true;
}
}  // namespace loadout_detail

inline bool ParseSupportKind(const wchar_t* a,const wchar_t* b,SupportWeapon* out) noexcept {
    loadout_detail::Trim(a,b);
    for(const auto& k:kSupportKindNames)if(loadout_detail::Same(a,b,k.name)){*out=k.kind;return true;}
    return false;
}
// `text` (one SupportPreset_<KEY> value) into *out. `seatCount`: the entry's (SupportCallSeats; 0 = no seats, so any preset is
// refused). False with *out empty and `why` naming the fault; an empty text is true with count 0.
inline bool ParseSupportPreset(const wchar_t* text,int seatCount,SupportPreset* out,wchar_t* why,std::size_t whyCapacity) noexcept {
    using namespace loadout_detail;
    SupportPreset p{};
    if(why && whyCapacity)why[0]=0;
    bool parsed=Tokens(text,[&](const wchar_t* a,const wchar_t* b) noexcept {
        const wchar_t* star=Find(a,b,L'*');
        const wchar_t* at=Find(a,star,L'@');
        SupportWeapon kind;
        if(!ParseSupportKind(a,at,&kind)){Say(why,whyCapacity,L"未知兵种：",a,at);return false;}
        SupportLook look;
        if(at<star) {
            const wchar_t* colon=Find(at+1,star,L':');
            if(!Colour(at+1,colon,&look.primary) || (colon<star && !Colour(colon+1,star,&look.secondary))) {
                Say(why,whyCapacity,L"颜色应为 @RRGGBB 或 @RRGGBB:RRGGBB（X = 原色）：",at,star);return false;
            }
        }
        int n=1;
        if(star<b && !Count(star+1,b,kSupportLoadoutMost,&n)){Say(why,whyCapacity,L"人数应为 *1 到 *12：",star,b);return false;}
        if(p.count+n>kSupportLoadoutMost){Say(why,whyCapacity,L"超过 12 人");return false;}
        for(int i=0;i<n;++i){p.kind[p.count]=kind;p.look[p.count]=look;++p.count;}
        return true;
    });
    if(parsed && p.count>0 && p.count>seatCount) {
        parsed=false;
        if(seatCount<=0)Say(why,whyCapacity,L"该单位没有可编组的座位");
        else Say(why,whyCapacity,L"人数超过该单位的座位数");
    }
    if(out)*out=parsed ? p : SupportPreset{};
    return parsed;
}
constexpr SupportLoadout PresetLoadout(const SupportPreset& p) noexcept {
    SupportLoadout l{};
    l.count=p.count>kSupportLoadoutMost ? kSupportLoadoutMost : p.count<0 ? 0 : p.count;
    for(int i=0;i<l.count;++i)l.soldier[i]=p.kind[i];
    return l;
}
// The colours of composed soldier i: the preset's where the load still has the preset's kind in that seat (the player may
// have changed the load on the bar's composition panel: a changed seat comes stock).
constexpr SupportLook PresetLookFor(const SupportPreset& p,const SupportLoadout& load,int i) noexcept {
    return i>=0 && i<p.count && i<load.count && load.soldier[i]==p.kind[i] ? p.look[i] : SupportLook{};
}

// --- Vehicle pylons ---
// The stores a pylon may carry (pylib/vcobjects.py STORES: the files EDF6VC_<NAME>_<rounds>.SGO the plugin already
// knows, src/stores.inc kStores), a code each (1..15, 0 = none) for the 64-bit variant. `jet` / `tank`: whom it may go on
// (a jet fires its missiles, rockets and bombs itself, src/jet_combat.cpp; a tank's rounds and its gun-launched missile
// ride its main gun's control, src/payload.cpp).
enum class LoadRole : std::uint8_t { air, ground, bomb, rocket, gun };
struct LoadStore { std::uint8_t code; const wchar_t* name; const wchar_t* label; LoadRole role; bool jet,tank; };
inline constexpr LoadStore kLoadStores[]={
    {1,L"AAM_S",L"AIM-9X 近程空空导弹",LoadRole::air,true,false},
    {2,L"AAM_M",L"AIM-120 中程空空导弹",LoadRole::air,true,false},
    {3,L"AAM_L",L"AIM-54 远程空空导弹",LoadRole::air,true,false},
    {4,L"AGM",L"AGM-65 空地导弹",LoadRole::ground,true,false},
    {5,L"AGM_L",L"AGM-114 轻型空地导弹",LoadRole::ground,true,false},
    {6,L"MK82",L"Mk 82 炸弹",LoadRole::bomb,true,false},
    {7,L"RKT",L"Hydra 70 火箭巢",LoadRole::rocket,true,false},
    {8,L"AP",L"APFSDS 穿甲弹",LoadRole::gun,false,true},
    {9,L"HE",L"HE 榴弹",LoadRole::gun,false,true},
    {10,L"GLM",L"LAHAT 炮射导弹",LoadRole::ground,false,true},
};
inline constexpr int kLoadStoreCount=static_cast<int>(sizeof(kLoadStores)/sizeof(kLoadStores[0]));
// The round counts a pylon may hold, a 4-bit code each (the store file is EDF6VC_<NAME>_<count>.SGO).
inline constexpr int kRoundCounts[16]={1,2,3,4,5,6,8,10,12,15,19,20,25,30,38,40};
constexpr int RoundCode(int rounds) noexcept {
    for(int i=0;i<16;++i)if(kRoundCounts[i]==rounds)return i;
    return -1;
}
constexpr const LoadStore* LoadStoreOf(int code) noexcept {
    for(const auto& s:kLoadStores)if(s.code==code)return &s;
    return nullptr;
}

// The vehicles a support entry brings that take pylons, and what they are made from: the support tank (the stock
// V505_TANK_MISSION, src/support_spawn.h; the plugin builds its holders past the stock one, src/stores.cpp) and the
// plugin's jets (tools/make_jets.py: the guns L / R, the stores, the fuel tank fourth). The helicopters are not offered:
// their NPC pilot fires only the stock holders (src/heli.cpp kStoreHolder), pylons past those would never fire.
enum class VehicleBody : std::uint8_t { none, tank, strike, fighter, interceptor, multirole };
// `most` pylons: a tank's stock gun and 4 (src/payload.h kMostPayload 8 a seat), a jet's 5 (its guns L / R, the fuel tank
// and 5 make 8 holders: src/jet_combat.cpp ReadArms reads at most 8, src/stores.h kMostStores 7 stores).
struct VehicleBodyRow { VehicleBody body; const wchar_t* name; const wchar_t* base; int most; bool tank; };
inline constexpr VehicleBodyRow kVehicleBodies[]={
    {VehicleBody::tank,L"TANK",L"V505_TANK_MISSION.SGO",4,true},
    {VehicleBody::strike,L"STRIKE",L"EDF6VC_JET_STRIKE.SGO",5,false},
    {VehicleBody::fighter,L"FIGHTER",L"EDF6VC_JET_FIGHTER.SGO",5,false},
    {VehicleBody::interceptor,L"INTERCEPTOR",L"EDF6VC_JET_INTERCEPTOR.SGO",5,false},
    {VehicleBody::multirole,L"MULTIROLE",L"EDF6VC_JET_MULTIROLE.SGO",5,false},
};
constexpr const VehicleBodyRow* VehicleBodyOf(VehicleBody b) noexcept {
    for(const auto& r:kVehicleBodies)if(r.body==b)return &r;
    return nullptr;
}
// The catalog keys whose vehicle takes a SupportVehicle_<KEY> (support_dispatch.cpp SupportCallKey, airstrike.cpp's calls).
struct VehicleKey { const wchar_t* key; VehicleBody body; };
inline constexpr VehicleKey kVehicleKeys[]={
    {L"TANK_CREWED",VehicleBody::tank},{L"TANK_DELIVERY",VehicleBody::tank},
    {L"STRIKE",VehicleBody::strike},{L"STRIKE_F",VehicleBody::strike},
    {L"FIGHTER",VehicleBody::fighter},{L"FIGHTER_F",VehicleBody::fighter},
    {L"INTERCEPTOR",VehicleBody::interceptor},{L"INTERCEPTOR_F",VehicleBody::interceptor},
    {L"MULTIROLE",VehicleBody::multirole},{L"MULTIROLE_F",VehicleBody::multirole},
};
inline VehicleBody VehicleBodyOfKey(const wchar_t* key) noexcept {
    if(!key)return VehicleBody::none;
    for(const auto& k:kVehicleKeys)if(!_wcsicmp(k.key,key))return k.body;
    return VehicleBody::none;
}

inline constexpr int kPylonsMost=7;
struct VehicleLoadout {
    VehicleBody body=VehicleBody::none;   // none: no loadout (the vehicle as it always came)
    bool apGun=false;                     // a tank's main gun: the 90 mm smooth-bore (AP) rather than the howitzer (HE)
    int count=0;                          // pylons
    std::uint8_t store[kPylonsMost]{};    // kLoadStores code
    std::uint8_t rounds[kPylonsMost]{};   // kRoundCounts code
    constexpr bool On() const noexcept { return body!=VehicleBody::none; }
};
// `text` (one SupportVehicle_<KEY> value) for `body`. Empty: true, no loadout. False: *out none, `why` says why.
inline bool ParseVehicleLoadout(const wchar_t* text,VehicleBody body,VehicleLoadout* out,wchar_t* why,std::size_t whyCapacity) noexcept {
    using namespace loadout_detail;
    VehicleLoadout l{};
    if(why && whyCapacity)why[0]=0;
    const VehicleBodyRow* row=VehicleBodyOf(body);
    int tokens=0;bool guns=false,gun=false;
    bool parsed=Tokens(text,[&](const wchar_t* a,const wchar_t* b) noexcept {
        ++tokens;
        if(!row){Say(why,whyCapacity,L"该单位没有可配置的挂点");return false;}
        if(guns){Say(why,whyCapacity,L"GUNS 表示只带机炮，不能再写挂载");return false;}
        if(!row->tank && Same(a,b,L"GUNS")){guns=true;return true;}
        if(row->tank && tokens==1) {
            if(Same(a,b,L"HE") || Same(a,b,L"AP")){gun=true;l.apGun=Same(a,b,L"AP");return true;}
            Say(why,whyCapacity,L"坦克的第一项是主炮 HE 或 AP：",a,b);return false;
        }
        const wchar_t* colon=Find(a,b,L':');
        const wchar_t *na=a,*nb=colon;Trim(na,nb);
        const LoadStore* store=nullptr;
        for(const auto& s:kLoadStores)if(Same(na,nb,s.name))store=&s;
        if(!store || !(row->tank ? store->tank : store->jet)) {
            Say(why,whyCapacity,row->tank ? L"坦克可挂：AP / HE / GLM，不能挂：" : L"飞机可挂：AAM_S / AAM_M / AAM_L / AGM / AGM_L / MK82 / RKT，不能挂：",na,nb);
            return false;
        }
        int n=0;
        if(colon>=b || !Count(colon+1,b,99,&n) || RoundCode(n)<0) {
            Say(why,whyCapacity,L"挂点写成 名称:数量，数量可选 1 2 3 4 5 6 8 10 12 15 19 20 25 30 38 40：",a,b);return false;
        }
        if(l.count>=row->most){Say(why,whyCapacity,row->tank ? L"坦克最多 4 个挂点" : L"飞机最多 5 个挂点");return false;}
        l.store[l.count]=store->code;l.rounds[l.count]=static_cast<std::uint8_t>(RoundCode(n));++l.count;
        return true;
    });
    if(parsed && tokens && row && row->tank && !gun){parsed=false;Say(why,whyCapacity,L"坦克的第一项是主炮 HE 或 AP");}
    if(parsed && tokens)l.body=body;
    if(out)*out=parsed ? l : VehicleLoadout{};
    return parsed;
}

// --- The 64-bit variant of a plan unit (support_net.h Unit::variant) ---
// Bit 63: applied (the unit is made from the variant's file; clear: the host only tells what it wanted, the unit is
// stock, a peer without the file writes it to its pending list). Bit 62: a vehicle loadout (else a soldier look).
// A look: bit 0 primary set, bit 1 secondary set, bits 2-25 primary, 26-49 secondary. A loadout: bits 0-2 pylons,
// bit 3 the tank's AP gun, bits 4-59 a pylon a byte (store code, then the round code in the high nibble).
inline constexpr std::uint64_t kVariantApplied=1ull<<63,kVariantVehicle=1ull<<62;
constexpr std::uint64_t LookVariant(const SupportLook& l) noexcept {
    if(!l.On())return 0;
    std::uint64_t v=0;
    if(l.primary!=kStockColour)v|=1ull|(static_cast<std::uint64_t>(l.primary&0xFFFFFF)<<2);
    if(l.secondary!=kStockColour)v|=2ull|(static_cast<std::uint64_t>(l.secondary&0xFFFFFF)<<26);
    return v;
}
constexpr bool LookOfVariant(std::uint64_t v,SupportLook* out) noexcept {
    const std::uint64_t bits=v&~kVariantApplied;
    if(!bits || (bits&kVariantVehicle) || (bits>>50))return false;
    SupportLook l;
    if(bits&1ull)l.primary=static_cast<std::int32_t>((bits>>2)&0xFFFFFF);
    else if((bits>>2)&0xFFFFFF)return false;
    if(bits&2ull)l.secondary=static_cast<std::int32_t>((bits>>26)&0xFFFFFF);
    else if((bits>>26)&0xFFFFFF)return false;
    if(!l.On())return false;
    if(out)*out=l;
    return true;
}
constexpr std::uint64_t LoadoutVariant(const VehicleLoadout& l) noexcept {
    if(!l.On() || l.count<0 || l.count>kPylonsMost)return 0;
    std::uint64_t v=kVariantVehicle|static_cast<std::uint64_t>(l.count)|(l.apGun ? 8ull : 0ull);
    for(int i=0;i<l.count;++i)
        v|=static_cast<std::uint64_t>((l.store[i]&0xF)|((l.rounds[i]&0xF)<<4))<<(4+8*i);
    return v;
}
// Back into a loadout for `body` (whatever a peer sent: every field checked as the parser would).
inline bool LoadoutOfVariant(std::uint64_t v,VehicleBody body,VehicleLoadout* out) noexcept {
    const std::uint64_t bits=v&~kVariantApplied;
    const VehicleBodyRow* row=VehicleBodyOf(body);
    if(!row || !(bits&kVariantVehicle) || ((bits>>60)&3u))return false;
    VehicleLoadout l{};l.body=body;l.count=static_cast<int>(bits&7u);l.apGun=(bits&8u)!=0;
    if(l.count>row->most || (l.apGun && !row->tank))return false;
    for(int i=0;i<kPylonsMost;++i) {
        const auto byte=static_cast<std::uint8_t>((bits>>(4+8*i))&0xFFu);
        if(i>=l.count){if(byte)return false;continue;}
        const LoadStore* s=LoadStoreOf(byte&0xF);
        if(!s || !(row->tank ? s->tank : s->jet))return false;
        l.store[i]=byte&0xF;l.rounds[i]=byte>>4;
    }
    if(out)*out=l;
    return true;
}

// --- Generated files (tools/support_loadout.py writes them, from the name alone; the plugin derives the same names) ---
namespace loadout_detail {
struct Name {
    wchar_t buf[96];std::size_t n=0;
    void Put(wchar_t c) noexcept {if(n+1<sizeof(buf)/sizeof(buf[0]))buf[n++]=c;}
    void Puts(const wchar_t* s) noexcept {for(;*s;++s)Put(*s);}
    void HexOf(std::uint64_t v,int digits) noexcept {for(int s=(digits-1)*4;s>=0;s-=4)Put(L"0123456789ABCDEF"[(v>>s)&0xF]);}
    std::size_t Out(wchar_t* out,std::size_t capacity,bool path) noexcept {
        buf[n]=0;
        const wchar_t* prefix=path ? L"app:/object/" : L"";
        const std::size_t total=std::wcslen(prefix)+n;
        if(!out || !capacity)return 0;
        if(total+1>capacity){out[0]=0;return 0;}
        std::size_t o=0;
        for(const wchar_t* s=prefix;*s;++s)out[o++]=*s;
        for(std::size_t i=0;i<n;++i)out[o++]=path ? static_cast<wchar_t>(std::towlower(buf[i])) : buf[i];
        out[o]=0;
        return o;
    }
};
}  // namespace loadout_detail
// EDF6VC_NPC_<KIND>[_L]_<PRIMARY|X>_<SECONDARY|X>.SGO, e.g. EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO: the stock template of that
// kind (its _LEADER for a leader) with its soldier_color entries recoloured. `path`: the CreateObject spelling instead
// (app:/object/<lower case>). Returns the length, 0 when it does not fit.
inline std::size_t SupportLookFile(SupportWeapon kind,bool leader,const SupportLook& look,wchar_t* out,std::size_t capacity,
                                   bool path=false) noexcept {
    loadout_detail::Name n;
    const auto colour=[&](std::int32_t v) noexcept {if(v==kStockColour)n.Put(L'X');else n.HexOf(static_cast<std::uint64_t>(v),6);};
    n.Puts(L"EDF6VC_NPC_");
    for(const wchar_t* s=SupportKindIniName(kind);*s;++s)n.Put(static_cast<wchar_t>(std::towupper(*s)));
    if(leader)n.Puts(L"_L");
    n.Put(L'_');colour(look.primary);n.Put(L'_');colour(look.secondary);n.Puts(L".SGO");
    return n.Out(out,capacity,path);
}
// EDF6VC_LO_<BODY>_<the variant in 16 hex digits, applied bit clear>.SGO: the body's base SGO with these pylons.
inline std::size_t VehicleVariantFile(const VehicleLoadout& l,wchar_t* out,std::size_t capacity,bool path=false) noexcept {
    const VehicleBodyRow* row=VehicleBodyOf(l.body);
    const std::uint64_t v=LoadoutVariant(l);
    if(!row || !v){if(out && capacity)out[0]=0;return 0;}
    loadout_detail::Name n;
    n.Puts(L"EDF6VC_LO_");n.Puts(row->name);n.Put(L'_');n.HexOf(v,16);n.Puts(L".SGO");
    return n.Out(out,capacity,path);
}

// --- Which variant files a peer has (its hello's Bloom filter: 256 bits, three probes) ---
// FNV-1a 64 of the file name, upper case (the plugin and the installer write upper-case names; Windows matches either).
inline std::uint64_t VariantHash(const wchar_t* file) noexcept {
    std::uint64_t h=1469598103934665603ull;
    for(;file && *file;++file) {
        const auto c=static_cast<std::uint32_t>(std::towupper(*file));
        h^=c&0xFFu;h*=1099511628211ull;h^=(c>>8)&0xFFu;h*=1099511628211ull;
    }
    return h;
}
inline constexpr std::size_t kVariantBloomBytes=32;
inline void BloomAdd(unsigned char* bloom,std::uint64_t hash) noexcept {
    for(int k=0;k<3;++k){const auto bit=(hash>>(k*8))&0xFFu;bloom[bit>>3]|=static_cast<unsigned char>(1u<<(bit&7u));}
}
inline bool BloomHas(const unsigned char* bloom,std::uint64_t hash) noexcept {
    for(int k=0;k<3;++k){const auto bit=(hash>>(k*8))&0xFFu;if(!(bloom[bit>>3]&(1u<<(bit&7u))))return false;}
    return true;
}
}  // namespace crew
