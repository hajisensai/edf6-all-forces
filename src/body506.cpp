// The 506 body the plugin flies its own aircraft in (jet.cpp, subcarrier.cpp, playerjet.cpp).
//
// Which aircraft a 506 is comes from its SGO's mark in the speed gain k (veh+0x162C): the one table of the
// marks is here, so no two owners can claim the same one and no owner compares floats of its own.
// The 506's physics step (slot 57) and its message handler (slot 9) are hooked once, here: after the stock step
// the body goes to the step of the owner its mark names, which gives the velocity and spin to set; a message
// goes to the owner first (body506.h SubMessage / PlayerJetMessage).
// The tools every owner needs the same way (the body part, the attitude, the ground and ceiling, the bones, the
// game's step, the death) are here too (body506.h).
#include "body506.h"
#include "memory.h"
#include "subcarrier.h"
#include "vecmath.h"
#include <cwchar>

namespace crew {
namespace {
using vec::Cross;using vec::Dot;using vec::Len;
constexpr unsigned kHeli506=0x17DB238,kPhysics506=0x61B710;
constexpr std::size_t kSlotPhysics=57,kSlotMessage=9,kSpeedGain=0x162C,kBody=0x1650;
constexpr unsigned kSetLinearVelocity=0x11B18F0,kSetAngularVelocity=0x11B1760;
// The 506's message handler (docs/subcarrier-re.md §8.1): it takes the water message itself, the rest goes to the
// vehicle handler 0x62ECB0, which runs the death step 0x6329B0 for 0x1000000F (docs/player-jet-re.md §4).
constexpr unsigned kMessage506=0x652E70,kVehicleDie=0x6329B0;
// The heli's "body" part (jet.cpp FixBodyPart): 0x6EA4B0(parts, name) -> index or -1, kept at +0x1530.
constexpr unsigned kFindPart=0x6EA4B0;
constexpr std::size_t kParts=0x1320,kBodyPart=0x1530;
const wchar_t* const kFuselageBones[]={L"bomber501",L"bomber401",L"body"};
constexpr std::size_t kCeiling=0x20B2998,kCeilingY=0x3C;
constexpr float kUnderProbe=600.0f,kGroundProbe=3000.0f;
constexpr std::size_t kInstBoneCount=0x20,kBoneStride=0x110;

// The marks (testrange/gen.py JETS, tools/make_jets.py, tools/make_sub.py write them into the SGOs):
// jets 7001-7099 (jet.cpp kKinds and kCarrierMarks name each), the submarine carrier 7101, the player
// jets 7201-7299 (playerjet.cpp kKinds). No stock 506 has a speed gain this large.
struct MarkRange { float first,last; PluginBody body; };
constexpr MarkRange kMarks[]={
    {7001.0f,7099.0f,PluginBody::jet},
    {7101.0f,7101.0f,PluginBody::sub},
    {7201.0f,7299.0f,PluginBody::playerJet},
};

using PhysicsFn=void(__fastcall*)(void*);
using MessageFn=bool(__fastcall*)(void*,std::uint32_t,void*);
using SetVecFn=void(*)(void*,const float*);
using StepFn=bool(*)(unsigned char*,float*,float*) noexcept;
using OwnerMessageFn=bool(*)(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept;
using FindPartFn=std::int32_t(__fastcall*)(void*,const wchar_t*);
PhysicsFn nextPhysics=nullptr;
MessageFn nextMessage=nullptr;
bool physicsOk=false,messageOk=false,dieOk=false,bodyPartOk=false;
// The death message's data: the vehicle handler's 0x1000000F case reads only the vehicle (0x6329B0(vehicle));
// zeros, not garbage, should anything read it.
alignas(16) unsigned char noData[0xA0]{};

const unsigned char kPhysicsSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8};   // push rbx; sub rsp,20h; mov rbx,rcx; call
// Both jump through the Havok world interface; only the slot differs (0xA8 linear, 0xB0 angular).
const unsigned char kSetLinSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xA8,0x00,0x00};
const unsigned char kSetAngSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xB0,0x00,0x00};
const unsigned char kMessageSig[]={0x48,0x89,0x5C,0x24,0x08,0x55,0x56,0x57};
const unsigned char kWaterCmpSig[]={0x81,0xFA,0x25,0x00,0x00,0x10};   // 0x652E8E cmp edx,10000025h: the rest to 0x62ECB0
// 0x6329B0: mov [rsp+8],rbx; push rdi; sub rsp,20h; imul rax,[rcx+618h],340h (the seat loop)
const unsigned char kDieSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x69,0x81,0x18,0x06,0x00,0x00,0x40,0x03,0x00,0x00};
const unsigned char kFindPartSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x50};

