// EDF4.1's giant dragon (DragonBig401) on EDF6's DragonSmall: what the class cannot be told through its SGO
// (tools/make_edf41_objects.py DRAGON_*, jobs/4bf89026/tmp/dragon41/NOTES.md, the bullet API: tmp/bulletapi/NOTES.md).
// Only an object whose SGO carries the key `edf41_dragon` is touched; EDF6's own dragons go through unchanged.
//
//  - The flame. 4.1's DragonBig breathes FlameBullet02, one a frame, from the MAB locator 発射口 (+Z, a random cone of
//    0.2 rad) between the MAB events 火炎開始 and 火炎終了 of its attack clips (EDF41.exe 0x1ee7c0 / 0x1ee7e0 / 0x1ed530);
//    EDF6's class registers no such event and fires FlameBullet01 fireballs (0x33AE30). edf41_dragon lists each attack
//    state with the clip times of the two events; each frame the main CAS player is in one of them inside its window,
//    one FlameBullet02 is spawned the way the game's own guns spawn theirs (0x6D05C0: an InitParam 0x1001D0, its team
//    owner+0x314 at +0x90, the owner's weak_ptr at +0x38, then 0x1194280(mgr, matrix, factory, param)). The bullet's numbers
//    are edf41_flame's ([factory, [0, [kv...], custom]]: speed, life, size, hit stop, colour) and its custom parameter
//    (dragon_flame_bullet_param, copied once into memory of the plugin's own: the bullet keeps a pointer to it); the damage
//    is 4.1's, level x 0.00375 (0x1ed3e3..0x1ed474; level = [+0x49C], as the class's own init 0x334660 reads it).
//  - The glide pass. In a glide, the class fires a fireball volley (0x3359CA -> 0x33AE30); 4.1 chooses at the approach,
//    half the time a pass over the target breathing (fly_glide_attack), half the time a hover breathing (fly_attack:
//    EDF41 0x1f0fc0). The call at 0x3359CA does that instead: a pass plays fly_glide_attack on the main player (and glide
//    again once it ends), a hover enters the class's own fly_attack state (0x33A320, as the call next to it does).
//  - The ranges: 4.1 wanders to points up to 500 from home (0x1f0122) where the class picks within 100 (the calls at
//    0x334A21 / 0x334A57 of 0x33AC10, radius in xmm2), stays on the ground to fight a target within 500 (EDF41 0x1efd83;
//    the class writes 100 to [+0x1C70], 0x3346B0) and recovers from an attack with a factor 1.0 (0x1f000c; the class's
//    [+0x1CA0] = 10 / s + 0.5).
//
// Keys are read the way the class reads its own (classic SGO): 0x528A0(&object+0xA0, name) gives the 12-byte node
// {type (0 array, 1 int, 2 float), count, value or offset of the first child from the node} or null.
// EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
constexpr std::size_t kVtable=0x17ABA40;
constexpr std::size_t kSlotTick=7;
constexpr std::size_t kTick=0x334930;
constexpr std::size_t kCruise=0x33AC10;
constexpr std::size_t kCruiseSites[]={0x334A21,0x334A57};
constexpr std::size_t kFireball=0x33AE30;
constexpr std::size_t kGlideFireSite=0x3359CA;
constexpr std::size_t kBodySetState=0x3389E0;   // (state machine, {fn, 0}*, 0)
constexpr std::size_t kHoverAttackState=0x33A320;
constexpr std::size_t kBodyMachineAt=0x1CD0;
constexpr std::size_t kKeyNode=0x528A0;
constexpr std::size_t kStateByName=0x11664F0;    // (cas, player, name, 1) -> state or -1
constexpr std::size_t kPlayState=0x116BFC0;      // (cas, player, state, 0)
constexpr std::size_t kLocatorMatrix=0x6BB5A0;   // (locator handle*, out 4x4)
constexpr std::size_t kFactoryByName=0x1195C50;
constexpr std::size_t kParamCtor=0x1001D0,kParamDtor=0x100C90,kWeakAssign=0x6CEBB0,kSpawn=0x1194280;
constexpr std::size_t kBulletMgr=0x20B2958;

