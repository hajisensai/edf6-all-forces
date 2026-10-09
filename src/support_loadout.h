// Out-of-game support loadouts (the user, 2026-10-09: "在游戏外可以编辑小组，还有载具、npc的挂载，npc的外观颜色等。例如可以给
// 我叫的坦克全选ap弹，也可以一半ap一半he"). Pure text logic, no Windows or game: tests/support_loadout_test.cpp runs it
// offline, tools/support_loadout.py (the installer's editor and the SGO generator) mirrors it and
// tests/support_loadout_ini_test.py keeps the two equal. Feasibility and evidence: docs/feature-2026-10-09-loadout-editor.md.
//
// EDF6VehicleCrew.ini [VehicleCrew], the one source of truth (the plugin rereads it on every save):
//   SupportPreset_<KEY>=rifle@1E3A8A*2,rocket,sniper     a seated entry's soldiers in seat order (a leader every four):
//       kind            one of kSupportKindNames (the 14 stock AI templates: 5 Ranger, 5 Wing Diver, 4 Fencer weapons)
//       @RRGGBB         its primary colour (soldier_color change_color0), @RRGGBB:RRGGBB primary and secondary
//                       (change_color1); X for a colour left stock (@X:FFFFFF). No @: the stock look.
//       *n              n soldiers of that spec (1-12)
//     Empty or absent: the entry's own load (the ini weapons). More than the entry's seats, or malformed: refused, the own
//     load, and the problem named (HUD and log).
//   SupportTankRounds=AP            every support tank's main gun (TANK_CREWED and TANK_DELIVERY): AP or HE
//   SupportTankRounds=AP,HE,HE      a list: the k-th tank called this mission takes entry k (repeating)
//   SupportTankRounds=AP:1,HE:1     a ratio: tanks interleave to stay as close to it as whole tanks can (AP,HE,AP,HE...)
//     Empty or absent: HE (the stock V505_TANK_MISSION, as before).
// A colour and an AP gun need a generated SGO (tools/support_loadout.py writes Mods/OBJECT/<SupportLookFile> and
// EDF6VC_SUPPORT_TANK_AP.SGO). Missing, or a peer in the room (no capability bit is left: support_protocol.h), the soldier /
// tank comes stock, logged; the kinds and the composition themselves are networked (kCapLoadout).
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
// `text` (one SupportPreset_<KEY> value) into *out. `seats`: the entry's (SupportCallSeats; 0 = no seats, so any preset is
// refused). False with *out empty and `why` naming the fault; an empty text is true with count 0.
inline bool ParseSupportPreset(const wchar_t* text,int seats,SupportPreset* out,wchar_t* why,std::size_t whyCapacity) noexcept {
    using namespace loadout_detail;
    SupportPreset p{};
    if(why && whyCapacity)why[0]=0;
    bool ok=Tokens(text,[&](const wchar_t* a,const wchar_t* b) noexcept {
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
    if(ok && p.count>0 && p.count>seats) {
        ok=false;
        if(seats<=0)Say(why,whyCapacity,L"该单位没有可编组的座位");
        else Say(why,whyCapacity,L"人数超过该单位的座位数");
    }
    if(out)*out=ok ? p : SupportPreset{};
    return ok;
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

// --- The support tank's main gun ---
// HE: the stock V505_TANK_MISSION (its mission_setup gun is v_505tank_cannon01, the Blacker E1's 105 mm howitzer:
// RocketBullet01, an 8 m blast). AP: EDF6VC_SUPPORT_TANK_AP.SGO, the same hull with the Blacker A1's mount (EWEAPON419:
// v_505tank_cannon01s, the 90 mm smooth-bore: SolidBullet01Rail, penetrating, no blast).
enum class TankRound : std::uint8_t { he, ap, count };
struct TankRoundName { TankRound round; const wchar_t* name; const wchar_t* label; };
inline constexpr TankRoundName kTankRoundNames[]={{TankRound::he,L"HE",L"榴弹（原版）"},{TankRound::ap,L"AP",L"穿甲"}};
static_assert(sizeof(kTankRoundNames)/sizeof(kTankRoundNames[0])==static_cast<std::size_t>(TankRound::count),"one name per round");
inline constexpr int kRoundMixMost=16;
inline constexpr unsigned kRoundWeightMost=1000;
struct RoundMix {
    int count=0;          // 0: every tank HE (stock)
    bool ratio=false;     // weights (AP:1,HE:1) rather than a per-tank list (AP,HE,HE)
    TankRound round[kRoundMixMost]{};
    unsigned weight[kRoundMixMost]{};
};
inline bool ParseTankRound(const wchar_t* a,const wchar_t* b,TankRound* out) noexcept {
    loadout_detail::Trim(a,b);
    for(const auto& r:kTankRoundNames)if(loadout_detail::Same(a,b,r.name)){*out=r.round;return true;}
    return false;
}
// List: ROUND[*n],... (n repeats; at most kRoundMixMost tanks a cycle). Ratio: ROUND:weight,... with every token
// weighted (1..1000, a trailing % allowed). False with *out empty and `why` set.
inline bool ParseRoundMix(const wchar_t* text,RoundMix* out,wchar_t* why,std::size_t whyCapacity) noexcept {
    using namespace loadout_detail;
    RoundMix m{};int weighted=0,plain=0;
    if(why && whyCapacity)why[0]=0;
    bool ok=Tokens(text,[&](const wchar_t* a,const wchar_t* b) noexcept {
        const wchar_t* colon=Find(a,b,L':');
        const wchar_t* star=Find(a,colon,L'*');
        TankRound round;
        if(!ParseTankRound(a,star,&round)){Say(why,whyCapacity,L"未知弹种（AP / HE）：",a,star);return false;}
        if(colon<b) {
            const wchar_t* end=b;
            if(end>colon+1 && end[-1]==L'%')--end;
            int w=0;
            if(star<colon || !Count(colon+1,end,static_cast<int>(kRoundWeightMost),&w)){Say(why,whyCapacity,L"比例应为 弹种:1 到 1000：",a,b);return false;}
            if(m.count>=kRoundMixMost){Say(why,whyCapacity,L"弹种项过多（最多 16 项）");return false;}
            m.round[m.count]=round;m.weight[m.count]=static_cast<unsigned>(w);++m.count;++weighted;return true;
        }
        int n=1;
        if(star<b && !Count(star+1,b,kRoundMixMost,&n)){Say(why,whyCapacity,L"重复次数应为 *1 到 *16：",star,b);return false;}
        if(m.count+n>kRoundMixMost){Say(why,whyCapacity,L"逐车列表过长（一轮最多 16 辆）");return false;}
        for(int i=0;i<n;++i){m.round[m.count]=round;m.weight[m.count]=1;++m.count;}
        ++plain;return true;
    });
    if(ok && weighted && plain){ok=false;Say(why,whyCapacity,L"不能混用逐车列表与比例（要么 AP,HE,HE，要么 AP:1,HE:2）");}
    m.ratio=weighted>0;
    if(out)*out=ok ? m : RoundMix{};
    return ok;
}
// The round of the k-th tank (0-based) this mission. A list repeats; a ratio picks, at each tank, the round furthest
// below its share so far ((k+1)·w − W·given, ties to the earlier token): after any number of tanks every round is within
// one tank of its share, and 1:1 alternates starting with the first named.
inline TankRound PickRound(const RoundMix& m,unsigned k) noexcept {
    if(m.count<=0)return TankRound::he;
    if(!m.ratio)return m.round[k%static_cast<unsigned>(m.count)];
    long long total=0;
    for(int i=0;i<m.count;++i)total+=m.weight[i];
    long long given[kRoundMixMost]{};
    int pick=0;
    for(unsigned step=0;step<=k;++step) {
        long long best=0;pick=-1;
        for(int i=0;i<m.count;++i) {
            const long long deficit=static_cast<long long>(step+1)*m.weight[i]-total*given[i];
            if(pick<0 || deficit>best){best=deficit;pick=i;}
        }
        ++given[pick];
    }
    return m.round[pick];
}

// --- Generated files (tools/support_loadout.py writes them; the plugin derives the same names) ---
inline constexpr const wchar_t* kSupportTankApFile=L"EDF6VC_SUPPORT_TANK_AP.SGO";
inline constexpr const wchar_t* kSupportTankApPath=L"app:/object/edf6vc_support_tank_ap.sgo";
// EDF6VC_NPC_<KIND>[_L]_<PRIMARY|X>_<SECONDARY|X>.SGO, e.g. EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO: the stock template of that
// kind (its _LEADER for a leader) with its soldier_color entries recoloured. `path`: the CreateObject spelling instead
// (app:/object/<lower case>). Returns the length, 0 when it does not fit.
inline std::size_t SupportLookFile(SupportWeapon kind,bool leader,const SupportLook& look,wchar_t* out,std::size_t capacity,
                                   bool path=false) noexcept {
    if(!out || !capacity)return 0;
    wchar_t buf[96];std::size_t n=0;
    const auto put=[&](wchar_t c) noexcept {if(n+1<sizeof(buf)/sizeof(buf[0]))buf[n++]=c;};
    const auto puts=[&](const wchar_t* s) noexcept {for(;*s;++s)put(*s);};
    const auto colour=[&](std::int32_t v) noexcept {
        if(v==kStockColour){put(L'X');return;}
        for(int shift=20;shift>=0;shift-=4)put(L"0123456789ABCDEF"[(v>>shift)&0xF]);
    };
    puts(L"EDF6VC_NPC_");
    for(const wchar_t* s=SupportKindIniName(kind);*s;++s)put(static_cast<wchar_t>(std::towupper(*s)));
    if(leader)puts(L"_L");
    put(L'_');colour(look.primary);put(L'_');colour(look.secondary);puts(L".SGO");
    buf[n]=0;
    const wchar_t* prefix=path ? L"app:/object/" : L"";
    const std::size_t total=std::wcslen(prefix)+n;
    if(total+1>capacity){out[0]=0;return 0;}
    std::size_t o=0;
    for(const wchar_t* s=prefix;*s;++s)out[o++]=*s;
    for(std::size_t i=0;i<n;++i)out[o++]=path ? static_cast<wchar_t>(std::towlower(buf[i])) : buf[i];
    out[o]=0;
    return o;
}
}  // namespace crew
