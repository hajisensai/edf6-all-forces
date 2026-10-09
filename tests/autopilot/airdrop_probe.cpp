// The stock air delivery probe (test only, part of EDF6Autopilot): the game's own vehicle request run in a mission,
// and what its transport hands the container read as it happens. Asked by the autopilot command "probe airdrop".
//
// The chain (docs/airdrop-vehicle-re.md): an Air Raider's vehicle call -> Transporter508 (its init, vtable slot 50
// 0x5E5070) -> CreateObject of the container (call 0x5E5260) -> the container's configure (0x5E8B40, call 0x5E5687)
// -> the transporter's release event (slot 47 0x5E5CE0, call 0x5E5D3D to the container's 0x5E84D0) -> the container
// falls and, settled, makes the vehicle (its unload state 0x5E8C00, CreateObject call 0x5E8FDC).
// Every one of those calls is a direct rel32 call, so each is redirected at its call site to a logging hook that
// calls the original: nothing about the game's own run changes. The transporter's init is read from its frame at
// the container's creation (its prologue checked: push x8, lea rbp,[rsp-108h], sub rsp,208h).
//
// What gets the game to call a vehicle: the probe holds the candidate keys one after another (through the autopilot's
// keyboard imports, never the desktop's) until the transporter's init is seen, then stops and names the key.
#include "airdrop_probe.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <intrin.h>
#include <cstdint>
#include <cstring>

