// The vehicle HUD (docs/hud-re.md, ini VehicleHud / VehicleHudCount / VehicleHudRange).
//  - Over each of the Cfg().vehicleHudCount nearest NPC-driven friendly vehicles within Cfg().vehicleHudRange of the
//    player, on screen: one line "<kind> <hp>%  G <rounds>  M <missiles>  D <drones>  F m:ss" and an HP bar under
//    it. The gun / missile counts only for one armed with them (its pilot seat's weapons), the drones only for a
//    drone carrier, the fuel only where the plugin limits it (jets, called helis; "RTB" once it withdraws).
//  - Per submarine carrier a panel at the screen's right in the carrier's own colours (navy, cyan): its name, the
//    hull (a wide bar, HP in figures) and each deck part (a bar each; "DOWN" and the repair time once worn out).
//  - Per submarine carrier its world bars (drawn whatever VehicleHud says): a wide one over its tower for the hull
//    with its name and HP, a short one over each deck part with its name (DOWN and the repair time once worn out).
// Every bar has an edge, tenths marked, and a damage trail: what a hit took stays lighter for a moment and then
// drains to the new HP. A bar over something in the world is drawn larger the nearer it is (kNearScale at the
// camera, kFarScale from its own reading distance on): its size on the screen never grows as it moves away.
// Both are drawn with the game's own HUD primitives from the follower gauge's call (subcarrier.cpp GaugeHook,
// after the stock gauges): 0xC2FB0, the quad the gauge bars are made of, and the text sequence the rescue
// message and the multiplayer name tags use. The data is gathered on the game thread (HudSee, from every
// vehicle's input) into a table of its own, keyed by each vehicle's ObjRef; once a frame (HudPublish) the whole
// of it, with the player's position, is published as one snapshot (a triple buffer: the game thread never waits,
// the draw thread always has a whole frame's copy, never half of one and half of the next). The draw reads only
// the snapshot: it never touches a vehicle, allocates nothing and asks no VirtualQuery. The draw thread's clock
// is the scene step (step_fresh.h): a snapshot not published in the last steps (the shared frame tick stopped while the
// scene runs: no player) is not drawn; the scene's end (crew::SceneOver, its resources released) and a new mission's
// load make every one stale, as the count stands still in the menus; nothing is drawn while the game is paused (GamePaused).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "hud_internal.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "scene.h"
#include "debug_spawn_api.h"
#include "ground.h"
#include "highcam.h"
#include "sightzoom_api.h"
#include "subcarrier.h"

namespace crew {
namespace hudi {
constexpr std::size_t kQuadDrawer=0x2139A78;

struct Sig { unsigned rva; unsigned char bytes[12]; std::size_t size; };
const Sig kQuadSigs[]={
    {kQuad,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89},12},
    {0x804582,{0x48,0x8B,0x0D,0xEF,0x54,0x93,0x01},7},            // the gauge's drawer: mov rcx,[EDF+0x2139A78]
    {0x8045B3,{0xC7,0x44,0x24,0x20,0x05,0x00,0x00,0x00},8},       // topology 5
};
const Sig kTextSigs[]={
    {kTextMake,{0x40,0x53,0x48,0x83,0xEC,0x30,0x4C,0x8B,0x41,0x38,0x0F,0x57},12},
    {kTextBegin,{0x48,0x8B,0x49,0x18,0xE9,0xF7,0xFD,0xFF,0xFF},9},
    {kTextMeasure,{0x4C,0x8B,0xDC,0x49,0x89,0x6B,0x10,0x49,0x89,0x73,0x18,0x57},12},
    {kTextDraw,{0x48,0x8B,0x49,0x18,0xE9,0x87,0xEF,0xFF,0xFF},9},
    {kTextEnd,{0x48,0x8B,0x49,0x18,0xE9,0xC7,0xFE,0xFF,0xFF},9},
    {kTextFree,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48},12},
    {0x808568,{0x48,0x8B,0x0D,0x51,0xA4,0x8A,0x01},7},            // the rescue message's font manager: [EDF+0x20B29C0]
};
// The screen the game's HUD is laid out on (src/hudscale.h, docs/hud-re.md §0.1): *(*(EDF+0x2137090)+0x10), its width
// and height (int) at +0x20 / +0x24. A HUD text of the game's reads it so (0x94E24F: mov rax,[EDF+0x2137090];
// mov rcx,[rax+0x10]; mov eax,[rcx+0x20]), as does the follower gauge's bar (0x8044B1).
constexpr std::size_t kUiScreen=0x2137090,kUiScreenObj=0x10,kUiW=0x20,kUiH=0x24;
const Sig kUiSigs[]={
    {0x94E24F,{0x48,0x8B,0x05,0x3A,0x8E,0x7E,0x01,0x48,0x8B,0x48,0x10,0x8B},12},
    {0x8044B1,{0x48,0x8B,0x05,0xD8,0x2B,0x93,0x01},7},
};
// The rescue message's draw (0x808410) makes these calls in this order: the sequence this file repeats.
const unsigned kTextCalls[][2]={{0x808611,kTextMake},{0x808622,kTextBegin},{0x80863F,kTextMeasure},{0x8086A9,kTextDraw},
                                {0x8086B5,kTextEnd},{0x8086BF,kTextFree}};
struct LongSig { unsigned rva; unsigned char bytes[44]; std::size_t size; };
const LongSig kWorldTextSigs[]={
    // begin 0x113B2A0 (rdi = core): the orthographic projection into core+0x2F0..0x32F
    {0x113B40F,{0x0F,0x28,0x00,0x0F,0x29,0x87,0xF0,0x02,0x00,0x00,0x0F,0x28,0x48,0x10,0x0F,0x29,0x8F,0x00,0x03,0x00,0x00,
                0x0F,0x28,0x40,0x20,0x0F,0x29,0x87,0x10,0x03,0x00,0x00,0x0F,0x28,0x48,0x30,0x0F,0x29,0x8F,0x20,0x03,0x00,0x00},43},
    // ...and the view's identity's ones at core+0x330..0x36F (its zeros written just before)
    {0x113B466,{0xC7,0x87,0x58,0x03,0x00,0x00,0x00,0x00,0x80,0x3F,0x48,0xC7,0x87,0x44,0x03,0x00,0x00,0x00,0x00,0x80,0x3F,
                0x48,0xC7,0x87,0x30,0x03,0x00,0x00,0x00,0x00,0x80,0x3F,0xC7,0x87,0x6C,0x03,0x00,0x00,0x00,0x00,0x80,0x3F},42},
    {0x113B5DF,{0x4D,0x8D,0xBE,0x30,0x03,0x00,0x00},7},   // draw 0x113B4B0: lea r15,[r14+330h] (view x projection from there)
    {0x113B932,{0xF6,0x46,0x30,0x01},4},                   // draw: test byte [rsi+30h],1 (bit 0: the system constants left alone)
};

// The game's text language (docs/hud-re.md §11): Option_Language's index (0 ja, 1 en, 2 kr, 3 cn: Traditional, 4 sc:
// Simplified), one int EDF.dll reads through its getter 0xE4000 (mov eax,[rip+X]; ret) and sets through 0xE4030
// (mov [rip+X],ecx; ret) from the options (0x9457DE: options+0x310) and from Steam's language at start (0x706EF0 maps
// english / japanese / koreana / tchinese / schinese to 1 / 0 / 2 / 3 / 4). The font manager's loader takes the getter's
// value for its fonts' order (0x963724 call 0xE4000, 0x96372E call 0x963830).
constexpr unsigned kLangGet=0xE4000,kLangSet=0xE4030,kFontLoad=0x963830;
constexpr std::size_t kLangValue=0x20B2B30;
const Sig kLangSigs[]={
    {kLangGet,{0x8B,0x05,0x2A,0xEB,0xFC,0x01,0xC3},7},   // mov eax,[EDF+0x20B2B30]; ret
    {kLangSet,{0x89,0x0D,0xFA,0xEA,0xFC,0x01,0xC3},7},   // mov [EDF+0x20B2B30],ecx; ret
};
bool langOk=false;
// Game thread only.
struct Work { ObjRef ref; stepfresh::Step seen; bool logged; Data d; };
Work work[kEntries]{};
constexpr unsigned kFresh=4;
Snapshot snaps[3]{};
void PublishInstruments(const Snapshot& snap) noexcept;
std::atomic<unsigned> middle{1};
unsigned front=2;   // draw thread

