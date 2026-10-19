// The ultimate strike (docs/plan-2026-10-19-ultimate.md; the user, 2026-10-19: 「特效拉满 昏天暗地」「不怕性能问题，误伤也
// 没关系」「清一片区域的终极技能」): the satellite call EDF6VC_CALL_ULTIMATE's fire (airstrike.cpp, the satellite's fire
// state) starts it; UltimateTick plays it a frame at a time from src/ultimate_plan.h's timeline and seed.
//  - rounds: the plugin's DemoIndirectFire SGOs (jet_bay.cpp EmcFire, kEmcFiles ult*; tools/make_ultimate.py), owned by the
//    caller (its kills; friends inside the area are hit too: the user's 「误伤也没关系」 — the rounds' team filter is the
//    game's, as every other round of the caller's);
//  - effects without damage: EffectGenUtil's explosions, fires, smoke and camera quake, StencilLight flashes (edf41classes.h);
//  - the sky: the game's own light fade manager, what the missions' EventLightTool_EnvFadeAnimation calls (EnvFade below),
//    faded into the dark at the start and back to what it was (a snapshot of the renderer's values) at the end.
// One show at a time (a second call while one plays is refused and logged). Offline only for now: online, the call's
// other machines do not see it (docs/plan-2026-10-19-ultimate.md, M3).
#include "crew.h"
#include "edf41classes.h"
#include "heli.h"          // MapRay
#include "memory.h"
#include "ultimate_plan.h"
#include <cmath>
#include <cstring>

