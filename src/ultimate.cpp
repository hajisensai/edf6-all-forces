// The ultimate strike (docs/plan-2026-10-19-ultimate.md; the user, 2026-10-19: 「特效拉满 昏天暗地」「不怕性能问题，误伤也
// 没关系」「清一片区域的终极技能」): the satellite call EDF6VC_CALL_ULTIMATE's fire (airstrike.cpp, the satellite's fire
// state) starts it; UltimateTick plays it a frame at a time from src/ultimate_plan.h's timeline and seed.
//  - rounds: the plugin's DemoIndirectFire SGOs (jet_bay.cpp EmcFire, kEmcFiles ult*; tools/make_ultimate.py), owned by the
//    caller (its kills; friends inside the area are hit too: the user's 「误伤也没关系」 — the rounds' team filter is the
//    game's, as every other round of the caller's);
//  - effects without damage: EffectGenUtil's explosions, fires, smoke and camera quake, StencilLight flashes (edf41classes.h);
//  - the sky: the game's own light fade manager, what the missions' EventLightTool_EnvFadeAnimation calls (EnvFade below),
//    faded into the dark at the start and back to what it was (a snapshot of the renderer's values) at the end;
//  - the circles and shock waves: flat disc objects of the plugin's (far_object.h, pylib/magic_circle.py);
//  - the sounds: the player's recordings (jetaudio.h UltClip) or else the game's own cues (Sound below); no speech.
// Several shows at once (ultimate_show.h Table: a second call while one plays is a show of its own), this machine's
// player's and every other machine's player's (online: ultimate_net_protocol.h; the user, 2026-10-20: 「所有功能都要支持
// 联机」). Every machine plays a show whole from its numbers; its rounds deal damage on the caller's machine alone
// (jet_bay.cpp ShellMake: OnlineShotCounts). A show whose caller is gone (left the room, deleted) fires no more rounds and
// plays its effects out.
#include "command_identity.h"
#include "crew.h"
#include "edf41classes.h"
#include "far_object.h"
#include "heli.h"          // MapRay
#include "hudtext.h"
#include "jetaudio.h"      // the recordings (UltClip)
#include "memory.h"
#include "plugin_net.h"
#include "support_protocol.h"
#include "support_soldier.h"   // ReadNativeObjectId
#include "ultimate_net.h"
#include "ultimate_net_protocol.h"
#include "ultimate_plan.h"
#include "ultimate_show.h"
#include <cmath>
#include <cstdio>
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
// Where the type's value lives (renderer or environment) and how many of its four floats mean something (the fog
// distance's last two and the fog background's last three are unused).
struct Slot { FadeType type; bool env; std::size_t at; int used; };
constexpr Slot kSlots[]={{kLight0,false,0xB50,4},{kAmbient,false,0xC00,4},{kFogDistance,false,0xC30,2},{kFogColor,false,0xC20,4},
                         {kFogBg,false,0xC3C,1},{kSky,true,0x130,4}};
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
// The value slot `i` has now, or false.
bool SlotNow(int i,float* out) noexcept {
    unsigned char* const b=SlotBase(kSlots[i]);
    if(!b || !Readable(b+kSlots[i].at,16))return false;
    std::memcpy(out,b+kSlots[i].at,16);
    return std::isfinite(out[0]+out[1]+out[2]+out[3]);
}
int SlotOf(FadeType t) noexcept {
    for(int i=0;i<kSlotCount;++i)if(kSlots[i].type==t)return i;
    return -1;
}

// The dark sky, one for every show (ultimate_show.h SkyRefs): the snapshot taken by the first show to darken it, what the
// shows last faded each value to (`wrote`: SkyBack fades back only the values still at it), brought back by the last.
struct Sky {
    SkyRefs refs;
    float snap[kSlotCount][4]{};
    float wrote[kSlotCount][4]{};
    bool ours[kSlotCount]{};   // the shows faded it since the snapshot
} sky;

