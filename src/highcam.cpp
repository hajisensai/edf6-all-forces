// The artillery's high camera (README 喀秋莎 / 自行榴弹炮 "高视角"; docs/camera-re.md): while the player drives an
// indirect-fire vehicle (the Katyusha, the self-propelled howitzer), a key (ini HighCamKey, 'C') or pad button (ini
// HighCamButton, R3) switches between the vehicle's own camera and a high one looking down over where the rounds land.
//  - How. The player's camera (CharacterGhostCamera, vtable 0x1768C10) copies its target object's camera block every
//    frame (slot 4 0xF86A0: obj+0x170..+0x21F -> camera+0x3B0.., 0xF875C; obj+0x230.. instead while byte obj+0x224 is
//    set) and its third-person state (0xFAF20) puts the eye at frame x obj+0x180 and looks from there at frame x
//    obj+0x170, both points in the target's frame (its world matrix obj+0x60), the eye eased toward that point by
//    obj+0x190 a frame. game_object_camera_setting fills that block (0x54DDF0: [0] -> +0x170, [1] -> +0x180, [3] ->
//    +0x190) when the object is made. So the plugin writes the block's two points (look-at, eye) of the vehicle it
//    rides: the game keeps its own easing (the switch glides over ~0.5 s), its collision (0xF6590 from the frame's
//    origin + 1 m up to the look-at point, then out to the eye) and the aim through the screen's centre (CameraRay:
//    launcher.cpp's elevation solve and the CCIP use the camera as drawn).
//  - The stock points are kept when the high view is first written and written back when it is switched off, when
//    the player gets out (the next frame of that vehicle sees no player in seat 0), when the plugin or the feature is
//    switched off in the ini, and before another vehicle takes it over. A new mission forgets them (its vehicles are new).
//  - The high view: the eye HighCamHeight m up and HighCamBack m behind the vehicle's origin (its frame), looking down
//    HighCamPitch deg ahead; the look-at point is on that line where it comes down to the stock look-at's height (over
//    the vehicle's front, clear of the ground, so the game's collision ray to it stays free).
// Not verified in the game (static RE only): that the riding camera's target is the vehicle (and not the rider, whose
// state 2 0xFB9F0 uses seat cameras), and which frame turns the camera with the aim. Debug=1 logs HIGHCAM lines once
// a second with the block and the drawn camera's eye in the vehicle's frame, to be compared with the eye written.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "edf/weapon.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
// The object's camera block (0x54DDF0 writes it, 0xF86A0 copies it): look-at point, eye point (both xyzw in the
// object's frame), the eye's easing a frame; diagnostics: the alternate block's switch, the angle frame's switch and
// angles, the zoom, the blend toward the block's matrix.
constexpr std::size_t kCamLook=0x170,kCamEye=0x180,kCamEase=0x190,kCamAngleOn=0x1B0,kCamAngles=0x1C0,kCamZoom=0x1D0,
                      kCamBlend=0x1D4,kCamAltOn=0x224;
// The seat's input (docs/stores-re.md §4): 1 = a pad (0 keyboard and mouse), the pad's button bits.
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8;
// An indirect-fire weapon: marked lofted (the Katyusha's rockets) or ground (the howitzer's shells) and its rounds
// living at least this many frames (the howitzer 1200, the Katyusha 1500; the stock-class guns that share the ground
// mark, the Bohr's grenades 100 and EDF6AutoTurret's flak, live under 2 s).
constexpr std::int32_t kIndirectLife=600;
constexpr ULONGLONG kLogMs=1000,kCueMs=200;
constexpr float kPi=3.14159265f;

struct HighCam {
    ObjRef ref;                 // the vehicle whose block holds the high view (or held it last)
    unsigned char* v;
    bool applied;               // its block is the plugin's now; `stock` holds the vehicle's own
    float stockLook[4],stockEye[4];
    bool want;                  // the player's choice, kept from vehicle to vehicle in the mission
    bool held;                  // the key / button as last read (a press is the edge)
    ULONGLONG logAt;
};
HighCam cam{};

struct Cue { bool on,keys; ULONGLONG at; };
Cue cue{};
SRWLOCK cueLock=SRWLOCK_INIT;

