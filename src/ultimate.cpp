// The ultimate strike (docs/plan-2026-10-19-ultimate.md; the user, 2026-10-19: 「特效拉满 昏天暗地」「不怕性能问题，误伤也
// 没关系」「清一片区域的终极技能」): the satellite call EDF6VC_CALL_ULTIMATE's fire (airstrike.cpp, the satellite's fire
// state) starts it; UltimateTick plays it a frame at a time from src/ultimate_plan.h's timeline and seed.
//  - rounds: the plugin's DemoIndirectFire SGOs (jet_bay.cpp EmcFire, kEmcFiles ult*; tools/make_ultimate.py), owned by the
//    caller (its kills; friends inside the area are hit too: the user's 「误伤也没关系」 — the rounds' team filter is the
//    game's, as every other round of the caller's);
//  - effects without damage: EffectGenUtil's explosions, fires, smoke and camera quake, StencilLight flashes (edf41classes.h);
//  - the sky: the game's own light fade manager, what the missions' EventLightTool_EnvFadeAnimation calls (EnvFade below),
//    faded into the dark at the start and back to what it was (a snapshot of the renderer's values) at the end.
//  - the circles and shock waves: flat disc objects of the plugin's (FarEventObject, pylib/magic_circle.py);
//  - the sounds: the player's recordings (jetaudio.h UltClip) or else the game's own cues (Sound below); no speech.
// One show at a time (a second call while one plays is refused and logged). Offline only for now: online, the call's
// other machines do not see it (docs/plan-2026-10-19-ultimate.md, M3).
#include "crew.h"
#include "edf41classes.h"
#include "heli.h"          // MapRay
#include "jetaudio.h"      // the recordings (UltClip)
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
// A flat disc object of the plugin's (a magic circle, a shock wave): see the circles below.
struct Disc { unsigned char* obj; const void* ctrl; bool tried; };
struct Show {
    bool on=false;
    const unsigned char* caller=nullptr;
    float at[3]{};           // the centre, on the ground
    float damage=0.0f;       // the weapon's (+0x89C), shared out below
    std::uint64_t seed=0;
    ULONGLONG start=0;       // GameFrame() at frame 0
    int done=-1;             // the last frame played
    RoundObj aims[kAims]{},gather[kGatherRays]{},beam{},core{};
    bool snapped=false;
    float snap[kSlotCount][4]{};
    float plane[kGroundCircles]{};   // each ground circle's height: over the highest ground under it (CirclePlane)
    Disc circles[kCircleCount]{},waves[kWaveCount]{};
    bool heard=false;        // a recording of the show's played: the plugin's voices beat every frame (jetaudio.h Beat)
    int thunder=-100;        // the storm frame of the last thunder
} show;

// ---- the magic circles and the shock waves (pylib/magic_circle.py, tools/make_ultimate.py; ultimate_plan.h) ----
// Each a FarEventObject with no ragdoll, as the parachute's canopy (playerjet.cpp Chute*, docs/player-jet-re.md §8): its
// update only renders its model at its matrix (+0x60), which the plugin writes every frame (turned, its x and z rows
// scaled: the circle opens and closes, the wave runs out); nothing collides with it. Made with CreateObject (preloaded with
// the mission: PreloadUltimate), put on the neutral team, deleted when it has closed or the show ends; forgotten at a new
// mission.
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
    {L"app:/object/edf6vc_ult_circle_shock.sgo",L"EDF6VC_ULT_CIRCLE_SHOCK.SGO","shock"},
};
static_assert(sizeof(kCircleFiles)/sizeof(kCircleFiles[0])==kModelCount,"a file a model");
bool circleReady[kModelCount]{};   // preloaded this mission

