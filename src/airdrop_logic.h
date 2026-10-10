// The stock container airdrop as data (docs/airdrop-vehicle-re.md), and the plugin carrier's decisions. Pure: no game
// memory is touched here (tools/airdrop_check.cpp runs it offline). EDF.dll TimeDateStamp 0x678CCB46.
//
// The stock chain: an Air Raider's vehicle request -> Transporter508's init (0x5E5070) makes a Transporter_Container
// (CreateObject with ContainerInitParam), configures it (0x5E8B40: the vehicle's SGO, a VehicleSetup, the level), holds it
// by writing its world matrix every frame, and lets it go (0x5E84D0): it falls by its own physics and, settled, makes the
// vehicle (0x5E8C00, CreateObject 0x5E8FDC), team 5, and deletes itself. The plugin's transport aircraft is the carrier here:
// it makes, configures, holds and lets go a stock container the same way. One container is one vehicle. The carrier is the
// plugin's transport helicopter (EDF6VC_HELI_TRANSPORT): it hovers over the point as the stock transporter does; the
// transport plane, on a ~600 m turn at its slowest, flew round the point 200-500 m off it in the tests (2026-10-10).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace airdrop {
// A derived network identity (0x776790(&id, owner's network object, counter): id, counter, 0, type 5, ...). A carrier
// with no network object gets all zeros (0x7767AF: the owner null skips the fill), the plugin's aircraft among them.
struct NetId { std::uint32_t id,counter,zero,type,extra,pad; std::uint64_t tail; };
static_assert(sizeof(NetId)==0x20,"0x776790 fills 0x20 bytes");

// Transporter_Container::InitParam as Transporter508's init builds it on its stack (0x5E51F1..0x5E5245); captured at
// run time with the stock call (tests/autopilot probe, docs/evidence/airdrop-probe-2026-10-10.txt: vtable EDF+0x17D7818,
// the base zero, the derived id, the counter).
struct alignas(16) ContainerInitParam {
    const void* vtable;           // +0x00 Transporter_Container::InitParam
    unsigned char base[0x28];     // +0x08 InitParamBase@SceneObject's fields: zero
    NetId derived;                // +0x30 the container's network identity, derived from its carrier's
    std::uint32_t counter;        // +0x50 the carrier's derived-id counter (Transporter508 +0x778; unset 0xFFFFFFFF)
    std::uint32_t pad;            // +0x54 (the stock param ends at +0x58)
    unsigned char tail[8];        // to the 16-byte alignment: never read
};
static_assert(sizeof(ContainerInitParam)==0x60,"the stock 0x58 bytes, aligned");
static_assert(offsetof(ContainerInitParam,derived)==0x30 && offsetof(ContainerInitParam,counter)==0x50,"0x5E5226 / 0x5E5245");
constexpr std::uint32_t kNoCounter=0xFFFFFFFFu;   // what the probe read in an offline stock call (transporter +0x778)

// A vehicle's setup as the container keeps it (+0xC38, copied there by 0x5E44F0): its text form (an MSVC std::wstring)
// and an SGO value (a variant: 16 bytes, then its type, 0xFFFF empty). The unload (0x5E9054..0x5E90A8) parses the text
// when it has one (0x62D890), else hands the value to the vehicle's slot 46 (+0x170), else neither. The stock request
// hands a value of type 2 (a reference into its weapon's SGO: Ammo_CustomParameter[4][3]) and no text (probe).
struct VehicleSetup {
    wchar_t text[8];              // +0x00 the string's own buffer (capacity 7 and under)
    std::uint64_t textSize;       // +0x10
    std::uint64_t textCapacity;   // +0x18
    unsigned char value[0x10];    // +0x20 the variant's storage
    std::uint16_t valueType;      // +0x30
    unsigned char pad[6];
};
static_assert(sizeof(VehicleSetup)==0x38 && offsetof(VehicleSetup,value)==0x20 && offsetof(VehicleSetup,valueType)==0x30,
              "0x5E54EA..0x5E5505 / container +0xC48, +0xC58, +0xC68");
constexpr std::uint16_t kNoValue=0xFFFF;
// No text, no value: the container leaves the vehicle's setup to whoever made the request (the plugin applies the
// vehicle's own mission setup when it is made: support_spawn.cpp ApplySupportVehicleSetup).
constexpr VehicleSetup EmptySetup() noexcept {
    VehicleSetup s{};
    s.textCapacity=7;s.valueType=kNoValue;
    return s;
}

// A std::shared_ptr's two words as the configure takes its requester (0x5E8B54: the control block at +8, the object at
// +0): both null for a call with no requester.
struct SharedRef { const void* object; const void* control; };
static_assert(sizeof(SharedRef)==16,"a shared_ptr");

// Where the carried container hangs: kBellyDrop under the carrier's origin, level, facing the carrier's heading (the stock
// transporter copies a locator of its model; the plugin's transport helicopter has none for it). `carrier`
// and `out`: 4x4 row-major, rows right / up / forward / position (the game's).
constexpr float kBellyDrop=8.0f;
inline void CarryMatrix(const float* carrier,float* out) noexcept {
    float x=carrier[8],z=carrier[10];
    float len=std::sqrt(x*x+z*z);
    if(!(len>1e-4f)){x=0.0f;z=1.0f;len=1.0f;}
    x/=len;z/=len;
    const float m[16]={z,0,-x,0, 0,1,0,0, x,0,z,0, carrier[12],carrier[13]-kBellyDrop,carrier[14],1};
    for(int i=0;i<16;++i)out[i]=m[i];
}

// When the carrier lets go (the container falls straight down from where it is let go: it has no physics until then,
// 0x5E8C00's entry makes its body from its matrix alone): over the point (`dist` the level distance to it, m; the
// transport helicopter hovers there: heli.cpp HeliFerry), or at the closest it came on a pass that went near
// (`closest`), once it is moving away again (a carrier that overflies its hover point).
// Or where it has come to hover, the nearest it can get (`sinceCloser`: ms since it last came kCloser m nearer): the
// helicopter keeps its soft edge's band (heli.cpp), so a point by the map's edge is hovered short of (2026-10-10: 145 m
// short, RM015's player by its south edge); the container then lands as near as its carrier may go.
constexpr float kOverPoint=25.0f,kPassNear=90.0f,kPassAway=8.0f,kCloser=1.0f;
constexpr std::uint64_t kHoverSettleMs=5000;
inline bool ReleaseNow(float dist,float closest,std::uint64_t sinceCloser) noexcept {
    return dist<=kOverPoint || (closest<=kPassNear && dist>=closest+kPassAway) || sinceCloser>=kHoverSettleMs;
}
// The carrier's time to reach the point before it lets go wherever it is (a carrier that cannot get there must not keep
// the container), and the container's to make its vehicle once let go (the stock one waits to settle: seconds).
constexpr std::uint64_t kCarryMs=240000,kFallMs=60000;
}  // namespace airdrop