bool KeyHeld(int vk) noexcept {
    if(vk<=0)return false;
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

// Whether the seat holds an indirect-fire weapon (kIndirectLife).
bool IndirectFire(const unsigned char* seat) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>8 || !Readable(holders,count*8))return false;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,edf::kWeaponAccuracyScale+4))continue;
        const std::int32_t mark=At<std::int32_t>(w,edf::kWeaponMark);
        if((mark==edf::kMarkLofted || mark==edf::kMarkGround) && At<std::int32_t>(w,edf::kWeaponAmmoAlive)>=kIndirectLife)return true;
    }
    return false;
}

// The high view's eye and look-at in the vehicle's frame (xyz; w is left as the block has it).
void HighPoints(const float* stockLook,float* look,float* eye) noexcept {
    const Config& c=Cfg();
    const float p=c.highCamPitch*kPi/180.0f,down=std::sin(p),ahead=std::cos(p);
    eye[0]=0.0f;eye[1]=c.highCamHeight;eye[2]=-c.highCamBack;
    const float drop=c.highCamHeight-stockLook[1];
    const float along=drop>1.0f ? drop/down : 1.0f;
    look[0]=0.0f;look[1]=eye[1]-down*along;look[2]=eye[2]+ahead*along;
}

void Apply(unsigned char* v) noexcept {
    float* const look=reinterpret_cast<float*>(v+kCamLook);
    float* const eye=reinterpret_cast<float*>(v+kCamEye);
    if(!cam.applied) {
        std::memcpy(cam.stockLook,look,16);std::memcpy(cam.stockEye,eye,16);
        cam.applied=true;
    }
    float l[3],e[3];
    HighPoints(cam.stockLook,l,e);
    std::memcpy(look,l,12);std::memcpy(eye,e,12);
}

void Restore(const char* why) noexcept {
    if(cam.applied) {
        std::memcpy(cam.v+kCamLook,cam.stockLook,16);std::memcpy(cam.v+kCamEye,cam.stockEye,16);
        Log("HIGHCAM v=%p off (%s): stock look (%.1f,%.1f,%.1f) eye (%.1f,%.1f,%.1f) back",cam.v,why,cam.stockLook[0],cam.stockLook[1],
            cam.stockLook[2],cam.stockEye[0],cam.stockEye[1],cam.stockEye[2]);
    }
    cam.applied=false;
}

// Once a second with Debug: the block as it is, and the eye of the camera drawn (CameraRay) in the vehicle's frame,
// with how far that is from the block's eye (0 when the camera puts the eye there; the collision pulls it in, the
// easing lags it, a turn with the aim moves it round).
void DebugLog(const unsigned char* v,const char* mode) noexcept {
    const ULONGLONG now=GetTickCount64();
    if(!Cfg().debug || now-cam.logAt<kLogMs)return;
    cam.logAt=now;
    const float* look=reinterpret_cast<const float*>(v+kCamLook);
    const float* eye=reinterpret_cast<const float*>(v+kCamEye);
    const float* ang=reinterpret_cast<const float*>(v+kCamAngles);
    Log("HIGHCAM v=%p %s: look (%.1f,%.1f,%.1f,%.1f) eye (%.1f,%.1f,%.1f,%.1f) ease %.2f alt %d angleFrame %d (%.2f,%.2f) zoom %.2f blend %.2f",v,mode,
        look[0],look[1],look[2],look[3],eye[0],eye[1],eye[2],eye[3],At<float>(v,kCamEase),At<unsigned char>(v,kCamAltOn),
        At<unsigned char>(v,kCamAngleOn),ang[0],ang[1],At<float>(v,kCamZoom),At<float>(v,kCamBlend));
    float at[3],dir[3];
    if(!CameraRay(at,dir))return;
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float d[3]={at[0]-m[12],at[1]-m[13],at[2]-m[14]};
    float local[3],ld[3];
    for(int r=0;r<3;++r) {
        local[r]=d[0]*m[r*4]+d[1]*m[r*4+1]+d[2]*m[r*4+2];
        ld[r]=dir[0]*m[r*4]+dir[1]*m[r*4+1]+dir[2]*m[r*4+2];
    }
    const float off=std::sqrt((local[0]-eye[0])*(local[0]-eye[0])+(local[1]-eye[1])*(local[1]-eye[1])+(local[2]-eye[2])*(local[2]-eye[2]));
    const float pitch=std::asin(dir[1]<-1.0f ? -1.0f : dir[1]>1.0f ? 1.0f : dir[1])*180.0f/kPi;
    Log("HIGHCAM v=%p %s: camera eye in the vehicle's frame (%.1f,%.1f,%.1f), %.1f m from the block's eye; look (%.2f,%.2f,%.2f) pitch %.1f deg",
        v,mode,local[0],local[1],local[2],off,ld[0],ld[1],ld[2],pitch);
}

