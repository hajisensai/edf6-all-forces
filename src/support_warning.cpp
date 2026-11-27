// The supports' warnings on the ground (support_warning.h): kept here on the game thread, their outlines laid on what stands
// under them (MapRay: terrain or a roof) once, when they are made; the HUD copies them under the lock.
#include "support_warning.h"
#include "support_warning_logic.h"
#include "crew.h"
#include "heli.h"            // MapRay
#include "memory.h"
#include "support_aircraft.h"
#include "support_call.h"
#include "support_net.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
using warning::Look;
constexpr int kRingPoints=48;
constexpr int kKept=16;
constexpr unsigned long long kRouteMs=30000;     // a deployment's ring shows this long (its route: while its lead flies it)
constexpr int kWayPoints=4;                      // a deployment's route as laid (warning::Route, a marked run's line)
constexpr unsigned long long kPreviewMs=250;     // the map's preview lasts while asked again within this
constexpr float kGroundReach=400.0f;             // m over and under a point its ground is looked for
constexpr float kMarkRing=40.0f;                 // m: the ring at a jet's or a vehicle's point (no circle of its own)

struct Kept {
    bool on=false;
    WarningView view{};
    wchar_t name[28]{};
    ULONGLONG until=0;       // GameMs
    bool timed=true;         // its words count down
    // A deployment's: its lead aircraft or vehicle (gone: the warning goes), and its route as laid (`ways` points, 0 for a
    // ring), drawn each frame from where the lead is on along what is still ahead of it (warning::RouteAhead).
    ObjRef lead{};
    float way[kWayPoints][3]{};
    int ways=0;
};
SRWLOCK lock=SRWLOCK_INIT;
Kept kept[kKept];
Kept preview;
int nextSlot=0;
// The preview's last ask (the outline is made again only when the point or the entry changed).
int previewCatalog=-1;
float previewAt[3]{};
float previewHeading[3]{};
bool previewRun=false;
ULONGLONG previewPlannedAt=0;

// What stands at (x, z) near height `y`: the first surface down from kGroundReach over it; `y` when there is none.
float GroundAt(float x,float y,float z) noexcept {
    const float from[3]={x,y+kGroundReach,z},to[3]={x,y-kGroundReach,z};
    float hit[3];
    return MapRay(from,to,hit)>=0.0f ? hit[1] : y;
}
void OnGround(float* p) noexcept { p[1]=GroundAt(p[0],p[1],p[2])+0.5f; }

void SetRing(WarningView& v,const warning::Circle& c,Look look,bool ground) noexcept {
    v.look=look;v.closed=true;v.points=kRingPoints;
    warning::Ring(c,kRingPoints,v.pt);
    if(ground)for(int i=0;i<kRingPoints;++i)OnGround(v.pt[i]);
    std::memcpy(v.label,c.centre,12);
    v.label[1]=ground ? GroundAt(c.centre[0],c.centre[1],c.centre[2]) : c.centre[1];
}
void SetPolygon(WarningView& v,const float (*corners)[3],int n,Look look) noexcept {
    v.look=look;v.closed=true;v.points=0;
    // Each side cut into pieces laid on the ground, so a long strip follows the terrain.
    constexpr int kPieces=8;
    for(int k=0;k<n && v.points+kPieces<=kWarnPoints;++k) {
        const float* a=corners[k];const float* b=corners[(k+1)%n];
        for(int j=0;j<kPieces;++j) {
            float* p=v.pt[v.points++];
            const float t=static_cast<float>(j)/kPieces;
            for(int i=0;i<3;++i)p[i]=a[i]+(b[i]-a[i])*t;
            OnGround(p);
        }
    }
    float c[3]{};
    for(int k=0;k<n;++k)for(int i=0;i<3;++i)c[i]+=corners[k][i]/static_cast<float>(n);
    std::memcpy(v.label,c,12);
}
void SetRoute(WarningView& v,const float (*pts)[3],int n) noexcept {
    v.look=Look::route;v.closed=false;v.points=n<kWarnPoints ? n : kWarnPoints;
    for(int i=0;i<v.points;++i)std::memcpy(v.pt[i],pts[i],12);
    std::memcpy(v.label,pts[0],12);
}