void Fade(FadeType t,const float* to,float sec) noexcept {
    void* const mgr=FadeMgr();
    const int i=SlotOf(t);
    if(!mgr || i<0)return;
    alignas(16) const float v[4]={to[0],to[1],to[2],to[3]};
    reinterpret_cast<EnvFadeFn>(image+kEnvFade)(mgr,static_cast<int>(t),v,sec);
    std::memcpy(sky.wrote[i],v,16);
    sky.ours[i]=true;
}
bool Snap() noexcept {
    bool ok=FadeMgrMade()!=nullptr;
    for(int i=0;i<kSlotCount && ok;++i)ok=SlotNow(i,sky.snap[i]);
    for(bool& o:sky.ours)o=false;
    if(!ok)Log("ULTIMATE no light fade manager or its values this scene: the sky stays as it is");
    return ok;
}
// The snapshot's colour of `t` times (r, g, b), its alpha kept, faded to over `sec`.
void Tint(FadeType t,float r,float g,float b,float sec) noexcept {
    const int i=SlotOf(t);
    if(!sky.refs.HasSnapshot() || i<0)return;
    const float* const s=sky.snap[i];
    const float v[4]={s[0]*r,s[1]*g,s[2]*b,s[3]};
    Fade(t,v,sec);
}
void Darken(float sec) noexcept {
    if(!sky.refs.HasSnapshot())return;
    const float fog[4]={0.32f,0.03f,0.09f,1.0f};
    Fade(kFogColor,fog,sec);
    const float* const d=sky.snap[SlotOf(kFogDistance)];
    const float dist[4]={40.0f,d[1]>1800.0f ? 1800.0f : d[1],d[2],d[3]};
    Fade(kFogDistance,dist,sec);
    const float bg[4]={0.85f,0.0f,0.0f,0.0f};
    Fade(kFogBg,bg,sec);
    Tint(kAmbient,0.35f,0.12f,0.22f,sec);
    Tint(kLight0,0.45f,0.15f,0.3f,sec);
    Tint(kSky,0.55f,0.08f,0.18f,sec);
}
// Back to the snapshot over `sec`: only the values the shows faded and that are still where they left them. A value the
// mission's script faded since (its own EnvFade: a storm coming in mid-show) is the mission's now, and stays (before
// 2026-10-20 the snapshot was written over it). The shows' fades are all done by then (the last, a flash's way back,
// 1.5 s after the last ring; the sky comes back 5 s later), so "where they left them" is their last target (推测: a fade
// ends on its target exactly; ultimate_show.h SkyStillOurs allows 2 %).
void SkyBack(float sec) noexcept {
    if(!sky.refs.HasSnapshot() || !FadeMgr())return;
    int back=0,kept=0;
    for(int i=0;i<kSlotCount;++i) {
        if(!sky.ours[i])continue;
        float now[4];
        if(SlotNow(i,now) && SkyStillOurs(now,sky.wrote[i],kSlots[i].used)){Fade(kSlots[i].type,sky.snap[i],sec);++back;}
        else ++kept;
        sky.ours[i]=false;
    }
    Log("ULTIMATE the sky comes back over %.0f s: %d values back to the snapshot, %d left as the mission faded them",sec,back,kept);
}
// The main beam's flash: the light and ambient white-violet at once, back to the dark over `sec`.
// (The way back: FlashBack, a few frames later; a new fade of a type replaces the one running.)
void Flash(float strength) noexcept {
    Tint(kLight0,1.6f*strength,1.2f*strength,2.0f*strength,0.08f);
    Tint(kAmbient,1.3f*strength,1.0f*strength,1.6f*strength,0.08f);
}
void FlashBack(float sec) noexcept {
    Tint(kLight0,0.45f,0.15f,0.3f,sec);
    Tint(kAmbient,0.35f,0.12f,0.22f,sec);
}

// ---- the shows ----
struct Show {
    bool on=false;
    bool remote=false;       // another machine's player's call
    Key key{};
    // The caller: a weak reference of the game's (its control block's weak count held, so the block outlives the object),
    // checked every frame: gone (its machine left, the object deleted), the show fires no more rounds and plays its
    // effects out. `by` the object, only while `live`.
    edf41c::WeakRef caller{};
    const unsigned char* by=nullptr;
    bool live=false;
    float at[3]{};           // the centre, on the ground
    float damage=0.0f;       // the weapon's (+0x89C), shared out below
    std::uint64_t seed=0;
    ULONGLONG start=0;       // GameFrame() at frame 0
    int done=-1;             // the last frame played
    int seekTo=-1;           // a seek asked for by a message (applied at the next tick), -1 none
    int sought=-1;           // the frame the last seek landed on (its lasting state made there, its one-offs dropped)
    RoundObj aims[kAims]{},gather[kGatherRays]{},beam{},core{};
    float plane[kGroundCircles]{};   // each ground circle's height: over the highest ground under it (CirclePlane)
    ObjRef circles[kCircleCount]{},waves[kWaveCount]{};
    bool circleTried[kCircleCount]{},waveTried[kWaveCount]{};
    bool skyHeld=false;      // this show holds the dark sky (sky.refs)
    bool heard=false;        // a recording of the show's played: the plugin's voices beat every frame (jetaudio.h Beat)
    int thunder=-100;        // the storm frame of the last thunder
    ULONGLONG keptAt=0;      // this machine's: when its last keep went out (GetTickCount64)
    ULONGLONG begunMs=0;     // GetTickCount64 when it began here
    bool stockMatched=false; // another machine's: a call of its caller's weapon here was matched to it (airstrike.cpp)
};
Show shows[kMaxShows];
Table table;
std::uint32_t origin=0,serial=0;   // this machine's shows: this mission's run, the last show's number

// ---- the magic circles and the shock waves (pylib/magic_circle.py, tools/make_ultimate.py; ultimate_plan.h) ----
// Each a FarEventObject (far_object.h) preloaded with the mission (PreloadUltimate), its matrix written every frame
// (turned, its x and z rows scaled: the circle opens and closes, the wave runs out), deleted when it has closed or the show
// ends; forgotten at a new mission.
constexpr unsigned kPreloadFn=0x7A3780;
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

