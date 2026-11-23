// 整备界面武器页的游戏侧：weapon_equip.h（逆向：docs/weapon-select-re.md）。
#include "weapon_equip.h"
#include "code_signatures.h"
#include "crew.h"
#include "memory.h"
#include "weapon_records.h"
#include <cstdint>
#include <cwchar>
#include <memory>
#include <vector>

namespace crew::wequip {
namespace {
// EDF.dll 0x678CCB46 RVAs（weapon_equip_signatures.inc 逐一核对）。
constexpr std::size_t kGameStatus=0x20B2890;      // GS*
constexpr std::size_t kRowCount=0xE23F0,kGetRow=0xE1CF0,kFindRow=0xE1BA0,kLevelCap=0xD8850;
constexpr std::size_t kGsTable=0x130,kTableRows=0x188;   // cfg = GS+0x130；[cfg+0x188] 为空：表未载入
constexpr std::size_t kGsCapOn=0x14FC8;           // 字节：等级上限生效（0x8BC863）
constexpr std::size_t kGsClass=0x6E90,kGsEquipped=0x6E98,kGsPlayerStride=0x3E60,kGsClassStride=0x18;
constexpr std::size_t kRowName=0x00,kRowCategory=0x10,kRowLevel=0x20;
constexpr int kClasses=4;

using RowCountFn=std::uint32_t(__fastcall*)(void*);
using GetRowFn=void*(__fastcall*)(void*,void*,std::uint32_t);
using FindRowFn=int(__fastcall*)(void*,const wchar_t*);
using LevelCapFn=int(__fastcall*)(void*);

using Signature=CodeSignature;
#include "weapon_equip_signatures.inc"

bool ready=false;

unsigned char* Gs() noexcept { return At<unsigned char*>(image,kGameStatus); }
// cfg（表已载入）或空。
void* Table(unsigned char* gs) noexcept {
    if(!gs || !Readable(gs+kGsTable+kTableRows,8) || !At<void*>(gs+kGsTable,kTableRows))return nullptr;
    return gs+kGsTable;
}
int RowCountRaw() noexcept {
    __try {
        void* cfg=Table(Gs());
        return cfg ? static_cast<int>(reinterpret_cast<RowCountFn>(image+kRowCount)(cfg)) : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
bool GetRowRaw(int row,unsigned char* buffer) noexcept {
    __try {
        void* cfg=Table(Gs());
        if(!cfg || row<0 || static_cast<std::uint32_t>(row)>=reinterpret_cast<RowCountFn>(image+kRowCount)(cfg))return false;
        reinterpret_cast<GetRowFn>(image+kGetRow)(cfg,buffer,static_cast<std::uint32_t>(row));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
int LevelCapRaw() noexcept {
    __try {
        unsigned char* gs=Gs();
        if(!gs || !Readable(gs+kGsCapOn,1) || !At<unsigned char>(gs,kGsCapOn))return -1;
        const int cap=reinterpret_cast<LevelCapFn>(image+kLevelCap)(gs);
        return cap<0 ? -1 : cap;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
int* EquippedCell(int who,int cls,int slot) noexcept {
    unsigned char* gs=Gs();
    if(!gs || who<0 || who>=kPlayers || cls<0 || cls>=kClasses || slot<0 || slot>=wlist::kSlotsMost)return nullptr;
    unsigned char* at=gs+kGsEquipped+static_cast<std::size_t>(who)*kGsPlayerStride+static_cast<std::size_t>(cls)*kGsClassStride+
                      static_cast<std::size_t>(slot)*4;
    return reinterpret_cast<int*>(at);
}
}  // namespace

bool InstallWeaponEquip() noexcept {
    ready=CheckSignatures(kSignatures,"WEQUIP");
    Log("WEQUIP ready=%d",ready);
    return ready;
}
bool Ready() noexcept { return ready; }

int RowCount() noexcept { return ready ? RowCountRaw() : -1; }

int FindRow(const wchar_t* id) noexcept {
    if(!ready || !id)return -1;
    __try {
        void* cfg=Table(Gs());
        return cfg ? reinterpret_cast<FindRowFn>(image+kFindRow)(cfg,id) : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}

bool ReadRow(int row,RowFacts* out) noexcept {
    if(!ready || !out)return false;
    alignas(8) unsigned char buffer[0x100]{};
    if(!GetRowRaw(row,buffer))return false;
    *out=RowFacts{};
    out->category=At<std::int32_t>(buffer,kRowCategory);
    out->level=At<std::int32_t>(buffer,kRowLevel);
    const auto name=At<const wchar_t*>(buffer,kRowName);
    for(std::size_t i=0;name && i+1<std::size(out->id) && Readable(name+i,sizeof(wchar_t)) && name[i];++i)
        out->id[i]=name[i];
    return true;
}

bool Owned(int row) noexcept {
    if(!ready || row<0)return false;
    unsigned char* gs=Gs();
    if(!gs)return false;
    unsigned char* rec=WeaponRecord(gs,static_cast<std::uint32_t>(row));
    return rec && Readable(rec,4) && (At<std::uint32_t>(rec,0)&1u);
}

int LevelCap() noexcept { return ready ? LevelCapRaw() : -1; }

int CurrentClass(int who) noexcept {
    unsigned char* gs=ready?Gs():nullptr;
    if(!gs || who<0 || who>=kPlayers)return -1;
    unsigned char* at=gs+kGsClass+static_cast<std::size_t>(who)*kGsPlayerStride;
    if(!Readable(at,4))return -1;
    const int cls=At<std::int32_t>(at,0);
    return cls>=0 && cls<kClasses ? cls : -1;
}

bool ReadEquipped(int who,int cls,std::array<int,wlist::kSlotsMost>* out) noexcept {
    if(!ready || !out)return false;
    int* cells=EquippedCell(who,cls,0);
    if(!cells || !Readable(cells,sizeof(int)*wlist::kSlotsMost))return false;
    for(std::size_t i=0;i<out->size();++i)(*out)[i]=cells[i];
    return true;
}

bool BuildCatalog(wlist::Catalog* out) {
    const int rows=RowCount();
    if(rows<0 || !out)return false;
    *out=wlist::Catalog::BuildGenerated([](const wchar_t* id){return FindRow(id);});
    for(int row=0;row<rows;++row){
        if(out->ByRow(row))continue;
        RowFacts f;
        if(!ReadRow(row,&f))continue;
        out->AddExtra(row,f.category,f.level,f.id);
    }
    return true;
}

bool ReadLoadout(int who,int cls,wlist::Loadout* out) {
    if(!out)return false;
    const int rows=RowCount();
    if(rows<0 || !ReadEquipped(who,cls,&out->equipped))return false;
    auto bits=std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(rows));
    for(int row=0;row<rows;++row)(*bits)[static_cast<std::size_t>(row)]=Owned(row)?1:0;
    out->owned=[bits](int row){return row>=0 && static_cast<std::size_t>(row)<bits->size() && (*bits)[static_cast<std::size_t>(row)];};
    out->levelCap=LevelCap();
    return true;
}

EquipResult Equip(int who,int cls,int slot,int row) noexcept {
    EquipResult r;
    std::array<int,wlist::kSlotsMost> equipped{};
    RowFacts f;
    if(!ready || !ReadRow(row,&f) || !ReadEquipped(who,cls,&equipped))return r;
    r.verdict=wlist::CheckEquipRow(wlist::kWeaponSlotRules,std::size(wlist::kWeaponSlotRules),cls,slot,row,f.category,f.level,
                                   Owned(row),LevelCap(),equipped);
    if(r.verdict!=wlist::Verdict::ok)return r;
    int* cell=EquippedCell(who,cls,slot);
    if(!cell || !Readable(cell,sizeof(int),true))return r;
    *cell=row;
    r.written=true;
    Log("WEQUIP player %d class %d slot %d = row %d (%ls)",who,cls,slot,row,f.id);
    return r;
}

wlist::ClassPlan SetClassSlots(int who,int cls,const std::array<int,wlist::kSlotsMost>& want) noexcept {
    wlist::ClassPlan p;
    p.verdict.fill(wlist::Verdict::absent);
    std::array<int,wlist::kSlotsMost> current{};
    if(!ready || !ReadEquipped(who,cls,&current))return p;
    int* cells=EquippedCell(who,cls,0);
    if(!cells || !Readable(cells,sizeof(int)*wlist::kSlotsMost,true))return p;
    p=wlist::PlanClassSlots(wlist::kWeaponSlotRules,std::size(wlist::kWeaponSlotRules),cls,current,want,LevelCap(),
                            [](int row,int* category,int* level,bool* owned){
                                RowFacts f;
                                if(!ReadRow(row,&f))return false;
                                *category=f.category;*level=f.level;*owned=Owned(row);
                                return true;
                            });
    for(std::size_t s=0;s<current.size();++s)
        if(p.slots[s]!=current[s])cells[s]=p.slots[s];
    Log("WEQUIP player %d class %d: %d slots written",who,cls,p.changed);
    return p;
}

bool SetClass(int who,int cls) noexcept {
    unsigned char* gs=ready?Gs():nullptr;
    if(!gs || who<0 || who>=kPlayers || cls<0 || cls>=kClasses)return false;
    unsigned char* at=gs+kGsClass+static_cast<std::size_t>(who)*kGsPlayerStride;
    if(!Readable(at,4,true))return false;
    Put<std::int32_t>(at,0,cls);
    return true;
}

}  // namespace crew::wequip