constexpr std::size_t kSgoAt=0xA0;
constexpr std::size_t kCasAt=0x960,kMainPlayerAt=0x5E4;
// A CAS player: +0xF0 the clip length, +0xE8 its play time (the CAS update 0x1165B40 adds the frame's advance at +0xF4, which
// 0x116DFD0 accumulates as dt x the speed at +0x100, then clears it; 0x116BFC0
// zeroes it on a new clip), +0x104 the state.
constexpr std::size_t kPlayers=0x60,kPlayerSize=0x160,kPlayerLength=0xF0,kPlayerTime=0xE8,kPlayerState=0x104;
constexpr std::size_t kMuzzleAt=0x1798;          // the locator 発射口 (0x32E78B)
constexpr std::size_t kGlideStateAt=0x1514;
constexpr std::size_t kLevelAt=0x49C,kHpAt=0x2F8,kTeamAt=0x314,kWeakAt=0x28;
constexpr std::size_t kGroundRadiusAt=0x1C70,kCooldownFactorAt=0x1CA0;
constexpr std::size_t kPosAt=0x90;               // vec4 (edf41ai.cpp)
constexpr std::uint32_t kTrackEvery=300,kTrackLines=120;   // the track log: every 5 s, the first 10 minutes
// InitParam (0x1E0 bytes): speed +0x94, life +0x98 (int), damage +0x9C, direct hits pass friends +0xB7, size +0xC0,
// hit stop +0xCC, colour +0xD0, custom variant +0xE8 (tag u16 at +0xF8, 1 = an SGO node), seed +0x1A0.
constexpr std::size_t kParamSize=0x1E0;
constexpr float kCruiseRadius=500.0f,kGroundRadius=500.0f,kCooldownFactor=1.0f,kDamagePerLevel=0.00375f,kCone=0.2f;
constexpr int kMostWindows=4;

struct Node {
    std::int32_t type;
    std::int32_t count;
    std::int32_t value;
};
struct Handle {
    void* sgo;
};
using TickFn=std::uint64_t(__fastcall*)(void*,void*);
using CruiseFn=void(__fastcall*)(void*,void*,float);
using FireballFn=void(__fastcall*)(void*);
using KeyNodeFn=const Node*(__fastcall*)(void*,const wchar_t*);
using StateByNameFn=int(__fastcall*)(void*,int,const wchar_t*,bool);
using PlayStateFn=bool(__fastcall*)(void*,int,int,int);
using SetStateFn=void(__fastcall*)(void*,const void*,int);
using LocatorMatrixFn=void*(__fastcall*)(void*,float*);
using FactoryByNameFn=void*(__fastcall*)(void*,const wchar_t*);
using ParamCtorFn=void*(__fastcall*)(void*);
using ParamDtorFn=void(__fastcall*)(void*);
using WeakAssignFn=void*(__fastcall*)(void*,const void*);
using SpawnFn=void*(__fastcall*)(void*,const float*,void*,void*);

struct Window {
    int state=-1;
    float from=0.0f,to=0.0f;
};
struct Dragon {
    bool marked=false,ok=false;
    Window windows[kMostWindows];
    int windowCount=0;
    int glideAttack=-1;
    bool glidePass=false;
    float speed=2.0f,size=4.0f,hitStop=0.95f,color[4]={10.0f,10.0f,10.0f,1.0f};
    std::int32_t life=360;
    const Node* custom=nullptr;
    std::uint64_t rng=0;
    std::uint64_t shots=0;
    ULONGLONG seen=0;
    int lastState=-2;
    int logged=0;
    unsigned probe=0;
    std::uint32_t ticks=0;
};

TickFn nextTick=nullptr;
std::mutex lock;
std::unordered_map<const void*,Dragon> dragons;
std::vector<std::vector<Node>> customCopies;   // never freed: a bullet keeps a pointer into one

const Node* Key(void* self,const wchar_t* name) noexcept {
    return reinterpret_cast<KeyNodeFn>(image+kKeyNode)(static_cast<char*>(self)+kSgoAt,name);
}
const Node* Child(const Node* array,int i) noexcept {
    return reinterpret_cast<const Node*>(reinterpret_cast<const char*>(array)+array->value)+i;
}
float Number(const Node* n) noexcept {
    if(n->type==2) {
        float f;
        std::memcpy(&f,&n->value,4);
        return f;
    }
    return static_cast<float>(n->value);
}
bool IsArray(const Node* n,int count) noexcept { return n && n->type==0 && n->count==count; }

// The subtree at `src` (numbers and arrays only) laid out in `out` from `at`, each array's children together after it.
bool CopyInto(const Node* src,std::vector<Node>& out,std::size_t at) noexcept {
    out[at]=*src;
    if(src->type==1 || src->type==2)return true;
    if(src->type!=0 || src->count<0 || src->count>16)return false;
    const std::size_t first=out.size();
    out.resize(first+static_cast<std::size_t>(src->count));
    out[at].value=static_cast<std::int32_t>((first-at)*sizeof(Node));
    for(int i=0;i<src->count;++i)
        if(!CopyInto(Child(src,i),out,first+static_cast<std::size_t>(i)))return false;
    return true;
}