namespace autopilot {
namespace {
constexpr std::size_t kCreateObject=0x11945E0,kConfigure=0x5E8B40,kRelease=0x5E84D0,kTransporterInit=0x5E5070;
constexpr std::size_t kContainerCreateCall=0x5E5260,kConfigureCall=0x5E5687,kReleaseCall=0x5E5D3D,kVehicleCreateCall=0x5E8FDC;
// The transporter init's prologue: 8 pushes (0x40), lea rbp,[rsp-108h], sub rsp,208h.
const unsigned char kInitPrologue[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,
                                     0xF8,0xFE,0xFF,0xFF,0x48,0x81,0xEC,0x08,0x02,0x00,0x00};
constexpr std::size_t kInitFrame=0x208+0x40;   // its entry rsp (at its return address) above its rsp at a call
constexpr std::size_t kInitRbp=0x148;          // its entry rsp above its rbp
constexpr std::size_t kObjectMatrix=0x60;      // a scene object's world matrix (4 rows of 4 floats, position last)

using CreateObjectFn=unsigned char*(*)(void*,const float*,const wchar_t*,void*);
using ConfigureFn=void(*)(unsigned char*,void*,const wchar_t*,void*,float);
using ReleaseFn=void(*)(unsigned char*);

unsigned char* image=nullptr;
LogFn say=nullptr;
HoldFn hold=nullptr;
volatile LONG transporterSeen=0;
volatile LONG vehicleSeen=0;
unsigned char* volatile vehicle=nullptr;
ULONGLONG vehicleAt=0;
unsigned char* volatile container=nullptr;
ULONGLONG missionAt=0;

std::size_t Rva(const void* p) noexcept {
    const auto at=reinterpret_cast<std::uintptr_t>(p),base=reinterpret_cast<std::uintptr_t>(image);
    return at>=base && at<base+0x22CE000 ? at-base : 0;
}

void Hex(const char* what,const void* p,std::size_t size) noexcept {
    if(!p || !edf::Readable(p,size)){say("  %s %p: unreadable",what,p);return;}
    const auto b=static_cast<const unsigned char*>(p);
    for(std::size_t row=0;row<size;row+=32) {
        char line[160]{};int at=0;
        for(std::size_t i=row;i<row+32 && i<size;++i)at+=wsprintfA(line+at,i%8==0 ? " %02X" : "%02X",b[i]);
        say("  %s+%03zx%s",what,row,line);
    }
}

void Text(const char* what,const wchar_t* text) noexcept {
    char narrow[160]{};
    __try {
        for(int i=0;i<159 && text && edf::Readable(text+i,2) && text[i];++i)
            narrow[i]=text[i]>=32 && text[i]<127 ? static_cast<char>(text[i]) : '?';
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    say("  %s=%p \"%s\"",what,text,narrow);
}

// An MSVC std::wstring (buffer or pointer at +0, size +0x10, capacity +0x18).
void WString(const char* what,const unsigned char* s) noexcept {
    if(!edf::Readable(s,0x20)){say("  %s %p: unreadable",what,s);return;}
    const auto size=*reinterpret_cast<const unsigned long long*>(s+0x10),cap=*reinterpret_cast<const unsigned long long*>(s+0x18);
    const wchar_t* chars=cap>7 ? *reinterpret_cast<const wchar_t* const*>(s) : reinterpret_cast<const wchar_t*>(s);
    say("  %s wstring size=%llu cap=%llu",what,size,cap);
    if(size)Text(what,chars);
}

// The stock value of a vehicle setup: a variant, 16 bytes of storage then its type index (u16, 0xFFFF empty). The
// storage's pointers followed one level, so a list's elements show.
void Variant(const char* what,const unsigned char* v) noexcept {
    if(!edf::Readable(v,0x18)){say("  %s %p: unreadable",what,v);return;}
    const unsigned index=*reinterpret_cast<const unsigned short*>(v+0x10);
    say("  %s variant index=%u (0xFFFF empty)",what,index);
    Hex(what,v,0x18);
    for(int i=0;i<2;++i) {
        const auto p=*reinterpret_cast<const unsigned char* const*>(v+i*8);
        if(!Rva(p) && edf::Readable(p,0x60)) {
            char name[48];wsprintfA(name,"%s.q%d->",what,i);
            Hex(name,p,0x60);
            for(int j=0;j<4;++j) {
                const auto q=*reinterpret_cast<const unsigned char* const*>(p+j*8);
                if(!Rva(q) && edf::Readable(q,0x40)){wsprintfA(name,"%s.q%d->q%d->",what,i,j);Hex(name,q,0x40);}
            }
        }
    }
}

// The setup the configure copies (0x5E44F0) into the container +0xC38: a wstring (its text form) then a variant.
void Setup(const char* what,const unsigned char* s) noexcept {
    Hex(what,s,0x38);
    WString(what,s);
    Variant(what,s+0x20);
}

void Object(const char* what,const unsigned char* o) noexcept {
    if(!edf::Readable(o,0xA0)){say("  %s %p: unreadable",what,o);return;}
    const auto vt=*reinterpret_cast<const unsigned char* const*>(o);
    const float* m=reinterpret_cast<const float*>(o+kObjectMatrix);
    say("  %s=%p vtable EDF+%#zx pos (%.2f, %.2f, %.2f) fwd (%.3f, %.3f, %.3f)",what,o,Rva(vt),m[12],m[13],m[14],m[8],m[9],m[10]);
}

unsigned char* ContainerCreate(void* mgr,const float* m,const wchar_t* sgo,void* param) {
    const auto ret=reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
    InterlockedExchange(&transporterSeen,1);
    say("PROBE container CreateObject (in Transporter508 init 0x5E5070)");
    __try {
        const std::uintptr_t entry=ret+8+kInitFrame;
        const auto args=reinterpret_cast<const unsigned char*>(entry+0x28);
        const auto transporter=reinterpret_cast<const unsigned char*>(m)-kObjectMatrix;
        Object("transporter",transporter);
        Hex("transporter+0x120",transporter+0x120,0x20);
        say("  transporter +0x778 (derived id counter) = %u",*reinterpret_cast<const unsigned*>(transporter+0x778));
        Hex("init.stackargs[5..10]",args,0x30);
        Text("init.arg5 containerSgo",*reinterpret_cast<const wchar_t* const*>(args));
        Text("init.arg6 vehicleSgo",*reinterpret_cast<const wchar_t* const*>(args+8));
        Text("init.arg7 setupText",*reinterpret_cast<const wchar_t* const*>(args+0x10));
        const auto value=*reinterpret_cast<const unsigned char* const*>(args+0x18);
        say("  init.arg8 setupValue=%p",value);
        if(value)Variant("init.arg8",value);
        say("  init.arg9 int=%d arg10 level=%.4f",*reinterpret_cast<const int*>(args+0x20),*reinterpret_cast<const float*>(args+0x28));
        const auto parentSp=*reinterpret_cast<const unsigned char* const*>(entry-kInitRbp-0x60);
        Hex("init.arg2 parent shared_ptr",parentSp,0x10);
        if(edf::Readable(parentSp,0x10))Object("parent",*reinterpret_cast<const unsigned char* const*>(parentSp));
        Hex("init derivedNetId(rbp+58h)",reinterpret_cast<const void*>(entry-kInitRbp+0x58),0x20);
        Text("create.sgo",sgo);
        Hex("create.matrix",m,0x40);
        say("  create.param vtable EDF+%#zx",Rva(*static_cast<void* const*>(param)));
        Hex("create.param",param,0x58);
    } __except(EXCEPTION_EXECUTE_HANDLER){say("  (fault reading the transporter's frame)");}
    unsigned char* const made=reinterpret_cast<CreateObjectFn>(image+kCreateObject)(mgr,m,sgo,param);
    container=made;
    __try{Object("container",made);}__except(EXCEPTION_EXECUTE_HANDLER){}
    return made;
}

void Configure(unsigned char* box,void* parent,const wchar_t* vehicleSgo,void* setup,float level) {
    say("PROBE container configure 0x5E8B40 container=%p level=%.4f",box,level);
    __try {
        Hex("configure.parent shared_ptr",parent,0x10);
        if(edf::Readable(parent,0x10))Object("configure.parent",*static_cast<const unsigned char* const*>(parent));
        Text("configure.vehicleSgo",vehicleSgo);
        Setup("configure.setup",static_cast<const unsigned char*>(setup));
    } __except(EXCEPTION_EXECUTE_HANDLER){say("  (fault reading the configure's arguments)");}
    reinterpret_cast<ConfigureFn>(image+kConfigure)(box,parent,vehicleSgo,setup,level);
    __try {
        Hex("container+0xB40",box+0xB40,0x170);
        WString("container+0xB80",box+0xB80);
        Setup("container+0xC38",box+0xC38);
    } __except(EXCEPTION_EXECUTE_HANDLER){say("  (fault reading the configured container)");}
}

void Release(unsigned char* box) {
    say("PROBE container release 0x5E84D0 container=%p",box);
    __try{Object("container",box);Hex("container+0xB40",box+0xB40,0x40);}__except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<ReleaseFn>(image+kRelease)(box);
}

unsigned char* VehicleCreate(void* mgr,const float* m,const wchar_t* sgo,void* param) {
    say("PROBE vehicle CreateObject (container unload 0x5E8C00)");
    __try {
        Text("vehicle.sgo",sgo);
        Hex("vehicle.matrix",m,0x40);
        say("  vehicle.param vtable EDF+%#zx",Rva(*static_cast<void* const*>(param)));
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    unsigned char* const made=reinterpret_cast<CreateObjectFn>(image+kCreateObject)(mgr,m,sgo,param);
    __try{Object("vehicle",made);}__except(EXCEPTION_EXECUTE_HANDLER){}
    vehicleAt=GetTickCount64();
    vehicle=made;
    InterlockedExchange(&vehicleSeen,1);
    return made;
}

bool Redirect(std::size_t site,std::size_t target,void* hook,const char* name) noexcept {
    bool changed=false;
    const bool ok=edf::RedirectCall(image+site,image+target,hook,changed);
    say("PROBE hook %s at EDF+%#zx: %s",name,site,ok ? "in" : changed ? "HALF" : "not in");
    return ok;
}

// The keys tried as the vehicle call, in order (mouse middle and side buttons first, then letters, digits, shift,
// ctrl, space, the right button). Not Esc, Tab, Alt or the function keys (menus, the window).
const int kCallKeys[]={0x04,0x05,0x06,'V','F','G','Q','E','X','Z','C','B','T','H','R','Y','U','1','2','3','4','5',
                       0x10,0x11,0x20,0x02};
constexpr ULONGLONG kTrialDelayMs=25000,kHoldMs=1500,kGapMs=2500;
int trial=0;
ULONGLONG trialAt=0;
bool holding=false;
int vehicleReads=0;
}  // namespace

bool InstallAirdropProbe(unsigned char* img,LogFn logFn,HoldFn holdFn) noexcept {
    image=img;say=logFn;hold=holdFn;
    if(!edf::Matches(image,kTransporterInit,kInitPrologue,sizeof(kInitPrologue))) {
        say("PROBE Transporter508 init EDF+%#zx not as read: no probe",kTransporterInit);
        return false;
    }
    bool ok=Redirect(kContainerCreateCall,kCreateObject,reinterpret_cast<void*>(&ContainerCreate),"container create");
    ok=Redirect(kConfigureCall,kConfigure,reinterpret_cast<void*>(&Configure),"configure") && ok;
    ok=Redirect(kReleaseCall,kRelease,reinterpret_cast<void*>(&Release),"release") && ok;
    ok=Redirect(kVehicleCreateCall,kCreateObject,reinterpret_cast<void*>(&VehicleCreate),"vehicle create") && ok;
    return ok;
}

void AirdropProbeMissionStarted() noexcept {missionAt=GetTickCount64();}

void AirdropProbeTick() noexcept {
    if(!say)return;
    const ULONGLONG now=GetTickCount64();
    if(missionAt && !transporterSeen && now>=missionAt+kTrialDelayMs && trial<static_cast<int>(_countof(kCallKeys))) {
        if(!trialAt)trialAt=now;
        if(!holding && now>=trialAt) {
            hold(kCallKeys[trial],true);holding=true;trialAt=now+kHoldMs;
            say("PROBE try call key %02x",kCallKeys[trial]);
        } else if(holding && now>=trialAt) {
            hold(kCallKeys[trial],false);holding=false;trialAt=now+kGapMs;++trial;
        }
    }
    if(holding && transporterSeen) {
        hold(kCallKeys[trial],false);holding=false;
        say("PROBE the vehicle call key: %02x (the transporter came while it was held)",kCallKeys[trial]);
    }
    // The delivered vehicle at 1, 3, 6 and 10 s.
    static const ULONGLONG kReads[]={1000,3000,6000,10000};
    if(vehicleSeen && vehicleReads<4 && now>=vehicleAt+kReads[vehicleReads]) {
        __try {
            const unsigned char* v=vehicle;
            say("PROBE vehicle after %llu ms",kReads[vehicleReads]);
            Object("vehicle",v);
            if(edf::Readable(v+0x680,4))say("  vehicle +0x67C level=%.4f",*reinterpret_cast<const float*>(v+0x67C));
        } __except(EXCEPTION_EXECUTE_HANDLER){say("  (vehicle unreadable)");}
        ++vehicleReads;
    }
}
}  // namespace autopilot