StepFn StepOf(PluginBody body) noexcept {
    switch(body) {
        case PluginBody::jet: return &JetBodyStep;
        case PluginBody::sub: return &SubBodyStep;
        case PluginBody::playerJet: return &PlayerJetBodyStep;
        default: return nullptr;
    }
}
OwnerMessageFn MessageOwner(PluginBody body) noexcept {
    switch(body) {
        case PluginBody::sub: return &SubMessage;
        case PluginBody::playerJet: return &PlayerJetMessage;
        default: return nullptr;   // the jets take their messages as the stock 506 does
    }
}

void __fastcall PhysicsHook(void* vehicle) {
    nextPhysics(vehicle);
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        // Every plugin body has its body part, however it came into the world: a jet a mission script placed (the
        // test range's NPC and enemy jets, src/jet.cpp CrewPlaced) never went through the spawn's fix, and shot down
        // its 506 crash state (0x650010, state 1) read part -1: EDF+0x650137, 2026-10-04 (FixBodyPart506).
        if(At<std::int32_t>(v,kBodyPart)==-1 && BodyOf(v)!=PluginBody::none)FixBodyPart506(v,"BODY506");
        const StepFn step=StepOf(BodyOf(v));
        if(!step)return;
        alignas(16) float lin[4]{},ang[4]{};
        if(!step(v,lin,ang))return;
        const auto body=At<void*>(v,kBody);
        if(!body)return;
        reinterpret_cast<SetVecFn>(image+kSetLinearVelocity)(body,lin);
        reinterpret_cast<SetVecFn>(image+kSetAngularVelocity)(body,ang);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        static ULONGLONG at=0;
        const ULONGLONG now=GetTickCount64();
        if(now-at>5000){at=now;Log("BODY506 physics: fault in a body's step (vehicle %p)",vehicle);}
    }
}

// The owner's say on a message (see body506.h): true to take it whole. A fault in the owner leaves the message as
// it came.
bool Dispatch(void* obj,std::uint32_t msg,void* data,MessageRestore* restore) noexcept {
    __try {
        auto v=static_cast<unsigned char*>(obj);
        const OwnerMessageFn owner=MessageOwner(BodyOf(v));
        return owner && owner(v,msg,data,restore);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        restore->at=nullptr;
        static ULONGLONG at=0;
        const ULONGLONG now=GetTickCount64();
        if(now-at>5000){at=now;Log("BODY506 message %08X: fault in a body's handler (vehicle %p)",msg,obj);}
        return false;
    }
}

bool __fastcall MessageHook(void* obj,std::uint32_t msg,void* data) {
    MessageRestore restore{nullptr,0.0f};
    if(Dispatch(obj,msg,data,&restore))return true;
    const bool handled=nextMessage(obj,msg,data);
    if(restore.at) {
        __try { *restore.at=restore.was; } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    return handled;
}

bool InstallMessages() noexcept {
    if(!Matches(kMessage506,kMessageSig,sizeof(kMessageSig)) || !Matches(0x652E8E,kWaterCmpSig,sizeof(kWaterCmpSig))) {
        Log("BODY506 message handler: unexpected EDF.dll code: no water / damage handling, no death message");
        return false;
    }
    const auto slot=reinterpret_cast<void**>(image+kHeli506)+kSlotMessage;
    void* const current=*slot;
    if(current!=image+kMessage506)Log("BODY506 messages: slot 9 is %p, not EDF+%X: chained onto it",current,kMessage506);
    nextMessage=reinterpret_cast<MessageFn>(current);
    return PatchVtableSlot(slot,current,reinterpret_cast<void*>(&MessageHook));
}
}  // namespace

float BodyMark(const void* vehicle) noexcept {
    const auto v=static_cast<const unsigned char*>(vehicle);
    if(!Readable(v,kSpeedGain+4) || At<const unsigned char*>(v,0)!=image+kHeli506)return 0.0f;
    return At<float>(v,kSpeedGain);
}

PluginBody BodyOf(const void* vehicle) noexcept {
    const float k=BodyMark(vehicle);
    for(const auto& m:kMarks)if(k>=m.first && k<=m.last)return m.body;
    return PluginBody::none;
}