bool SetBarrage(WarningView& v,int kind,const float* target,const float* heading) noexcept {
    warning::BarrageFootprint footprint;
    if(!warning::BarrageArea(kind,target,heading,&footprint))return false;
    if(footprint.rectangular)SetPolygon(v,footprint.corners,4,Look::strike);
    else SetRing(v,footprint.circle,Look::strike,true);
    return true;
}

Kept& Claim() noexcept {
    for(Kept& k:kept)if(!k.on)return k;
    Kept& k=kept[nextSlot];   // full: the oldest made goes
    nextSlot=(nextSlot+1)%kKept;
    return k;
}
std::uint32_t madeCount=0;   // the warnings made: each one's id (WarningView::id); the preview's is its own
constexpr std::uint32_t kPreviewWarning=0xFFFFFFFFu;
void Put(Kept& slot,const Kept& made) noexcept {
    AcquireSRWLockExclusive(&lock);
    slot=made;
    slot.view.id=&slot==&preview ? kPreviewWarning : (++madeCount==kPreviewWarning ? ++madeCount : madeCount);
    ReleaseSRWLockExclusive(&lock);
}

// What a catalog entry brings in the air (its spec), false for none.
bool AirSpec(int catalog,SupportAircraft* spec) noexcept {
    return SupportAircraftSpec(catalog,spec);
}
// A flown support's outline from `from` (where it comes in) to `mark`: its route, and what it does at the point.
// `ring`: a second outline (a guard's circle, a sortie's area); false when there is none.
bool FlownOutline(const SupportAircraft& spec,const float* from,const float* mark,Kept& route,Kept& ring) noexcept {
    const auto pass=static_cast<SortiePass>(spec.sortie);
    float beyond=0.0f;
    if(pass==SortiePass::gunRun || pass==SortiePass::missilePass || pass==SortiePass::carpet || pass==SortiePass::popUp ||
       pass==SortiePass::missileRain)beyond=sortie::kPassBeyond;
    if(pass==SortiePass::heliStrafe)beyond=sortie::kHeliPassBeyond;
    float pts[4][3];
    const int n=warning::Route(from,mark,beyond,pts);
    SetRoute(route.view,pts,n);
    ring.view=WarningView{};
    warning::Circle c;std::memcpy(c.centre,mark,12);
    if(pass==SortiePass::carpet) {
        float corners[4][3];
        warning::CarpetStrip(from,mark,20.0f,corners);
        SetPolygon(ring.view,corners,4,Look::strike);
        return true;
    }
    if(pass==SortiePass::heliCircle){c.radius=sortie::kCircleRadius;SetRing(ring.view,c,Look::strike,true);return true;}
    if(pass==SortiePass::heliHunter){c.radius=sortie::kHunterStation;SetRing(ring.view,c,Look::orbit,true);return true;}
    if(pass!=SortiePass::none || spec.follow)return false;
    c.radius=spec.heli>=0 && Cfg().heliGuardRadius>0.0f ? Cfg().heliGuardRadius : kMarkRing;
    SetRing(ring.view,c,Look::orbit,true);
    return true;
}
void Name(Kept& k,const wchar_t* name) noexcept { _snwprintf_s(k.name,_countof(k.name),_TRUNCATE,L"%ls",name ? name : L""); }
}  // namespace

void WarnBarrage(int kind,const float* target,const float* heading,int endFrame) noexcept {
    if(!target || !heading)return;
    Kept made;
    made.on=true;
    if(!SetBarrage(made.view,kind,target,heading))return;
    int strike=-1;
    for(int i=0;i<SupportStrikeCount();++i)if(SupportStrikeBarrage(i)==kind){strike=i;break;}
    Name(made,SupportStrikeName(strike));
    made.until=GameMs()+warning::BarrageMs(endFrame);
    Put(Claim(),made);
    Log("WARN barrage %d: %d outline points for %llu ms",kind,made.view.points,warning::BarrageMs(endFrame));
}

void WarnUltimate(const float* at,unsigned long long ms) noexcept {
    if(!at)return;
    warning::Circle c;std::memcpy(c.centre,at,12);c.radius=warning::UltimateRadius();
    Kept made;
    made.on=true;
    SetRing(made.view,c,Look::strike,true);
    for(int i=0;i<SupportStrikeCount();++i)if(SupportStrikeBarrage(i)<0){Name(made,SupportStrikeName(i));break;}
    made.until=GameMs()+ms;
    Put(Claim(),made);
}