bool DiscLive(const Disc& d) noexcept {
    unsigned char* const o=d.obj;
    return o && Readable(o,kObjTeam+4) && At<const void*>(o,0)==image+kFarEventVtable && At<const void*>(o,kObjSelfCtrl)==d.ctrl &&
           !(o[kObjFlagsAt]&kObjGone);
}
void DiscDrop(Disc& d) noexcept {
    __try { if(DiscLive(d))reinterpret_cast<void(*)(void*)>(image+kDeleteFn)(d.obj); }
    __except(FaultLog("ULTIMATE disc delete",GetExceptionInformation())){}
    d.obj=nullptr;d.ctrl=nullptr;
}
// A disc's matrix: turned by `angle` about the vertical, its x and z rows scaled by `scale`, at height `y`.
void DiscMatrix(float angle,float scale,float y,float* m) noexcept {
    const float c=std::cos(angle)*scale,s=std::sin(angle)*scale;
    const float out[16]={c,0,-s,0, 0,1,0,0, s,0,c,0, show.at[0],y,show.at[2],1};
    std::memcpy(m,out,sizeof(out));
}
// True when made (the first try only: a disc that cannot be made is not tried again this show).
bool DiscMake(Disc& d,CircleModel cm,const float* m) noexcept {
    d.tried=true;
    const int model=static_cast<int>(cm);
    if(!circleReady[model] || !At<void*>(image,kObjectMgr)) {
        Log("ULTIMATE %s disc not made: %s",kCircleFiles[model].name,!circleReady[model] ?
            "not preloaded this mission (python tools/make_ultimate.py, or the installer)" : "no object manager");
        return false;
    }
    CircleParam param{image+kInitParamVt,{}};
    unsigned char* o=nullptr;
    __try { o=reinterpret_cast<CreateFn>(image+kCreateObjectFn)(At<void*>(image,kObjectMgr),m,kCircleFiles[model].sgo,&param); }
    __except(FaultLog("ULTIMATE disc create (off this mission)",GetExceptionInformation())){circleReady[model]=false;return false;}
    if(!o)return false;
    if(At<const void*>(o,0)!=image+kFarEventVtable) {
        Log("ULTIMATE disc %p is no FarEventObject: deleted",o);
        reinterpret_cast<void(*)(void*)>(image+kDeleteFn)(o);
        return false;
    }
    SetObjectTeam(o,kTeamNeutral);
    d.obj=o;d.ctrl=At<const void*>(o,kObjSelfCtrl);
    return true;
}
// A disc this frame: made on its first shown frame (true then), put at `m`, deleted once not shown.
bool DiscFrame(Disc& d,CircleModel cm,bool shown,const float* m) noexcept {
    if(!shown) {
        if(d.obj)DiscDrop(d);
        return false;
    }
    if(!d.obj)return !d.tried && DiscMake(d,cm,m);
    __try {
        if(!DiscLive(d)){d.obj=nullptr;return false;}
        std::memcpy(d.obj+kObjMatrix,m,64);
    } __except(FaultLog("ULTIMATE disc move",GetExceptionInformation())){d.obj=nullptr;}
    return false;
}
void DiscsDrop() noexcept {
    for(Disc& d:show.circles)if(d.obj)DiscDrop(d);
    for(Disc& d:show.waves)if(d.obj)DiscDrop(d);
}
// The base height of circle `i`'s lift: the ground plane for a ground circle, the centre for a sky one.
float CircleBase(int i) noexcept { return kCircles[i].sky ? show.at[1] : show.plane[i]; }
// Circle `i`'s point at `rim` of its radius and `angle` on its turn on frame `frame`, in the world.
void CircleWorld(int i,int frame,float rim,float angle,float* out) noexcept {
    float p[3];
    CirclePoint(i,frame,rim,angle,p);
    out[0]=show.at[0]+p[0];out[1]=CircleBase(i)+p[1];out[2]=show.at[2]+p[2];
}
// Where the main beam starts on frame `frame`: the top sky circle's centre.
void BeamTop(int frame,float* out) noexcept {
    out[0]=show.at[0];out[1]=show.at[1]+CircleAt(kSkyTop,frame).lift;out[2]=show.at[2];
}

