// EOS performs the native game's SEARCH_TYPE range query before returning rooms.
// Namespace that existing key, including Coop's reflected family. Vanilla's
// invitation check also rejects the namespaced value before joining/loading data.
#include "mod_room.h"
#include <Windows.h>
#include <cstring>
#include <atomic>
#include "edf/patch.h"
namespace crew {
extern unsigned char* image;
namespace {
struct Data { int version; const char* key; std::int64_t value; int type; };
struct Attribute { int version; Data* data; int visibility; };
struct Options { int version; const Data* data; int mode; };
struct IndexOptions { int version; unsigned index; };
struct KeyOptions { int version; const char* key; };
struct JoinOptions { int version; void* details; const void* user; };
struct Info { int result; void* client; const char* lobby; };
using Callback=void(*)(const Info*);
using Put=int(*)(void*,const Options*);
using Copy=int(*)(void*,const IndexOptions*,Attribute**);
using CopyKey=int(*)(void*,const KeyOptions*,Attribute**);
using Release=void(*)(Attribute*);
using Join=void(*)(void*,const JoinOptions*,void*,Callback);
Put add=nullptr,filter=nullptr;
Copy copy=nullptr;
CopyKey rawCopy=nullptr;
Release release=nullptr;
Join join=nullptr;
using Create=void(*)(void*,const void*,void*,Callback);
Create create=nullptr;
std::atomic<bool> ready{false};
void Refuse(void* client,Callback cb) {
    const Info info{10,client,nullptr};
    if(cb)cb(&info);
}
void CreateRoom(void* h,const void* o,void* client,Callback cb) {
    if(!ready.load(std::memory_order_acquire))return Refuse(client,cb);
    create(h,o,client,cb);
}
bool Type(const Data* d) noexcept {return d && d->key && d->type==1 && !std::strcmp(d->key,"SEARCH_TYPE");}
int Add(void* h,const Options* o) {
    if(!o || !Type(o->data))return add(h,o);
    // A durable marker survives DirectNet's persisted room attributes, whose
    // game-facing type may already have been decoded by another wrapper.
    const Data profile{1,"AF_PROFILE",1,1};
    const Options marker{2,&profile,0};
    const int marked=add(h,&marker);
    if(marked)return marked;
    Data d=*o->data;d.value=EncodeAllForcesRoom(d.value);
    Options options=*o;options.data=&d;
    return add(h,&options);
}
int Filter(void* h,const Options* o) {
    if(!o || !Type(o->data))return filter(h,o);
    Data d=*o->data;d.value=EncodeAllForcesRoom(d.value);
    Options options=*o;options.data=&d;
    return filter(h,&options);
}
int CopyAttribute(void* h,const IndexOptions* o,Attribute** out) {
    const int result=copy(h,o,out);
    if(!result && out && *out && Type((*out)->data) && AllForcesWireType((*out)->data->value))
        (*out)->data->value=DecodeAllForcesRoom((*out)->data->value);
    return result;
}
void JoinRoom(void* h,const JoinOptions* o,void* client,Callback cb) {
    if(!ready.load(std::memory_order_acquire))return Refuse(client,cb);
    // A later-loaded All Forces wrapper can see Coop's synthetic lobby handles.
    // Ask their owner first: passing such a pointer to the EOS SDK is invalid.
    const auto coop=GetModuleHandleW(L"EDF6Coop.dll");
    using VirtualCheck=int(*)(void*);
    const auto check=coop ? reinterpret_cast<VirtualCheck>(GetProcAddress(coop,"EDF6Coop_AllForcesVirtualRoom")) : nullptr;
    const int virtualRoom=o && check ? check(o->details) : -1;
    if(virtualRoom==1)return join(h,o,client,cb);
    Attribute* attr=nullptr;
    const KeyOptions key{1,"SEARCH_TYPE"};
    const bool allowed=virtualRoom<0 && o && rawCopy(o->details,&key,&attr)==0 && attr && Type(attr->data) && AllForcesWireType(attr->data->value);
    if(attr)release(attr);
    if(allowed)return join(h,o,client,cb);
    // EOS_InvalidParameters. No original JoinLobby call and therefore no
    // game session/resource loading, including direct invitations to vanilla.
    Refuse(client,cb);
}
void** Slot(const char* name) noexcept {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    const auto rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(!rva)return nullptr;
    for(auto p=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image+rva);p->Name;++p) {
        if(_stricmp(reinterpret_cast<const char*>(image+p->Name),"EOSSDK-Win64-Shipping.dll") || !p->OriginalFirstThunk)continue;
        auto names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(image+p->OriginalFirstThunk);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+p->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            const auto* byName=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image+names->u1.AddressOfData);
            if(!std::strcmp(reinterpret_cast<const char*>(byName->Name),name))return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}
bool InstallHooks() noexcept {
    ready.store(false,std::memory_order_release);
    // Install entry gates first and leave them resident if any subsequent setup
    // fails. A half-installed namespace must never publish/join a vanilla room.
    const char* names[]={"EOS_Lobby_CreateLobby","EOS_Lobby_JoinLobby","EOS_LobbyModification_AddAttribute","EOS_LobbySearch_SetParameter","EOS_LobbyDetails_CopyAttributeByIndex"};
    void* hooks[]={reinterpret_cast<void*>(&CreateRoom),reinterpret_cast<void*>(&JoinRoom),reinterpret_cast<void*>(&Add),reinterpret_cast<void*>(&Filter),reinterpret_cast<void*>(&CopyAttribute)};
    void** next[]={reinterpret_cast<void**>(&create),reinterpret_cast<void**>(&join),reinterpret_cast<void**>(&add),reinterpret_cast<void**>(&filter),reinterpret_cast<void**>(&copy)};
    void** slots[5]{};
    for(unsigned i=0;i<2;++i)if(!(slots[i]=Slot(names[i])))return false;
    for(unsigned i=0;i<5;++i) {
        if(i>=2 && !(slots[i]=Slot(names[i])))return false;
        if(edf::ChainVtableSlot(slots[i],hooks[i],next[i]))continue;
        for(unsigned j=2;j<i;++j)edf::PatchVtableSlot(slots[j],hooks[j],*next[j]);
        return false;
    }
    if(!rawCopy || !release)return false;
    ready.store(true,std::memory_order_release);
    return true;
}
}
bool InstallModRoom() noexcept {
    const auto eos=GetModuleHandleW(L"EOSSDK-Win64-Shipping.dll");
    if(!image)return false;
    rawCopy=eos ? reinterpret_cast<CopyKey>(GetProcAddress(eos,"EOS_LobbyDetails_CopyAttributeByKey")) : nullptr;
    release=eos ? reinterpret_cast<Release>(GetProcAddress(eos,"EOS_Lobby_Attribute_Release")) : nullptr;
    return InstallHooks();
}
}
// Coop reads EOS directly as well as through EDF's IAT. Its read side uses this
// bridge, never the presence of the resource files or a permissive metadata flag.
extern "C" __declspec(dllexport) std::int64_t EDF6AF_DecodeRoomType(std::int64_t value) noexcept {
    return crew::DecodeAllForcesRoom(value);
}
extern "C" __declspec(dllexport) bool EDF6AF_RoomIsolationReady() noexcept {
    return crew::ready.load(std::memory_order_acquire);
}