// A disc's matrix: turned by `angle` about the vertical, its x and z rows scaled by `scale`, at height `y`.
void DiscMatrix(const Show& s,float angle,float scale,float y,float* m) noexcept {
    const float c=std::cos(angle)*scale,n=std::sin(angle)*scale;
    const float out[16]={c,0,-n,0, 0,1,0,0, n,0,c,0, s.at[0],y,s.at[2],1};
    std::memcpy(m,out,sizeof(out));
}
// A disc this frame: made on its first shown frame (true then: the first try only, a disc that cannot be made is not
// tried again this show), put at `m`, deleted once not shown.
bool DiscFrame(ObjRef& d,bool& tried,CircleModel cm,bool shown,const float* m) noexcept {
    if(!shown) {
        if(d)farobj::Drop(d);
        return false;
    }
    if(d) {
        if(!farobj::Move(d,m))d=ObjRef{};
        return false;
    }
    if(tried)return false;
    tried=true;
    const int model=static_cast<int>(cm);
    if(!circleReady[model] || !scene::ObjectManager()) {
        Log("ULTIMATE %s disc not made: %s",kCircleFiles[model].name,!circleReady[model] ?
            "not preloaded this mission (python tools/make_ultimate.py, or the installer)" : "no object manager");
        return false;
    }
    farobj::Made why=farobj::Made::none;
    d=farobj::Make(kCircleFiles[model].sgo,m,&why);
    if(why==farobj::Made::faulted)circleReady[model]=false;   // off this mission
    if(!d)Log("ULTIMATE %s disc not made: %s",kCircleFiles[model].name,farobj::MadeText(why));
    return static_cast<bool>(d);
}
void DiscsDrop(Show& s) noexcept {
    for(ObjRef& d:s.circles)if(d)farobj::Drop(d);
    for(ObjRef& d:s.waves)if(d)farobj::Drop(d);
}
// The base height of circle `i`'s lift: the ground plane for a ground circle, the centre for a sky one.
float CircleBase(const Show& s,int i) noexcept { return kCircles[i].sky ? s.at[1] : s.plane[i]; }
// Circle `i`'s point at `rim` of its radius and `angle` on its turn on frame `frame`, in the world.
void CircleWorld(const Show& s,int i,int frame,float rim,float angle,float* out) noexcept {
    float p[3];
    CirclePoint(i,frame,rim,angle,p);
    out[0]=s.at[0]+p[0];out[1]=CircleBase(s,i)+p[1];out[2]=s.at[2]+p[2];
}
// Where the main beam starts on frame `frame`: the top sky circle's centre.
void BeamTop(const Show& s,int frame,float* out) noexcept {
    out[0]=s.at[0];out[1]=s.at[1]+CircleAt(kSkyTop,frame).lift;out[2]=s.at[2];
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
void Sound(Show& s,audio::UltClip c,const float* pos,float pitch=1.0f) noexcept {
    if(audio::UltClipThere(c)) {
        SoundPlace p{};
        const bool placed=SoundAt(pos,nullptr,&p);
        const float d=placed ? p.distance : 0.0f,g=kSfx[c].volume*(d>kSfxNear ? kSfxNear/d : 1.0f);
        audio::Beat(GameEffectVolume());
        s.heard=true;
        audio::PlayUlt(c,audio::Heard{g*(placed ? p.left : 1.0f),g*(placed ? p.right : 1.0f),pitch*(placed ? p.doppler : 1.0f),
                                      d/2500.0f<1.0f ? d/2500.0f : 1.0f});
        return;
    }
    edf41c::SeParams sp{};
    sp.volume=kSfx[c].volume;sp.pitch=kSfx[c].pitch*pitch;sp.inner=50.0f;sp.reference=600.0f;sp.cull=20.0f;
    edf41c::PlayCue(kSfx[c].cue,pos,nullptr,sp);
}

// Frame `frame`: the circles opened, turned, closed; a circle's opening sounds (not on the frame a seek lands on: the
// circles there were open already).
void CirclesFrame(Show& s,int frame) noexcept {
    for(int i=0;i<kCircleCount;++i) {
        const CirclePose pose=CircleAt(i,frame);
        alignas(16) float m[16];
        DiscMatrix(s,pose.angle,pose.scale,CircleBase(s,i)+pose.lift,m);
        if(DiscFrame(s.circles[i],s.circleTried[i],kCircles[i].model,pose.shown,m) && frame!=s.sought) {
            const float c[3]={s.at[0],CircleBase(s,i)+pose.lift,s.at[2]};
            Sound(s,audio::kUltCircle,c,1.0f-0.08f*static_cast<float>(i));
        }
    }
}

// The damage's shares: a bolt (all its rounds), a ring's blast, the main beam's round (a frame, penetrating), a spark.
constexpr float kBoltShare=0.25f,kSparkShare=0.25f,kBlastShare=1.0f,kBeamShare=0.03f,kCoreShare=0.03f;

// The ground under (x, z) near the centre's height (the show's rounds land on it), else the centre's height.
float GroundAt(float x,float y,float z) noexcept {
    const float from[3]={x,y+400.0f,z},to[3]={x,y-400.0f,z};
    float hit[3];
    return MapRay(from,to,hit)>=0.0f ? hit[1] : y;
}
float GroundAt(const Show& s,float x,float z) noexcept { return GroundAt(x,s.at[1],z); }

// Ground circle `i`'s height over the centre `at` (on the ground): kPlaneOver over the highest ground under its disc as
// far as it opens out (the centre and rings out to kPlaneReach of its radius: a slope or a bump inside it does not cut
// through it), but never more than kPlaneMost over the centre (a cliff or a tower in the area: the circle cuts into it
// there). The caller's machine's (sent with the show): every machine's circles at the same heights.
float CirclePlane(const float* at,int i) noexcept {
    constexpr int kRingsOut=5,kRound=32;
    const float reach=kModelRadius[static_cast<int>(kCircles[i].model)]*kCircles[i].size*kPlaneReach;
    float top=at[1];
    for(int r=1;r<=kRingsOut;++r)
        for(int k=0;k<kRound;++k) {
            const float a=2.0f*kPi*(static_cast<float>(k)+0.5f*static_cast<float>(r&1))/kRound,d=reach*static_cast<float>(r)/kRingsOut;
            const float y=GroundAt(at[0]+d*std::cos(a),at[1],at[2]+d*std::sin(a));
            if(y>top)top=y;
        }
    top+=kPlaneOver;
    return top<at[1]+kPlaneMost ? top : at[1]+kPlaneMost;
}

void Drop(RoundObj& r) noexcept { if(r.obj)RoundDrop(r); }
void DropRounds(Show& s) noexcept {
    for(auto& a:s.aims)Drop(a);
    for(auto& g:s.gather)Drop(g);
    Drop(s.beam);Drop(s.core);
}
// A round of the show's, by its caller: none once the caller is gone.
RoundObj Fire(const Show& s,EmcRound kind,const float* from,const float* to,float damage) noexcept {
    return s.live ? EmcFire(kind,s.by,from,to,damage) : RoundObj{};
}
// Round `r` of `kind` from `from` at `to` this frame at `size`: fired when there is none yet, else steered.
void Steer(const Show& s,RoundObj& r,EmcRound kind,const float* from,const float* to,float size) noexcept {
    if(!r.obj)r=Fire(s,kind,from,to,0.0f);
    else if(!RoundSteer(r,from,to)){r=RoundObj{};return;}
    RoundSize(r,size);
}

const float kUp[3]={0.0f,1.0f,0.0f};
// The flashes' lights (StencilLight's raw colour: a stock explosion's helper scales its colour by 2.5): low, so the dark sky
// stays dark (2026-10-19 first run: 6.0 over 300 m turned the whole field pink, the beam's 5-6 washed the picture white).
const float kCrimson[4]={0.9f,0.08f,0.2f,1.0f},kBoltLight[4]={0.8f,1.0f,1.4f,1.0f},kViolet[4]={1.0f,0.3f,1.3f,1.0f};
const float kGold[4]={1.2f,0.9f,0.5f,1.0f};

// The dark sky as frame `frame` has it (ultimate_show.h LastingAt): taken (the snapshot by the first show to take it,
// darkened over what is left of the charge's fade) or let go (the last to let go brings it back).
void SkyFrame(Show& s,int frame) noexcept {
    const bool dark=LastingAt(frame).skyDark;
    if(dark && !s.skyHeld) {
        s.skyHeld=true;
        if(sky.refs.Acquire(GameFrame()))sky.refs.Snapped(Snap());
        Darken(DarkSecFrom(frame));
    } else if(!dark && s.skyHeld) {
        s.skyHeld=false;
        if(sky.refs.Release(GameFrame(),static_cast<std::uint64_t>(kSkyBackSec*kFps)))SkyBack(kSkyBackSec);
    }
}

void ChargeFrame(Show& s,int f) noexcept {
    if(f==0) {
        edf41c::Quake(s.at,0.6f,kRadius*4.0f);
        Sound(s,audio::kUltCharge,s.at);
    }
    for(int i=0;i<kAims;++i) {
        const Aim a=AimAt(i,f);
        if(!a.shown){Drop(s.aims[i]);continue;}
        const float from[3]={s.at[0]+a.from[0],s.at[1]+a.from[1],s.at[2]+a.from[2]};   // a sky circle's rim
        const float x=s.at[0]+a.at[0],z=s.at[2]+a.at[1];
        const float to[3]={x,GroundAt(s,x,z),z};
        Steer(s,s.aims[i],EmcRound::ultSight,from,to,a.size);
    }
    if(f%30==0)edf41c::StencilLight(s.at,kCrimson,kRadius*0.8f,40);
    if(f%45==0)edf41c::Quake(s.at,0.25f+0.5f*f/kChargeEnd,kRadius*3.0f);
}

// The gather (show frame `frame`, kGatherAt..kStormEnd): each ray's pulse from its circle's rim to the beam's start.
void GatherFrame(Show& s,int frame) noexcept {
    float top[3];
    BeamTop(s,frame,top);
    const int g=frame-kGatherAt;
    if(g==0)Sound(s,audio::kUltGather,top);
    for(int k=0;k<kGatherRays;++k) {
        const GatherRay r=GatherAt(k,frame);
        float rim[3];
        CircleWorld(s,r.circle,frame,kGatherRim,r.angle,rim);
        const float d[3]={top[0]-rim[0],top[1]-rim[1],top[2]-rim[2]};
        const float len=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(!(len>kGatherReach)){Drop(s.gather[k]);continue;}
        const float t=r.slide*(len-kGatherReach)/len;   // the pulse ends on the beam's start at the latest
        const float from[3]={rim[0]+d[0]*t,rim[1]+d[1]*t,rim[2]+d[2]*t};
        Steer(s,s.gather[k],EmcRound::ultGather,from,top,r.size);
    }
    const float t=static_cast<float>(g)/kGather;
    if(g%8==0)edf41c::StencilLight(top,kGold,40.0f+160.0f*t,10);
    if(g%20==0)edf41c::Quake(s.at,0.3f+0.9f*t,kRadius*4.0f);
}

void StormFrame(Show& s,int f) noexcept {
    const int frame=f+kChargeEnd;
    if(f==0)for(auto& a:s.aims)Drop(a);
    Bolt bolts[kMaxBoltsAFrame];
    const int n=BoltsAt(s.seed,f,bolts);
    const float lift=s.at[1]+CircleAt(kSkyLow,frame).lift;   // the lowest sky circle
    for(int k=0;k<n;++k) {
        const float x=s.at[0]+bolts[k].at[0],z=s.at[2]+bolts[k].at[1];
        const float to[3]={x,GroundAt(s,x,z),z};
        const float from[3]={s.at[0]+bolts[k].from[0],lift,s.at[2]+bolts[k].from[1]};
        Fire(s,EmcRound::ultBolt,from,to,s.damage*kBoltShare/kBoltFrames);
        const float above[3]={to[0],to[1]+2.0f,to[2]};
        Fire(s,EmcRound::ultSpark,above,to,s.damage*kSparkShare);
        edf41c::StencilLight(to,kBoltLight,110.0f,8);
        edf41c::Explosion(to,kUp,20.0f);   // the spark's size (ULT_SPARK_RADIUS 40): the storm fills the circle
        if(k==0 && f-s.thunder>=9) {       // a thunder now and then, never on top of the last
            s.thunder=f;
            Rng g=FrameRng(s.seed^0x7A0Dull,f);
            Sound(s,audio::kUltThunder,to,0.85f+0.3f*g.Unit());
        }
    }
    if(frame>=kGatherAt) {
        GatherFrame(s,frame);
        return;
    }
    if(f%20==0)edf41c::Quake(s.at,0.5f,kRadius*3.0f);
    if(f%50==25) {   // the whole sky lights with the storm now and then
        Tint(kLight0,1.1f,1.2f,1.6f,0.05f);
    } else if(f%50==31) FlashBack(0.4f);
}

void CannonFrame(Show& s,int f) noexcept {
    float top[3];
    const int frame=f+kStormEnd;
    BeamTop(s,frame,top);
    const float scale=BeamScale(f);
    if(f==0) {
        for(auto& g:s.gather)Drop(g);
        Flash(1.0f);
        edf41c::Quake(s.at,1.5f,kRadius*5.0f);
        Sound(s,audio::kUltBeam,s.at);
    }
    // The main beam lasts the cannon: fired on its first frame, or on the frame a seek lands on inside it.
    if((f==0 || frame==s.sought) && LastingAt(frame).beam && !s.beam.obj) {
        s.beam=Fire(s,EmcRound::ultBeam,top,s.at,s.damage*kBeamShare);
        s.core=Fire(s,EmcRound::ultCore,top,s.at,s.damage*kCoreShare);
    }
    if(f==6)FlashBack(1.2f);
    if(scale>0.0f) {
        RoundSteer(s.beam,top,s.at);RoundSteer(s.core,top,s.at);
        RoundSize(s.beam,kBeamSize*scale);
        RoundSize(s.core,kCoreSize*scale);
    }
    if(f%10==0)edf41c::StencilLight(s.at,kViolet,kRadius,14);
    const int k=RingAt(f);
    if(k>=0) {
        const Ring r=RingOf(k,s.seed);
        for(int i=0;i<r.count;++i) {
            const float a=r.phase+2.0f*kPi*static_cast<float>(i)/r.count;
            const float x=s.at[0]+r.radius*std::cos(a),z=s.at[2]+r.radius*std::sin(a);
            const float to[3]={x,GroundAt(s,x,z),z};
            const float above[3]={x,to[1]+3.0f,z};
            Fire(s,EmcRound::ultBlast,above,to,s.damage*kBlastShare);
            edf41c::BigExplosion(to,kUp,kRingBlast*0.5f,1.0f);
        }
        Sound(s,audio::kUltBlast,s.at,1.0f-0.02f*static_cast<float>(k));
        edf41c::Quake(s.at,1.2f,kRadius*4.0f);
        if(k%3==0) {   // the flash of every third ring
            Tint(kLight0,1.3f,0.9f,1.5f,0.05f);
        } else if(k%3==1) FlashBack(0.5f);
    }
}

// The shock waves on show frame `frame`: each disc run out; a ground wave sounds as it starts and throws up dust at its
// front while that is still near the area; the great one shakes the world and flashes.
void WavesFrame(Show& s,int frame) noexcept {
    for(int w=0;w<kWaveCount;++w) {
        const WaveSpec& ws=kWaves[w];
        const WavePose p=WaveAt(w,frame);
        alignas(16) float m[16];
        DiscMatrix(s,p.angle,p.scale,(ws.sky ? s.at[1] : s.plane[0])+ws.lift,m);   // a ground wave: over the widest circle
        DiscFrame(s.waves[w],s.waveTried[w],CircleModel::shock,p.shown,m);
        if(frame==ws.at && !ws.sky) {
            Sound(s,audio::kUltShock,s.at,w==kGreatWave ? 0.85f : 1.0f);
            if(w==kGreatWave) {
                Flash(1.3f);
                edf41c::Quake(s.at,2.5f,kRadius*8.0f);
            }
        }
        if(frame==ws.at+8 && w==kGreatWave)FlashBack(1.5f);
        if(!p.shown || ws.sky || (frame-ws.at)%5 || p.radius>kRadius*1.6f)continue;
        for(int k=0;k<16;++k) {
            const float a=2.0f*kPi*(static_cast<float>(k)+0.5f*static_cast<float>((frame-ws.at)/5%2))/16.0f;
            const float x=s.at[0]+p.radius*std::cos(a),z=s.at[2]+p.radius*std::sin(a);
            const float pt[3]={x,GroundAt(s,x,z),z};
            edf41c::Smoke(pt,w==kGreatWave ? 40.0f : 25.0f);
        }
    }
}

void AfterFrame(Show& s,int f) noexcept {
    if(f==0)DropRounds(s);
    float off[2];
    if(FireAt(s.seed,f,off)) {
        const float x=s.at[0]+off[0],z=s.at[2]+off[1];
        const float p[3]={x,GroundAt(s,x,z),z};
        edf41c::Blazing(p,kUp,6.0f);
        if(f%12==0)edf41c::Smoke(p,25.0f);
    }
}

// ---- online (ultimate_net_protocol.h) ----
// The caller's native ID for a message, or false (the caller has none: nobody else can find it).
bool Message(const Show& s,ultimate_net::Kind kind,ultimate_net::Msg& m) noexcept {
    m=ultimate_net::Msg{};
    m.kind=kind;m.origin=s.key.origin;m.serial=s.key.serial;
    m.frame=s.done<0 ? 0 : s.done>kAfterEnd ? kAfterEnd : s.done;
    m.seed=s.seed;m.damage=s.damage;
    std::memcpy(m.at,s.at,12);std::memcpy(m.plane,s.plane,sizeof(m.plane));
    bool ok=false;
    __try { ok=s.live && ReadNativeObjectId(s.by,m.caller); } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    return ok;
}
// This machine's show `s` to every peer that has the channel (or to `peer` alone).
void Send(Show& s,ultimate_net::Kind kind,std::uint32_t peer=0) noexcept {
    if(s.remote || !plugin_net::Ready())return;
    ultimate_net::Msg m;
    unsigned char bytes[ultimate_net::kWire];
    if(!Message(s,kind,m) || !ultimate_net::Encode(m,bytes,sizeof(bytes)))return;
    if(peer)plugin_net::Send(peer,support_net::kExtUltimate,bytes,sizeof(bytes));
    else plugin_net::Broadcast(support_net::kExtUltimate,bytes,sizeof(bytes));
    s.keptAt=GetTickCount64();
}

// The caller's HUD line (hud.cpp UltimateBanner, the draw thread): a peer without the channel sees the stock beam.
SRWLOCK cueLock=SRWLOCK_INIT;
wchar_t cueText[160]{};
ULONGLONG cueAt=0;
void SayOldPeers() noexcept {
    const std::uint32_t n=plugin_net::PeerCount();
    std::uint32_t old=0;
    for(std::uint32_t p=1;p<=n;++p)if(!plugin_net::PeerHas(p,support_net::kExtUltimate))++old;
    if(!old)return;
    Log("ULTIMATE %u of %u peers did not announce the ultimate's channel (an older build): they may see the stock beam",old,n);
    AcquireSRWLockExclusive(&cueLock);
    _snwprintf_s(cueText,_TRUNCATE,L"%ls",hudtext::Tr(hudtext::Tx::ultimateOldPeers));
    cueAt=GetTickCount64();
    ReleaseSRWLockExclusive(&cueLock);
}

// ---- a show's life ----
void Clear(Show& s) noexcept {
    edf41c::WeakReset(s.caller);
    s=Show{};
}
// Show `s` (slot `i`) over: its rounds and discs deleted, the sky let go of (the last brings it back), its end said.
void Finish(int i,const char* why) noexcept {
    Show& s=shows[i];
    if(!s.on)return;
    if(!s.remote)Send(s,ultimate_net::Kind::end);
    DropRounds(s);
    DiscsDrop(s);
    if(s.skyHeld) {
        s.skyHeld=false;
        if(sky.refs.Release(GameFrame(),static_cast<std::uint64_t>(kSkyBackSec*kFps)))SkyBack(kSkyBackSec);
    }
    Log("ULTIMATE %s show %08X:%u over (%s)",s.remote ? "a peer's" : "this machine's",s.key.origin,s.key.serial,why);
    table.Release(i);
    Clear(s);
}

// The caller still there (its weak reference's object alive)? Gone: no more rounds (C: before 2026-10-20 the show kept
// firing rounds by a pointer to the freed object).
void CheckCaller(Show& s) noexcept {
    bool live=false;
    __try { live=edf41c::WeakAlive(s.caller); } __except(EXCEPTION_EXECUTE_HANDLER){live=false;}
    if(live || !s.live)return;
    s.live=false;s.by=nullptr;
    DropRounds(s);
    Log("ULTIMATE show %08X:%u: its caller is gone (left, or deleted): no more rounds, its effects play out",s.key.origin,s.key.serial);
}

// The frame's events and its lasting state.
void Play(Show& s,int f) noexcept {
    s.done=f;
    SkyFrame(s,f);
    if(f<kAfterEnd) {
        CirclesFrame(s,f);
        WavesFrame(s,f);
    }
    switch(PhaseAt(f)) {
        case Phase::charge: ChargeFrame(s,f); break;
        case Phase::storm: StormFrame(s,f-kChargeEnd); break;
        case Phase::cannon: CannonFrame(s,f-kStormEnd); break;
        case Phase::after: AfterFrame(s,f-kCannonEnd); break;
        case Phase::over: break;
    }
}
// Seek to `frame` (E): the rounds that do not last there dropped, the frames before it never played (their bolts, blasts,
// flashes and sounds dropped); its lasting state (the circles, the waves, the aim and gather beams, the main beam, the
// sky) is made by playing the frame itself (Play: SkyFrame, the discs made when missing, CannonFrame's beam on the sought
// frame).
void Seek(Show& s,int frame) noexcept {
    if(PhaseAt(frame)!=Phase::charge)for(auto& a:s.aims)Drop(a);
    if(frame<kGatherAt || frame>=kStormEnd)for(auto& g:s.gather)Drop(g);
    if(!LastingAt(frame).beam){Drop(s.beam);Drop(s.core);}
    s.sought=frame;
    s.start=GameFrame()-static_cast<ULONGLONG>(frame);
    Play(s,frame);
}

void Tick(int i) noexcept {
    Show& s=shows[i];
    CheckCaller(s);
    if(s.seekTo>=0) {
        const int to=s.seekTo;
        s.seekTo=-1;
        if(to>s.done)Seek(s,to);
    }
    const int frame=static_cast<int>(GameFrame()-s.start);
    const TickPlan p=PlanTick(s.done,frame);
    if(p.seek && p.from<kAfterEnd)Seek(s,p.from);
    else for(int f=p.from;f<=p.to && f<kAfterEnd;++f)Play(s,f);
    if(frame>=kAfterEnd || s.done>=kAfterEnd-1){s.done=kAfterEnd;Finish(i,"its time");return;}
    // The plugin's voices go quiet without a beat (paused: no tick, no beat): the recordings play on with the show.
    if(s.heard && audio::Running())audio::Beat(GameEffectVolume());
}

// A show into slot `i`: the caller held weakly, the numbers as given.
Show& Begin(int i,bool remote,const Key& k,const unsigned char* caller,const float* at,const float* plane,float damage,
            std::uint64_t seed,int frame) noexcept {
    Show& s=shows[i];
    Clear(s);
    s.on=true;s.remote=remote;s.key=k;
    edf41c::WeakSet(s.caller,caller);
    s.live=edf41c::WeakAlive(s.caller);
    s.by=s.live ? caller : nullptr;
    std::memcpy(s.at,at,12);
    std::memcpy(s.plane,plane,sizeof(s.plane));
    s.damage=std::isfinite(damage) && damage>0.0f ? damage : 0.0f;
    s.seed=seed;
    s.start=GameFrame()-static_cast<ULONGLONG>(frame);
    s.begunMs=GetTickCount64();
    (void)audio::ClipsReady();   // the recordings read on their thread from the first call: ready by the first sound
    return s;
}
}  // namespace