bool CallsTo(unsigned site,unsigned target) noexcept {
    const unsigned char* p=image+site;
    return p[0]==0xE8 && static_cast<std::int64_t>(site)+5+At<std::int32_t>(p,1)==static_cast<std::int64_t>(target);
}

// Its pilot seat's weapons: rounds in the guns, missiles in the homing ones (-1: none of that kind).
void ReadAmmo(unsigned char* v,Data& d) noexcept {
    d.guns=d.missiles=-1;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>8 || !Readable(holders,count*8))return;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponAmmo+4))continue;
        const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        std::int32_t& sum=At<std::int32_t>(w,kWeaponLockon)==kHoming ? d.missiles : d.guns;
        sum=(sum<0 ? 0 : sum)+(ammo>0 ? ammo : 0);
    }
}

void Kind(Data& d,const char* kind) noexcept { strncpy_s(d.kind,kind,_TRUNCATE); }

// The vehicle's entry, a new one in a free slot, the slot of a gone object at the same address or of one not
// seen for a while; nullptr with every slot a fresh one's (it is not shown).
Work* WorkFor(const void* v,stepfresh::Step step) noexcept {
    Work* slot=nullptr;
    for(auto& w:work) {
        if(w.ref.Is(v))return &w;
        if(!slot && (!w.ref || w.ref.obj==v || !stepfresh::Fresh(w.seen,step,4*stepfresh::kEveryStep)))slot=&w;
    }
    if(slot)*slot=Work{ObjRef::Of(v),step,false,{}};
    return slot;
}

// The draw thread's snapshot: the newest published one.
const Snapshot& Latest() noexcept {
    if(middle.load(std::memory_order_acquire)&kFresh)front=middle.exchange(front,std::memory_order_acq_rel)&3u;
    return snaps[front];
}

// The game's screen (kUiScreen): 0 x 0 when it is not there to read (hudscale.h then takes the viewport's).
void UiScreen(int* w,int* h) noexcept {
    *w=*h=0;
    if(!uiOk)return;
    __try {
        const void* const holder=At<const void*>(image,kUiScreen);
        const void* const screen=holder ? At<const void*>(holder,kUiScreenObj) : nullptr;
        if(screen){*w=At<std::int32_t>(screen,kUiW);*h=At<std::int32_t>(screen,kUiH);}
    } __except(EXCEPTION_EXECUTE_HANDLER){*w=*h=0;}
}
// The HUD's scale for a viewport `w` x `h` (hudscale.h: the game's screen, the ini's HudScale).
float HudScaleOf(int w,int h) noexcept {
    int uiW,uiH;
    UiScreen(&uiW,&uiH);
    return hudscale::Of(uiW,uiH,w,h,Cfg().hudScale);
}

// What is drawn, logged when it changes (Debug, once in 10 s at most).
void DrawLog(int shown,int panels,const Line* lines,int count,int width,int height) noexcept {
    static int lastShown=-1,lastPanels=-1;
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || (shown==lastShown && panels==lastPanels) || now-at<10000)return;
    at=now;lastShown=shown;lastPanels=panels;
    int uiW,uiH;
    UiScreen(&uiW,&uiH);
    Log("HUD draw %dx%d (screen %dx%d, scale %.3f): %d readout(s), %d carrier panel(s), text=%d",width,height,uiW,uiH,
        HudScaleOf(width,height),shown,panels,textOk);
    for(int i=0;i<count && i<12;++i)Log("HUD   \"%ls\" at (%.0f,%.0f) %.0fx%.0f",lines[i].text,lines[i].x,lines[i].y,lines[i].w,lines[i].h);
}
}  // namespace hudi

void ControlKeyName(int vk,wchar_t* out,int size) noexcept { KeyName(vk,out,size); }
void ControlPadName(int mask,wchar_t* out,int size) noexcept { PadBitsName(mask,out,size); }

