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

// 原版说明栏（0xD8A90）用的：WEAPONTEXT 文字表句柄 GS+0xD0 {doc*, u32 节点}，空时 0xD9350(GS) 载入；SGO 节点访问。
constexpr std::size_t kGsText=0xD0;
constexpr std::size_t kNodeChild=0x52DE0,kNodeDouble=0x52F50,kNodeCount=0x53060,kNodeString=0x551E0,kNodeIsArray=0xD91C0;
constexpr std::size_t kSpecEval=0xD7CE0,kTextLoad=0xD9350;
constexpr std::size_t kRecordStars=4;             // WeaponRecord：u32 flags，8 个星级字节

struct Node { void* doc; std::uint32_t index; std::uint32_t pad; };
using NodeChildFn=std::uint32_t(__fastcall*)(const Node*,std::uint32_t);
using NodeDoubleFn=double(__fastcall*)(const Node*);
using NodeCountFn=std::uint32_t(__fastcall*)(const Node*);
using NodeStringFn=const wchar_t*(__fastcall*)(const Node*);
using NodeIsArrayFn=std::uint32_t(__fastcall*)(const Node*);   // 低字节
using SpecEvalFn=void*(__fastcall*)(void* out,const Node*,const unsigned char* stars);
using TextLoadFn=void(__fastcall*)(void*);

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
// ---- 说明栏属性（0xD8A90 的读法）。SEH 里只碰 POD，字符串复制出来再转成 wlist 类型。 ----
constexpr std::uint32_t kSpecLinesMost=48,kSpecParamsMost=10;   // $0..$9：0xD9B80 只认一位数字
constexpr std::size_t kSpecTextMost=256;
struct RawParam { float value; std::uint32_t type; std::uint8_t star,maxStar; std::uint32_t isFloat; int curve; };
struct RawLine {
    wchar_t label[kSpecTextMost],format[kSpecTextMost];
    std::uint32_t nodes,params;
    RawParam param[kSpecParamsMost];
};
struct RawSpec { std::uint32_t lines; RawLine line[kSpecLinesMost]; };

void CopyWide(wchar_t* to,const wchar_t* from) noexcept {
    std::size_t i=0;
    for(;from && i+1<kSpecTextMost && from[i];++i)to[i]=from[i];
    to[i]=0;
}
Node Child(const Node& n,std::uint32_t i) noexcept {
    return Node{n.doc,reinterpret_cast<NodeChildFn>(image+kNodeChild)(&n,i),0};
}
// 0xD7CE0 的取整：|x| 加 0.5 截断，符号跟 x。
int RoundNode(double v) noexcept {
    const int r=static_cast<int>(static_cast<long long>((v<0 ? -v : v)+0.5));
    return v<0 ? -r : r;
}
bool ReadSpecRaw(int row,RawSpec* out) noexcept {
    __try {
        unsigned char* gs=Gs();
        if(!gs || row<0)return false;
        unsigned char* rec=WeaponRecord(gs,static_cast<std::uint32_t>(row));
        if(!rec || !Readable(rec,4+8))return false;
        const unsigned char* stars=rec+kRecordStars;
        if(!At<void*>(gs,kGsText))reinterpret_cast<TextLoadFn>(image+kTextLoad)(gs);   // 同 0xD8ACF..0xD8AE0
        const Node table{At<void*>(gs,kGsText),At<std::uint32_t>(gs,kGsText+8),0};
        if(!table.doc)return false;
        const auto count=reinterpret_cast<NodeCountFn>(image+kNodeCount);
        const auto str=reinterpret_cast<NodeStringFn>(image+kNodeString);
        const auto dbl=reinterpret_cast<NodeDoubleFn>(image+kNodeDouble);
        const auto isArray=reinterpret_cast<NodeIsArrayFn>(image+kNodeIsArray);
        const auto eval=reinterpret_cast<SpecEvalFn>(image+kSpecEval);
        if(static_cast<std::uint32_t>(row)>=count(&table))return false;   // 原版 0xD8A90 开头同样：row >= 项数不显示
        const Node entry=Child(table,static_cast<std::uint32_t>(row));
        if(count(&entry)<3)return false;
        const Node props=Child(entry,2);
        const std::uint32_t lines=count(&props);
        if(lines>kSpecLinesMost){Log("WEQUIP row %d: %u spec lines, more than %u",row,lines,kSpecLinesMost);return false;}
        out->lines=lines;
        for(std::uint32_t i=0;i<lines;++i){
            RawLine& ln=out->line[i];
            const Node prop=Child(props,i);
            ln.nodes=count(&prop);
            ln.label[0]=ln.format[0]=0;
            ln.params=0;
            if(ln.nodes>=1){const Node n=Child(prop,0);CopyWide(ln.label,str(&n));}
            if(ln.nodes>=2){const Node n=Child(prop,1);CopyWide(ln.format,str(&n));}
            for(std::uint32_t k=2;k<ln.nodes && ln.params<kSpecParamsMost;++k){
                const Node param=Child(prop,k);
                alignas(16) unsigned char result[32]{};
                eval(result,&param,stars);
                RawParam& p=ln.param[ln.params++];
                p.value=At<float>(result,0);
                p.type=At<std::uint32_t>(result,4);
                p.star=At<std::uint8_t>(result,8);
                p.maxStar=At<std::uint8_t>(result,9);
                p.isFloat=At<std::uint32_t>(result,0xC);
                p.curve=-1;
                if(isArray(&param)&0xFFu){
                    const Node mode=Child(param,1);
                    const int c=RoundNode(dbl(&mode))&3;
                    if(c==0 || c==1)p.curve=c;
                }
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
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

wlist::Badge Badge(int row) noexcept {
    if(!ready || row<0)return wlist::Badge::none;
    unsigned char* gs=Gs();
    unsigned char* rec=gs ? WeaponRecord(gs,static_cast<std::uint32_t>(row)) : nullptr;
    return rec && Readable(rec,4) ? wlist::BadgeOf(At<std::uint32_t>(rec,0)) : wlist::Badge::none;
}

bool ReadSpec(int row,wlist::WeaponSpec* out) {
    if(!ready || !out || row<0 || row>=RowCount())return false;
    auto raw=std::make_unique<RawSpec>();
    if(!ReadSpecRaw(row,raw.get()))return false;
    out->clear();
    for(std::uint32_t i=0;i<raw->lines;++i){
        const RawLine& r=raw->line[i];
        wlist::SpecLine ln;
        ln.label=r.label;ln.format=r.format;ln.nodes=static_cast<int>(r.nodes);
        for(std::uint32_t k=0;k<r.params;++k){
            const RawParam& q=r.param[k];
            wlist::SpecParam p;
            p.value=q.value;p.type=static_cast<int>(q.type);p.star=q.star;p.maxStar=q.maxStar;p.isFloat=q.isFloat!=0;
            p.curve=q.curve;
            ln.params.push_back(p);
        }
        out->push_back(std::move(ln));
    }
    return true;
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
    // 低位：持有；高两位：wlist::Badge
    auto bits=std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(rows));
    for(int row=0;row<rows;++row)
        (*bits)[static_cast<std::size_t>(row)]=static_cast<std::uint8_t>((Owned(row)?1:0)|(static_cast<unsigned>(Badge(row))<<1));
    auto at=[bits](int row)->std::uint8_t{
        return row>=0 && static_cast<std::size_t>(row)<bits->size() ? (*bits)[static_cast<std::size_t>(row)] : 0;
    };
    out->owned=[at](int row){return (at(row)&1u)!=0;};
    out->badge=[at](int row){return static_cast<wlist::Badge>(at(row)>>1);};
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