bool UltimateStart(const unsigned char* caller,const float* at,float damage,std::uint64_t seed) noexcept {
    if(!caller || !at || !std::isfinite(at[0]+at[1]+at[2]) || !EmcRoundReady(EmcRound::ultBeam)) {
        Log("ULTIMATE not started: %s",!EmcRoundReady(EmcRound::ultBeam) ? "its rounds were not preloaded this mission "
            "(python tools/make_ultimate.py, or the installer)" : "no caller / target");
        return false;
    }
    if(!origin)origin=plugin_net::NewOrigin();
    const Key k{origin,serial+1};
    const int i=table.Claim(k,false,0);
    if(i<0) {
        Log("ULTIMATE not started: %d shows play already (the most at once)",kMaxShows);
        return false;
    }
    ++serial;
    float centre[3]={at[0],GroundAt(at[0],at[1],at[2]),at[2]};
    float plane[kGroundCircles];
    for(int c=0;c<kGroundCircles;++c)plane[c]=CirclePlane(centre,c);
    Show& s=Begin(i,false,k,caller,centre,plane,damage,seed,0);
    s.done=-1;
    const float* const by=reinterpret_cast<const float*>(caller+0x90);   // its position (the matrix's last row)
    Log("ULTIMATE show %08X:%u at (%.0f,%.0f,%.0f), %.0f m from the caller, damage %.0f seed %016llX (%d playing): charge %d, "
        "storm %d, cannon %d, after %d frames",k.origin,k.serial,s.at[0],s.at[1],s.at[2],std::hypot(by[0]-s.at[0],by[2]-s.at[2]),
        s.damage,static_cast<unsigned long long>(seed),table.Count(),kChargeEnd,kStormEnd-kChargeEnd,kCannonEnd-kStormEnd,
        kAfterEnd-kCannonEnd);
    if(plugin_net::Ready()) {
        Send(s,ultimate_net::Kind::start);
        SayOldPeers();
    }
    return true;
}

