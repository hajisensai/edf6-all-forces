// The map's real play area (playarea.h): measured once a mission off the map's ground with map rays.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "playarea.h"
#include "crew.h"
#include "heli.h"
#include "memory.h"

namespace crew {
namespace {
// The move area manager (docs/map-edge-re.md §1): *(image+0x20B2998) - 8, its box's corners at +0x10 / +0x20. Its centre
// is the map's (bigworld.cpp WidenMoveArea keeps it): the lanes start there.
constexpr std::size_t kMoveArea=0x20B2998,kMoveMin=0x10,kMoveMax=0x20;
// ms into the mission (as bigworld.cpp's probe): the map's pieces are in by then.
constexpr ULONGLONG kMeasureAfterMs=5000;
// A ray straight down from kRayTop to kRayBottom (the probe's: the highest ground of a map is well under it).
constexpr float kRayTop=3000.0f,kRayBottom=-3000.0f;

PlayArea walls{};
bool areaSet=false,measured=false;
int side=0;                       // the next side measured: 0 +x, 1 -x, 2 +z, 3 -z
float centre[2]={0.0f,0.0f};
float edges[4]={-1.0f,-1.0f,-1.0f,-1.0f};
ULONGLONG measureFrom=0;
float lowest=0.0f;                // the lowest ground a lane's ray found (PlayArea::floor)
bool anyGround=false;

PlayArea Square() noexcept {
    const float a=PlayEdge();
    return PlayArea{{-a,-a},{a,a},false};
}

bool GroundAt(float x,float z) noexcept {
    const float a[3]={x,kRayTop,z},b[3]={x,kRayBottom,z};
    float hit[3];
    if(MapRay(a,b,hit)<0.0f)return false;
    if(!anyGround || hit[1]<lowest){lowest=hit[1];anyGround=true;}
    return true;
}

void ReadCentre() noexcept {
    centre[0]=centre[1]=0.0f;
    const auto p=At<unsigned char*>(image,kMoveArea);
    unsigned char* const m=p ? p-8 : nullptr;
    if(!m || !Readable(m+kMoveMin,kMoveMax+16-kMoveMin))return;
    const float* lo=reinterpret_cast<const float*>(m+kMoveMin);
    const float* hi=reinterpret_cast<const float*>(m+kMoveMax);
    const float x=0.5f*(lo[0]+hi[0]),z=0.5f*(lo[2]+hi[2]);
    if(std::isfinite(x) && std::isfinite(z) && std::fabs(x)<WorldHalf() && std::fabs(z)<WorldHalf()){centre[0]=x;centre[1]=z;}
}

// Side `s`'s ground edge, m out from the centre: the median of its lanes (playarea.h).
float MeasureSide(int s) noexcept {
    const int k=s/2;                       // 0: along x, 1: along z
    const float out=s%2 ? -1.0f : 1.0f;
    float lanes[area::kLanes];
    for(int l=0;l<area::kLanes;++l) {
        const float across=centre[1-k]+static_cast<float>(l-area::kLanes/2)*area::kLaneGap;
        lanes[l]=area::LaneEdge([&](float d) noexcept {
            const float along=centre[k]+out*d;
            return k==0 ? GroundAt(along,across) : GroundAt(across,along);
        },WorldHalf());
    }
    return area::MedianEdge(lanes);
}
}  // namespace

PlayArea MapPlayArea() noexcept {
    return areaSet ? walls : Square();
}

void PlayAreaTick() noexcept {
    if(measured)return;
    const ULONGLONG ms=GameMs();
    if(!measureFrom){measureFrom=ms;return;}
    if(ms-measureFrom<kMeasureAfterMs)return;
    if(side==0)ReadCentre();
    edges[side]=MeasureSide(side);
    if(++side<4)return;
    measured=true;
    walls=area::Combine(centre,edges,PlayEdge());
    walls.floor=lowest;walls.hasFloor=anyGround && walls.ground;
    areaSet=true;
    Log("AREA the map's ground from its centre (%.0f,%.0f): +x %.0f -x %.0f +z %.0f -z %.0f m (-1: none); the walls x %.0f..%.0f, "
        "z %.0f..%.0f (%s, %.0f m inside its edge; the physics square +-%.0f)",centre[0],centre[1],edges[0],edges[1],edges[2],edges[3],
        walls.lo[0],walls.hi[0],walls.lo[1],walls.hi[1],walls.ground ? "on the ground's edge" : "no ground found: the physics square",
        area::kVoidMargin,PlayEdge());
    if(walls.hasFloor)Log("AREA a void within the walls (the big map's seams) is floored at the lowest ground found, %.0f m",walls.floor);
}

void ResetPlayArea() noexcept {
    areaSet=measured=false;side=0;measureFrom=0;
    for(float& e:edges)e=-1.0f;
    lowest=0.0f;anyGround=false;
}
}  // namespace crew