const Node* CopyOf(const Node* src) noexcept {
    std::vector<Node> copy(1);
    copy.reserve(64);
    if(!CopyInto(src,copy,0))return nullptr;
    customCopies.push_back(std::move(copy));
    return customCopies.back().data();
}

// The object's configuration: its windows (CAS states resolved), the flame's numbers. false: not a 4.1 dragon (or a
// key is malformed: then it is left as the class makes it).
bool Configure(void* self,Dragon& d) noexcept {
    const Node* windows=Key(self,L"edf41_dragon");
    const Node* flame=Key(self,L"edf41_flame");
    if(!windows || windows->type!=0 || !flame || !IsArray(flame,2))return false;
    char* me=static_cast<char*>(self);
    void* cas=me+kCasAt;
    const int mainPlayer=*reinterpret_cast<int*>(me+kMainPlayerAt);
    const auto byName=reinterpret_cast<StateByNameFn>(image+kStateByName);
    for(int i=0;i<windows->count && d.windowCount<kMostWindows;++i) {
        const Node* w=Child(windows,i);
        const Node* s=IsArray(w,3) ? Child(w,0) : nullptr;
        if(!s || s->type!=3 || s->count<=0 || s->count>=64)return false;
        wchar_t name[64]{};   // a string node: count characters at node + value, not NUL-ended
        std::memcpy(name,reinterpret_cast<const char*>(s)+s->value,static_cast<std::size_t>(s->count)*sizeof(wchar_t));
        Window& out=d.windows[d.windowCount++];
        out.state=byName(cas,mainPlayer,name,true);
        out.from=Number(Child(w,1));
        out.to=Number(Child(w,2));
    }
    d.glideAttack=byName(cas,mainPlayer,L"fly_glide_attack",true);
    const Node* body=Child(flame,1);
    if(!IsArray(body,3))return false;
    const Node* kv=Child(body,1);
    if(!kv || kv->type!=0 || kv->count%2)return false;
    for(int i=0;i+1<kv->count;i+=2) {
        const int id=Child(kv,i)->value;
        const Node* v=Child(kv,i+1);
        if(id==0)d.speed=Number(v);
        else if(id==1)d.life=static_cast<std::int32_t>(Number(v));
        else if(id==8)d.size=Number(v);
        else if(id==11)d.hitStop=Number(v);
        else if(id==12 && IsArray(v,4))
            for(int c=0;c<4;++c)d.color[c]=Number(Child(v,c));
    }
    d.custom=CopyOf(Child(body,2));
    return d.custom!=nullptr;
}

float Uniform(std::uint64_t& rng) noexcept {
    rng=rng*6364136223846793005ull+1442695040888963407ull;
    return static_cast<float>((rng>>40)&0xFFFFFF)/16777216.0f;
}