void WarnDeployment(const support_net::Plan& plan,const ObjRef& lead) noexcept {
    if(!plan.count)return;
    const auto& unit=plan.units[0];
    const float* from=unit.matrix+12;
    float mark[3];std::memcpy(mark,plan.target,12);
    if(!std::isfinite(from[0]+from[1]+from[2]+mark[0]+mark[1]+mark[2]))return;
    const int catalog=static_cast<int>(plan.catalogId);
    const wchar_t* const name=SupportCallName(catalog);
    Kept route,ring;
    route.on=true;route.timed=false;
    SupportAircraft spec;
    const bool flown=AirSpec(catalog,&spec);
    if(flown && plan.run.half>0.0f) {
        const float dx=plan.run.x*plan.run.half,dz=plan.run.z*plan.run.half;
        const float pts[3][3]={{from[0],from[1],from[2]},{mark[0]-dx,mark[1],mark[2]-dz},
                              {mark[0]+dx,mark[1],mark[2]+dz}};
        SetRoute(route.view,pts,3);
    } else if(flown)ring.on=FlownOutline(spec,from,mark,route,ring);
    else {
        // A ground support, a transport, an airdrop: where it comes from and where it goes.
        const float pts[2][3]={{from[0],from[1],from[2]},{mark[0],mark[1],mark[2]}};
        SetRoute(route.view,pts,2);
        warning::Circle c;std::memcpy(c.centre,mark,12);c.radius=kMarkRing;
        SetRing(ring.view,c,Look::orbit,true);ring.on=true;
    }
    route.until=ring.until=GameMs()+kRouteMs;
    ring.timed=ring.view.look==Look::strike;
    // Both the lead's (the user, 2026-11-27: 「飞机进场会有个红线停留在原地」): laid from where it was made and kept a
    // fixed 30 s, the route stood at the entry, its words there, while the aircraft flew on. Now its route runs from the
    // lead on (SupportWarningTick) and goes once the lead has flown it or is gone; its ring goes with the lead too.
    route.lead=ring.lead=lead;
    if(lead) {
        route.until=~0ull;
        route.ways=route.view.points<kWayPoints ? route.view.points : kWayPoints;
        for(int i=0;i<route.ways;++i)std::memcpy(route.way[i],route.view.pt[i],12);
    }
    Name(route,name);Name(ring,L"");
    Put(Claim(),route);
    if(ring.on)Put(Claim(),ring);
}