namespace crew {
int FaultLog(const char* where,const EXCEPTION_POINTERS* e) noexcept;   // jet.cpp
namespace {
using namespace ultimate;

// ---- the sky: the light fade manager (docs/plan-2026-10-19-ultimate.md §天色) ----
// The manager: *(EDF+0x20B34A0), made per scene (ctor 0x5BC830); 0x5BEFA0 would make one when there is none, so it is read,
// never asked for. EnvFade 0x5C1160(mgr, type, const float4* target, float seconds): the missions' EnvFadeAnimation (its
// script thunks 0x1A5FB0 / 0x1D4430 pass the seconds as they are; MISSION.AC calls it with 3, 5, 20 s). Its types write the
// renderer *(EDF+0x20B2958) (0x5C0A20's jump table 0x5C10E4) or, from SKY_COLOR on, the light environment
// *(*(EDF+0x20B2990)+0x258); read here for the snapshot the show fades back to.
constexpr unsigned kFadeMgr=0x20B34A0,kEnvFade=0x5C1160,kRenderer=0x20B2958,kEnvRoot=0x20B2990,kMgrGet=0x5BEFA0;
constexpr std::size_t kEnvOf=0x258;
const unsigned char kEnvFadeSig[]={0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x18,0xF3,0x0F,0x11,0x5C,0x24,0x20,0x89,0x54,0x24};
// 0x5BEFA4: mov rax,[rip+0x1AF44F5] (= kFadeMgr), the global the getter reads
const unsigned char kMgrGetSig[]={0x48,0x83,0xEC,0x68,0x48,0x8B,0x05,0xF5,0x44,0xAF,0x01,0x48,0x85,0xC0};
enum FadeType : int { kLight0=0, kAmbient=12, kFogDistance=13, kFogColor=14, kFogBg=16, kSky=21 };
struct Slot { FadeType type; bool env; std::size_t at; };   // where the type's value lives (renderer or environment)
constexpr Slot kSlots[]={{kLight0,false,0xB50},{kAmbient,false,0xC00},{kFogDistance,false,0xC30},{kFogColor,false,0xC20},
                         {kFogBg,false,0xC3C},{kSky,true,0x130}};
constexpr int kSlotCount=static_cast<int>(sizeof(kSlots)/sizeof(kSlots[0]));
using EnvFadeFn=void(__fastcall*)(void*,int,const float*,float);
int skyOk=-1;   // the code is the one read (-1: not looked yet)

bool SkyOk() noexcept {
    if(skyOk<0) {
        skyOk=Matches(kEnvFade,kEnvFadeSig,sizeof(kEnvFadeSig)) && Matches(kMgrGet,kMgrGetSig,sizeof(kMgrGetSig)) ? 1 : 0;
        if(!skyOk)Log("ULTIMATE the light fade code (0x5C1160 / 0x5BEFA0) is not as known: the sky stays as it is");
    }
    return skyOk==1;
}
void* FadeMgr() noexcept {
    return SkyOk() && Readable(image+kFadeMgr,8) ? At<void*>(image,kFadeMgr) : nullptr;
}
// The manager made when the scene has none yet (the test range's scripts never fade, so none is made there): its getter
// 0x5BEFA0, as every script call of EventLightTool_EnvFadeAnimation does (0x1A5FDC). Only from a show, so inside a
// mission's scene (outside one the getter would make a manager no scene owns).
using MgrGetFn=void*(__fastcall*)();
void* FadeMgrMade() noexcept {
    if(void* const mgr=FadeMgr())return mgr;
    if(!SkyOk())return nullptr;
    void* const mgr=reinterpret_cast<MgrGetFn>(image+kMgrGet)();
    Log("ULTIMATE light fade manager: none in this scene, made %p",mgr);
    return mgr;
}
unsigned char* SlotBase(const Slot& s) noexcept {
    if(!s.env)return Readable(image+kRenderer,8) ? At<unsigned char*>(image,kRenderer) : nullptr;
    unsigned char* const root=Readable(image+kEnvRoot,8) ? At<unsigned char*>(image,kEnvRoot) : nullptr;
    return root && Readable(root+kEnvOf,8) ? At<unsigned char*>(root,kEnvOf) : nullptr;
}
// The value type `t` has now (4 floats; the fog distance's last two and the fog background's last three unused), or false.
bool SlotNow(FadeType t,float* out) noexcept {
    for(const Slot& s:kSlots) {
        if(s.type!=t)continue;
        unsigned char* const b=SlotBase(s);
        if(!b || !Readable(b+s.at,16))return false;
        std::memcpy(out,b+s.at,16);
        return std::isfinite(out[0]+out[1]+out[2]+out[3]);
    }
    return false;
}
void Fade(FadeType t,const float* to,float sec) noexcept {
    void* const mgr=FadeMgr();
    if(!mgr)return;
    alignas(16) const float v[4]={to[0],to[1],to[2],to[3]};
    reinterpret_cast<EnvFadeFn>(image+kEnvFade)(mgr,static_cast<int>(t),v,sec);
}

// ---- the show ----
struct Show {
    bool on=false;
    const unsigned char* caller=nullptr;
    float at[3]{};           // the centre, on the ground
    float damage=0.0f;       // the weapon's (+0x89C), shared out below
    std::uint64_t seed=0;
    ULONGLONG start=0;       // GameFrame() at frame 0
    int done=-1;             // the last frame played
    RoundObj aims[kAims]{},beam{},core{};
    bool snapped=false;
    float snap[kSlotCount][4]{};
    float plane=0.0f;        // the ground circles' height: a little over the area's highest ground (CirclePlane)
    struct { unsigned char* obj; const void* ctrl; bool tried; } circles[kCircleCount]{};
} show;

// ---- the magic circles (pylib/magic_circle.py, tools/make_ultimate.py; ultimate_plan.h kCircles) ----
// Each a FarEventObject with no ragdoll, as the parachute's canopy (playerjet.cpp Chute*, docs/player-jet-re.md §8): its
// update only renders its model at its matrix (+0x60), which the plugin writes every frame (turned, its x and z rows
// scaled: the circle opens and closes); nothing collides with it. Made with CreateObject (preloaded with the mission:
// PreloadUltimate), put on the neutral team, deleted when it has closed or the show ends; forgotten at a new mission.
constexpr unsigned kFarEventVtable=0x17D5DA0,kCreateObjectFn=0x11945E0,kInitParamVt=0x1762068,kObjectMgr=0x20B2958;
constexpr unsigned kPreloadFn=0x7A3780,kDeleteFn=0x118A1B0;
constexpr std::size_t kObjMatrix=0x60,kObjSelfCtrl=0x30,kObjTeam=0x314,kObjFlagsAt=0x18;
constexpr unsigned char kObjGone=4;
struct alignas(16) CircleParam { const void* vtable; unsigned char rest[0x28]; };
using CreateFn=unsigned char*(*)(void*,const float*,const wchar_t*,CircleParam*);
struct CircleFile { const wchar_t* sgo; const wchar_t* file; const char* name; };
const CircleFile kCircleFiles[]={   // CircleModel's order
    {L"app:/object/edf6vc_ult_circle_outer.sgo",L"EDF6VC_ULT_CIRCLE_OUTER.SGO","outer"},
    {L"app:/object/edf6vc_ult_circle_star.sgo",L"EDF6VC_ULT_CIRCLE_STAR.SGO","star"},
    {L"app:/object/edf6vc_ult_circle_core.sgo",L"EDF6VC_ULT_CIRCLE_CORE.SGO","core"},
    {L"app:/object/edf6vc_ult_circle_sky.sgo",L"EDF6VC_ULT_CIRCLE_SKY.SGO","sky"},
};
static_assert(sizeof(kCircleFiles)/sizeof(kCircleFiles[0])==static_cast<std::size_t>(CircleModel::sky)+1,"a file a model");
bool circleReady[sizeof(kCircleFiles)/sizeof(kCircleFiles[0])]{};   // preloaded this mission

bool CircleLive(int i) noexcept {
    unsigned char* const o=show.circles[i].obj;
    return o && Readable(o,kObjTeam+4) && At<const void*>(o,0)==image+kFarEventVtable &&
           At<const void*>(o,kObjSelfCtrl)==show.circles[i].ctrl && !(o[kObjFlagsAt]&kObjGone);
}
void CircleDrop(int i) noexcept {
    __try { if(CircleLive(i))reinterpret_cast<void(*)(void*)>(image+kDeleteFn)(show.circles[i].obj); }
    __except(FaultLog("ULTIMATE circle delete",GetExceptionInformation())){}
    show.circles[i].obj=nullptr;show.circles[i].ctrl=nullptr;
}
// Circle `i`'s matrix for `pose`: turned by its angle about the vertical, its x and z rows scaled, at its height.
void CircleMatrix(int i,const CirclePose& pose,float* m) noexcept {
    const float c=std::cos(pose.angle)*pose.scale,s=std::sin(pose.angle)*pose.scale;
    const float y=(kCircles[i].sky ? show.at[1] : show.plane)+pose.lift;
    const float out[16]={c,0,-s,0, 0,1,0,0, s,0,c,0, show.at[0],y,show.at[2],1};
    std::memcpy(m,out,sizeof(out));
}
void CircleMake(int i,const float* m) noexcept {
    show.circles[i].tried=true;
    const int model=static_cast<int>(kCircles[i].model);
    if(!circleReady[model] || !At<void*>(image,kObjectMgr)) {
        Log("ULTIMATE circle %d (%s) not made: %s",i,kCircleFiles[model].name,!circleReady[model] ?
            "not preloaded this mission (python tools/make_ultimate.py, or the installer)" : "no object manager");
        return;
    }
    CircleParam param{image+kInitParamVt,{}};
    unsigned char* o=nullptr;
    __try { o=reinterpret_cast<CreateFn>(image+kCreateObjectFn)(At<void*>(image,kObjectMgr),m,kCircleFiles[model].sgo,&param); }
    __except(FaultLog("ULTIMATE circle create (off this mission)",GetExceptionInformation())){circleReady[model]=false;return;}
    if(!o)return;
    if(At<const void*>(o,0)!=image+kFarEventVtable) {
        Log("ULTIMATE circle %p is no FarEventObject: deleted",o);
        reinterpret_cast<void(*)(void*)>(image+kDeleteFn)(o);
        return;
    }
    SetObjectTeam(o,kTeamNeutral);
    show.circles[i].obj=o;show.circles[i].ctrl=At<const void*>(o,kObjSelfCtrl);
}
// Every circle on show frame `frame`: made on its first, put where its pose says, deleted once closed.
void CirclesFrame(int frame) noexcept {
    for(int i=0;i<kCircleCount;++i) {
        const CirclePose pose=CircleAt(i,frame);
        if(!pose.shown) {
            if(show.circles[i].obj)CircleDrop(i);
            continue;
        }
        alignas(16) float m[16];
        CircleMatrix(i,pose,m);
        if(!show.circles[i].obj) {
            if(!show.circles[i].tried)CircleMake(i,m);
            continue;
        }
        __try {
            if(!CircleLive(i)){show.circles[i].obj=nullptr;continue;}
            std::memcpy(show.circles[i].obj+kObjMatrix,m,sizeof(m));
        } __except(FaultLog("ULTIMATE circle move",GetExceptionInformation())){show.circles[i].obj=nullptr;}
    }
}
void CirclesDrop() noexcept { for(int i=0;i<kCircleCount;++i)if(show.circles[i].obj)CircleDrop(i); }

// The damage's shares: a bolt (all its rounds), a ring's blast, the main beam's round (a frame, penetrating), a spark.
constexpr float kBoltShare=0.25f,kSparkShare=0.25f,kBlastShare=1.0f,kBeamShare=0.03f,kCoreShare=0.03f;

// The ground under (x, z) near the centre's height (the show's rounds land on it), else the centre's height.
float GroundAt(float x,float z) noexcept {
    const float from[3]={x,show.at[1]+400.0f,z},to[3]={x,show.at[1]-400.0f,z};
    float hit[3];
    return MapRay(from,to,hit)>=0.0f ? hit[1] : show.at[1];
}

// The ground circles' height: over the highest ground of the centre and a ring at 60 % of the area (so a slope does not cut
// through them), but never more than kPlaneMost over the centre (a cliff in the area: the circle cuts into it there).
constexpr float kPlaneMost=35.0f;
float CirclePlane() noexcept {
    float top=show.at[1];
    for(int k=0;k<12;++k) {
        const float a=2.0f*kPi*static_cast<float>(k)/12.0f;
        const float y=GroundAt(show.at[0]+0.6f*kRadius*std::cos(a),show.at[2]+0.6f*kRadius*std::sin(a));
        if(y>top)top=y;
    }
    return top<show.at[1]+kPlaneMost ? top : show.at[1]+kPlaneMost;
}

void Snap() noexcept {
    show.snapped=FadeMgrMade()!=nullptr;
    for(int i=0;i<kSlotCount && show.snapped;++i)show.snapped=SlotNow(kSlots[i].type,show.snap[i]);
    if(!show.snapped)Log("ULTIMATE no light fade manager or its values this scene: the sky stays as it is");
}
const float* Snapped(FadeType t) noexcept {
    for(int i=0;i<kSlotCount;++i)if(kSlots[i].type==t)return show.snap[i];
    return nullptr;
}
// The snapshot's colour of `t` times (r, g, b), its alpha kept, faded to over `sec`.
void Tint(FadeType t,float r,float g,float b,float sec) noexcept {
    const float* const s=Snapped(t);
    if(!show.snapped || !s)return;
    const float v[4]={s[0]*r,s[1]*g,s[2]*b,s[3]};
    Fade(t,v,sec);
}
void Darken() noexcept {
    if(!show.snapped)return;
    const float fog[4]={0.32f,0.03f,0.09f,1.0f};
    Fade(kFogColor,fog,kSkyDarkSec);
    const float* const d=Snapped(kFogDistance);
    const float dist[4]={40.0f,d && d[1]>1800.0f ? 1800.0f : (d ? d[1] : 1800.0f),d ? d[2] : 0.0f,d ? d[3] : 0.0f};
    Fade(kFogDistance,dist,kSkyDarkSec);
    const float bg[4]={0.85f,0.0f,0.0f,0.0f};
    Fade(kFogBg,bg,kSkyDarkSec);
    Tint(kAmbient,0.35f,0.12f,0.22f,kSkyDarkSec);
    Tint(kLight0,0.45f,0.15f,0.3f,kSkyDarkSec);
    Tint(kSky,0.55f,0.08f,0.18f,kSkyDarkSec);
}
// The main beam's flash: the light and ambient white-violet at once, back to the dark over `sec`.
// (The way back: FlashBack, a few frames later; a new fade of a type replaces the one running.)
void Flash(float strength) noexcept {
    if(!show.snapped)return;
    Tint(kLight0,1.6f*strength,1.2f*strength,2.0f*strength,0.08f);
    Tint(kAmbient,1.3f*strength,1.0f*strength,1.6f*strength,0.08f);
}
void FlashBack(float sec) noexcept {
    Tint(kLight0,0.45f,0.15f,0.3f,sec);
    Tint(kAmbient,0.35f,0.12f,0.22f,sec);
}
void SkyBack(float sec) noexcept {
    if(!show.snapped)return;
    for(int i=0;i<kSlotCount;++i)Fade(kSlots[i].type,show.snap[i],sec);
}

void Drop(RoundObj& r) noexcept { if(r.obj)RoundDrop(r); }
void DropAll() noexcept {
    for(auto& a:show.aims)Drop(a);
    Drop(show.beam);Drop(show.core);
}

const float kUp[3]={0.0f,1.0f,0.0f};
// The flashes' lights (StencilLight's raw colour: a stock explosion's helper scales its colour by 2.5): low, so the dark sky
// stays dark (2026-10-19 first run: 6.0 over 300 m turned the whole field pink, the beam's 5-6 washed the picture white).
const float kCrimson[4]={0.9f,0.08f,0.2f,1.0f},kBoltLight[4]={0.8f,1.0f,1.4f,1.0f},kViolet[4]={1.0f,0.3f,1.3f,1.0f};

void ChargeFrame(int f) noexcept {
    if(f==0) {
        Snap();
        Darken();
        edf41c::Quake(show.at,0.6f,kRadius*4.0f);
    }
    for(int i=0;i<kAims;++i) {
        const Aim a=AimAt(i,f);
        const float from[3]={show.at[0]+a.from[0],show.at[1]+kAimHeight,show.at[2]+a.from[1]};
        const float x=show.at[0]+a.at[0],z=show.at[2]+a.at[1];
        const float to[3]={x,GroundAt(x,z),z};
        if(!show.aims[i].obj)show.aims[i]=EmcFire(EmcRound::ultSight,show.caller,from,to,0.0f);
        else if(!RoundSteer(show.aims[i],from,to)){show.aims[i]=RoundObj{};continue;}
        RoundSize(show.aims[i],a.size);
    }
    if(f%30==0)edf41c::StencilLight(show.at,kCrimson,kRadius*0.8f,40);
    if(f%45==0)edf41c::Quake(show.at,0.25f+0.5f*f/kChargeEnd,kRadius*3.0f);
}

void StormFrame(int f) noexcept {
    if(f==0)for(auto& a:show.aims)Drop(a);
    Bolt bolts[kMaxBoltsAFrame];
    const int n=BoltsAt(show.seed,f,bolts);
    for(int k=0;k<n;++k) {
        const float x=show.at[0]+bolts[k].at[0],z=show.at[2]+bolts[k].at[1];
        const float to[3]={x,GroundAt(x,z),z};
        const float from[3]={x+bolts[k].lean[0],to[1]+kBoltHeight,z+bolts[k].lean[1]};
        EmcFire(EmcRound::ultBolt,show.caller,from,to,show.damage*kBoltShare/kBoltFrames);
        const float above[3]={to[0],to[1]+2.0f,to[2]};
        EmcFire(EmcRound::ultSpark,show.caller,above,to,show.damage*kSparkShare);
        edf41c::StencilLight(to,kBoltLight,110.0f,8);
        edf41c::Explosion(to,kUp,20.0f);   // the spark's size (ULT_SPARK_RADIUS 40): the storm fills the circle
    }
    if(f%20==0)edf41c::Quake(show.at,0.5f,kRadius*3.0f);
    if(f%50==25 && show.snapped) {   // the whole sky lights with the storm now and then
        Tint(kLight0,1.1f,1.2f,1.6f,0.05f);
    } else if(f%50==31) FlashBack(0.4f);
}

void CannonFrame(int f) noexcept {
    const float top[3]={show.at[0],show.at[1]+kBeamHeight,show.at[2]};
    const float scale=BeamScale(f);
    if(f==0) {
        show.beam=EmcFire(EmcRound::ultBeam,show.caller,top,show.at,show.damage*kBeamShare);
        show.core=EmcFire(EmcRound::ultCore,show.caller,top,show.at,show.damage*kCoreShare);
        Flash(1.0f);
        edf41c::Quake(show.at,1.5f,kRadius*5.0f);
    }
    if(f==6)FlashBack(1.2f);
    if(scale>0.0f) {
        RoundSize(show.beam,kBeamSize*scale);
        RoundSize(show.core,kCoreSize*scale);
    }
    if(f%10==0)edf41c::StencilLight(show.at,kViolet,kRadius,14);
    const int k=RingAt(f);
    if(k>=0) {
        const Ring r=RingOf(k,show.seed);
        for(int i=0;i<r.count;++i) {
            const float a=r.phase+2.0f*kPi*static_cast<float>(i)/r.count;
            const float x=show.at[0]+r.radius*std::cos(a),z=show.at[2]+r.radius*std::sin(a);
            const float to[3]={x,GroundAt(x,z),z};
            const float above[3]={x,to[1]+3.0f,z};
            EmcFire(EmcRound::ultBlast,show.caller,above,to,show.damage*kBlastShare);
            edf41c::BigExplosion(to,kUp,kRingBlast*0.5f,1.0f);
        }
        edf41c::Quake(show.at,1.2f,kRadius*4.0f);
        if(k%3==0) {   // the flash of every third ring
            Tint(kLight0,1.3f,0.9f,1.5f,0.05f);
        } else if(k%3==1) FlashBack(0.5f);
    }
}

void AfterFrame(int f) noexcept {
    if(f==0)DropAll();
    float off[2];
    if(FireAt(show.seed,f,off)) {
        const float x=show.at[0]+off[0],z=show.at[2]+off[1];
        const float p[3]={x,GroundAt(x,z),z};
        edf41c::Blazing(p,kUp,6.0f);
        if(f%12==0)edf41c::Smoke(p,25.0f);
    }
    if(f==kSkyBackAt-kCannonEnd)SkyBack(kSkyBackSec);
}

void Finish(const char* why) noexcept {
    DropAll();
    CirclesDrop();
    Log("ULTIMATE over (%s)",why);
    show.on=false;
}
}  // namespace

bool UltimateStart(const unsigned char* caller,const float* at,float damage,std::uint64_t seed) noexcept {
    if(show.on) {
        Log("ULTIMATE a show is playing already: this call is spent with nothing");
        return false;
    }
    if(!caller || !at || !std::isfinite(at[0]+at[1]+at[2]) || !EmcRoundReady(EmcRound::ultBeam)) {
        Log("ULTIMATE not started: %s",!EmcRoundReady(EmcRound::ultBeam) ? "its rounds were not preloaded this mission "
            "(python tools/make_ultimate.py, or the installer)" : "no caller / target");
        return false;
    }
    show=Show{};
    show.on=true;
    show.caller=caller;
    std::memcpy(show.at,at,12);
    show.at[1]=GroundAt(at[0],at[2]);
    show.plane=CirclePlane();
    show.damage=std::isfinite(damage) && damage>0.0f ? damage : 0.0f;
    show.seed=seed;
    show.start=GameFrame();
    const float* const by=reinterpret_cast<const float*>(caller+0x90);   // its position (the matrix's last row)
    Log("ULTIMATE at (%.0f,%.0f,%.0f), %.0f m from the caller, damage %.0f seed %016llX: charge %d, storm %d, cannon %d, after %d "
        "frames",show.at[0],show.at[1],show.at[2],std::hypot(by[0]-show.at[0],by[2]-show.at[2]),show.damage,
        static_cast<unsigned long long>(seed),kChargeEnd,kStormEnd-kChargeEnd,kCannonEnd-kStormEnd,kAfterEnd-kCannonEnd);
    return true;
}

void UltimateTick() noexcept {
    if(!show.on)return;
    const ULONGLONG now=GameFrame();
    const int frame=static_cast<int>(now-show.start);
    // Every frame from the last played to now (a hitch skips none: the rings and the sky must all happen)
    for(int f=show.done+1;f<=frame && show.on;++f) {
        show.done=f;
        if(f<kAfterEnd)CirclesFrame(f);
        switch(PhaseAt(f)) {
            case Phase::charge: ChargeFrame(f); break;
            case Phase::storm: StormFrame(f-kChargeEnd); break;
            case Phase::cannon: CannonFrame(f-kStormEnd); break;
            case Phase::after: AfterFrame(f-kCannonEnd); break;
            case Phase::over: Finish("its time"); break;
        }
    }
}

// A mission's start (jet_spawn.cpp, with the shells): the circles' objects preloaded when their files are installed.
void PreloadUltimate(void* mgr) noexcept {
    int ready=0;
    for(std::size_t i=0;i<sizeof(kCircleFiles)/sizeof(kCircleFiles[0]);++i) {
        circleReady[i]=mgr && jet::ModFileThere(kCircleFiles[i].file);
        if(circleReady[i]){reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreloadFn)(mgr,kCircleFiles[i].sgo,2,-1);++ready;}
    }
    Log("ULTIMATE preload: %d of 4 magic circles",ready);
}

void ResetUltimate() noexcept {
    if(show.on)Log("ULTIMATE a new mission: the show is forgotten");
    show=Show{};
}
}  // namespace crew
