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

// Not deleted, its own weak reference live (dead or alive).
bool Present(const unsigned char* object,ObjRef* ref) noexcept {
    if(!Readable(object,kDead+1) || (object[0x18]&4))return false;
    const auto ctrl=At<const unsigned char*>(object,kSelfCtrl);
    if(!Readable(ctrl,16) || At<LONG>(ctrl,8)<=0 || At<const void*>(object,kSelf)!=object)return false;
    *ref=ObjRef{object,ctrl};return true;
}
bool Live(const unsigned char* object,ObjRef* ref) noexcept {
    return Present(object,ref) && !object[kDead];
}
bool Alive(const NetLife life,const unsigned char* object,ObjRef* ref) noexcept {
    return life==NetLife::live ? Live(object,ref) : Present(object,ref);
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

struct Query {
    netref::Kind kind=netref::Kind::none;
    NetLife life=NetLife::live;
    int slot=-1;
    const unsigned char* id=nullptr;   // a native NetId's
    // What the walk met: a player's live and (dead) present objects, a NetId's objects; two different ones of either: none.
    ObjRef live{},present{},match{};
    void* livePuid=nullptr;void* presentPuid=nullptr;
    bool liveTwice=false,presentTwice=false,matchTwice=false;
};
struct Visitor {
    const void* const* vtable;
    Query q[kMaxNetRefs]{};
    unsigned n=0,expected=0;
    bool players=false,natives=false;
};
bool Same(const ObjRef& a,const ObjRef& b) noexcept { return a.obj==b.obj && a.ctrl==b.ctrl; }
// `ref` into `slot`, `twice` set when another object came already (the same object met again in another team is no second).
void Note(ObjRef& slot,bool& twice,const ObjRef& ref) noexcept {
    if(slot && !Same(slot,ref))twice=true;
    slot=ref;
}
void __fastcall Ignore(void*) noexcept {}
void __fastcall Visit(Visitor* v,const unsigned char* object) noexcept {
    if(!object)return;
    __try {
        ObjRef ref{};
        if(!Present(object,&ref))return;
        if(v->players) {
            void* puid=nullptr;int slot=-1;
            if(Player(object,v->expected,&puid,&slot)) {
                const bool live=!object[kDead];
                for(unsigned i=0;i<v->n;++i) {
                    Query& q=v->q[i];
                    if(q.kind!=netref::Kind::player || q.slot!=slot)continue;
                    if(live){Note(q.live,q.liveTwice,ref);q.livePuid=puid;}
                    else {Note(q.present,q.presentTwice,ref);q.presentPuid=puid;}
                }
            }
        }
        if(!v->natives)return;
        unsigned char id[32]{};
        if(!ReadNativeObjectId(object,id))return;
        for(unsigned i=0;i<v->n;++i) {
            Query& q=v->q[i];
            if(q.kind!=netref::Kind::native || std::memcmp(id,q.id,32))continue;
            if(Alive(q.life,object,&ref))Note(q.match,q.matchTwice,ref);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
const void* const kVisitorVtable[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};

NetResolved Result(const Query& q) noexcept {
    NetResolved r{};
    r.slot=q.slot;
    if(q.kind==netref::Kind::player) {
        if(q.live && !q.liveTwice){r.obj=q.live;r.puid=q.livePuid;r.found=true;}
        else if(!q.live && q.life==NetLife::present && q.present && !q.presentTwice) {
            r.obj=q.present;r.puid=q.presentPuid;r.dead=true;r.found=true;
        }
    } else if(q.kind==netref::Kind::native && q.match && !q.matchTwice) {
        r.obj=q.match;r.found=true;
        r.dead=static_cast<const unsigned char*>(q.match.obj)[kDead]!=0;
    }
    return r;
}
}  // namespace netobj

bool EncodeNetRef(const ObjRef& who,unsigned char out[32]) noexcept {
    using namespace netobj;
    if(!out)return false;
    std::memset(out,0,32);
    __try {
        const auto object=static_cast<const unsigned char*>(who.obj);
        ObjRef ref{};
        if(!Present(object,&ref) || ref.ctrl!=who.ctrl)return false;
        void* puid=nullptr;int slot=-1;
        const unsigned expected=Expected();
        if(expected && Player(object,expected,&puid,&slot))return netref::MakePlayer(slot,out);
        // A player soldier outside the mission's players (no User bound yet): no reference another machine would share.
        if(IsAnyPlayer(object))return false;
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
            if(q.kind==netref::Kind::player)visitor.players=true;
            any=any || q.kind!=netref::Kind::none;
        }
        for(unsigned i=0;i<n;++i)out[i].slot=visitor.q[i].slot;
        if(!any)return true;   // nothing asked: nothing found, no walk
        if(!Matches(kEnumAllTeams,kEnumSignature,sizeof(kEnumSignature)))return false;
        void* mgr=At<void*>(image,kTeamManager);
        const auto status=At<const unsigned char*>(image,kGameStatus);
        if(!Readable(mgr,0x50) || !Readable(status,0x14FFC))return false;
        void* teams=At<void*>(mgr,0x38);
        visitor.expected=At<unsigned>(status,0x14FF8);
        if(!visitor.expected || visitor.expected>kMaxPlayers || !team_walk::WalkRows(mgr))return false;
        reinterpret_cast<WalkFn>(image+kEnumAllTeams)(mgr,&visitor);
        // The world changed under the walk (a mission's end): nothing it met is trusted.
        if(At<void*>(image,kTeamManager)!=mgr || At<const void*>(image,kGameStatus)!=status || At<void*>(mgr,0x38)!=teams ||
           At<unsigned>(status,0x14FF8)!=visitor.expected)return false;
        for(unsigned i=0;i<n;++i)out[i]=Result(visitor.q[i]);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){for(unsigned i=0;i<n;++i)out[i]=NetResolved{};return false;}
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