void Publish(bool on,bool keys) noexcept {
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{on,keys,GetTickCount64()};
    ReleaseSRWLockExclusive(&cueLock);
}

// The key or button pressed this frame (its edge).
bool Pressed(const unsigned char* seat,bool keys) noexcept {
    const Config& c=Cfg();
    const bool down=keys ? KeyHeld(c.highCamKey) : (At<std::uint16_t>(seat,kSeatButtons)&static_cast<std::uint16_t>(c.highCamButton))!=0;
    const bool press=down && !cam.held;
    cam.held=down;
    return press;
}
}  // namespace

void HighCamFrame(unsigned char* v) noexcept {
    const Config& c=Cfg();
    const bool mine=cam.applied && cam.v==v;
    if(mine && (!c.enabled || !c.highCam)){Restore(c.enabled ? "HighCam=0" : "plugin off");return;}
    if(!c.enabled || !c.highCam || SeatCount(v)==0)return;
    const unsigned char* seat=SeatAt(v,0);
    const bool driven=!v[kDead] && SeatRider(seat)==Rider::player && IndirectFire(seat);
    if(!driven) {
        if(mine)Restore(v[kDead] ? "the vehicle is wrecked" : "the player got out");
        return;
    }
    if(!cam.ref.Is(v)) {   // a vehicle the player has just taken: the last one's block given back first
        if(cam.applied && cam.ref.Is(cam.v))Restore("another vehicle");
        cam.applied=false;cam.ref=ObjRef::Of(v);cam.v=v;cam.held=true;   // a key held while boarding is no press
        Log("HIGHCAM v=%p: an indirect-fire vehicle with the player at seat 0; high view %s (key 0x%X, pad button 0x%X)",v,
            cam.want ? "on" : "off",c.highCamKey,c.highCamButton);
    }
    const bool keys=At<unsigned char>(seat,kSeatPad)==0;
    if(Pressed(seat,keys)) {
        cam.want=!cam.want;
        Log("HIGHCAM v=%p %s by the %s",v,cam.want ? "on" : "off",keys ? "key" : "pad button");
    }
    if(cam.want) {
        const bool was=cam.applied;
        Apply(v);
        if(!was) {
            const float* l=reinterpret_cast<const float*>(v+kCamLook);
            const float* e=reinterpret_cast<const float*>(v+kCamEye);
            Log("HIGHCAM v=%p on: stock look (%.1f,%.1f,%.1f) eye (%.1f,%.1f,%.1f) -> look (%.1f,%.1f,%.1f) eye (%.1f,%.1f,%.1f)",v,
                cam.stockLook[0],cam.stockLook[1],cam.stockLook[2],cam.stockEye[0],cam.stockEye[1],cam.stockEye[2],l[0],l[1],l[2],e[0],e[1],e[2]);
        }
    } else if(cam.applied) {
        Restore("key");
    }
    DebugLog(v,cam.applied ? "high" : "stock");
    Publish(cam.applied,keys);
}

bool PlayerHighCam(bool* on,bool* keys) noexcept {
    AcquireSRWLockShared(&cueLock);
    const Cue c=cue;
    ReleaseSRWLockShared(&cueLock);
    if(!c.at || GetTickCount64()-c.at>kCueMs)return false;
    *on=c.on;*keys=c.keys;
    return true;
}

void ResetHighCam() noexcept {
    cam=HighCam{};
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{};
    ReleaseSRWLockExclusive(&cueLock);
}
}  // namespace crew
