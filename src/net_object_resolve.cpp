#include "net_object_resolve.h"
#include "coop_extension_api.h"
#include "memory.h"
#include "team_walk.h"
#include "support_soldier.h"
#include <cstring>

// The one native all-team walk every resolver of object references makes (net_object_resolve.h). Static RE
// (docs/mission-participants-native.md): 5E0C80 visits every team's GameObjects with no HP or dead-state filter, a player
// soldier keeps its User bound at +1ED0 (its control block +1ED8 live) dead or alive (dead / revivable players are in
// every sealed participant snapshot, mission_participants.cpp), and only a scene-deleted actor (+18 bit 2) leaves.
namespace crew {
namespace netobj {
constexpr unsigned kEnumAllTeams=0x5E0C80,kTeamManager=0x20B2978,kGameStatus=0x20B2890;
constexpr unsigned kMaxPlayers=1024;
constexpr unsigned char kEnumSignature[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83};
using WalkFn=void(__fastcall*)(void*,void*);

// Its own weak reference live (still allocated as itself: dead or alive, scene-deleted or not). What it is called by.
bool Named(const unsigned char* object,ObjRef* ref) noexcept {
    if(!Readable(object,kDead+1))return false;
    const auto ctrl=At<const unsigned char*>(object,kSelfCtrl);
    if(!Readable(ctrl,16) || At<LONG>(ctrl,8)<=0 || At<const void*>(object,kSelf)!=object)return false;
    *ref=ObjRef{object,ctrl};return true;
}
// Named and not scene-deleted (+18 bit 2), dead or alive: what a reference finds.
bool Present(const unsigned char* object,ObjRef* ref) noexcept {
    return Named(object,ref) && !(object[0x18]&4);
}
bool Live(const unsigned char* object,ObjRef* ref) noexcept {
    return Present(object,ref) && !object[kDead];
}
bool Alive(const NetLife life,const unsigned char* object,ObjRef* ref) noexcept {
    return life==NetLife::live ? Live(object,ref) : Present(object,ref);
}
// A player's soldier (any machine's): named by its slot alone, never by its NetId (another on every machine).
bool PlayerSoldier(const unsigned char* object) noexcept {
    return Readable(object,0x1EE0) && IsSoldierClass(object) && IsAnyPlayer(object);
}
// A mission player's soldier: its slot (User+48, inside the mission's `expected` players) and its native PUID (User+18).
bool Player(const unsigned char* object,unsigned expected,void** puid,int* slot) noexcept {
    if(!Readable(object,0x1EE0) || !IsAnyPlayer(object) || !IsSoldierClass(object))return false;
    const auto user=At<const unsigned char*>(object,0x1ED0);
    const auto ctrl=At<const unsigned char*>(object,0x1ED8);
    if(!Readable(user,0x4C) || !Readable(ctrl,16) || At<LONG>(ctrl,8)<=0)return false;
    const int index=At<int>(user,0x48);
    if(index<0 || static_cast<unsigned>(index)>=expected)return false;
    if(slot)*slot=index;
    *puid=At<void*>(user,0x18);return *puid!=nullptr;
}
// The mission's player count (*(EDF+20B2890)+14FF8), 0 without a world.
unsigned Expected() noexcept {
    const auto status=At<const unsigned char*>(image,kGameStatus);
    if(!Readable(status,0x14FFC))return 0;
    const unsigned expected=At<unsigned>(status,0x14FF8);
    return expected<=kMaxPlayers ? expected : 0;
}
bool Same(const ObjRef& a,const ObjRef& b) noexcept { return a.obj==b.obj && a.ctrl==b.ctrl; }

// The world a walk ran in: its team manager, its rows, its status and player count.
struct World {
    void* mgr=nullptr;void* teams=nullptr;const void* status=nullptr;unsigned expected=0;
    bool operator==(const World& o) const noexcept {
        return mgr==o.mgr && teams==o.teams && status==o.status && expected==o.expected;
    }
};
bool CurrentWorld(World* w) noexcept {
    *w=World{};
    void* mgr=At<void*>(image,kTeamManager);
    const auto status=At<const unsigned char*>(image,kGameStatus);
    if(!Readable(mgr,0x50) || !Readable(status,0x14FFC))return false;
    const unsigned expected=At<unsigned>(status,0x14FF8);
    if(!expected || expected>kMaxPlayers)return false;
    *w=World{mgr,At<void*>(mgr,0x38),status,expected};return true;
}

// The mission players one walk met (every player soldier with its User bound, dead or alive), kept for its frame
// (GameFrame): a player's reference is found again in that frame without another walk (an enemy's attack on a player,
// rechecked every frame). Borrowed pointers, never pinned: each one asked about is checked again (Present, its own weak
// reference, its slot and PUID) before use, and one that fails sends the lookup back to a walk. Game thread only.
struct Seen { ObjRef ref{};void* puid=nullptr;int slot=-1; };
constexpr unsigned kMaxSeen=2*kMaxPlayers;
struct Players {
    ULONGLONG frame=0;World world{};bool valid=false,overflow=false;
    unsigned count=0;
    Seen seen[kMaxSeen];
};
Players players;
void Record(Players& t,const ObjRef& ref,void* puid,int slot) noexcept {
    for(unsigned i=0;i<t.count;++i)if(Same(t.seen[i].ref,ref))return;   // the same object met again in another team
    if(t.count==kMaxSeen){t.overflow=true;return;}
    t.seen[t.count++]=Seen{ref,puid,slot};
}
// Seen `s` is still the player it was: present, its weak reference, its slot and PUID.
bool StillSeen(const Seen& s,unsigned expected) noexcept {
    ObjRef now{};void* puid=nullptr;int slot=-1;
    const auto object=static_cast<const unsigned char*>(s.ref.obj);
    return Present(object,&now) && Same(now,s.ref) && Player(object,expected,&puid,&slot) && slot==s.slot && puid==s.puid;
}

struct Query {
    netref::Kind kind=netref::Kind::none;
    NetLife life=NetLife::live;
    int slot=-1;
    const unsigned char* id=nullptr;   // a native NetId's
    ObjRef match{};                    // a NetId's object; two different ones: ambiguous
    bool matchTwice=false;
};
struct Visitor {
    const void* const* vtable;
    Query q[kMaxNetRefs]{};
    unsigned n=0,expected=0;
    bool natives=false;
    Players* table=nullptr;
};
void __fastcall Ignore(void*) noexcept {}
void __fastcall Visit(Visitor* v,const unsigned char* object) noexcept {
    if(!object)return;
    __try {
        ObjRef ref{};
        if(!Present(object,&ref))return;
        if(PlayerSoldier(object)) {   // by its slot alone: its NetId is matched by no reference
            void* puid=nullptr;int slot=-1;
            if(Player(object,v->expected,&puid,&slot))Record(*v->table,ref,puid,slot);
            return;
        }
        if(!v->natives)return;
        unsigned char id[32]{};
        if(!ReadNativeObjectId(object,id))return;
        for(unsigned i=0;i<v->n;++i) {
            Query& q=v->q[i];
            if(q.kind!=netref::Kind::native || std::memcmp(id,q.id,32))continue;
            if(Alive(q.life,object,&ref)) {
                if(q.match && !Same(q.match,ref))q.matchTwice=true;
                q.match=ref;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
const void* const kVisitorVtable[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};

// One native all-team walk in world `w`: the player table refilled, the NetId queries matched. False when it could not
// run, the world changed under it (a mission's end), or more player soldiers than the table holds.
bool WalkWorld(Visitor& v,const World& w,ULONGLONG frame) noexcept {
    players.valid=false;players.overflow=false;players.count=0;
    if(!Matches(kEnumAllTeams,kEnumSignature,sizeof(kEnumSignature)) || !team_walk::WalkRows(w.mgr))return false;
    v.expected=w.expected;v.table=&players;
    reinterpret_cast<WalkFn>(image+kEnumAllTeams)(w.mgr,&v);
    World after{};
    if(!CurrentWorld(&after) || !(after==w) || players.overflow){players.count=0;return false;}
    players.valid=true;players.frame=frame;players.world=w;
    return true;
}
NetResolved Result(const Query& q,unsigned expected) noexcept {
    NetResolved r{};
    r.slot=q.slot;
    if(q.kind==netref::Kind::player) {
        const Seen* live=nullptr;const Seen* dead=nullptr;
        unsigned lives=0,deaths=0;
        for(unsigned i=0;i<players.count;++i) {
            const Seen& s=players.seen[i];
            if(s.slot!=q.slot || !StillSeen(s,expected))continue;
            if(!static_cast<const unsigned char*>(s.ref.obj)[kDead]){live=&s;++lives;}
            else {dead=&s;++deaths;}
        }
        if(lives==1){r.obj=live->ref;r.puid=live->puid;r.found=true;}
        else if(lives>1)r.ambiguous=true;
        else if(q.life==NetLife::present) {
            if(deaths==1){r.obj=dead->ref;r.puid=dead->puid;r.dead=true;r.found=true;}
            else if(deaths>1)r.ambiguous=true;
        }
    } else if(q.kind==netref::Kind::native && q.match) {
        if(q.matchTwice)r.ambiguous=true;
        else {
            r.obj=q.match;r.found=true;
            r.dead=static_cast<const unsigned char*>(q.match.obj)[kDead]!=0;
        }
    }
    return r;
}
}  // namespace netobj

bool EncodeNetRef(const ObjRef& who,unsigned char out[32]) noexcept {
    using namespace netobj;
    if(!out)return false;
    std::memset(out,0,32);
    __try {
        // What it is called, not whether it may be found: dead, or deleted this frame, it keeps its name (a lock's owner,
        // a projectile's extra still sent); whether it is found is the receiver's NetLife.
        const auto object=static_cast<const unsigned char*>(who.obj);
        ObjRef ref{};
        if(!Named(object,&ref) || ref.ctrl!=who.ctrl)return false;
        if(PlayerSoldier(object)) {
            // A player by its slot alone; one outside the mission's players (no User bound) has no name another machine shares.
            void* puid=nullptr;int slot=-1;
            const unsigned expected=Expected();
            return expected && Player(object,expected,&puid,&slot) && netref::MakePlayer(slot,out);
        }
        unsigned char id[32]{};
        return ReadNativeObjectId(object,id) && netref::MakeNative(id,out);
    } __except(EXCEPTION_EXECUTE_HANDLER){std::memset(out,0,32);return false;}
}
bool EncodeNetRef(const void* object,unsigned char out[32]) noexcept {
    if(out)std::memset(out,0,32);
    __try { return object && out && Readable(object,kSelfCtrl+8) && EncodeNetRef(ObjRef::Of(object),out); }
    __except(EXCEPTION_EXECUTE_HANDLER){if(out)std::memset(out,0,32);return false;}
}

bool ResolveNetRefs(const unsigned char* const* refs,const NetLife* lives,unsigned n,NetResolved* out) noexcept {
    using namespace netobj;
    if(!out || n>kMaxNetRefs || (n && (!refs || !lives)))return false;
    for(unsigned i=0;i<n;++i)out[i]=NetResolved{};
    __try {
        Visitor visitor{kVisitorVtable};
        visitor.n=n;
        bool any=false;
        for(unsigned i=0;i<n;++i) {
            Query& q=visitor.q[i];
            q.kind=netref::Classify(refs[i],&q.slot);q.life=lives[i];
            if(q.kind==netref::Kind::invalid)return false;
            if(q.kind==netref::Kind::native){q.id=refs[i];visitor.natives=true;}
            any=any || q.kind!=netref::Kind::none;
        }
        for(unsigned i=0;i<n;++i)out[i].slot=visitor.q[i].slot;
        if(!any)return true;   // nothing asked: nothing found, no walk
        World world{};
        if(!CurrentWorld(&world))return false;
        // Players alone, this frame's table of this world, every one asked about still itself, and each asked slot has one
        // there to its NetLife (live for live): no walk. A slot with none (gone, or a respawn made since the walk) walks.
        const ULONGLONG frame=GameFrame();
        bool cached=!visitor.natives && frame && players.valid && players.frame==frame && players.world==world;
        for(unsigned j=0;cached && j<n;++j) {
            const Query& q=visitor.q[j];
            if(q.kind!=netref::Kind::player)continue;
            bool held=false;
            for(unsigned i=0;cached && i<players.count;++i) {
                const Seen& s=players.seen[i];
                if(s.slot!=q.slot)continue;
                if(!StillSeen(s,world.expected)){cached=false;break;}
                held=held || q.life==NetLife::present || !static_cast<const unsigned char*>(s.ref.obj)[kDead];
            }
            cached=cached && held;
        }
        if(!cached && !WalkWorld(visitor,world,frame))return false;
        for(unsigned i=0;i<n;++i)out[i]=Result(visitor.q[i],world.expected);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){for(unsigned i=0;i<n;++i)out[i]=NetResolved{};players.valid=false;return false;}
}
bool ResolveNetRef(const unsigned char* ref,NetLife life,NetResolved* out) noexcept {
    const unsigned char* refs[1]={ref};
    const NetLife lives[1]={life};
    return ResolveNetRefs(refs,lives,1,out);
}

bool StillNetRef(const unsigned char* ref,NetLife life,const NetResolved& r) noexcept {
    using namespace netobj;
    if(!r.found)return false;
    __try {
        const auto object=static_cast<const unsigned char*>(r.obj.obj);
        ObjRef now{};
        if(!Alive(life,object,&now) || !Same(now,r.obj))return false;
        int slot=-1;
        switch(netref::Classify(ref,&slot)) {
        case netref::Kind::player: {
            void* puid=nullptr;int found=-1;
            const unsigned expected=Expected();
            return expected && Player(object,expected,&puid,&found) && found==slot && puid==r.puid && (object[kDead]!=0)==r.dead;
        }
        case netref::Kind::native: {
            unsigned char id[32]{};
            return ReadNativeObjectId(object,id) && !std::memcmp(id,ref,32);
        }
        default: return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool SenderOwnsSlot(int slot,const char* authenticatedPuid) noexcept {
    if(slot<0 || slot>=netref::kPlayerSlots || !authenticatedPuid || !authenticatedPuid[0] ||
       !std::memchr(authenticatedPuid,0,sizeof(EDF6CoopPeer::id)))return false;
    const HMODULE coop=GetModuleHandleW(L"EDF6Coop.dll");
    const auto resolve=coop ? reinterpret_cast<EDF6CoopResolveMissionPlayerPuidFn>(
        GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid")) : nullptr;
    if(!resolve)return false;
    __try {
        EDF6CoopPeer who{};
        if(!resolve(static_cast<std::int32_t>(slot),&who) || !who.id[0] || !std::memchr(who.id,0,sizeof(who.id)))return false;
        return !std::strcmp(who.id,authenticatedPuid);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
