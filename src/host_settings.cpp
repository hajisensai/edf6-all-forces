// The host's settings and the room's builds online: host_settings.h (the logic and the word: host_settings_logic.h).
#include "host_settings.h"
#include "host_settings_logic.h"
#include "coop_attr.h"
#include "crew.h"
#include "hudtext.h"
#include "version.h"
#include <Windows.h>
#include <atomic>
#include <cwchar>

namespace crew {
namespace {
using namespace hostcfg;

// What the reads found (written by the tick and the mission sync, read by every thread).
std::atomic<int> hostRecruit{-1},hostCover{-1};   // the host's values as a guest; -1: this machine's ini
std::atomic<bool> hostSilent{false};              // a guest whose host published no word (an older All Forces)
std::atomic<bool> bikeGunner{true};               // this mission's 408 gunner seat (Edf41BikeGunnerAllowed)
// The builds (the menu's lines; the UI thread's).
Builds builds{};
SRWLOCK lock=SRWLOCK_INIT;   // the tick (UI thread) and the mission sync (the script's thread) both read

coopattr::Publisher publisher{kKey};
coopattr::Beat beat;

Word Mine() noexcept {
    Word w;
    strncpy_s(w.version,EDF6VC_VERSION,_TRUNCATE);
    w.features=kFeatures;
    w.vanillaRecruit=Cfg().vanillaRecruit;
    w.npcFreeSquadCover=Cfg().npcFreeSquadCover;
    return w;
}

struct Survey { const char* mine; Builds builds; std::uint32_t common=~0u; };
void EDF6COOP_MA_CALL CountMember(void* context,const EDF6CoopMemberValue* m) {
    auto* s=static_cast<Survey*>(context);
    if(!m || !m->memberId)return;
    Word w;
    const bool has=m->hasValue && m->value && ParseWord(m->value,w);
    Count(s->builds,m->isSelf!=0,has,has ? &w : nullptr,s->mine);
    if(!m->isSelf)s->common&=has ? w.features : 0u;   // this machine has its own build's (its word may not be out yet)
}

// One read of the room: the host's values and every member's build. `mission`: also decide this mission's
// every-member features. Logged when what it decides changes.
void Read(bool mission) noexcept {
    const coopattr::Table* t=coopattr::Api();
    const EDF6CoopRoomState room=coopattr::Room();
    Word host;
    bool hostWord=false;
    Survey survey{EDF6VC_VERSION};
    bool surveyed=false;
    if(t && room.inRoom) {
        coopattr::Value v;
        __try {
            if(coopattr::HostValue(t,kKey,v) && v.found && v.has)hostWord=ParseWord(v.text,host);
            surveyed=t->snapshot(kKey,&CountMember,&survey,nullptr)!=0;
        } __except(EXCEPTION_EXECUTE_HANDLER){hostWord=false;surveyed=false;}
    }
    const Effective e=Decide(room.inRoom!=0,room.isHost!=0,hostWord,host,Cfg().vanillaRecruit,Cfg().npcFreeSquadCover);
    const int r=e.source==Source::host ? (e.vanillaRecruit ? 1 : 0) : -1,c=e.source==Source::host ? (e.npcFreeSquadCover ? 1 : 0) : -1;
    const bool silent=e.source==Source::localHostSilent;
    const int wasR=hostRecruit.exchange(r),wasC=hostCover.exchange(c);
    const bool wasSilent=hostSilent.exchange(silent);
    if(wasR!=r || wasC!=c || wasSilent!=silent) {
        if(e.source==Source::host)Log("HOSTCFG playing by the host's VanillaRecruit=%d NpcFreeSquadCover=%d (this machine's ini: %d %d)",r,c,
                                      Cfg().vanillaRecruit,Cfg().npcFreeSquadCover);
        else if(silent)Log("HOSTCFG the host published no %s (an older All Forces): VanillaRecruit / NpcFreeSquadCover from this machine's ini",kKey);
        else Log("HOSTCFG VanillaRecruit / NpcFreeSquadCover from this machine's ini (%s)",room.inRoom ? "the host" : "no room");
    }
    AcquireSRWLockExclusive(&lock);
    const Builds was=builds;
    builds=surveyed ? survey.builds : Builds{};
    const Builds now=builds;
    ReleaseSRWLockExclusive(&lock);
    if(now.silent!=was.silent || now.differing!=was.differing || now.missing!=was.missing)
        Log("HOSTCFG the room's builds: %u members, %u without %s (an older All Forces), %u with other features, missing 0x%X",
            now.members,now.silent,kKey,now.differing,now.missing);
    if(mission) {
        // Offline: the gunner seat as built. Online: only with every member's word saying it has it (every machine then
        // decides the same; one older member and the 408 is a stock Car for everyone).
        const bool allowed=BikeGunnerAtSync(room.inRoom!=0,surveyed,survey.builds.silent,survey.common);
        if(bikeGunner.exchange(allowed)!=allowed || !allowed)
            Log("HOSTCFG this mission's 408 sidecar gunner seat: %s",allowed ? "open" :
                "closed (a member's All Forces does not have it: the 408 drives as a stock Car for everyone)");
    } else {
        // The room / HQ frame (no mission decided here): out of a room the seat is this machine's again, so the last
        // online mission's closed seat does not follow into an offline one.
        const bool open=BikeGunnerOutOfMission(room.inRoom!=0,bikeGunner.load());
        if(bikeGunner.exchange(open)!=open)Log("HOSTCFG out of the room: the 408 sidecar gunner seat is this machine's again (open)");
    }
}
}  // namespace

void HostSettingsRoomTick() noexcept {
    if(!beat.Due(GetTickCount64()))return;
    const coopattr::Table* t=coopattr::Api();
    if(!t){publisher.Forget();Read(false);return;}
    char word[kWordMost];
    if(FormatWord(Mine(),word,sizeof(word))) {
        std::uint32_t why=0;
        coopattr::Publisher::Result r=coopattr::Publisher::Result::refusedAgain;
        __try { r=publisher.Publish(t,word,&why); } __except(EXCEPTION_EXECUTE_HANDLER){r=coopattr::Publisher::Result::refusedAgain;}
        if(r==coopattr::Publisher::Result::published)Log("HOSTCFG told the room %s=%s",kKey,word);
        else if(r==coopattr::Publisher::Result::refused)Log("HOSTCFG EDF6Coop refused %s=%s (lastError %u): tried again next beat",kKey,word,why);
    }
    Read(false);
}

void HostSettingsMissionSync() noexcept { Read(true); }

void HostSettingsOfflineMission() noexcept {
    const bool open=BikeGunnerOffline();
    if(bikeGunner.exchange(open)!=open)Log("HOSTCFG an offline mission: the 408 sidecar gunner seat is this machine's (open)");
}

bool VanillaRecruitInEffect() noexcept {
    const int h=hostRecruit.load(std::memory_order_relaxed);
    return h>=0 ? h!=0 : Cfg().vanillaRecruit;
}
bool NpcFreeSquadCoverInEffect() noexcept {
    const int h=hostCover.load(std::memory_order_relaxed);
    return h>=0 ? h!=0 : Cfg().npcFreeSquadCover;
}
bool Edf41BikeGunnerAllowed() noexcept { return bikeGunner.load(std::memory_order_relaxed); }

int HostSettingsLines(roomsync::SyncLine* out,int capacity) noexcept {
    using hudtext::Tr;using hudtext::Tx;
    int n=0;
    const int r=hostRecruit.load(),c=hostCover.load();
    if(n<capacity && r>=0 && (r!=(Cfg().vanillaRecruit ? 1 : 0) || c!=(Cfg().npcFreeSquadCover ? 1 : 0))) {
        _snwprintf_s(out[n].text,_TRUNCATE,Tr(Tx::hostSettingsHost),Tr(r ? Tx::hostSettingsOn : Tx::hostSettingsOff),
                     Tr(c ? Tx::hostSettingsOn : Tx::hostSettingsOff));
        out[n++].warn=false;
    } else if(n<capacity && hostSilent.load()) {
        _snwprintf_s(out[n].text,_TRUNCATE,L"%ls",Tr(Tx::hostSettingsSilent));
        out[n++].warn=true;
    }
    AcquireSRWLockShared(&lock);
    const Builds b=builds;
    ReleaseSRWLockShared(&lock);
    if(n<capacity && (b.silent || b.differing)) {
        wchar_t names[96]{};
        const struct { std::uint32_t bit; Tx text; } kNames[]={{kFeatBikeGunner,Tx::featBikeGunner},{kFeatCreditReload41,Tx::featCreditReload41},
            {kFeatBikeCarDrive,Tx::featBikeCarDrive},{kFeatOutfit41Colours,Tx::featOutfit41Colours},{kFeatNoClimb41,Tx::featNoClimb41},
            {kFeatHostSettings,Tx::featHostSettings}};
        for(const auto& k:kNames) {
            if(!(b.missing&k.bit))continue;
            const std::size_t used=std::wcslen(names);
            _snwprintf_s(names+used,_countof(names)-used,_TRUNCATE,L"%ls%ls",used ? Tr(Tx::versionListSep) : L"",Tr(k.text));
        }
        _snwprintf_s(out[n].text,_TRUNCATE,Tr(Tx::buildsDiffer),b.silent+b.differing,names);
        out[n++].warn=true;
    }
    return n;
}
}  // namespace crew
