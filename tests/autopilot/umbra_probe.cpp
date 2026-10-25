// The Umbra input probe (test only, part of EDF6Autopilot): asked by the autopilot command "probe umbra". Written for the
// grey map of EDF4.1's first mission (2026-10-25, docs/feedback-2026-10-25-greymap.md), which it traced to the V301's gun.
//
// EDF.dll calls umbra_sandlot.dll through its import table (all EDF.dll TimeDateStamp 0x678CCB46):
//   0x17567D0 Object::setObjectToCellMatrix(const Matrix4x4&)  (its one caller 0x11B32F3, inside 0x11B30C0)
//   0x1756950 OBBModel::create(const Matrix4x4&)               (0x11E07E5)
//   0x1756898 Camera::setCameraToCellMatrix(const Matrix4x4&)  (0x11D4532, 0x11D46E4)
// Each entry is swapped for a wrapper that checks the 16 floats (not finite, or past kFar) and calls on. Rows:
//   UMBRA BAD     a bad matrix, its call path (return addresses as EDF.dll RVAs) and the owner found on the stack (class,
//                 weapons, the bones whose matrix is bad); before it, UMBRA BEFORE: that object's last 16 placements;
//   UMBRA TANK    four times a second, every Vehicle601_Tank's seat-0 aim axes {min, max, angle, rate}, its gun bone's
//                 matrix and its rotation about x ("phys x": the barrel's pitch as drawn; it must follow the aim's);
//   UMBRA calls   every 10 s, the counts.
// Nothing about the game's own run changes.
#include "umbra_probe.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace autopilot {
namespace {
constexpr std::size_t kObjectMatrix=0x17567D0,kObbCreate=0x1756950,kCameraMatrix=0x1756898;
constexpr float kFar=1.0e6f;              // no map reaches a million units
constexpr std::size_t kMaxRows=2000;

using ObjectMatrixFn=void(__fastcall*)(void*,const float*);
using ObbCreateFn=void*(__fastcall*)(const float*);

unsigned char* image=nullptr;
unsigned char* imageEnd=nullptr;
LogFn say=nullptr;
ObjectMatrixFn realObject=nullptr,realCamera=nullptr;
ObbCreateFn realObb=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
std::vector<std::string> pending;
std::size_t rows=0;
volatile LONG64 calls[3]{},bad[3]{};
ULONGLONG lastCount=0;

bool Bad(const float* m) noexcept {
    for(int i=0;i<16;++i)if(!std::isfinite(m[i]) || std::fabs(m[i])>kFar)return true;
    return false;
}

// The object a bad placement belongs to: the stack above the call holds the vehicle / soldier whose model node
// (vtable at +0, the Umbra object at +0x10) this is, at one of the owners' node offsets (a vehicle's +0xE40, a soldier's
// +0x860 / +0x8A0, ...); else the node itself. Its class from the RTTI next to its vtable, and the model's bones whose
// world matrix is not finite or out of reach (node+0xA0: records +0x10, 0x110 each, world at +0xB0; count +0x20).
const char* ClassOf(const unsigned char* q) noexcept {
    const auto vt=*reinterpret_cast<unsigned char* const*>(q);
    if(vt<image || vt>=imageEnd)return "?";
    const auto col=*reinterpret_cast<unsigned char* const*>(vt-8);
    if(col<image || col>=imageEnd)return "?";
    return reinterpret_cast<const char*>(image+*reinterpret_cast<const std::uint32_t*>(col+0xC)+0x10);
}

bool Describe(const unsigned char* q,std::size_t node,char* out,std::size_t cap) noexcept {
    __try {
        if(*reinterpret_cast<void* const*>(q+node+0x10)==nullptr)return false;
        const float* pos=reinterpret_cast<const float*>(q+0x90);
        int at=std::snprintf(out,cap," owner %p+%zX vt %zX %s pos %g %g %g",q,node,
                             static_cast<std::size_t>(*reinterpret_cast<unsigned char* const*>(q)-image),ClassOf(q),pos[0],pos[1],pos[2]);
        // The vehicle's weapon holders (veh+0x638, count +0x648, 0x48 each, weapon +0x10): shots fired +0xBD0, fire
        // state +0xBD4, rounds +0xBE8.
        if(node==0xE40) {
            const auto holders=*reinterpret_cast<const unsigned char* const*>(q+0x638);
            const std::uint64_t count=*reinterpret_cast<const std::uint64_t*>(q+0x648);
            for(std::uint64_t h=0;holders && h<count && h<4;++h) {
                const auto w=*reinterpret_cast<const unsigned char* const*>(holders+h*0x48+0x10);
                if(w && at>0 && static_cast<std::size_t>(at)<cap)
                    at+=std::snprintf(out+at,cap-static_cast<std::size_t>(at)," weapon%llu shots %d state %d rounds %d",
                                      static_cast<unsigned long long>(h),*reinterpret_cast<const int*>(w+0xBD0),
                                      *reinterpret_cast<const int*>(w+0xBD4),*reinterpret_cast<const int*>(w+0xBE8));
            }
        }
        const unsigned char* inst=q+node+0xA0;
        const auto recs=*reinterpret_cast<const unsigned char* const*>(inst+0x10);
        const std::uint32_t n=*reinterpret_cast<const std::uint32_t*>(inst+0x20);
        int shown=0;
        for(std::uint32_t b=0;recs && b<n && b<512 && shown<4;++b) {
            const float* w=reinterpret_cast<const float*>(recs+static_cast<std::size_t>(b)*0x110+0xB0);
            if(!Bad(w))continue;
            ++shown;
            if(at>0 && static_cast<std::size_t>(at)<cap)
                at+=std::snprintf(out+at,cap-static_cast<std::size_t>(at)," bone %u/%u [%g %g %g | %g %g %g]",b,n,w[0],w[5],w[10],w[12],w[13],w[14]);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

const unsigned char* Holding(const std::uintptr_t* sp,const void* who,std::size_t node) noexcept {
    for(int i=0;i<0x200;++i) {
        __try {
            const auto q=reinterpret_cast<const unsigned char*>(sp[i]);
            if(reinterpret_cast<std::uintptr_t>(q)<0x10000)continue;
            if(*reinterpret_cast<void* const*>(q+node+0x10)==who)return q;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return nullptr;
}

void Owner(const void* who,char* out,std::size_t cap) noexcept {
    out[0]='\0';
    if(!who)return;
    CONTEXT c{};
    c.ContextFlags=CONTEXT_CONTROL;
    RtlCaptureContext(&c);
    const auto* sp=reinterpret_cast<const std::uintptr_t*>(c.Rsp);
    constexpr std::size_t kNodes[]={0xE40,0x860,0x8A0,0x1160,0};
    for(const std::size_t node:kNodes)
        if(const unsigned char* q=Holding(sp,who,node)) { Describe(q,node,out,cap); return; }
}

void Keep(int kind,const void* who,const float* m) noexcept {
    InterlockedIncrement64(&bad[kind]);
    char owner[400];
    Owner(who,owner,sizeof(owner));
    void* frames[8]{};
    const USHORT n=RtlCaptureStackBackTrace(1,8,frames,nullptr);
    char path[160]{};
    std::size_t at=0;
    for(USHORT i=0;i<n && at+12<sizeof(path);++i) {
        const auto p=static_cast<unsigned char*>(frames[i]);
        at+=static_cast<std::size_t>(std::snprintf(path+at,sizeof(path)-at,p>=image && p<imageEnd ? " %zX" : " ?",
                                                   static_cast<std::size_t>(p-image)));
    }
    char line[900];
    std::snprintf(line,sizeof(line),
        "UMBRA BAD %s %p tick=%llu path%s%s m=[%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g]",
        kind==0 ? "object" : kind==1 ? "obbmodel" : "camera",who,GetTickCount64(),path,owner,
        m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]);
    AcquireSRWLockExclusive(&lock);
    if(rows<kMaxRows) { pending.emplace_back(line); ++rows; }
    ReleaseSRWLockExclusive(&lock);
}

// Each object's last placements (its box: the three axes' lengths and the centre), written out with a bad one. For a
// Vehicle601_Tank's model (found once per object: the call came through the vehicle's model update 0x630220 -> 0x6C06A0,
// and the stack holds the vehicle whose +0xE50 is this object), also its seat 0 aim's two axes {min, max, angle, rate}
// (seat +0xE0, axes +0x10, 0x40 each) and the matrices of bones 3 (cannon_main) and 4 (cannon) (bone records at
// the model node's +0xA0 +0x10, 0x110 each: the bone's own pose at +0xB0).
volatile LONG64 tankLooks=0,tankThrough=0,tankFound=0;
constexpr std::size_t kTank=0x17DC250,kModelUpdateRet=0x6C06F7,kVehicleUpdateRet=0x630235;
struct Past { ULONGLONG tick; float size[3],at[3]; float axis[2][4]; float bone[2][6]; float norm[4][2]; float phys[3]; };
constexpr int kPast=16;
struct Ring { Past p[kPast]; int next=0; const unsigned char* tank=nullptr; int tries=0; };
std::unordered_map<const void*,Ring> past;
SRWLOCK pastLock=SRWLOCK_INIT;   // the updates may run on more than one thread

const unsigned char* TankOf(const void* object) noexcept {
    void* frames[10]{};
    const USHORT n=RtlCaptureStackBackTrace(0,10,frames,nullptr);
    bool through=false;   // the model update, then the vehicle's (whatever this probe's own frames inline)
    for(USHORT i=0;i+1<n;++i)through=through || (frames[i]==image+kModelUpdateRet && frames[i+1]==image+kVehicleUpdateRet);
    InterlockedIncrement64(&tankLooks);
    if(!through)return nullptr;
    InterlockedIncrement64(&tankThrough);
    CONTEXT c{};
    RtlCaptureContext(&c);
    const unsigned char* q=Holding(reinterpret_cast<const std::uintptr_t*>(c.Rsp),object,0xE40);
    if(q)InterlockedIncrement64(&tankFound);
    __try { return q && *reinterpret_cast<unsigned char* const*>(q)==image+kTank ? q : nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void TankState(const unsigned char* v,Past& e) noexcept {
    __try {
        const auto seats=*reinterpret_cast<unsigned char* const*>(v+0x608);
        const unsigned char* aim=seats+0xE0;
        for(int a=0;a<2;++a)std::memcpy(e.axis[a],aim+0x10+a*0x40,16);
        const unsigned char* inst=v+0xE40+0xA0;
        const auto recs=*reinterpret_cast<const unsigned char* const*>(inst+0x10);
        for(int b=0;b<2;++b) {
            const float* w=reinterpret_cast<const float*>(recs+static_cast<std::size_t>(3+b)*0x110+0xB0);
            e.bone[b][0]=w[0]; e.bone[b][1]=w[5]; e.bone[b][2]=w[10]; e.bone[b][3]=w[8]; e.bone[b][4]=w[9]; e.bone[b][5]=w[2];
        }
        // Bone 4's pose (+0xB0): its rotation about x (atan2 of row 1), about z (atan2 of row 0), and row 1's length.
        {
            const float* l=reinterpret_cast<const float*>(recs+static_cast<std::size_t>(4)*0x110+0xB0);
            e.phys[0]=std::atan2(l[6],l[5]); e.phys[1]=std::atan2(l[1],l[0]); e.phys[2]=std::sqrt(l[4]*l[4]+l[5]*l[5]+l[6]*l[6]);
        }
        // The largest row length of the +0x70 and +0xB0 matrices of bones 3..6 (1: rigid).
        for(int b=0;b<4;++b)
            for(int k=0;k<2;++k) {
                const float* m=reinterpret_cast<const float*>(recs+static_cast<std::size_t>(3+b)*0x110+(k ? 0xB0 : 0x70));
                float big=0.0f;
                for(int c=0;c<3;++c)big=std::fmax(big,std::sqrt(m[4*c]*m[4*c]+m[4*c+1]*m[4*c+1]+m[4*c+2]*m[4*c+2]));
                e.norm[b][k]=big;
            }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void Remember(const void* object,const float* m) noexcept {
    Ring& r=past[object];
    if(!r.tank && r.tries<64) { ++r.tries; r.tank=TankOf(object); }
    Past& e=r.p[r.next++%kPast];
    e=Past{};
    e.tick=GetTickCount64();
    for(int i=0;i<3;++i) {
        e.size[i]=std::sqrt(m[4*i]*m[4*i]+m[4*i+1]*m[4*i+1]+m[4*i+2]*m[4*i+2]);
        e.at[i]=m[12+i];
    }
    if(r.tank)TankState(r.tank,e);
}

void History(const void* object) noexcept {
    const auto it=past.find(object);
    if(it==past.end())return;
    const Ring& r=it->second;
    for(int i=0;i<kPast;++i) {
        const Past& e=r.p[(r.next+i)%kPast];
        if(!e.tick)continue;
        char line[800];
        std::snprintf(line,sizeof(line),"UMBRA BEFORE %p tick=%llu size %g %g %g at %g %g %g%s yaw[%g %g %g %g] pitch[%g %g %g %g] "
                      "main[%g %g %g fwd %g %g %g] cannon[%g %g %g fwd %g %g %g] norms +70/+B0 b3 %.5f/%.5f b4 %.5f/%.5f b5 %.5f/%.5f b6 %.5f/%.5f",object,e.tick,e.size[0],e.size[1],e.size[2],
                      e.at[0],e.at[1],e.at[2],r.tank ? " tank" : "",e.axis[0][0],e.axis[0][1],e.axis[0][2],e.axis[0][3],
                      e.axis[1][0],e.axis[1][1],e.axis[1][2],e.axis[1][3],e.bone[0][0],e.bone[0][1],e.bone[0][2],e.bone[0][3],
                      e.bone[0][4],e.bone[0][5],e.bone[1][0],e.bone[1][1],e.bone[1][2],e.bone[1][3],e.bone[1][4],e.bone[1][5],e.norm[0][0],e.norm[0][1],e.norm[1][0],
                      e.norm[1][1],e.norm[2][0],e.norm[2][1],e.norm[3][0],e.norm[3][1]);
        AcquireSRWLockExclusive(&lock);
        if(rows<kMaxRows) { pending.emplace_back(line); ++rows; }
        ReleaseSRWLockExclusive(&lock);
    }
}

void __fastcall Object(void* object,const float* m) noexcept {
    InterlockedIncrement64(&calls[0]);
    AcquireSRWLockExclusive(&pastLock);
    if(Bad(m))History(object);
    try { Remember(object,m); } catch(...) {}
    ReleaseSRWLockExclusive(&pastLock);
    if(Bad(m))Keep(0,object,m);
    realObject(object,m);
}
void* __fastcall Obb(const float* m) noexcept {
    InterlockedIncrement64(&calls[1]);
    if(Bad(m))Keep(1,nullptr,m);
    return realObb(m);
}
void __fastcall Camera(void* camera,const float* m) noexcept {
    InterlockedIncrement64(&calls[2]);
    if(Bad(m))Keep(2,camera,m);
    realCamera(camera,m);
}

// The import entry swapped when it points outside EDF.dll (into umbra_sandlot.dll); the original kept first.
template<class Fn> bool Swap(std::size_t rva,Fn& real,void* with) noexcept {
    auto** const entry=reinterpret_cast<void**>(image+rva);
    void* const now=*entry;
    if(!now || (static_cast<unsigned char*>(now)>=image && static_cast<unsigned char*>(now)<imageEnd))return false;
    real=reinterpret_cast<Fn>(now);
    DWORD old=0;
    if(!VirtualProtect(entry,8,PAGE_READWRITE,&old))return false;
    const bool installed=InterlockedCompareExchangePointer(entry,with,now)==now;
    VirtualProtect(entry,8,old,&old);
    return installed;
}
}  // namespace

bool InstallUmbraProbe(unsigned char* img,LogFn log) noexcept {
    image=img; say=log;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(img+reinterpret_cast<const IMAGE_DOS_HEADER*>(img)->e_lfanew);
    imageEnd=img+nt->OptionalHeader.SizeOfImage;
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46u)return false;
    return Swap(kObjectMatrix,realObject,reinterpret_cast<void*>(&Object)) && Swap(kObbCreate,realObb,reinterpret_cast<void*>(&Obb)) &&
           Swap(kCameraMatrix,realCamera,reinterpret_cast<void*>(&Camera));
}

void UmbraProbeTick() noexcept {
    if(!say)return;
    std::vector<std::string> out;
    AcquireSRWLockExclusive(&lock);
    out.swap(pending);
    ReleaseSRWLockExclusive(&lock);
    for(const std::string& line:out)say("%s",line.c_str());
    const ULONGLONG now=GetTickCount64();
    static ULONGLONG lastTank=0;
    if(now-lastTank>=250) {   // every tank's latest frame, four times a second
        lastTank=now;
        std::vector<std::string> tanks;
        AcquireSRWLockExclusive(&pastLock);
        for(const auto& [object,r]:past) {
            if(!r.tank)continue;
            const Past& e=r.p[(r.next+kPast-1)%kPast];
            char line[600];
            std::snprintf(line,sizeof(line),"UMBRA TANK %p v=%p size %.2f %.2f %.2f yaw[%.3f %.3f %.3f %.3f] pitch[%.3f %.3f %.3f %.3f] "
                          "cannon[%.3f %.3f %.3f fwd %.3f %.3f %.3f] norms b4 %.5f/%.5f b6 %.5f/%.5f phys x %.4f z %.4f len %.5f",object,r.tank,e.size[0],e.size[1],e.size[2],e.axis[0][0],
                          e.axis[0][1],e.axis[0][2],e.axis[0][3],e.axis[1][0],e.axis[1][1],e.axis[1][2],e.axis[1][3],e.bone[1][0],
                          e.bone[1][1],e.bone[1][2],e.bone[1][3],e.bone[1][4],e.bone[1][5],e.norm[1][0],e.norm[1][1],e.norm[3][0],e.norm[3][1],e.phys[0],e.phys[1],e.phys[2]);
            tanks.emplace_back(line);
        }
        ReleaseSRWLockExclusive(&pastLock);
        for(const std::string& line:tanks)say("%s",line.c_str());
    }
    if(now-lastCount>=10000) {
        lastCount=now;
        say("UMBRA calls object %lld (bad %lld) obbmodel %lld (bad %lld) camera %lld (bad %lld); tank looks %lld through %lld found %lld",
            calls[0],bad[0],calls[1],bad[1],calls[2],bad[2],tankLooks,tankThrough,tankFound);
    }
}
}  // namespace autopilot