bool InstallHud() noexcept {
    __try {
        bool quad=true,text=true;
        for(const auto& q:kQuadSigs)quad=quad && Matches(q.rva,q.bytes,q.size);
        for(const auto& t:kTextSigs)text=text && Matches(t.rva,t.bytes,t.size);
        for(const auto& c:kTextCalls)text=text && CallsTo(c[0],c[1]);
        bool ui=true;
        for(const auto& u:kUiSigs)ui=ui && Matches(u.rva,u.bytes,u.size);
        bool lang=CallsTo(0x963724,kLangGet) && CallsTo(0x96372E,kFontLoad);
        for(const auto& l:kLangSigs)lang=lang && Matches(l.rva,l.bytes,l.size);
        quadOk=quad;textOk=quad && text;uiOk=ui;langOk=lang;
        unsigned worldOff=0;
        for(const auto& w:kWorldTextSigs)if(!worldOff && !Matches(w.rva,w.bytes,w.size))worldOff=w.rva;
        worldTextOk=textOk && !worldOff;
        if(worldOff)Log("HUD world text off: the text renderer's matrices are not as docs/world-text-re.md 5.2 has them "
                        "(EDF+%X): the world labels flat (2D)",worldOff);
        Log("HOOK hud quad=%d text=%d screen=%d language=%d worldText=%d (drawn from the follower gauge's call: HOOK sub gauge=1 "
            "needed; screen=0: sized on the viewport; language=0: HudLanguage auto is English; worldText: WorldText3D=%d)",quadOk,
            textOk,uiOk,langOk,worldTextOk,Cfg().worldText3D);
        Log("HUD language: game %d, HudLanguage %d: %s",GameTextLanguage(),Cfg().hudLanguage,
            hudtext::Name(hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage())));
        return quadOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool HudReady() noexcept { return quadOk; }

int GameTextLanguage() noexcept {
    if(!langOk)return -1;
    __try {
        const std::int32_t v=At<std::int32_t>(image,kLangValue);
        return v>=0 && v<=4 ? v : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}

void HudSee(unsigned char* v) noexcept {
    if(!Cfg().vehicleHud || !quadOk || v[kDead] || IsSub(v))return;
    if(SeatCount(v)==0 || SeatRider(SeatAt(v,0))!=Rider::dummy)return;   // NPC-driven only
    for(unsigned i=1;i<SeatCount(v);++i)if(SeatRider(SeatAt(v,i))==Rider::player)return;   // not the one the player is in (a gunner)
    const std::int32_t team=At<std::int32_t>(v,kTeam);
    if(team!=player.team && team!=kTeamFriend && team!=kTeamVehicle)return;
    const ULONGLONG ms=GameMs();   // player.at's clock (a wall tick here never matched it: no readout was ever shown)
    if(!player.at || ms-player.at>2000)return;
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float dx=pos[0]-player.pos[0],dy=pos[1]-player.pos[1],dz=pos[2]-player.pos[2];
    if(Cfg().vehicleHudRange>0.0f && dx*dx+dy*dy+dz*dz>Cfg().vehicleHudRange*Cfg().vehicleHudRange)return;
    const stepfresh::Step step=stepfresh::Now();
    Work* const w=WorkFor(v,step);
    if(!w)return;
    Data d{};
    d.key=v;
    std::memcpy(d.pos,pos,12);
    d.hp=At<float>(v,kHp);d.hpMax=At<float>(v,kHpMax);
    d.fuel=-1.0f;d.drones=-1;
    ReadAmmo(v,d);
    JetHudInfo jet{};
    if(IsJet(v)) {
        d.top=4.0f;
        if(JetHud(v,&jet)){Kind(d,jet.role);d.fuel=jet.fuelSec;d.drones=jet.drones;d.leaving=jet.leaving;}
        else Kind(d,"jet");
    } else if(IsHelicopter(v)) {
        d.top=6.0f;Kind(d,"heli");
        float fuel=0.0f;
        if(HeliFuel(v,&fuel)){d.fuel=fuel;d.leaving=fuel<=0.0f;}
    } else if(IsGroundRobo(v)) {
        d.top=6.0f;Kind(d,"crawler");
    } else if(IsDrillTank(v)) {
        d.top=5.0f;Kind(d,"drill");
    } else {
        d.top=4.0f;Kind(d,"npc");
    }
    w->d=d;w->seen=step;
    if(Cfg().debug && !w->logged) {
        w->logged=true;
        Log("HUD v=%p %s: hp %.0f/%.0f guns %d missiles %d drones %d fuel %.0fs%s",v,d.kind,d.hp,d.hpMax,d.guns,d.missiles,d.drones,
            d.fuel,d.leaving ? " (leaving)" : "");
    }
}

void HudPublish() noexcept {
    if(!quadOk)return;
    Snapshot& s=snaps[back];
    s.step=stepfresh::Now();
    std::memcpy(s.me,player.pos,sizeof(s.me));
    s.count=0;
    s.cockpit=PlayerJetHud(&s.jet);
    s.heli=PlayerHeliCue(&s.heliCue);
    s.heliFly=PlayerHeliHud(&s.heliHud);   // the HUD (HeliFlightHud), or only the mouse-aim flight's square
    s.drill=PlayerDrillCue(&s.drillCue);
    s.emc=PlayerEmcCue(&s.emcCue);
    s.launcher=PlayerLauncher(&s.launch);
    s.barga=PlayerBargaAim(&s.bargaAim);
    s.heliSight=PlayerHeliSight(&s.heliAim);
    s.gunner=PlayerGunnerHud(&s.gun) || DreadnoughtGunnerHud(&s.gun);   // the gunship's gun or a Dreadnought turret
    s.sensor=PlayerVehicleContacts(&s.sensorHud);
    s.highCam=PlayerHighCam(&s.highCamOn,&s.highCamKeys);
    s.turret=PlayerTurretAim(&s.turretAim);
    s.mountedOptic=SightZoomMounted(nullptr);
    s.payload=PlayerPayload(&s.payloadHud);
    s.stock=PlayerStockHud(&s.stockHud);   // the stock vehicles' HUD (StockVehicleHud; a heli's stores)
    s.warned=WarnLatest(&s.warn);   // the aircraft's warnings (warn.cpp WarnTick, this frame's: it runs first)
    s.seats=PlayerSeatPrompt(&s.seatPrompt);
    s.lookHint=VehicleLookHint(&s.lookKeys);
    s.entrance=PlayerBoardingEntrance(&s.boardingEntrance);
    s.cmdHint=NpcCommandHint(&s.commandHint);
    s.turretCamOk=PlayerTurretCam(&s.turretCam);   // the turret camera (turretcam.cpp): the gun's mark, free look
    s.nix=PlayerNixTorso(&s.nixTorso);   // the Nix's legs and torso (nix.cpp): the stock HUD's hull / turret ring
    s.sazabi=PlayerSazabiCue(&s.sazabiCue);   // the Sazabi's thrusters, weapons, flight, aim and lock (sazabi.cpp; SazabiHud)
    // The Dreadnought's own HUD (DreadnoughtHud): the stock heli's readouts are none of its (its guns and missiles never
    // fire, it is no heli): theirs give way.
    s.dreadnought=PlayerDreadnoughtCue(&s.dnCue);
    if(s.dreadnought){s.stock=false;s.heliFly=false;s.heli=false;s.heliSight=false;}
    // The stock weapon gauge gives way (stockgauge.cpp, HideStockGauges) where HudDraw lists the vehicle's weapons and
    // their rounds: a plugin aircraft's stores line (CockpitStrip, the old Cockpit, HeliStrip), StockBlock, a stock heli's
    // HeliStrip (HeliFlightHud), the Sazabi's own HUD (SazabiPanel / SazabiSpecial: its rifle, missiles, cannon, funnels;
    // the stock gauge listed its seat's beam rifles and shield missiles and the 506 body's FUEL). Its text must draw: the
    // lists are text.
    const bool heliLists=s.stock && s.stockHud.heli && s.heliFly && !s.cockpit && Cfg().heliFlightHud;
    SetStockGaugeCover(textOk && (s.cockpit || (s.stock && !s.cockpit && !s.stockHud.heli) || heliLists || (s.sazabi && !s.cockpit) || s.dreadnought));
    s.armor=PlayerStockArmor(&s.armorRo);   // the stock armor gauge's numbers while that cover hides it (stockgauge.cpp)
    s.proteus=PlayerProteus(&s.proteusRo);   // the Proteus's stance, shield, field, launcher (proteus.cpp)
    s.screens=FirstPersonScreens(&s.scr,PlayerHuman());    // the cockpit's screens in a vehicle's first person (firstperson.cpp)
    if(s.screens && s.scr.physical)PublishInstruments(s);
    if(Cfg().vehicleHud)
        for(const auto& w:work)if(w.ref && stepfresh::Fresh(w.seen,s.step,stepfresh::kEveryStep))s.d[s.count++]=w.d;
    back=middle.exchange(back|kFresh,std::memory_order_acq_rel)&3u;
}
namespace hudi {

// The last frame's view-projection, for the game thread (the player jet keeps its mouse aim on the screen with it).
SRWLOCK viewLock=SRWLOCK_INIT;
float lastViewProj[16];
bool hasViewProj=false;

void KeepViewProj(const float* viewProj) noexcept {
    AcquireSRWLockExclusive(&viewLock);
    std::memcpy(lastViewProj,viewProj,sizeof(lastViewProj));hasViewProj=true;
    ReleaseSRWLockExclusive(&viewLock);
}
pview::Store playerViews{};   // under viewLock
}  // namespace hudi
void KeepPlayerViewProj(const void* camera,const float* viewProj) noexcept {
    passCamera=camera;
    passPlayerRefKey=passPlayerSoldierKey=0;
    if(!camera || !viewProj)return;   // as KeepViewProj: the map's camera is not the aim's view
    const void* ref=nullptr;const void* soldier=nullptr;
    __try {
        if(!Readable(camera,0x368))return;
        ref=At<const void*>(camera,0x350);soldier=At<const void*>(camera,0x360);   // map.cpp kCamTargetRef / kCamTarget
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    passPlayerRefKey=reinterpret_cast<std::uintptr_t>(ref);passPlayerSoldierKey=reinterpret_cast<std::uintptr_t>(soldier);
    if(MapOwnsView())return; // aim views stay native while owner identity still follows each HUD pass
    AcquireSRWLockExclusive(&viewLock);
    playerViews.Keep(ref,soldier,viewProj,GetTickCount64());
    ReleaseSRWLockExclusive(&viewLock);
}

bool LastViewProj(float* out) noexcept {
    AcquireSRWLockShared(&viewLock);
    const bool ok=hasViewProj;
    if(ok)std::memcpy(out,lastViewProj,sizeof(lastViewProj));
    ReleaseSRWLockShared(&viewLock);
    return ok;
}
namespace hudi {

// `m` inverted (4x4, Gauss-Jordan with partial pivoting); false when it is singular.
bool Invert4(const float* m,float* out) noexcept { return mapcmd::Invert4(m,out); }

// `h` (row vector) times `m`.
void RowTimes(const float* h,const float* m,float* out) noexcept {
    for(int k=0;k<4;++k)out[k]=h[0]*m[k]+h[1]*m[4+k]+h[2]*m[8+k]+h[3]*m[12+k];
}

// The camera's eye and its look through the screen's centre, from the last frame's view-projection (row vectors,
// hud.cpp): the eye is where clip w is 0 with x and y (0, 0, 1, 0) x VP^-1), a point ahead the centre at mid depth.
bool RayOf(const float* vp,float* eye,float* dir) noexcept {
    float inv[16];
    if(!Invert4(vp,inv))return false;
    const float atEye[4]={0.0f,0.0f,1.0f,0.0f},ahead[4]={0.0f,0.0f,0.5f,1.0f};
    float e[4],a[4];
    RowTimes(atEye,inv,e);RowTimes(ahead,inv,a);
    if(std::fabs(e[3])<1e-9f || std::fabs(a[3])<1e-9f)return false;
    for(int i=0;i<3;++i){eye[i]=e[i]/e[3];dir[i]=a[i]/a[3]-eye[i];}
    if(!vec::Normalize(dir))return false;
    const float probe[4]={eye[0]+dir[0]*100.0f,eye[1]+dir[1]*100.0f,eye[2]+dir[2]*100.0f,1.0f};
    float c[4];RowTimes(probe,vp,c);
    return c[3]>0.0f && std::isfinite(eye[0]+eye[1]+eye[2]);
}
}  // namespace hudi
bool CameraRay(float* eye,float* dir) noexcept {
    float vp[16];
    return LastViewProj(vp) && RayOf(vp,eye,dir);
}
bool CameraRayOf(const void* human,float* eye,float* dir) noexcept {
    float vp[16];
    AcquireSRWLockShared(&viewLock);
    const bool ok=playerViews.Find(human,GetTickCount64(),vp);
    ReleaseSRWLockShared(&viewLock);
    return ok && RayOf(vp,eye,dir);
}
namespace hudi {
// A plugin module taken off for the rest of the mission by its faults (modules.cpp ModuleNotice; docs/module-registry.md):
// a moment, under the strike calls' line.
void ModuleBanner(Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    wchar_t line[160];
    if(!modules::ModuleNotice(line,_countof(line)))return;
    Label(text,lines,at,width*0.5f,height*0.5f+410.0f*s,1,kLineScale*0.85f,kAmber,L"%ls",line);
}
// The mark key with no enemy near the aim (npcai.cpp SendToPoint): what came of it under the point for a moment (the
// selected units sent there, how many; none selected; online); the point's own ring is the Q mark's (QMarkHud). An
// enemy near the aim but not on it: a ring there and the word to aim at it. The key held (kPingCleared): the same ring
// where this machine's mark was, and the word that it is cleared.
void NpcPingHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    NpcPing p{};
    float x,y,depth;
    if(!NpcPingReadout(&p) || !Project(vp,p.at,width,height,&x,&y,&depth))return;
    const float* tint=p.given>0 ? kMapOrder : kAmber;
    if(p.given==kPingNearEnemy || p.given==kPingCleared) {
        Arc(drawer,ctx,x,y,14.0f*s,0.0f,kTurn,2.0f*s,20,tint);
        Seg(drawer,ctx,x,y-6.0f*s,x,y+6.0f*s,2.0f*s,tint);Seg(drawer,ctx,x-6.0f*s,y,x+6.0f*s,y,2.0f*s,tint);
    }
    if(p.given>=0)WorldLabel(text,lines,at,vp,width,height,p.at,WorldKey(kWorldPing,p.wall),0.0f,26.0f*s,1,kLineScale*0.7f,tint,Tr(Tx::npcPingSent),p.given);
    else WorldLabel(text,lines,at,vp,width,height,p.at,WorldKey(kWorldPing,p.wall),0.0f,26.0f*s,1,kLineScale*0.7f,tint,L"%ls",
               Tr(p.given==-2 ? Tx::cmdOfflineOnly : p.given==kPingNearEnemy ? Tx::npcPingNearEnemy :
                  p.given==kPingCleared ? Tx::npcPingCleared : Tx::npcPingNoUnit));
}

// The aura trucks (aura.cpp): each standing aura's edge on the ground near the player, dashed in its kind's colour, and
// under the banners what reaches the player now (one line: the kinds and their strengths).
void AuraHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int* at) noexcept {
    static AuraView v;   // the draw thread's own (a view is too big to copy onto its stack each frame)
    if(!AuraViewNow(&v))return;
    static const float* const kTint[aura::kKinds]={kAmber,kGreen,kCyan};
    for(int i=0;i<v.rings && i<aura::kMaxSources;++i) {
        const AuraRingView& r=v.ring[i];
        const float* const tint=kTint[static_cast<int>(r.kind)%aura::kKinds];
        for(int j=0;j<r.points && j<kAuraRing;j+=2) {   // dashed
            float x0,y0,x1,y1;
            const int k=(j+1)%r.points;
            if(sight::ToScreen(vp,r.pt[j],1.0f,width,height,&x0,&y0) && sight::ToScreen(vp,r.pt[k],1.0f,width,height,&x1,&y1))
                WorldSeg(drawer,ctx,x0,y0,x1,y1,2.0f*s,tint);
        }
    }
    if(!v.here.kinds)return;
    wchar_t line[160];
    int n=std::swprintf(line,_countof(line),L"%ls",Tr(Tx::auraTitle));
    const auto room=[&]() noexcept { return n>=0 && n<static_cast<int>(_countof(line))-2; };
    const auto gap=[&]() noexcept { line[n++]=L' ';line[n++]=L' '; };
    const auto rest=[&]() noexcept { return _countof(line)-static_cast<std::size_t>(n); };
    const auto step=[&](int m) noexcept { n=m<0 ? -1 : n+m; };
    if(v.here.attack>0.0f && room()){gap();step(std::swprintf(line+n,rest(),Tr(Tx::auraAttack),static_cast<int>(std::lround(v.here.attack*100.0f))));}
    if(v.here.repair>0.0f && room()){gap();step(std::swprintf(line+n,rest(),Tr(Tx::auraRepair),static_cast<double>(v.here.repair*100.0f)));}
    if(v.here.ammo>0.0f && room()){gap();step(std::swprintf(line+n,rest(),Tr(Tx::auraAmmo),static_cast<double>(v.here.ammo*100.0f)));}
    if(n<0)return;
    const float* const tint=v.here.attack>0.0f ? kAmber : v.here.repair>0.0f ? kGreen : kCyan;
    Label(text,lines,at,width*0.5f,height*0.5f+318.0f*s,1,kLineScale*0.75f,tint,L"%ls",line);
}

// The catalog's categories in the HUD's words (debug_spawn.h Category, in its order): the debug spawn tool's menu and the
// test range's 「摆放」 panel.
const Tx* DebugSpawnCategoryWords() noexcept {
    static const Tx kNames[debugspawn::kCategoryCount]={Tx::debugSpawnVehicles,Tx::debugSpawnOurs,Tx::debugSpawnAircraft,
                                                        Tx::debugSpawnEnemies,Tx::debugSpawnEnemies41,Tx::debugSpawnSoldiers};
    return kNames;
}
// The result line (a spawn's, for a moment): its words, the row's name.
void DebugSpawnStatusLine(Text* text,Line* lines,int* at,float x,float y,int align,float scale,const DebugSpawnCue& c) noexcept {
    if(c.status==kDebugSpawnNone || c.statusRow<0 || c.statusRow>=debugspawn::kEntryCount)return;
    const wchar_t* const name=debugspawn::kEntries[c.statusRow].name;
    if(c.status==kDebugSpawnRequested || c.status==kDebugSpawnRefused) {   // a support call: its own answer after the name
        Label(text,lines,at,x,y,align,scale,c.status==kDebugSpawnRequested ? kGreen : kAmber,
              Tr(c.status==kDebugSpawnRequested ? Tx::debugSpawnRequested : Tx::debugSpawnRefused),name,c.note);
        return;
    }
    const Tx words=c.status==kDebugSpawnDone ? Tx::debugSpawnDone : c.status==kDebugSpawnOnline ? Tx::debugSpawnOnline :
                   c.status==kDebugSpawnNoPlace ? Tx::debugSpawnNoPlace : c.status==kDebugSpawnLoading ? Tx::placeLoadingNote :
                   Tx::debugSpawnFailed;
    Label(text,lines,at,x,y,align,scale,c.status==kDebugSpawnDone || c.status==kDebugSpawnLoading ? kGreen : kAmber,Tr(words),name);
}
// The debug spawn tool's menu (debug_spawn.cpp; ini DebugSpawn, off by default): a panel at the left, its category, a
// window of its rows round the one picked (an arrow at it; dim when it cannot be made now), the keys, and a spawn's
// result for a moment (also with the menu shut). Nothing when the tool is off.
void DebugSpawnHud(void* drawer,void* ctx,Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    DebugSpawnCue c{};
    if(!DebugSpawnReadout(&c))return;
    const float x=40.0f*s,top=height*0.22f,row=28.0f*s;
    constexpr int kWindow=10;
    if(!c.open){DebugSpawnStatusLine(text,lines,at,width*0.5f,height*0.5f+254.0f*s,1,kLineScale,c);return;}
    if(!LineSink{lines,at}.Room(kWindow+5,__func__))return;
    using debugspawn::Category;
    const Category cat=static_cast<Category>(c.category>=0 && c.category<debugspawn::kCategoryCount ? c.category : 0);
    const Tx* const kNames=DebugSpawnCategoryWords();
    static const Tx kHints[debugspawn::kCategoryCount]={Tx::debugSpawnHintVehicle,Tx::debugSpawnHintVehicle,Tx::debugSpawnHintAircraft,
                                                        Tx::debugSpawnHintEnemy,Tx::debugSpawnHintEnemy,Tx::debugSpawnHintSoldier};
    const int first=c.count<=kWindow ? 0 : (c.pick-kWindow/2<0 ? 0 : (c.pick-kWindow/2+kWindow>c.count ? c.count-kWindow : c.pick-kWindow/2));
    const int shown=c.count<kWindow ? c.count : kWindow;
    const int firstLine=*at;
    Label(text,lines,at,x,top,0,kTitleScale,kTitle,Tr(Tx::debugSpawnTitle),Tr(kNames[static_cast<int>(cat)]),c.pick+1,c.count);
    for(int i=0;i<shown;++i) {
        const int index=first+i,entry=debugspawn::RowOf(cat,index);
        if(entry<0)break;
        const bool picked=index==c.pick;
        const float* tint=!picked ? kWhite : c.ready ? kYellow : kAmber;
        Label(text,lines,at,x,top+row*(i+1.2f),0,kLineScale,tint,L"%ls %ls%ls",picked ? L">" : L"  ",debugspawn::kEntries[entry].name,
              picked && !c.ready ? Tr(Tx::debugSpawnUnavailable) : L"");
    }
    wchar_t k[5][24];
    for(int i=0;i<5;++i)KeyName(c.keys[i],k[i],_countof(k[i]));
    Label(text,lines,at,x,top+row*(shown+1.6f),0,kLineScale*0.8f,kTeal,Tr(Tx::debugSpawnKeys),k[1],k[2],k[3],k[4],k[0]);
    Label(text,lines,at,x,top+row*(shown+2.5f),0,kLineScale*0.8f,kTeal,L"%ls",Tr(kHints[static_cast<int>(cat)]));
    DebugSpawnStatusLine(text,lines,at,x,top+row*(shown+3.4f),0,kLineScale*0.8f,c);
    // The panel behind them, as wide as the widest line (the quads go down now, the text over them at DrawAll).
    float right=x+460.0f*s;
    for(int i=firstLine;i<*at;++i)if(lines[i].x+lines[i].w+12.0f*s>right)right=lines[i].x+lines[i].w+12.0f*s;
    const float y0=top-24.0f*s,y1=top+row*(shown+4)+8.0f*s;
    HideLinesUnder(lines,firstLine,x-12.0f*s,y0,right,y1);   // a Q mark's name tag, a squad's label: not over the menu
    Rect(drawer,ctx,x-12.0f*s,y0,right,y1,kPanel);
}
}  // namespace hudi

#include "cockpit_screens.inc"
namespace hudi {

// The controlled enemy's HUD in the player's place (the user, 2026-10-19: "操控怪物的时候玩家hud没有改成怪物的hud"):
// stockgauge.cpp hides the player's armor gauge and weapon gauges for the lease; here, where they were, the enemy's
// own: its name and HP bar top left (the armor gauge's corner, ArmorPanel's), and bottom right (the weapon gauges'
// corner) each attack with its own key and pad button and its native state. Radar, crosshair and damage marks stay.
constexpr int kEnemyHudRows=10;   // attack rows at most (the rest: the M card)
void EnemyControlHud(void* drawer,void* ctx,Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    enemy_control::Snapshot snapshot{};
    if(!EnemyControlReadSnapshotForView(passPlayerRefKey,&snapshot) &&
       !EnemyControlReadSnapshotForView(passPlayerSoldierKey,&snapshot))return;
    using Phase=enemy_control::Phase;
    if(snapshot.phase!=Phase::active && snapshot.phase!=Phase::pending && snapshot.phase!=Phase::restoring)return;
    const bool active=snapshot.phase==Phase::active;
    if(!LineSink{lines,at}.Room(6,__func__))return;
    // --- Top left: who, its HP, its side ---
    const float x=kArmorPanelX*s,top=kArmorPanelY*s,right=x+std::fmin(380.0f*s,width*0.38f),barW=right-x-16.0f*s;
    const bool hp=active && enemyui::HealthAvailable(snapshot.healthKnown,snapshot.health,snapshot.maxHealth);
    const float statusH=active ? (snapshot.sourceProtected ? 104.0f : 84.0f)+(hp ? 12.0f : 0.0f) : 52.0f;
    Rect(drawer,ctx,x-8.0f*s,top-8.0f*s,right,top+statusH*s,kMapBand);
    Rect(drawer,ctx,x-8.0f*s,top-8.0f*s,x-5.0f*s,top+statusH*s,kCyan);
    Label(text,lines,at,x,top+8.0f*s,0,kLineScale*0.72f,kCyan,Tr(Tx::enemyControlActive),snapshot.name);
    if(*at>0)FitLine(text,lines[*at-1],x,right-8.0f*s);
    float y=top+24.0f*s;
    if(!active) {
        Label(text,lines,at,x,y+12.0f*s,0,kLineScale*0.58f,kAmber,L"%ls",
            Tr(snapshot.phase==Phase::restoring ? Tx::enemyControlRestoring : Tx::enemyControlPending));
        if(*at>0)FitLine(text,lines[*at-1],x,right-8.0f*s);
        return;
    }
    if(hp) {
        const float share=snapshot.health/snapshot.maxHealth;
        Bar(drawer,ctx,x,y,barW,10.0f*s,share,share,HpColour(share>1.0f ? 1.0f : share),s);
        y+=12.0f*s;
    }
    wchar_t health[128]{};
    Label(text,lines,at,x,y+10.0f*s,0,kLineScale*0.6f,kWhite,L"%ls",EnemyHealthText(snapshot,health,_countof(health)));
    if(*at>0)FitLine(text,lines[*at-1],x,right-8.0f*s);
    y+=22.0f*s;
    Label(text,lines,at,x,y+10.0f*s,0,kLineScale*0.52f,kAmber,
        Tr(snapshot.followsOwnerTeam ? Tx::enemyControlTeamOwner : Tx::enemyControlTeamOriginal),snapshot.controlledTeam);
    if(*at>0)FitLine(text,lines[*at-1],x,right-8.0f*s);
    y+=20.0f*s;
    if(snapshot.sourceProtected) {
        Label(text,lines,at,x,y+10.0f*s,0,kLineScale*0.52f,kTeal,L"%ls",Tr(Tx::enemyControlSourceSafe));
        if(*at>0)FitLine(text,lines[*at-1],x,right-8.0f*s);
    }
    const float statusBottom=top+statusH*s;
    // --- Bottom right: key -> attack ---
    enemy_control::Ability list[kEnemyHudRows]{};
    const unsigned keyed=(std::min)(static_cast<unsigned>(kEnemyHudRows),enemy_control::kBindingCount);
    unsigned shown=EnemyControlAbilities(snapshot,0,list,keyed);
    const float rowH=26.0f*s,x1=width-kArmorPanelX*s,x0=x1-std::fmin(470.0f*s,width*0.44f),bottom=height-kArmorPanelY*s;
    // Rows that fit (a short split view): up to the status block when the two share columns, else the top margin;
    // the title, the rest's line and the hint always kept.
    const float ceiling=x0-8.0f*s<right ? statusBottom+8.0f*s : top-8.0f*s;
    const float room=bottom-ceiling-(26.0f+22.0f+22.0f+8.0f)*s;
    const unsigned fit=room>0.0f ? static_cast<unsigned>(room/rowH) : 0u;
    if(shown>fit)shown=fit;
    if(!LineSink{lines,at}.Room(3*static_cast<int>(shown)+4,__func__))shown=static_cast<unsigned>((std::max)(0,(LineSink{lines,at}.Free()-4)/3));
    const bool more=snapshot.abilityCount>shown;
    const float blockH=(26.0f+static_cast<float>(shown)*26.0f+(more || !shown ? 22.0f : 0.0f)+22.0f+8.0f)*s;
    float ry=bottom-blockH;
    Rect(drawer,ctx,x0-8.0f*s,ry,x1+8.0f*s,bottom,kMapBand);
    Rect(drawer,ctx,x1+5.0f*s,ry,x1+8.0f*s,bottom,kCyan);
    Label(text,lines,at,x0,ry+13.0f*s,0,kLineScale*0.6f,kCyan,L"%ls",Tr(Tx::enemyControlAttacks));
    if(*at>0)FitLine(text,lines[*at-1],x0,x1);
    ry+=26.0f*s;
    const float chip=std::fmin(130.0f*s,(x1-x0)*0.34f);
    for(unsigned i=0;i<shown;++i) {
        const auto& ability=list[i];
        wchar_t key[32]{},pad[32]{},chipText[64]{},state[96]{};
        const auto* binding=enemy_control::BindingOf(i);
        const wchar_t* keyName=EnemyKeyLabel(i,false,key,_countof(key));
        if(binding && binding->pad)_snwprintf_s(chipText,_countof(chipText),_TRUNCATE,L"%ls / %ls",keyName,EnemyKeyLabel(i,true,pad,_countof(pad)));
        else _snwprintf_s(chipText,_countof(chipText),_TRUNCATE,L"%ls",keyName);
        const bool unavailable=ability.state==enemy_control::AbilityState::unavailable;
        const bool cooling=ability.state==enemy_control::AbilityState::cooldown;
        const bool running=ability.state==enemy_control::AbilityState::active;
        Rect(drawer,ctx,x0,ry+2.0f*s,x0+chip,ry+rowH-3.0f*s,running ? kBtnLit : kBtnFill);
        Label(text,lines,at,x0+chip*0.5f,ry+rowH*0.47f,1,kLineScale*0.52f,unavailable ? kMapOrderDim : kWhite,L"%ls",chipText);
        if(*at>0) {
            Line& l=lines[*at-1];FitLine(text,l,x0+3.0f*s,x0+chip-3.0f*s);l.x=x0+(chip-l.w)*0.5f;
        }
        const float nameLeft=x0+chip+8.0f*s;
        float nameRight=x1;
        if(cooling || unavailable || running) {
            Label(text,lines,at,x1,ry+rowH*0.47f,2,kLineScale*0.48f,cooling ? kAmber : running ? kCyan : kMapOrderDim,L"%ls",
                EnemyAbilityState(ability,state,_countof(state)));
            if(*at>0) {
                Line& l=lines[*at-1];FitLine(text,l,nameLeft+(x1-nameLeft)*0.5f,x1);nameRight=l.x-6.0f*s;
            }
        }
        Label(text,lines,at,nameLeft,ry+rowH*0.47f,0,kLineScale*0.6f,unavailable ? kMapOrderDim : kWhite,L"%ls",ability.name);
        if(*at>0)FitLine(text,lines[*at-1],nameLeft,nameRight);
        ry+=rowH;
    }
    if(more) {
        Label(text,lines,at,x0,ry+11.0f*s,0,kLineScale*0.5f,kTeal,Tr(shown ? Tx::enemyControlMoreOnCard : Tx::enemyControlAllOnCard),
            static_cast<unsigned>(snapshot.abilityCount-shown));
        if(*at>0)FitLine(text,lines[*at-1],x0,x1);
        ry+=22.0f*s;
    } else if(!shown) {
        Label(text,lines,at,x0,ry+11.0f*s,0,kLineScale*0.5f,kAmber,L"%ls",Tr(Tx::enemyControlNoKeys));
        if(*at>0)FitLine(text,lines[*at-1],x0,x1);
        ry+=22.0f*s;
    }
    Label(text,lines,at,x0,ry+11.0f*s,0,kLineScale*0.5f,kTeal,L"%ls",Tr(Tx::enemyControlPlayHint));
    if(*at>0)FitLine(text,lines[*at-1],x0,x1);
}
}  // namespace hudi

void HudDraw(const float* viewProj,void* ctx,const void* viewport,const CarrierPanel* panels,int count) noexcept {
    // The aim's view (CameraRay) stays the game's while the map's camera shows: the turret, the launcher and the sights
    // hold where the player left them.
    if(viewProj && !MapOwnsView())KeepViewProj(viewProj);
    if(!quadOk || !viewProj || !ctx || !viewport)return;   // the carriers' bars are drawn whatever VehicleHud says
    // The pause menu: nothing of the plugin's (docs/hud-re.md §10). The game still draws the scene and this HUD pass every
    // frame, and steps the cameras; the stock HUD slides out (its layouts' transition_slide). The snapshot's own age
    // (once a wall-clock window) only caught the pause half a second in, and the carriers' bars and the map's marks not at all.
    if(GamePaused())return;
    __try {
        void* const drawer=At<void*>(image,kQuadDrawer);
        const int w=At<std::int32_t>(viewport,8),h=At<std::int32_t>(viewport,0xC);
        if(!drawer || w<=0 || h<=0)return;
        const float width=static_cast<float>(w),height=static_cast<float>(h),s=HudScaleOf(w,h);
        // The words in the language the game's text is in now (it may change in the options mid-game), or the ini's.
        hudtext::Use(hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage()));
        alignas(16) unsigned char renderer[kRendererSize]{},font[kFontSize]{};
        Text text{};
        text.ctx=ctx;text.renderer=renderer;text.font=font;text.mgr=At<unsigned char*>(image,kFontMgr);text.s=s;
        text.vp=viewProj;text.width=width;text.height=height;
        worldRef=worldtext::kViewRef;worldMapView=false;++worldFrame;worldLens=screenview::Lens{};
        Text* const t=textOk && text.mgr ? &text : nullptr;
        Line lines[kMaxLines]{};   // zeroed: no line is a world one unless WorldLabel made it so
        int at=0,shown=0;
        if(MapScreen(drawer,ctx,t,viewProj,width,height,s,lines,&at)) {   // the map view: its marks alone
            if(t && textOk)DrawAll(*t,lines,at);
            FreeText(text);
            return;
        }
        // The map's camera easing back to the player (map.cpp Camera, kEaseOut frames): its readout is gone and the view is
        // still the map's, so the world marks (the impact rings, the turret's box, the vehicle HUD) would land on a
        // view they were not made for. Nothing, like the stock HUD (its switch held until the camera is back).
        if(MapOwnsView() && MapHidesStockHud(passCamera)){FreeText(text);return;}
        EnemyControlHud(drawer,ctx,t,width,height,s,lines,&at);
        const ULONGLONG now=GetTickCount64();
        const sightzoom::Kind sightKind=SightZoomView();
        // Magnified, or the mounted optic up at 1x in a vehicle's first person (War Thunder's gunner sight): one snapshot
        // of the seat's sight (sightzoom.h ScopeField), the same answer the stock crosshair is held back by.
        SightScope sight{};
        const bool scopeOverlay=SightZoomScope(&sight);
        const float sightMagnification=scopeOverlay ? sight.zoom : 1.0f;
        const sightzoom::Mask sightMask=sightzoom::MaskOf(sight.kind,sight.eyepiece);
        scopeNow=ScopeNow{};
        {   // A validated seat/weapon chooses the mask. Aircraft/mechs keep their own HUD; overhead has no optic.
            const Snapshot& z=Latest();
            const bool drawn=scopeOverlay && stepfresh::Fresh(z.step);
            PublishScopeDrawn(drawn ? sight.human : nullptr);   // the stock crosshair gives way to it (scope_hold.h)
            if(drawn) {
                ScopeShade(drawer,ctx,t,width,height,s,sightMagnification,sightMask,lines,&at);
                // The optic's reticle goes with the weapon's marks (StockMark: ScopeArm), etched across this field.
                if(sightzoom::Round(sightMask))
                    scopeNow=ScopeNow{true,scope::Field{width*0.5f,height*0.5f,
                                      scopeview::Radius(width,height,sightMask==sightzoom::Mask::monocular)},sightMagnification,
                                      z.stock ? z.stockHud.zero : 0};
            }
        }
        // The scope's picture in the picture: every world mark from here on as the screen shows the world, the lens's
        // part at its power (screen_view.h; the user, 2026-11-27: 「开镜以后线会分离」).
        if(!scopeOverlay) {
            InfantryScope(drawer,ctx,t,viewProj,width,height,s,lines,&at);
            worldLens=ScopeLensView(viewProj,width,height);
        }
        for(int i=0;i<count && i<3;++i)CarrierBars(drawer,ctx,t,viewProj,width,height,s,panels[i],lines,&at,now);
        const Snapshot& snap=Latest();
        const bool mountedOptic=snap.mountedOptic || scopeOverlay; // also cover the first paint before next HUD publication
        // A switch the player makes shows for a moment (hud_cue.h Change, kSwitchMs): the picked store (forgotten while no
        // aircraft's stores show, so boarding shows none).
        static hudcue::Change storePick{};
        // The snapshot is the last steps' (step_fresh.h: the draw sees the step the game thread ran last; the pause menu
        // returned above).
        const bool fresh=stepfresh::Fresh(snap.step),storesShown=fresh && (snap.cockpit || snap.heliFly || snap.stock);
        if(!storesShown)storePick.seen=false;
        const int picked=SeatArmPick(SeatArmList(snap));
        const bool storeSwitched=storesShown && hudcue::Changed(storePick,picked,now,kSwitchMs);
        const bool rotorHud=snap.cockpit && snap.jet.rotor && Cfg().heliFlightHud;   // a rotor craft: the helicopter HUD
        // A vehicle's first person: the read-outs are on its cockpit's screens (DrawCockpitScreens below); the HUD's own panels
        // (strips, annunciator, blocks, tapes) give way, its aim marks and head-up symbols stay on the view.
        const bool screensOn=fresh && snap.screens,headUp=!screensOn || (!snap.scr.physical && HudGlass(snap.scr));
        const ULONGLONG launchAt=snap.warned ? snap.warn.launchAt : 0;
        if(fresh && snap.cockpit) {
            if(snap.jet.aiming)AimMarks(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(snap.jet.hasImpact)ImpactMark(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(!snap.jet.bomb)LockMark(drawer,ctx,viewProj,width,height,s,snap.jet);
            if(snap.jet.rotor && snap.jet.heli.aiming)FlightAim(drawer,ctx,t,viewProj,width,height,s,snap.jet.heli.aim,lines,&at);   // HUD or not
            if(rotorHud) {
                if(headUp)HeliHud(drawer,ctx,t,viewProj,width,height,s,snap.jet.heli,snap.jet.sym,launchAt,lines,&at);
                if(Cfg().playerJetGunSight)GunSight(drawer,ctx,viewProj,width,height,s,snap.jet.sym);
                wchar_t stores[128];
                StoresText(stores,_countof(stores),snap.jet,false);
                LoadCell cells[kMostStores];
                const int n=JetCells(snap.jet,cells);
                Line controls{};JetControls(controls,snap.jet);
                if(!screensOn)HeliStrip(drawer,ctx,t,width,height,s,snap.jet.heli,snap.jet.fuel,stores,cells,n,snap.jet.store,storeSwitched,lines,&at,&controls);
            } else {
                if(headUp)FighterHud(drawer,ctx,t,viewProj,width,height,s,snap.jet,launchAt,lines,&at);
                else if(Cfg().playerJetGunSight)GunSight(drawer,ctx,viewProj,width,height,s,snap.jet.sym);
                if(!screensOn && Cfg().playerJetFlightHud)CockpitStrip(drawer,ctx,t,width,height,s,snap.jet,storeSwitched,lines,&at);
                else if(!screensOn)Cockpit(drawer,ctx,t,width,height,s,snap.jet,lines,&at);
            }
            const bool annunciated=snap.warned && (rotorHud || Cfg().playerJetFlightHud);   // else the old panel's text
            if(annunciated && !screensOn)Annunciator(drawer,ctx,t,width,height,s,snap.warn,lines,&at);
            if(!screensOn)GearPanel(drawer,ctx,t,width,height,s,annunciated,lines,&at,now);
        }
        // A stock heli: the mouse-aim flight's square whenever the mouse flies it (HeliMouseAim), the helicopter HUD round it
        // with HeliFlightHud (off: the takeoff panel, as before).
        const bool heliFresh=fresh && snap.heliFly && !snap.cockpit,heliHud=heliFresh && Cfg().heliFlightHud;
        if(heliFresh && snap.heliHud.f.aiming)FlightAim(drawer,ctx,t,viewProj,width,height,s,snap.heliHud.f.aim,lines,&at);
        if(heliHud) {
            if(headUp)HeliHud(drawer,ctx,t,viewProj,width,height,s,snap.heliHud.f,snap.heliHud.sym,launchAt,lines,&at);
            const bool own=snap.stock && snap.stockHud.heli;   // its weapons, rounds and reloads (vhud.cpp)
            LoadCell cells[kStockArms];
            int pickedCell=-1;
            const int n=own ? StockCells(snap.stockHud,cells,&pickedCell) : 0;
            Line controls{};
            if(snap.payload && PayloadControlsShown(snap.payloadHud))PayloadControls(controls,snap.payloadHud);
            else if(snap.payload)AircraftControls(controls,snap.payloadHud.keys,snap.payloadHud.choices,snap.payloadHud.switchButton,0,false,false);
            if(!screensOn)HeliStrip(drawer,ctx,t,width,height,s,snap.heliHud.f,snap.heliHud.fuel,nullptr,cells,n,own ? pickedCell : -1,
                      storeSwitched,lines,&at,&controls);
            if(snap.warned && !screensOn)Annunciator(drawer,ctx,t,width,height,s,snap.warn,lines,&at);
        }
        if(fresh && snap.heli && !snap.cockpit && !heliHud && !screensOn)HeliPanel(drawer,ctx,t,width,height,s,snap.heliCue,lines,&at);
        // The stock vehicles' HUD up (StockVehicleHud): it carries the drill's line and the gun's marks, so DrillPanel and
        // TurretMark's square give way to it.
        const bool stockHud=fresh && snap.stock && !snap.cockpit && !snap.stockHud.heli;
        if(fresh && snap.drill && !snap.cockpit && !snap.heli && !stockHud && !screensOn)DrillPanel(drawer,ctx,t,width,height,s,snap.drillCue,lines,&at);
        if(fresh && snap.sazabi && !snap.cockpit) {   // the Sazabi's own HUD (it is no stock vehicle)
            SazabiHud(drawer,ctx,t,viewProj,width,height,s,snap.sazabiCue,lines,&at);
        }
        if(fresh && snap.dreadnought && !snap.cockpit && !screensOn)
            DreadnoughtHud(drawer,ctx,t,width,height,s,snap.dnCue,lines,&at);
        const bool gunnerSight=fresh && snap.gunner && !snap.cockpit;
        const bool mechSight=fresh && snap.sazabi && !snap.cockpit;
        const bool overhead=fresh && ((snap.highCam && snap.highCamOn) || sightKind==sightzoom::Kind::indirect);
        const int stockPick=stockHud ? snap.stockHud.sight : -1;
        const bool physicalSight=(stockHud && stockPick>=0 && stockPick<snap.stockHud.arms && stockPick<kStockArms && snap.stockHud.arm[stockPick].physicalOnly) ||
                                 (fresh && snap.heliSight && snap.heliAim.gun && snap.heliAim.physicalOnly);
        const bool launcherSight=stockHud && stockPick>=0 && stockPick<snap.stockHud.arms && snap.stockHud.arm[stockPick].lofted;
        if(fresh && snap.heliSight && !snap.cockpit && !gunnerSight && !mechSight)
            HeliGunSight(drawer,ctx,t,viewProj,width,height,s,snap.heliAim,lines,&at);
        if(fresh && snap.launcher && launcherSight)LauncherMarks(drawer,ctx,t,viewProj,width,height,s,snap.launch,lines,&at);
        if(fresh && snap.barga)BargaAimMarks(drawer,ctx,viewProj,width,height,s,snap.bargaAim);
        if(fresh && snap.highCam && !snap.cockpit)HighCamHint(t,width,height,s,snap.highCamOn,snap.highCamKeys,lines,&at);
        if(fresh && snap.lookHint) {
            wchar_t key[32];
            if(snap.lookKeys)KeyName(Cfg().freeLookKey,key,32);else SeatButtonName(Cfg().freeLookButton,key,32);
            Label(t,lines,&at,width*0.5f,height*0.86f,1,kLineScale*0.85f*s,kWhite,Tr(Tx::vehicleLookHold),key);
        }
        if(fresh && snap.seats)SeatLine(t,width,height,snap.seatPrompt,lines,&at);
        if(fresh && snap.entrance)EntranceMark(drawer,ctx,t,viewProj,width,height,s,snap.boardingEntrance,lines,&at);
        if(fresh && snap.cmdHint)CommandHintMark(drawer,ctx,t,viewProj,width,height,s,snap.commandHint,lines,&at);
        if(fresh && snap.turretCamOk && !mountedOptic && !snap.cockpit && !launcherSight && !overhead && !gunnerSight && !mechSight)
            TurretMark(drawer,ctx,viewProj,width,height,s,snap.turretCam,!stockHud);
        if(gunnerSight)GunnerMarks(drawer,ctx,t,viewProj,width,height,s,snap.gun,lines,&at);
        const auto thermalStatus=thermal::GetStatus();
        if(fresh && !overhead)ThermalControlHint(t,width,height,s,thermalStatus,lines,&at);
        if(fresh && snap.sensor && !overhead)VehicleContacts(drawer,ctx,t,viewProj,width,height,s,snap.sensorHud,thermalStatus,lines,&at);
        const bool showTurretOverlay=fresh && snap.turret && !mountedOptic && !snap.cockpit && !(physicalSight && snap.turretAim.ownGun) && (!stockHud || hudcue::TurretOverlayOwned(stockPick,snap.stockHud.turret)) && !launcherSight && !overhead && !gunnerSight && !mechSight;
        // A flip of EDF6AutoTurret's auto-aim (its readout's mode) while at its turret: the new state large for a moment,
        // whatever view hides the line. Forgotten while there is no readout, so boarding with the state it already had
        // is no flip.
        static hudcue::Change aimMode{};
        const bool turretRead=fresh && snap.turret;
        if(!turretRead)aimMode.seen=false;
        const bool aimFlipped=turretRead && hudcue::Changed(aimMode,static_cast<int>(snap.turretAim.mode),now,kSwitchMs);
        if(showTurretOverlay) {
            float controlsBottom=-1.0f;
            if(stockHud) {
                LoadCell cells[kStockArms];const int n=StockCells(snap.stockHud,cells);Line keys{};
                if(snap.payload && PayloadControlsShown(snap.payloadHud))PayloadControls(keys,snap.payloadHud);
                else if(snap.payload)AircraftControls(keys,snap.payloadHud.keys,snap.payloadHud.choices,snap.payloadHud.switchButton,0,false,false);
                const auto dock=LoadoutDockOf(t,width,height,s,cells,n,keys.text[0] ? 1 : 0);
                controlsBottom=dock.top-20.0f*s;
            }
            TurretAimMarks(drawer,ctx,t,viewProj,width,height,s,snap.turretAim,lines,&at,controlsBottom);
        }
        if(aimFlipped)AutoAimBanner(drawer,ctx,t,width,height,s,snap.turretAim.mode!=edf::aimlink::Mode::manual,lines,&at);
        if(stockHud) {
            const StockExtras x{fresh && snap.nix ? &snap.nixTorso : nullptr,fresh && snap.drill ? &snap.drillCue : nullptr,
                                fresh && snap.emc ? &snap.emcCue : nullptr,
                                fresh && snap.proteus ? &snap.proteusRo : nullptr,overhead,snap.armor ? &snap.armorRo : nullptr};
            if(screensOn)StockAimOnly(drawer,ctx,t,viewProj,width,height,s,snap.stockHud,x,lines,&at);
            else StockDockHud(drawer,ctx,t,viewProj,width,height,s,snap.stockHud,x,snap.payload ? &snap.payloadHud : nullptr,
                              storeSwitched,lines,&at);
        }
        // The hidden stock armor gauge's numbers for a vehicle whose HUD is no StockBlock (that one has them in it).
        if(fresh && snap.armor && !stockHud && !screensOn)ArmorPanel(drawer,ctx,t,s,snap.armorRo,lines,&at);
        QMarkHud(drawer,ctx,t,viewProj,width,height,s,lines,&at);
        MapSignalHud(drawer,ctx,t,viewProj,width,height,s,lines,&at);
        SupportWarnHud(drawer,ctx,t,viewProj,width,height,s,lines,&at);
        FormationBanner(t,width,height,s,lines,&at);
        SweepBanner(t,width,height,s,lines,&at);
        RescueCueBanner(t,width,height,s,lines,&at);
        StrikeBanner(t,width,height,s,lines,&at);
        ModuleBanner(t,width,height,s,lines,&at);
        NpcPingHud(drawer,ctx,t,viewProj,width,height,s,lines,&at);
        AuraHud(drawer,ctx,t,viewProj,width,height,s,lines,&at);
        DebugSpawnHud(drawer,ctx,t,width,height,s,lines,&at);
        if(Cfg().vehicleHud) {
            if(fresh)at=Readouts(drawer,ctx,t,viewProj,width,height,s,lines,at,snap,&shown,now);
            float top=height*0.28f;
            for(int i=0;i<count && i<3;++i)top=Panel(drawer,ctx,t,panels[i],i,count,width,top,s,lines,&at)+10.0f*s;
        }
        // A vehicle's first person: the weapons screen names a weapon by its picture and its round's tag (model geometry
        // has no game font); a switch shows the picked one's own name over the view for a moment, as the loadout strip's
        // banner does in third person (the strip itself gives way to the screen).
        if(screensOn && storeSwitched) {
            LoadCell cells[kStockArms>kMostStores ? kStockArms : kMostStores];
            int n=0,pickedCell=-1;
            if(snap.cockpit) {   // the cell of the picked store (a picked gun has no cell: no banner)
                n=JetCells(snap.jet,cells);
                for(int i=0;i<n;++i)if(cells[i].picked)pickedCell=i;
            }
            else if(snap.stock)n=StockCells(snap.stockHud,cells,&pickedCell);
            if(pickedCell>=0 && pickedCell<n)LoadoutBanner(drawer,ctx,t,width,height*0.8f,s,cells[pickedCell],lines,&at);
        }
        if(screensOn)DrawCockpitScreens(drawer,ctx,t,viewProj,width,height,s,snap,lines,&at);
        if(t && textOk)DrawAll(*t,lines,at);
        FreeText(text);
        worldLens=screenview::Lens{};
        DrawLog(shown,count,lines,at,w,h);
    } __except(EXCEPTION_EXECUTE_HANDLER) {worldLens=screenview::Lens{};}
}
bool HudScopeHoldsCrosshair(const void* soldier) noexcept {
    AcquireSRWLockShared(&scopeDrawnLock);
    const scopehold::Drawn d=scopeDrawn;
    ReleaseSRWLockShared(&scopeDrawnLock);
    return scopehold::Holds(d,soldier,GetTickCount64());
}
// A new mission (mission.cpp MissionStart): the last mission's vehicles are gone; the next publish is empty.
void ResetHud() noexcept {
    PublishScopeDrawn(nullptr);
    for(auto& w:work)w=Work{};
}
}  // namespace crew
