// Physical support entry planning. No game memory or spawning: callers provide verified terrain.
#pragma once
#include "playarea.h"
#include <cmath>

namespace crew::support {
enum class Refusal { none, noArea, noSky, noEntry, unsupported, online, cooldown, unavailable };
// An air arrival: `from` the lead's place in the air, `heading` level and unit toward the target. `to`: where the same
// line leaves the map again past the target (a pass's far end, MakePassLine), `beyond`: `from` stands past the observer's
// draw distance (false: no bearing had such a place inside the physical square; the farthest from the observer was taken).
struct Route { float from[3]{},heading[3]{},to[3]{}; bool beyond=false; };
// What an arrival may use, from the game (crew.h): `half` the physical square |x|,|z| <= half a body may be made and fly in
// (the Havok world and the jets' deletion: ArrivalHalf); `draw` the m out to which the observer's near camera draws
// (NearDrawDistance: the mission's FarClipZ raised by ini ViewDistance).
struct Reach { float half,draw; };
constexpr float kEntryInset=30.0f,kMinJourney=600.0f,kObserverClear=800.0f;

inline float FlatDistance(const float* a,const float* b) noexcept {
    const float x=a[0]-b[0],z=a[2]-b[2];return std::sqrt(x*x+z*z);
}

// Off the map (2026-10-10, the user: 「我叫的支援直接凭空出现了」「既然地图外飞来，直线之类的更合理的路线才对吧」). The
// measured play area (playarea.h) stands kVoidMargin INSIDE the ground's edge, and the old entry stood kEntryInset inside
// that: 180 m inside the ground, 894-941 m from the caller (2026-10-10 13:14-13:17 log), well within the 1000 m (ini
// ViewDistance: 3000 m) the near camera draws: the aircraft were made in plain sight. An air entry now stands past the
// ground's edge (GroundEdge) by kOffMap at least, inside the physical square (Reach::half), and, where the square holds
// one, past the observer's draw distance by kDrawClear.
constexpr float kOffMap=200.0f,kDrawClear=200.0f;
constexpr int kAirBearings=16;      // the bearings round the target an entry is looked for on
constexpr float kRouteStep=50.0f;   // m: along a bearing, the entry's distance is searched in these steps
// A pass's ends (MakePassLine): kPassOut past the ground's edge, at most what leaves kTurnRoom of its turn's radius plus
// kPassRoom inside the physical square (it turns round out there), at least kPassLeast past the ground's edge.
constexpr float kPassOut=300.0f,kPassLeast=100.0f,kTurnRoom=2.5f,kPassRoom=100.0f;

// The ground's edge: the measured walls stand kVoidMargin inside it (playarea.h Combine).
inline void GroundEdge(const PlayArea& area,float* lo,float* hi) noexcept {
    for(int k=0;k<2;++k){lo[k]=area.lo[k]-area::kVoidMargin;hi[k]=area.hi[k]+area::kVoidMargin;}
}
// m along unit (ux, uz) from (x, z) to the side of the box lo..hi (0: outside it already).
inline float ExitAlong(const float* lo,const float* hi,float x,float z,float ux,float uz) noexcept {
    float t=1e30f;
    if(ux>1e-6f)t=std::fmin(t,(hi[0]-x)/ux);else if(ux< -1e-6f)t=std::fmin(t,(lo[0]-x)/ux);
    if(uz>1e-6f)t=std::fmin(t,(hi[1]-z)/uz);else if(uz< -1e-6f)t=std::fmin(t,(lo[1]-z)/uz);
    return t>0.0f ? t : 0.0f;
}
// m from `at` along unit (ux, uz) to a pass's end there (see kPassOut); -1: no room for one past the ground's edge.
inline float PassEnd(const PlayArea& area,const Reach& reach,const float* at,float ux,float uz,float turn) noexcept {
    float glo[2],ghi[2];GroundEdge(area,glo,ghi);
    const float plo[2]={-reach.half,-reach.half},phi[2]={reach.half,reach.half};
    const float ground=ExitAlong(glo,ghi,at[0],at[2],ux,uz),square=ExitAlong(plo,phi,at[0],at[2],ux,uz);
    const float end=std::fmin(ground+kPassOut,square-kTurnRoom*turn-kPassRoom);
    return end>=ground+kPassLeast ? end : -1.0f;
}

// The air entry: on the bearing round the target that it is best on, as near the target as the rules let it stand (off
// the map, past the draw distance) and, where the square holds no place past the draw distance, as far from the observer
// as it can: those past the draw distance first, the one whose heading in is nearest `preferred` (the caller's own toward
// the target: it comes in over the caller's shoulder) among them; with none past it, the farthest from the observer. `turn` (m, a pass's turning radius: the paratroop plane):
// the line must also leave the map again past the target with room to turn round at either end (PassEnd), route.to that
// far end; 0: not needed (route.to then where the line leaves the ground's edge by kOffMap). `clear` validates a full
// size aircraft corridor, `height` the surface under a point (false: no ground there, the void off the map: no floor to
// keep over). Never falls back to anything on the map.
template<class Clear,class Height>
Refusal AirRoute(const PlayArea& area,const Reach& reach,const float* target,const float* observer,const float* preferred,
                 float altitude,float turn,Clear clear,Height height,Route& route) noexcept {
    if(!area.ground || !std::isfinite(target[0]+target[1]+target[2]) ||
       !(area.hi[0]-area.lo[0]>2*kEntryInset && area.hi[1]-area.lo[1]>2*kEntryInset))return Refusal::noArea;
    if(target[0]<area.lo[0] || target[0]>area.hi[0] || target[2]<area.lo[1] || target[2]>area.hi[1])return Refusal::noEntry;
    float glo[2],ghi[2];GroundEdge(area,glo,ghi);
    const float plo[2]={-reach.half,-reach.half},phi[2]={reach.half,reach.half};
    bool found=false;float best=-3.0f;
    for(int b=0;b<kAirBearings;++b) {
        const float a=6.28318531f*static_cast<float>(b)/kAirBearings,ux=std::cos(a),uz=std::sin(a);   // target -> entry
        const float closest=ExitAlong(glo,ghi,target[0],target[2],ux,uz)+kOffMap,farthest=ExitAlong(plo,phi,target[0],target[2],ux,uz);
        if(farthest<closest || farthest<kMinJourney)continue;
        // The nearest past the draw distance, else the farthest from the observer.
        float t=closest<kMinJourney ? kMinJourney : closest,most=-1.0f,mostAt=t;
        bool beyond=observer==nullptr;
        for(float s=t;!beyond;s+=kRouteStep) {
            const float at=s<farthest ? s : farthest,p[3]={target[0]+ux*at,0.0f,target[2]+uz*at};
            const float d=FlatDistance(p,observer);
            if(d>=reach.draw+kDrawClear){t=at;beyond=true;break;}
            if(d>most){most=d;mostAt=at;}
            if(at>=farthest)break;
        }
        if(!beyond)t=mostAt;
        if(observer && !beyond && most<kObserverClear)continue;
        const float edge[2]={target[0]+ux*t,target[2]+uz*t};
        Route candidate{{edge[0],target[1]+altitude,edge[1]},{-ux,0.0f,-uz},{target[0],target[1]+altitude,target[2]},beyond};
        float end;
        if(turn>0.0f) {
            end=PassEnd(area,reach,target,-ux,-uz,turn);
            if(end<0.0f || PassEnd(area,reach,target,ux,uz,turn)<0.0f)continue;   // it turns round at either end
        } else end=std::fmin(ExitAlong(glo,ghi,target[0],target[2],-ux,-uz)+kOffMap,ExitAlong(plo,phi,target[0],target[2],-ux,-uz));
        candidate.to[0]=target[0]-ux*end;candidate.to[2]=target[2]-uz*end;
        float top=target[1]+altitude;
        for(int sample=0;sample<=16;++sample) {
            const float f=static_cast<float>(sample)/16.0f;
            const float x=edge[0]+(target[0]-edge[0])*f,z=edge[1]+(target[2]-edge[1])*f;
            float y;
            if(!height(x,z,target[1],y) || !std::isfinite(y))continue;   // the void past the ground's edge: nothing to clear
            if(y+altitude>top)top=y+altitude;
        }
        candidate.from[1]=top;candidate.to[1]=top;
        const float over[3]={target[0],top,target[2]};
        if(!clear(candidate.from,over))continue;
        // Past the draw distance: the heading nearest `preferred`; none past it: the farthest from the observer (the least
        // seen of its appearing), the heading only between equals.
        const float align=candidate.heading[0]*preferred[0]+candidate.heading[2]*preferred[2];
        const float score=beyond ? 10.0f+align : most/(reach.draw+kDrawClear)+0.01f*align;
        if(!found || score>best){found=true;best=score;route=candidate;}
    }
    return found ? Refusal::none : Refusal::noEntry;
}

// The paratroop plane's passes (2026-10-10, the user: 「空降时外援飞来的路线不对……直线之类的更合理的路线才对吧」): one
// straight line through the drop point `at` along `dir` (the heading it came in on), flown either way between its ends
// `lo` (< 0) and `hi` (> 0), m along `dir` from `at`, each past the ground's edge with room to turn round (PassEnd).
struct PassLine { float at[3],dir[3];float lo,hi; };
inline bool MakePassLine(const PlayArea& area,const Reach& reach,const float* at,const float* heading,float turn,PassLine& out) noexcept {
    const float l=std::sqrt(heading[0]*heading[0]+heading[2]*heading[2]);
    if(!area.ground || l<1e-4f || !std::isfinite(at[0]+at[1]+at[2]))return false;
    const float ux=heading[0]/l,uz=heading[2]/l;
    const float hi=PassEnd(area,reach,at,ux,uz,turn),lo=PassEnd(area,reach,at,-ux,-uz,turn);
    if(hi<0.0f || lo<0.0f)return false;
    out=PassLine{{at[0],at[1],at[2]},{ux,0.0f,uz},-lo,hi};
    return true;
}

// Ground support must prove a complete route with the bounded planner (ground_navigation.h: +-256 cells round its
// origin, kNodes nodes) before anything is created. A stock map's edges stand 1100-1700 m from a central target, past
// that bound: every edge search ran out of nodes after ~1 min of planning and the request was refused (2026-10-09:
// nothing came, online or not). So the entries are candidates on rings round the target and where each bearing meets the
// measured area's wall within the rings' reach (the field's edge), inside the area (a vehicle needs ground), kMinJourney
// from the target and kObserverClear from the caller at least. What the caller cannot see comes first (2026-10-10: a
// vehicle was made 800 m from the caller in plain sight): past its draw distance, then hidden from it (`hidden(p)`: the
// map between its eye and the vehicle's top, the caller's map ray), and only as the last resort a visible one.
constexpr float kGroundRings[]={650.0f,800.0f,950.0f};
constexpr int kGroundRingCount=static_cast<int>(sizeof(kGroundRings)/sizeof(kGroundRings[0]));
constexpr int kGroundBearings=8,kGroundEntryMost=(kGroundRingCount+1)*kGroundBearings;
constexpr float kGroundPlanCell=4.0f;   // the support route profiles' cell (support_dispatch.cpp)
constexpr float kGroundEdgeApart=50.0f; // m: an edge point this little past a ring's is that one
static_assert(kGroundRings[kGroundRingCount-1]+kEntryInset<256*kGroundPlanCell,"every ring entry within the planner's square");
static_assert(kGroundRings[0]>=kMinJourney,"a visible journey from every entry");
enum class Seen : unsigned char { beyond, hidden, visible };
struct GroundEntries { float at[kGroundEntryMost][2]{}; Seen seen[kGroundEntryMost]{}; int count=0; };
// Valid candidates: those the caller cannot see first (Seen's order), then the nearest ring (the edge points after the
// rings), within one the farthest from the caller first. `draw`: the caller's draw distance (Reach::draw).
template<class Hidden>
GroundEntries GroundEntryCandidates(const PlayArea& area,const float* target,const float* observer,float draw,Hidden hidden) noexcept {
    GroundEntries out;
    if(!area.ground || !std::isfinite(target[0]+target[2]))return out;
    int ring[kGroundEntryMost]{};float away[kGroundEntryMost]{};
    const float lo[2]={area.lo[0]+kEntryInset,area.lo[1]+kEntryInset},hi[2]={area.hi[0]-kEntryInset,area.hi[1]-kEntryInset};
    const auto add=[&](float x,float z,int r) noexcept {
        const float p[3]={x,target[1],z};
        if(x<lo[0] || x>hi[0] || z<lo[1] || z>hi[1] || FlatDistance(p,target)<kMinJourney-0.5f)return;
        const float d=observer ? FlatDistance(p,observer) : 1e9f;
        if(d<kObserverClear)return;
        const Seen seen=d>=draw+kDrawClear ? Seen::beyond : hidden(p) ? Seen::hidden : Seen::visible;
        out.at[out.count][0]=x;out.at[out.count][1]=z;out.seen[out.count]=seen;ring[out.count]=r;away[out.count]=d;++out.count;
    };
    for(int b=0;b<kGroundBearings;++b) {
        const float a=6.28318531f*static_cast<float>(b)/kGroundBearings,ux=std::cos(a),uz=std::sin(a);
        for(int r=0;r<kGroundRingCount;++r)add(target[0]+ux*kGroundRings[r],target[2]+uz*kGroundRings[r],r);
        // The field's edge on this bearing, when within the rings' reach and not one of them (a ring within it, nearer
        // than kGroundEdgeApart: the rings past it are off the field).
        const float t=ExitAlong(lo,hi,target[0],target[2],ux,uz);
        bool ringed=t>kGroundRings[kGroundRingCount-1];
        for(int r=0;r<kGroundRingCount && !ringed;++r)ringed=kGroundRings[r]<=t && t-kGroundRings[r]<kGroundEdgeApart;
        if(!ringed)add(target[0]+ux*t,target[2]+uz*t,kGroundRingCount);
    }
    // Stable insertion sort: (seen, ring, farther from the caller first).
    const auto before=[&](int i,int k) noexcept {
        if(out.seen[i]!=out.seen[k])return out.seen[i]<out.seen[k];
        if(ring[i]!=ring[k])return ring[i]<ring[k];
        return away[i]>away[k];
    };
    for(int i=1;i<out.count;++i)for(int j=i;j>0 && before(j,j-1);--j) {
        for(int k=0;k<2;++k){const float t=out.at[j][k];out.at[j][k]=out.at[j-1][k];out.at[j-1][k]=t;}
        const Seen s=out.seen[j];out.seen[j]=out.seen[j-1];out.seen[j-1]=s;
        const int r=ring[j];ring[j]=ring[j-1];ring[j-1]=r;
        const float w=away[j];away[j]=away[j-1];away[j-1]=w;
    }
    return out;
}

// Air support flies in from the edge (2026-10-09): created in the air at the entry, at the route's height. Several
// aircraft of one call fly line abreast there: slot 0 the lead at the entry, then alternately left / right of it,
// kFormationSide apart across the heading, level with it (every slot inside the physical square: airstrike.cpp
// PlanAirSupport checks). `spacing` scales the interval (helicopters fly closer).
constexpr float kFormationSide=70.0f;
inline void AirFormationSlot(const Route& route,int slot,float spacing,float* out) noexcept {
    const int pair=(slot+1)/2;
    const float side=slot==0 ? 0.0f : (slot%2 ? 1.0f : -1.0f)*static_cast<float>(pair)*kFormationSide*spacing;
    const float hx=route.heading[0],hz=route.heading[2];
    out[0]=route.from[0]+hz*side;out[1]=route.from[1];out[2]=route.from[2]-hx*side;
}
// A takeoff point instead of the edge (2026-10-10, the user: 「能从机场起飞就从机场起飞吧，为什么要从外面来」): an aircraft
// that has somewhere near to start from (the sea rescue: a submarine carrier's deck; no stock map has an airfield or a
// helipad the plugin has identified) is made there, kTakeoffLift over the spot, its crew seated at once, and climbs out.
// `spots`: the candidates; the one nearest the target (flat) within kTakeoffReach, inside the measured area, whose climb
// column (kTakeoffClimb straight up) and level corridor from the top of it to over the target are clear, wins. None:
// noEntry, and the caller falls back to the edge (AirRoute). route.heading: level, towards the target.
constexpr float kTakeoffLift=2.0f,kTakeoffClimb=30.0f,kTakeoffReach=4000.0f;
template<class Clear>
Refusal TakeoffRoute(const PlayArea& area,const float* target,const float (*spots)[3],int count,Clear clear,Route& route) noexcept {
    if(!area.ground || !std::isfinite(target[0]+target[1]+target[2]))return Refusal::noArea;
    bool found=false;float best=0.0f;
    for(int i=0;spots && i<count;++i) {
        const float* s=spots[i];
        if(!std::isfinite(s[0]+s[1]+s[2]) || s[0]<area.lo[0] || s[0]>area.hi[0] || s[2]<area.lo[1] || s[2]>area.hi[1])continue;
        const float away=FlatDistance(s,target);
        if(away>kTakeoffReach || (found && away>=best))continue;
        const float from[3]={s[0],s[1]+kTakeoffLift,s[2]},top[3]={s[0],from[1]+kTakeoffClimb,s[2]};
        const float over[3]={target[0],top[1],target[2]};
        if(!clear(from,top) || (away>=1.0f && !clear(top,over)))continue;
        Route candidate{{from[0],from[1],from[2]},{0.0f,0.0f,1.0f}};
        if(away>=1.0f){candidate.heading[0]=(target[0]-s[0])/away;candidate.heading[2]=(target[2]-s[2])/away;}
        route=candidate;best=away;found=true;
    }
    return found ? Refusal::none : Refusal::noEntry;
}
} // namespace crew::support