bool Body506Ok() noexcept { return physicsOk; }
bool Body506MessageOk() noexcept { return messageOk; }
bool Die506Ok() noexcept { return dieOk; }
bool BodyPartOk() noexcept { return bodyPartOk; }

float GroundClearance(const float* p) noexcept {
    // Down to kGroundProbe: a body over a deep valley still sees its floor, so kNoGround means none at all.
    // Under the ground a ray down sees nothing: one from kUnderProbe over it finds the surface then.
    const float down[3]={p[0],p[1]-kGroundProbe,p[2]};
    float hit[3];
    if(MapRay(p,down,hit)>=0.0f)return p[1]-hit[1];
    const float top[3]={p[0],p[1]+kUnderProbe,p[2]};
    return MapRay(top,p,hit)>=0.0f ? p[1]-hit[1] : kNoGround;
}

// The player jet over the map's ceiling (the user, 2026-10-05: "who put in this 300 m limit?"). The heli input (slot
// 55, 0x6543A0) sets a body over the ceiling (*(*(image+0x20B2998)+0x3C), 300 m on this map) back down to it every
// frame, its position only (docs/jet-model-re.md, slot 55's clamp). The plugin's input hook keeps the player jet's
// height round it: CeilingHold reads the body's place before the stock input, CeilingLift puts the height back when
// the clamp took it down (the X/Z area clamp is already off for it: jet.cpp kAreaInset). The NPC jets keep under the
// ceiling on their own (jet_flight.cpp Guard).
constexpr unsigned kGetPosition=0x11B15B0,kSetPosition=0x11B1A00;
const unsigned char kGetPositionSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00,
                                       0x00,0x48,0x8B,0x48,0x58,0x48,0x8B,0x41,0x20,0x48,0x83,0xC1,0x20,0xFF,0x90,0x80,
                                       0x00,0x00,0x00,0x48,0x83,0xC0,0x30};
const unsigned char kSetPositionSig[]={0x40,0x55,0x48,0x83,0xEC,0x70,0x48,0x8D,0x6C,0x24,0x40,0x48,0x83,0xE5,0xE0,0x48};
int ceilingOk=-1;   // -1 not checked yet