// ---- the sounds: the player's recordings (jetaudio.h UltClip: tools/sound_pack.py) or the game's own cues ----
// The game's: cues of the always-loaded TIKYUUX_SE.ACB (src/edf41classes.h PlayCue), the stock satellite's and the
// genocide weapons' (the 2026-10-20 user: 「缺少音效」; no speech: 「没人说你用说话的语音啊」).
struct Sfx { const wchar_t* cue; float volume,pitch; };
const Sfx kSfx[audio::kUltClipCount]={   // UltClip's order
    {L"weapon_gimmick_Engineer_genocide_open",1.0f,0.7f},          // charge
    {L"weapon_fencer_HM_gimmick_genocideExtend",0.9f,0.8f},        // a circle opens
    {L"common_damages_impactWD_ThunderCluster",1.0f,0.8f},         // thunder
    {L"weapon_Fencer_HM_chargeLv3",1.0f,0.6f},                     // the gather
    {L"common_damages_kuubakuGenocideBlaster",1.0f,1.0f},          // the main beam
    {L"vhc_common_explode2_XL",1.0f,1.0f},                         // a ring's blasts
    {L"common_damages_explode_LL",1.0f,0.7f},                      // a shock wave
};
constexpr float kSfxNear=300.0f;   // m: a recording at full volume within this, falling off as 1/r past it
void Sound(audio::UltClip c,const float* pos,float pitch=1.0f) noexcept {
    if(audio::UltClipThere(c)) {
        SoundPlace p{};
        const bool placed=SoundAt(pos,nullptr,&p);
        const float d=placed ? p.distance : 0.0f,g=kSfx[c].volume*(d>kSfxNear ? kSfxNear/d : 1.0f);
        audio::Beat(GameEffectVolume());
        show.heard=true;
        audio::PlayUlt(c,audio::Heard{g*(placed ? p.left : 1.0f),g*(placed ? p.right : 1.0f),pitch*(placed ? p.doppler : 1.0f),
                                      d/2500.0f<1.0f ? d/2500.0f : 1.0f});
        return;
    }
    edf41c::SeParams sp{};
    sp.volume=kSfx[c].volume;sp.pitch=kSfx[c].pitch*pitch;sp.inner=50.0f;sp.reference=600.0f;sp.cull=20.0f;
    edf41c::PlayCue(kSfx[c].cue,pos,nullptr,sp);
}

void CirclesFrame(int frame) noexcept {
    for(int i=0;i<kCircleCount;++i) {
        const CirclePose pose=CircleAt(i,frame);
        alignas(16) float m[16];
        DiscMatrix(pose.angle,pose.scale,CircleBase(i)+pose.lift,m);
        if(DiscFrame(show.circles[i],kCircles[i].model,pose.shown,m)) {
            const float c[3]={show.at[0],CircleBase(i)+pose.lift,show.at[2]};
            Sound(audio::kUltCircle,c,1.0f-0.08f*static_cast<float>(i));
        }
    }
}

// The damage's shares: a bolt (all its rounds), a ring's blast, the main beam's round (a frame, penetrating), a spark.
constexpr float kBoltShare=0.25f,kSparkShare=0.25f,kBlastShare=1.0f,kBeamShare=0.03f,kCoreShare=0.03f;

// The ground under (x, z) near the centre's height (the show's rounds land on it), else the centre's height.
float GroundAt(float x,float z) noexcept {
    const float from[3]={x,show.at[1]+400.0f,z},to[3]={x,show.at[1]-400.0f,z};
    float hit[3];
    return MapRay(from,to,hit)>=0.0f ? hit[1] : show.at[1];
}

// Ground circle `i`'s height: kPlaneOver over the highest ground under its disc as far as it opens out (the centre and rings
// out to kPlaneReach of its radius: a slope or a bump inside it does not cut through it), but never more than kPlaneMost over
// the centre (a cliff or a tower in the area: the circle cuts into it there).
float CirclePlane(int i) noexcept {
    constexpr int kRingsOut=5,kRound=32;
    const float reach=kModelRadius[static_cast<int>(kCircles[i].model)]*kCircles[i].size*kPlaneReach;
    float top=show.at[1];
    for(int r=1;r<=kRingsOut;++r)
        for(int k=0;k<kRound;++k) {
            const float a=2.0f*kPi*(static_cast<float>(k)+0.5f*static_cast<float>(r&1))/kRound,d=reach*static_cast<float>(r)/kRingsOut;
            const float y=GroundAt(show.at[0]+d*std::cos(a),show.at[2]+d*std::sin(a));
            if(y>top)top=y;
        }
    top+=kPlaneOver;
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
    for(auto& g:show.gather)Drop(g);
    Drop(show.beam);Drop(show.core);
}
// Round `r` of `kind` from `from` at `to` this frame at `size`: fired when there is none yet, else steered.
void Steer(RoundObj& r,EmcRound kind,const float* from,const float* to,float size) noexcept {
    if(!r.obj)r=EmcFire(kind,show.caller,from,to,0.0f);
    else if(!RoundSteer(r,from,to)){r=RoundObj{};return;}
    RoundSize(r,size);
}

const float kUp[3]={0.0f,1.0f,0.0f};
// The flashes' lights (StencilLight's raw colour: a stock explosion's helper scales its colour by 2.5): low, so the dark sky
// stays dark (2026-10-19 first run: 6.0 over 300 m turned the whole field pink, the beam's 5-6 washed the picture white).
const float kCrimson[4]={0.9f,0.08f,0.2f,1.0f},kBoltLight[4]={0.8f,1.0f,1.4f,1.0f},kViolet[4]={1.0f,0.3f,1.3f,1.0f};
const float kGold[4]={1.2f,0.9f,0.5f,1.0f};