void UltimateTick() noexcept {
    for(int i=0;i<kMaxShows;++i)if(shows[i].on)Tick(i);
}

// A call of another machine's player's ultimate weapon here (airstrike.cpp SatFireHook): true when a show of that
// caller's began here lately and was not matched to a call yet (its stock beam is to be held back); false: none heard
// (yet): the stock beam stays unless the show's start comes before it fires (UltimateStockHoldBack).
bool UltimateHeardFrom(const unsigned char* caller) noexcept {
    for(Show& s:shows)
        if(s.on && s.remote && s.live && s.by==caller && !s.stockMatched && GetTickCount64()-s.begunMs<=kUltimateStockWaitMs) {
            s.stockMatched=true;
            return true;
        }
    return false;
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

// A new mission: every show forgotten, nothing touched (their objects and the sky were the last mission's scene's); this
// machine's shows from now on of a new run.
void ResetUltimate() noexcept {
    int n=0;
    for(Show& s:shows) {
        if(!s.on)continue;
        ++n;
        if(!s.remote)Send(s,ultimate_net::Kind::end);
        Clear(s);
    }
    if(n)Log("ULTIMATE a new mission: %d show(s) forgotten",n);
    table.Reset();
    sky.refs.Reset();
    for(bool& o:sky.ours)o=false;
    origin=plugin_net::NewOrigin();serial=0;
    UltimateStockReset();
}

bool PlayerUltimateCue(wchar_t* out,std::size_t capacity) noexcept {
    constexpr ULONGLONG kShowMs=8000;
    AcquireSRWLockShared(&cueLock);
    const bool fresh=cueAt && GetTickCount64()-cueAt<=kShowMs && out && capacity;
    if(fresh)_snwprintf_s(out,capacity,_TRUNCATE,L"%ls",cueText);
    ReleaseSRWLockShared(&cueLock);
    return fresh;
}

// ---- the channel (plugin_net.h: ULTS, support_protocol.h kExtUltimate) ----
namespace {
int netLogs=0;
void UpdateUltimateNetwork(const CommandNetworkContext&,std::uint64_t) noexcept {
    if(!plugin_net::Ready())return;
    const ULONGLONG now=GetTickCount64();
    for(Show& s:shows)
        if(s.on && !s.remote && s.done>=0 && now-s.keptAt>=ultimate_net::kKeepMs)Send(s,ultimate_net::Kind::keep);
}
// A peer newly has the channel: every show of this machine's that plays, at once (it picks them up where they are).
void UltimateNetworkJoined(std::uint32_t peer) noexcept {
    for(Show& s:shows)if(s.on && !s.remote && s.done>=0)Send(s,ultimate_net::Kind::keep,peer);
}
// The room changed (the peer indexes mean other machines now): another machine's shows play on, by their keys (a keep of
// one after the change is the same show: never a second); one whose caller left loses its caller (CheckCaller).
void ResetUltimateNetwork() noexcept {
    for(int i=0;i<kMaxShows;++i)if(shows[i].on && shows[i].remote)table.SetPeer(i,0);
}

bool ReceiveUltimateNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    ultimate_net::Msg m;
    if(!ultimate_net::Decode(bytes,size,m)) {
        if(netLogs++<16)Log("ULTIMATE NET peer %u: a message that does not read (%zu bytes): dropped",peer,size);
        return true;
    }
    // The caller must be the sender's own player (a peer never starts a show for another machine's); a dead one too (a
    // show plays on after its caller fell).
    MarkIdentities ids{};
    bool sender=false;
    __try { sender=SenderIsPlayer(m.caller,puid,&ids,true); } __except(EXCEPTION_EXECUTE_HANDLER){sender=false;}
    if(!sender || !ids.player.obj) {
        if(netLogs++<16)Log("ULTIMATE NET peer %u: show %08X:%u names a caller that is not its sender's player: dropped",peer,m.origin,m.serial);
        return true;
    }
    const Key k{m.origin,m.serial};
    const unsigned char* const caller=static_cast<const unsigned char*>(ids.player.obj);
    switch(table.Receive(k,m.kind==ultimate_net::Kind::end)) {
    case Act::none: break;
    case Act::begin: {
        if(!EmcRoundReady(EmcRound::ultBeam)) {
            if(netLogs++<16)Log("ULTIMATE NET peer %u's show %08X:%u: this machine's rounds are not preloaded: the stock beam stays",
                                peer,m.origin,m.serial);
            break;
        }
        const int i=table.Claim(k,true,peer);
        if(i<0){Log("ULTIMATE NET peer %u's show %08X:%u: %d shows play already: not played here",peer,m.origin,m.serial,kMaxShows);break;}
        const int from=StartFrame(m.frame);
        Show& s=Begin(i,true,k,caller,m.at,m.plane,m.damage,m.seed,from);
        s.done=-1;   // its first tick plays frame 0 on, or seeks when the caller is past kMostCatchUp already
        Log("ULTIMATE NET peer %u's show %08X:%u (%s at frame %d) at (%.0f,%.0f,%.0f), seed %016llX: played here from frame %d",
            peer,k.origin,k.serial,m.kind==ultimate_net::Kind::start ? "started" : "heard",m.frame,m.at[0],m.at[1],m.at[2],
            static_cast<unsigned long long>(m.seed),from);
        s.stockMatched=UltimateStockHoldBack(caller);
        break;
    }
    case Act::follow: {
        const int i=table.Find(k);
        Show& s=shows[i];
        if(!s.remote || s.by!=caller)break;   // not this caller's show (a key of this machine's own, another caller's)
        table.SetPeer(i,peer);
        if(Behind(s.done,m.frame)) {
            s.seekTo=StartFrame(m.frame);
            if(netLogs++<64)Log("ULTIMATE NET show %08X:%u is at frame %d here, its caller at %d: catching up",k.origin,k.serial,s.done,m.frame);
        }
        break;
    }
    case Act::stop: {
        Show& s=shows[table.Find(k)];
        if(!s.remote || s.by!=caller)break;
        const int to=EndSeek(s.done,m.frame);
        if(to>s.done)s.seekTo=to;
        Log("ULTIMATE NET peer %u ended show %08X:%u at its frame %d%s",peer,k.origin,k.serial,m.frame,to>=0 ? ": winding it down" : "");
        break;
    }
    }
    return true;
}
}  // namespace

plugin_net::Channel UltimateNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="ultimate";c.magic=ultimate_net::kMagic;c.version=ultimate_net::kVersion;c.featureBit=support_net::kExtUltimate;
    c.minSize=c.maxSize=ultimate_net::kWire;
    c.receive=&ReceiveUltimateNetwork;c.update=&UpdateUltimateNetwork;c.reset=&ResetUltimateNetwork;c.joined=&UltimateNetworkJoined;
    return c;
}
}  // namespace crew