bool CeilingHold(unsigned char* v,float* at) noexcept {
    __try {
        if(ceilingOk<0)ceilingOk=Matches(kGetPosition,kGetPositionSig,sizeof(kGetPositionSig)) &&
                                 Matches(kSetPosition,kSetPositionSig,sizeof(kSetPositionSig));
        if(!ceilingOk || BodyOf(v)!=PluginBody::playerJet)return false;
        void* const body=At<void*>(v,kBody);
        if(!body)return false;
        const float* p=reinterpret_cast<const float*(*)(void*)>(image+kGetPosition)(body);
        if(!p)return false;
        std::memcpy(at,p,16);
        return at[1]>CeilingY()-1.0f;   // under the ceiling the clamp never acts
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void CeilingLift(unsigned char* v,const float* at) noexcept {
    __try {
        void* const body=At<void*>(v,kBody);
        if(!body)return;
        const float* p=reinterpret_cast<const float*(*)(void*)>(image+kGetPosition)(body);
        if(!p || p[1]>=at[1]-0.01f)return;   // not taken down
        alignas(16) const float q[4]={p[0],at[1],p[2],p[3]};
        reinterpret_cast<void(*)(void*,const float*)>(image+kSetPosition)(body,q);
        Put<float>(v,kPosition+4,at[1]);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

float CeilingY() noexcept {
    const auto p=At<const unsigned char*>(image,kCeiling);
    if(!p || !Readable(p+kCeilingY,4))return 1e9f;
    const float y=At<float>(p,kCeilingY);
    return std::isfinite(y) ? y : 1e9f;
}

float GameStep(ULONGLONG since) noexcept {
    constexpr float kFrame=1.0f/60.0f;
    if(!since)return kFrame;
    const float s=static_cast<float>(since)*0.001f;
    return s<kFrame ? (s>0.001f ? s : 0.001f) : kFrame;
}

bool FixBodyPart506(unsigned char* v,const char* tag) noexcept {
    if(At<std::int32_t>(v,kBodyPart)!=-1)return true;
    if(!bodyPartOk){Log("%s v=%p has no body part and the lookup is off (going down it would crash)",tag,v);return false;}
    for(const auto name:kFuselageBones) {
        const auto i=reinterpret_cast<FindPartFn>(image+kFindPart)(v+kParts,name);
        if(i<0)continue;
        Put<std::int32_t>(v,kBodyPart,i);
        if(Cfg().debug)Log("%s v=%p body part: %ls (%d)",tag,v,name,i);
        return true;
    }
    // None of its bones is a fuselage: its root (bone 0), which every model has, so its crash state reads a bone, not
    // part -1 (EDF+0x650137).
    Put<std::int32_t>(v,kBodyPart,0);
    Log("%s v=%p has no fuselage bone: body part 0 (its root)",tag,v);
    return true;
}

void BodyAttitude(const unsigned char* v,const float* nose,const float* up,float gain,float maxRate,float* omega) noexcept {
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const float* r=m;const float* u=m+4;const float* f=m+8;
    float rx[3];Cross(u,f,rx);
    const float hand=Dot(rx,r)>=0.0f ? 1.0f : -1.0f;   // right = hand * up x forward
    float right[3];Cross(up,nose,right);
    for(int i=0;i<3;++i)right[i]*=hand;
    float w[3]={0,0,0},c[3];
    Cross(r,right,c);for(int i=0;i<3;++i)w[i]+=c[i];
    Cross(u,up,c);for(int i=0;i<3;++i)w[i]+=c[i];
    Cross(f,nose,c);for(int i=0;i<3;++i)w[i]+=c[i];
    for(int i=0;i<3;++i)w[i]*=0.5f*gain;
    const float l=Len(w);
    if(l>maxRate)for(int i=0;i<3;++i)w[i]*=maxRate/l;
    std::memcpy(omega,w,12);
}

unsigned char* BoneRecord506(const unsigned char* inst,const wchar_t* name) noexcept {
    if(!Readable(inst,kInstBoneCount+4))return nullptr;
    const auto count=At<std::int32_t>(inst,kInstBoneCount);
    const auto bones=At<unsigned char*>(inst,kInstBones506);
    if(count<=0 || count>256 || !Readable(bones,static_cast<std::size_t>(count)*kBoneStride))return nullptr;
    for(std::int32_t i=0;i<count;++i) {
        unsigned char* rec=bones+static_cast<std::size_t>(i)*kBoneStride;
        const auto n=At<const wchar_t*>(rec,0);
        if(n && Readable(n,32) && std::wcsncmp(n,name,16)==0)return rec;
    }
    return nullptr;
}

void HingePose(const float* bind,float angle,float* out) noexcept {
    const float co=std::cos(angle),si=std::sin(angle);
    for(int x=0;x<4;++x) {
        out[x]=bind[x];
        out[4+x]=co*bind[4+x]+si*bind[8+x];
        out[8+x]=-si*bind[4+x]+co*bind[8+x];
        out[12+x]=bind[12+x];
    }
}

bool Die506(unsigned char* v) noexcept {
    if(!dieOk)return false;
    const auto slots=At<MessageFn*>(v,0);
    slots[kSlotMessage](v,kMsgDie,noData);
    return true;
}

bool InstallBody506() noexcept {
    __try {
        const bool sig=Matches(kPhysics506,kPhysicsSig,sizeof(kPhysicsSig)) && Matches(kSetLinearVelocity,kSetLinSig,sizeof(kSetLinSig)) &&
                       Matches(kSetAngularVelocity,kSetAngSig,sizeof(kSetAngSig));
        if(!sig){Log("BODY506 profile mismatch: the plugin's aircraft are off");return false;}
        const auto slot=reinterpret_cast<void**>(image+kHeli506)+kSlotPhysics;
        void* const current=*slot;
        if(current!=image+kPhysics506)Log("BODY506 physics: chaining onto %p (another plugin)",current);
        nextPhysics=reinterpret_cast<PhysicsFn>(current);
        physicsOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PhysicsHook));
        messageOk=physicsOk && InstallMessages();
        // The death goes through the 506's own message slot (hooked or not) to the stock handler.
        dieOk=messageOk && Matches(kVehicleDie,kDieSig,sizeof(kDieSig));
        bodyPartOk=Matches(kFindPart,kFindPartSig,sizeof(kFindPartSig));
        Log("HOOK body506 physics=%d messages=%d die=%d bodyPart=%d",physicsOk,messageOk,dieOk,bodyPartOk);
        return physicsOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