// One FlameBullet02 from the muzzle, along its +Z turned by a random angle up to kCone.
void Breathe(char* self,Dragon& d) noexcept {
    void* mgr=*reinterpret_cast<void**>(image+kBulletMgr);
    void* factory=mgr ? reinterpret_cast<FactoryByNameFn>(image+kFactoryByName)(mgr,L"FlameBullet02") : nullptr;
    if(!factory) {
        if(d.logged<220) { ++d.logged; Log("EDF41DRAGON %p no FlameBullet02 factory (mgr %p)",self,mgr); }
        return;
    }
    alignas(16) float muzzle[16];
    reinterpret_cast<LocatorMatrixFn>(image+kLocatorMatrix)(self+kMuzzleAt,muzzle);
    const float* x=muzzle;
    const float* y=muzzle+4;
    const float* z=muzzle+8;
    const float tilt=kCone*Uniform(d.rng),spin=6.2831853f*Uniform(d.rng);
    const float a=std::sin(tilt)*std::cos(spin),b=std::sin(tilt)*std::sin(spin),c=std::cos(tilt);
    float f[3]={z[0]*c+x[0]*a+y[0]*b,z[1]*c+x[1]*a+y[1]*b,z[2]*c+x[2]*a+y[2]*b};
    const float fl=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if(fl<1e-6f) {
        if(d.logged<220) { ++d.logged; Log("EDF41DRAGON %p muzzle has no direction",self); }
        return;
    }
    f[0]/=fl; f[1]/=fl; f[2]/=fl;
    float r[3]={f[2],0.0f,-f[0]};
    float rl=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(rl<1e-4f) { r[0]=1.0f; r[2]=0.0f; rl=1.0f; }
    r[0]/=rl; r[2]/=rl;
    const float u[3]={f[1]*r[2]-f[2]*r[1],f[2]*r[0]-f[0]*r[2],f[0]*r[1]-f[1]*r[0]};
    alignas(16) const float m[16]={r[0],r[1],r[2],0.0f,u[0],u[1],u[2],0.0f,f[0],f[1],f[2],0.0f,
                                   muzzle[12],muzzle[13],muzzle[14],1.0f};
    alignas(16) unsigned char param[kParamSize];
    reinterpret_cast<ParamCtorFn>(image+kParamCtor)(param);
    const float damage=*reinterpret_cast<float*>(self+kLevelAt)*kDamagePerLevel;
    std::memcpy(param+0x94,&d.speed,4);
    std::memcpy(param+0x98,&d.life,4);
    std::memcpy(param+0x9C,&damage,4);
    std::memcpy(param+0xC0,&d.size,4);
    std::memcpy(param+0xCC,&d.hitStop,4);
    std::memcpy(param+0xD0,d.color,16);
    param[0xB7]=1;
    std::memcpy(param+0x90,self+kTeamAt,4);
    reinterpret_cast<WeakAssignFn>(image+kWeakAssign)(param+0x38,self+kWeakAt);
    const void* custom=d.custom;
    const std::uint16_t tag=1;
    std::memcpy(param+0xE8,&custom,8);
    std::memcpy(param+0xF8,&tag,2);
    const std::uint32_t seed=static_cast<std::uint32_t>(Uniform(d.rng)*65535.0f);
    std::memcpy(param+0x1A0,&seed,4);
    reinterpret_cast<SpawnFn>(image+kSpawn)(mgr,m,factory,param);
    reinterpret_cast<ParamDtorFn>(image+kParamDtor)(param);
    if(d.shots++==0)Log("EDF41DRAGON %p first flame: speed %.2f life %d damage %.3f size %.1f",self,d.speed,d.life,damage,d.size);
}

// The entry of `self` (a new one configured), or null when it is not a 4.1 dragon. Call with `lock` held.
Dragon* Find(void* self) noexcept {
    const ULONGLONG now=GetTickCount64();
    auto it=dragons.find(self);
    if(it!=dragons.end() && now-it->second.seen>5000)dragons.erase(it), it=dragons.end();
    if(it==dragons.end()) {
        Dragon d;
        d.marked=Key(self,L"edf41_dragon")!=nullptr;
        if(d.marked) {
            d.ok=Configure(self,d);
            d.rng=reinterpret_cast<std::uintptr_t>(self)^(now*0x9E3779B97F4A7C15ull);
            Log("EDF41DRAGON %p configured=%d windows=%d (%d %.1f..%.1f, %d %.1f..%.1f, %d %.1f..%.1f) glide attack state %d",self,
                d.ok ? 1 : 0,d.windowCount,d.windows[0].state,d.windows[0].from,d.windows[0].to,d.windows[1].state,d.windows[1].from,
                d.windows[1].to,d.windows[2].state,d.windows[2].from,d.windows[2].to,d.glideAttack);
        }
        it=dragons.emplace(self,d).first;
        if(dragons.size()>256)
            for(auto j=dragons.begin();j!=dragons.end();)
                j=(j->first!=self && now-j->second.seen>5000) ? dragons.erase(j) : std::next(j);
    }
    it->second.seen=now;
    return it->second.marked && it->second.ok ? &it->second : nullptr;
}

char* MainPlayer(char* self) noexcept {
    const int index=*reinterpret_cast<int*>(self+kMainPlayerAt);
    char* players=*reinterpret_cast<char**>(self+kCasAt+kPlayers);
    return index<0 || !players ? nullptr : players+static_cast<std::size_t>(index)*kPlayerSize;
}