void WarnPreview(int catalog,const float* target,const float* strikeHeading,const float* runStart) noexcept {
    if(catalog<0 || !target) {
        AcquireSRWLockExclusive(&lock);preview.on=false;ReleaseSRWLockExclusive(&lock);
        previewCatalog=-1;
        return;
    }
    const ULONGLONG now=GameMs();
    const float dx=target[0]-previewAt[0],dz=target[2]-previewAt[2];
    const bool turned=strikeHeading && strikeHeading[0]*previewHeading[0]+strikeHeading[2]*previewHeading[2]<0.999f;
    const bool moved=catalog!=previewCatalog || dx*dx+dz*dz>25.0f || turned || (runStart!=nullptr)!=previewRun;
    // Made again at most ten times a second (its outline's rays down to the ground), a flown one's route four (its entry is
    // the support planner's: rays along it).
    const bool throttled=catalog==previewCatalog && now-previewPlannedAt<(SupportCatalogAir(catalog) ? 250u : 100u);
    AcquireSRWLockShared(&lock);
    const bool shown=preview.on;
    ReleaseSRWLockShared(&lock);
    if(shown && (!moved || throttled)) {
        AcquireSRWLockExclusive(&lock);preview.until=now+kPreviewMs;ReleaseSRWLockExclusive(&lock);
        return;
    }
    previewCatalog=catalog;std::memcpy(previewAt,target,12);previewPlannedAt=now;
    previewRun=runStart!=nullptr;
    if(strikeHeading)std::memcpy(previewHeading,strikeHeading,12);
    else std::memset(previewHeading,0,sizeof(previewHeading));
    Kept made;
    made.on=true;made.timed=false;made.view.preview=true;
    const int strike=SupportCatalogStrike(catalog);
    if(strike>=0) {
        float automaticHeading[3];
        if(!strikeHeading){SupportStrikeHeading(target,automaticHeading);strikeHeading=automaticHeading;}
        const int kind=SupportStrikeBarrage(strike);
        if(kind>=0) {
            if(!SetBarrage(made.view,kind,target,strikeHeading))return;
        } else {
            warning::Circle c;std::memcpy(c.centre,target,12);c.radius=warning::UltimateRadius();
            SetRing(made.view,c,Look::strike,true);
        }
    } else if(runStart) {
        const float pts[2][3]={{runStart[0],runStart[1]+0.5f,runStart[2]},{target[0],target[1]+0.5f,target[2]}};
        SetRoute(made.view,pts,2);
    } else {
        SupportAircraft spec;
        float from[3];
        if(SupportCatalogAir(catalog) && AirSpec(catalog,&spec) && SupportPreviewEntry(catalog,target,from,strikeHeading)) {
            Kept ring;
            const bool both=FlownOutline(spec,from,target,made,ring);
            // One outline at a time: the route, and the ring it ends in folded into the same points where they fit.
            if(both && made.view.points+ring.view.points<=kWarnPoints) {
                // The ring drawn as its own loop: kept apart by a gap point (NaN) the HUD skips.
                float* gap=made.view.pt[made.view.points++];
                gap[0]=gap[1]=gap[2]=std::nanf("");
                for(int i=0;i<=ring.view.points && made.view.points<kWarnPoints;++i)
                    std::memcpy(made.view.pt[made.view.points++],ring.view.pt[i%ring.view.points],12);
                made.view.look=ring.view.look==Look::strike ? Look::strike : Look::route;
            }
        } else {
            warning::Circle c;std::memcpy(c.centre,target,12);c.radius=kMarkRing;
            SetRing(made.view,c,Look::orbit,true);
        }
        made.view.preview=true;
    }
    Name(made,SupportCallName(catalog));
    made.until=now+kPreviewMs;
    Put(preview,made);
}

void SupportWarningTick() noexcept {
    const ULONGLONG now=GameMs();
    AcquireSRWLockExclusive(&lock);
    for(Kept* k:{&preview}){if(k->on && now>=k->until)k->on=false;}
    for(Kept& k:kept) {
        if(!k.on)continue;
        if(now>=k.until){k.on=false;continue;}
        if(k.lead) {
            if(!objref::Exists(k.lead)){k.on=false;continue;}
            if(k.ways>=2) {
                const float* at=reinterpret_cast<const float*>(static_cast<const unsigned char*>(k.lead.obj)+kPosition);
                float ahead[kWayPoints][3];
                const int n=std::isfinite(at[0]+at[1]+at[2]) ? warning::RouteAhead(k.way,k.ways,at,ahead) : 0;
                if(n<2){k.on=false;continue;}
                k.view.points=n;
                for(int i=0;i<n;++i)std::memcpy(k.view.pt[i],ahead[i],12);
                std::memcpy(k.view.label,ahead[0],12);
            }
        }
        const unsigned long long left=(k.until-now+999)/1000;
        if(k.timed && k.name[0])_snwprintf_s(k.view.text,_countof(k.view.text),_TRUNCATE,L"%ls %llus",k.name,left);
        else _snwprintf_s(k.view.text,_countof(k.view.text),_TRUNCATE,L"%ls",k.name);
    }
    if(preview.on)_snwprintf_s(preview.view.text,_countof(preview.view.text),_TRUNCATE,L"%ls",preview.name);
    ReleaseSRWLockExclusive(&lock);
}

void ResetSupportWarnings() noexcept {
    AcquireSRWLockExclusive(&lock);
    for(Kept& k:kept)k=Kept{};
    preview=Kept{};
    ReleaseSRWLockExclusive(&lock);
    nextSlot=0;previewCatalog=-1;
}

int SupportWarningViews(WarningView* out,int most) noexcept {
    if(!out || most<=0)return 0;
    int n=0;
    AcquireSRWLockShared(&lock);
    for(const Kept& k:kept)if(k.on && n<most)out[n++]=k.view;
    if(preview.on && n<most)out[n++]=preview.view;
    ReleaseSRWLockShared(&lock);
    return n;
}
}  // namespace crew