void ChargeFrame(int f) noexcept {
    if(f==0) {
        Snap();
        Darken();
        edf41c::Quake(show.at,0.6f,kRadius*4.0f);
        Sound(audio::kUltCharge,show.at);
    }
    for(int i=0;i<kAims;++i) {
        const Aim a=AimAt(i,f);
        if(!a.shown){Drop(show.aims[i]);continue;}
        const float from[3]={show.at[0]+a.from[0],show.at[1]+a.from[1],show.at[2]+a.from[2]};   // a sky circle's rim
        const float x=show.at[0]+a.at[0],z=show.at[2]+a.at[1];
        const float to[3]={x,GroundAt(x,z),z};
        Steer(show.aims[i],EmcRound::ultSight,from,to,a.size);
    }
    if(f%30==0)edf41c::StencilLight(show.at,kCrimson,kRadius*0.8f,40);
    if(f%45==0)edf41c::Quake(show.at,0.25f+0.5f*f/kChargeEnd,kRadius*3.0f);
}

// The gather (show frame `frame`, kGatherAt..kStormEnd): each ray's pulse from its circle's rim to the beam's start.
void GatherFrame(int frame) noexcept {
    float top[3];
    BeamTop(frame,top);
    const int g=frame-kGatherAt;
    if(g==0)Sound(audio::kUltGather,top);
    for(int k=0;k<kGatherRays;++k) {
        const GatherRay r=GatherAt(k,frame);
        float rim[3];
        CircleWorld(r.circle,frame,kGatherRim,r.angle,rim);
        const float d[3]={top[0]-rim[0],top[1]-rim[1],top[2]-rim[2]};
        const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(!(len>kGatherReach)){Drop(show.gather[k]);continue;}
        const float s=r.slide*(len-kGatherReach)/len;   // the pulse ends on the beam's start at the latest
        const float from[3]={rim[0]+d[0]*s,rim[1]+d[1]*s,rim[2]+d[2]*s};
        Steer(show.gather[k],EmcRound::ultGather,from,top,r.size);
    }
    const float t=static_cast<float>(g)/kGather;
    if(g%8==0)edf41c::StencilLight(top,kGold,40.0f+160.0f*t,10);
    if(g%20==0)edf41c::Quake(show.at,0.3f+0.9f*t,kRadius*4.0f);
}

void StormFrame(int f) noexcept {
    const int frame=f+kChargeEnd;
    if(f==0)for(auto& a:show.aims)Drop(a);
    Bolt bolts[kMaxBoltsAFrame];
    const int n=BoltsAt(show.seed,f,bolts);
    const float lift=show.at[1]+CircleAt(kSkyLow,frame).lift;   // the lowest sky circle
    for(int k=0;k<n;++k) {
        const float x=show.at[0]+bolts[k].at[0],z=show.at[2]+bolts[k].at[1];
        const float to[3]={x,GroundAt(x,z),z};
        const float from[3]={show.at[0]+bolts[k].from[0],lift,show.at[2]+bolts[k].from[1]};
        EmcFire(EmcRound::ultBolt,show.caller,from,to,show.damage*kBoltShare/kBoltFrames);
        const float above[3]={to[0],to[1]+2.0f,to[2]};
        EmcFire(EmcRound::ultSpark,show.caller,above,to,show.damage*kSparkShare);
        edf41c::StencilLight(to,kBoltLight,110.0f,8);
        edf41c::Explosion(to,kUp,20.0f);   // the spark's size (ULT_SPARK_RADIUS 40): the storm fills the circle
        if(k==0 && f-show.thunder>=9) {    // a thunder now and then, never on top of the last
            show.thunder=f;
            Rng g=FrameRng(show.seed^0x7A0Dull,f);
            Sound(audio::kUltThunder,to,0.85f+0.3f*g.Unit());
        }
    }
    if(frame>=kGatherAt) {
        GatherFrame(frame);
        return;
    }
    if(f%20==0)edf41c::Quake(show.at,0.5f,kRadius*3.0f);
    if(f%50==25 && show.snapped) {   // the whole sky lights with the storm now and then
        Tint(kLight0,1.1f,1.2f,1.6f,0.05f);
    } else if(f%50==31) FlashBack(0.4f);
}