std::uint64_t __fastcall TickHook(void* self,void* arg) {
    const std::uint64_t r=nextTick(self,arg);
    std::lock_guard<std::mutex> hold(lock);
    Dragon* d=Find(self);
    if(!d)return r;
    char* me=static_cast<char*>(self);
    *reinterpret_cast<float*>(me+kGroundRadiusAt)=kGroundRadius;
    *reinterpret_cast<float*>(me+kCooldownFactorAt)=kCooldownFactor;
    char* clip=MainPlayer(me);
    if(!clip)return r;
    const int state=*reinterpret_cast<int*>(clip+kPlayerState);
    const float time=*reinterpret_cast<float*>(clip+kPlayerTime);
    if(state!=d->lastState && d->logged<200) {   // the first states each dragon plays (the windows' indices above)
        ++d->logged;
        Log("EDF41DRAGON %p state %d -> %d (length %.1f)",self,d->lastState,state,*reinterpret_cast<float*>(clip+kPlayerLength));
    }
    d->lastState=state;
    if(++d->ticks%kTrackEvery==0 && d->ticks/kTrackEvery<=kTrackLines) {   // where it flies (does it close on the fight?)
        const float* pos=reinterpret_cast<const float*>(me+kPosAt);
        Log("EDF41DRAGON %p track: pos %.0f %.0f %.0f state %d time %.0f hp %.0f",self,pos[0],pos[1],pos[2],state,time,
            *reinterpret_cast<float*>(me+kHpAt));
    }
    if(d->glidePass && state==d->glideAttack && time>=*reinterpret_cast<float*>(clip+kPlayerLength)) {
        d->glidePass=false;   // 4.1: the pass ends in the glide (0x1eef60)
        reinterpret_cast<PlayStateFn>(image+kPlayState)(me+kCasAt,*reinterpret_cast<int*>(me+kMainPlayerAt),
                                                        *reinterpret_cast<int*>(me+kGlideStateAt),0);
    }
    const bool alive=*reinterpret_cast<float*>(me+kHpAt)>0.0f;
    for(int i=0;i<d->windowCount;++i) {
        const Window& w=d->windows[i];
        if(w.state>=0 && state==w.state && d->logged<240 && (++d->probe%20)==0) {
            ++d->logged;
            Log("EDF41DRAGON %p attack state %d time %.1f window %.1f..%.1f hp %.0f",self,state,time,w.from,w.to,
                *reinterpret_cast<float*>(me+kHpAt));
        }
        if(alive && w.state>=0 && state==w.state && time>=w.from && time<=w.to) {
            Breathe(me,*d);
            break;
        }
    }
    return r;
}

void __fastcall CruiseHook(void* self,void* out,float radius) {
    {
        std::lock_guard<std::mutex> hold(lock);
        if(Find(self))radius=kCruiseRadius;
    }
    reinterpret_cast<CruiseFn>(image+kCruise)(self,out,radius);
}

void __fastcall GlideFireHook(void* self) {
    char* me=static_cast<char*>(self);
    bool pass=false;
    {
        std::lock_guard<std::mutex> hold(lock);
        Dragon* d=Find(self);
        if(!d) {
            reinterpret_cast<FireballFn>(image+kFireball)(self);
            return;
        }
        char* clip=MainPlayer(me);
        if(clip && *reinterpret_cast<int*>(clip+kPlayerState)==d->glideAttack)return;   // a pass under way
        pass=Uniform(d->rng)<0.5f && d->glideAttack>=0;
        Log("EDF41DRAGON %p glide attack: %s",self,pass ? "pass (fly_glide_attack)" : "hover (fly_attack)");
        if(pass) {
            d->glidePass=true;
            reinterpret_cast<PlayStateFn>(image+kPlayState)(me+kCasAt,*reinterpret_cast<int*>(me+kMainPlayerAt),
                                                            d->glideAttack,0);
        }
    }
    if(!pass) {
        struct { void* fn; std::uint64_t adj; } state{image+kHoverAttackState,0};
        reinterpret_cast<SetStateFn>(image+kBodySetState)(me+kBodyMachineAt,&state,0);
    }
}
}  // namespace

bool InstallEdf41Dragon() noexcept {
    if(!Cfg().edf41Ai) {
        Log("EDF41DRAGON off (ini Edf41Ai=0)");
        return false;
    }
    void** vtable=reinterpret_cast<void**>(image+kVtable);
    void* const tick=vtable[kSlotTick];
    if(tick!=image+kTick) {
        Log("EDF41DRAGON DragonSmall vtable slot 7 is %p, not EDF+%zX: not hooked",tick,kTick);
        return false;
    }
    nextTick=reinterpret_cast<TickFn>(tick);
    const bool t=PatchVtableSlot(vtable+kSlotTick,tick,reinterpret_cast<void*>(&TickHook));
    bool changed=false;
    int cruise=0;
    for(const std::size_t site:kCruiseSites)
        cruise+=RedirectCall(image+site,image+kCruise,reinterpret_cast<void*>(&CruiseHook),changed) ? 1 : 0;
    const bool glide=RedirectCall(image+kGlideFireSite,image+kFireball,reinterpret_cast<void*>(&GlideFireHook),changed);
    Log("EDF41DRAGON hooked: tick=%d cruise=%d/2 glide fire=%d",t ? 1 : 0,cruise,glide ? 1 : 0);
    return t && cruise==2 && glide;
}
}  // namespace crew