void CannonFrame(int f) noexcept {
    float top[3];
    BeamTop(f+kStormEnd,top);
    const float scale=BeamScale(f);
    if(f==0) {
        for(auto& g:show.gather)Drop(g);
        show.beam=EmcFire(EmcRound::ultBeam,show.caller,top,show.at,show.damage*kBeamShare);
        show.core=EmcFire(EmcRound::ultCore,show.caller,top,show.at,show.damage*kCoreShare);
        Flash(1.0f);
        edf41c::Quake(show.at,1.5f,kRadius*5.0f);
        Sound(audio::kUltBeam,show.at);
    }
    if(f==6)FlashBack(1.2f);
    if(scale>0.0f) {
        RoundSteer(show.beam,top,show.at);RoundSteer(show.core,top,show.at);
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
        Sound(audio::kUltBlast,show.at,1.0f-0.02f*static_cast<float>(k));
        edf41c::Quake(show.at,1.2f,kRadius*4.0f);
        if(k%3==0) {   // the flash of every third ring
            Tint(kLight0,1.3f,0.9f,1.5f,0.05f);
        } else if(k%3==1) FlashBack(0.5f);
    }
}

// The shock waves on show frame `frame`: each disc run out; a ground wave sounds as it starts and throws up dust at its
// front while that is still near the area; the great one shakes the world and flashes.
void WavesFrame(int frame) noexcept {
    for(int w=0;w<kWaveCount;++w) {
        const WaveSpec& s=kWaves[w];
        const WavePose p=WaveAt(w,frame);
        alignas(16) float m[16];
        DiscMatrix(p.angle,p.scale,(s.sky ? show.at[1] : show.plane[0])+s.lift,m);   // a ground wave: over the widest circle
        DiscFrame(show.waves[w],CircleModel::shock,p.shown,m);
        if(frame==s.at && !s.sky) {
            Sound(audio::kUltShock,show.at,w==kGreatWave ? 0.85f : 1.0f);
            if(w==kGreatWave) {
                Flash(1.3f);
                edf41c::Quake(show.at,2.5f,kRadius*8.0f);
            }
        }
        if(frame==s.at+8 && w==kGreatWave)FlashBack(1.5f);
        if(!p.shown || s.sky || (frame-s.at)%5 || p.radius>kRadius*1.6f)continue;
        for(int k=0;k<16;++k) {
            const float a=2.0f*kPi*(static_cast<float>(k)+0.5f*static_cast<float>((frame-s.at)/5%2))/16.0f;
            const float x=show.at[0]+p.radius*std::cos(a),z=show.at[2]+p.radius*std::sin(a);
            const float pt[3]={x,GroundAt(x,z),z};
            edf41c::Smoke(pt,w==kGreatWave ? 40.0f : 25.0f);
        }
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
    DiscsDrop();
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
    for(int i=0;i<kGroundCircles;++i)show.plane[i]=CirclePlane(i);
    show.damage=std::isfinite(damage) && damage>0.0f ? damage : 0.0f;
    show.seed=seed;
    show.start=GameFrame();
    (void)audio::ClipsReady();   // the recordings read on their thread from the first call: ready by the first sound
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
        if(f<kAfterEnd) {
            CirclesFrame(f);
            WavesFrame(f);
        }
        switch(PhaseAt(f)) {
            case Phase::charge: ChargeFrame(f); break;
            case Phase::storm: StormFrame(f-kChargeEnd); break;
            case Phase::cannon: CannonFrame(f-kStormEnd); break;
            case Phase::after: AfterFrame(f-kCannonEnd); break;
            case Phase::over: Finish("its time"); break;
        }
    }
    // The plugin's voices go quiet without a beat (paused: no tick, no beat): the recordings play on with the show.
    if(show.heard && audio::Running())audio::Beat(GameEffectVolume());
}

// A mission's start (jet_spawn.cpp, with the shells): the circles' objects preloaded when their files are installed.
void PreloadUltimate(void* mgr) noexcept {
    int ready=0;
    for(int i=0;i<kModelCount;++i) {
        circleReady[i]=mgr && jet::ModFileThere(kCircleFiles[i].file);
        if(circleReady[i]){reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreloadFn)(mgr,kCircleFiles[i].sgo,2,-1);++ready;}
    }
    Log("ULTIMATE preload: %d of %d magic circle models",ready,kModelCount);
}

void ResetUltimate() noexcept {
    if(show.on)Log("ULTIMATE a new mission: the show is forgotten");
    show=Show{};
}
}  // namespace crew
