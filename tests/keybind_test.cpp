// The plugins' keys in the game's key store (common/edf/keybind.h) and the key config pages' stubs (src/keyconfig_menu.h),
// offline:
//   - the game codes and virtual keys both ways;
//   - the slot catalog: bytes, pages, rows, each page's keys unique (the stock in-page check would swap them), each
//     slot's default the shipped ini's, every *Key of both inis a slot or listed as not one;
//   - the conflict matrix: every two actions (the plugins' and the stock defaults') on one key in one situation, held
//     against the list below (docs/plan-2026-10-25-keybinds.md §2 / §3.3: known, waiting on the user, or meant);
//   - every stub decoded: its jumps land on the stock jump table, the stock default and its join, its calls on the
//     callbacks, its stack moves balanced and aligned.
//
//   keybind_test <EDF6VehicleCrew.ini> <EDF6AutoTurret.ini>
#include "edf/keybind.h"
#include "keyconfig_menu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace kb=edf::keybind;
namespace km=crew::keymenu;
int failures=0,checks=0;
void Check(bool ok,const std::string& what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what.c_str());}
}
std::string Narrow(const wchar_t* w) { std::string s;for(;*w;++w)s+=static_cast<char>(*w);return s; }

void Codes() {
    for(int c=kb::kKeyFirst;c<=kb::kKeyLast;++c) {
        const int vk=kb::VkOfCode(c);
        Check(vk!=0 && kb::CodeOfVk(vk)==c,"code " + std::to_string(c) + " -> vk -> the same code");
    }
    for(int c=kb::kMouseL;c<=kb::kMouseSide2;++c)
        Check(c==kb::kMouseWheel ? kb::VkOfCode(c)==0 : kb::CodeOfVk(kb::VkOfCode(c))==c,"mouse code " + std::to_string(c));
    Check(kb::VkOfCode(kb::kNoAssign)==0 && kb::VkOfCode(0x05)==0 && kb::VkOfCode(0x79)==0 && kb::VkOfCode(-1)==0,
          "unassigned, a pad button, past the keyboard: no virtual key");
    Check(kb::CodeOfVk(0x10)==0x56 && kb::CodeOfVk(0xA0)==0x56 && kb::VkOfCode(0x56)==0x10,"Shift: either side's VK_SHIFT is ShiftL, read as VK_SHIFT");
    Check(kb::CodeOfVk(0x11)==0x58 && kb::VkOfCode(0x58)==0x11 && kb::VkOfCode(0x59)==0xA3,"Control: left as VK_CONTROL, right as VK_RCONTROL");
    Check(kb::CodeOfVk(0x02)==kb::kMouseR && kb::CodeOfVk(0x04)==kb::kMouseM && kb::CodeOfVk(0)==kb::kNoAssign &&
          kb::CodeOfVk(0xE2)==kb::kNoAssign,"mouse buttons; 0 and a key the game has no code for: unassigned");
    Check(kb::CodeOfVk('Q')==0x22 && kb::CodeOfVk(0x71)==0x41 && kb::CodeOfVk(0xDB)==0x52 && kb::CodeOfVk(0x21)==0x6B,
          "Q, F2, [, PageUp as the menu names them");
    Check(!kb::BindableCode(kb::kMouseWheel) && !kb::BindableCode(3) && !kb::BindableCode(kb::kNoAssign) &&
          kb::BindableCode(kb::kMouseM) && kb::BindableCode(0x63),"the plugin rows take keys and buttons held, not the wheel or a pad");
}

std::map<std::string,std::string> ReadIni(const char* path,const char* section) {
    std::map<std::string,std::string> out;
    std::ifstream f(path);
    Check(static_cast<bool>(f),std::string("read ") + path);
    std::string line,cur;
    while(std::getline(f,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.size()>2 && line[0]=='['){cur=line.substr(1,line.find(']')-1);continue;}
        if(cur!=section || line.empty() || line[0]==';')continue;
        const auto eq=line.find('=');
        if(eq!=std::string::npos)out[line.substr(0,eq)]=line.substr(eq+1);
    }
    return out;
}

void Catalog(const char* crewIni,const char* turretIni) {
    Check(kb::CatalogValid(),"the catalog's own rules");
    std::set<int> codes[kb::kPages];
    for(const kb::Slot& s:kb::kSlots) {
        const int code=kb::CodeOfVk(s.defaultVk);
        Check(code!=kb::kNoAssign,Narrow(s.ini) + ": its default has a game code");
        Check(codes[s.page-kb::kFirstPage].insert(code).second,Narrow(s.ini) + ": its default is the only one on its page");
        Check(s.action>kb::kListEnd && s.action<kb::kMarkerAction[1],Narrow(s.ini) + ": a spare slot");
    }
    for(int p=0;p<kb::kPages;++p)Check(kb::RowsOf(kb::kFirstPage+p)>0 && kb::RowsOf(kb::kFirstPage+p)<=kb::kMaxRows,"page rows fit");
    const auto crew=ReadIni(crewIni,"VehicleCrew"),turret=ReadIni(turretIni,"AutoTurret");
    for(const kb::Slot& s:kb::kSlots) {
        const auto& ini=s.owner==kb::Owner::crew ? crew : turret;
        const auto it=ini.find(Narrow(s.ini));
        Check(it!=ini.end() && std::strtol(it->second.c_str(),nullptr,0)==s.defaultVk,Narrow(s.ini) + ": the shipped ini's default");
    }
    // Every *Key of the inis: a slot, or one of these (not keyboard keys of the menu's kind).
    const std::set<std::string> notSlots={"NpcMarkKey","LockKey",   // the stock 「标记」's key (0: off)
        "DebugSpawnKey","DebugSpawnPrevKey","DebugSpawnNextKey","DebugSpawnCategoryKey","DebugSpawnSpawnKey",   // a dev tool, off by default
        "SeatNumberKeys"};   // on / off of the number keys
    for(const auto* ini:{&crew,&turret})
        for(const auto& [key,value]:*ini) {
            const auto ends=[&](const char* t){ const std::size_t n=std::strlen(t);return key.size()>=n && !key.compare(key.size()-n,n,t); };
            if(!ends("Key") && !ends("Keys"))continue;
            bool slot=false;
            for(const kb::Slot& s:kb::kSlots)slot=slot || Narrow(s.ini)==key;
            Check(slot || notSlots.count(key),key + ": a slot of the key config, or listed as not one");
        }
}

// --- The conflict matrix
enum Ctx : unsigned {
    kFoot=1,kGround=2,kHeli=4,kJet=8,kGunship=16,kDrill=32,kProteus=64,kDread=128,kSazabi=256,kTurret=512,kRoom=1024,kMech=2048,
    kVehicles=kGround|kHeli|kJet|kGunship|kDrill|kProteus|kDread|kSazabi|kTurret|kMech,
};
struct Bind { std::string name; int code; unsigned ctx; int page; };
std::vector<Bind> Binds() {
    std::vector<Bind> b;
    // the stock keyboard defaults (0xD48B0, 0xD4530 flags 0x10/0x20, 0xD52F0, 0xD50D0, 0xD4FD0, 0xD51A0, 0xD4530 0x80 / bit 11;
    // tests/keyconfig_native_audit.py reads them), the common page in every situation; Spot left out (the plugins follow it)
    const auto stock=[&](const char* n,int code,unsigned ctx,int page){b.push_back({n,code,ctx,page});};
    stock("stock Up",0x28,kFoot|kVehicles,0);stock("stock Down",0x24,kFoot|kVehicles,0);
    stock("stock Left",0x12,kFoot|kVehicles,0);stock("stock Right",0x15,kFoot|kVehicles,0);
    stock("stock RideRescue",0x16,kFoot|kVehicles,0);stock("stock Shotcut",0x25,kFoot|kVehicles,0);
    stock("stock Chat",0x58,kFoot|kVehicles,0);stock("stock WeaponShotcut1",0x2D,kFoot,0);
    stock("stock WeaponShotcut2",0x2E,kFoot,0);stock("stock WeaponShotcut3",0x2F,kFoot,0);stock("stock VoiceChat",0x27,kFoot|kVehicles,0);
    stock("stock Attack1",0x0C,kFoot,1);stock("stock Attack2",0x0E,kFoot,1);stock("stock Jump",0x63,kFoot,1);
    stock("stock WeaponSelect",0x0F,kFoot,1);stock("stock Reload",0x23,kFoot,1);stock("stock Dash/Boost",0x56,kFoot,1);
    stock("stock UseSub",0x2B,kFoot,1);stock("stock CallVehicle",0x17,kFoot,1);
    stock("stock DriveAccel",0x28,kGround,5);stock("stock DriveBrake",0x24,kGround,5);stock("stock HandBrake",0x63,kGround,5);
    stock("stock TanksAttack1",0x0C,kGround,5);stock("stock DriveHorn",0x0E,kGround,5);stock("stock TanksAttack2",0x0D,kGround,5);
    stock("stock HeliAttack1",0x0C,kHeli,7);stock("stock HeliAttack2",0x0E,kHeli,7);stock("stock HeliFlight",0x63,kHeli,7);
    // the plugins' slots and where each is read (docs/plan-2026-10-25-keybinds.md §1)
    const unsigned ctx[kb::kSlotCount]={
        kFoot,kFoot,kFoot,kFoot|kVehicles,kFoot|kVehicles,kFoot|kVehicles,kFoot|kVehicles,kFoot|kVehicles,kRoom,kFoot,kTurret,
        kVehicles&~kSazabi,kGround,kGround|kTurret,kGround|kTurret,kGround|kTurret,kVehicles,kVehicles,kVehicles,kGround|kProteus,kFoot,kGunship,
        kJet,kJet|kHeli,kJet,kJet,kJet,kDrill,kProteus,kProteus,kProteus,
        kDread,kDread,kDread,kDread,kDread,kDread,
        kSazabi,kSazabi,kSazabi,kSazabi,kSazabi,kSazabi,kSazabi};
    for(int i=0;i<kb::kSlotCount;++i)b.push_back({Narrow(kb::kSlots[i].ini),kb::CodeOfVk(kb::kSlots[i].defaultVk),ctx[i],kb::kSlots[i].page});
    return b;
}
// Two actions on one key where both are read (as the defaults ship). Each one is a known overlap: the user's 「好多重叠」
// (docs/feedback-2026-10-25-keybinds.md) waits on §3.3, or the two are meant to go together. A new pair fails here.
const char* const kKnown[]={
    // the common page's Shotcut T / Chat Ctrl / VoiceChat V / RideRescue E against the plugins' (plan §2 #17, now seen)
    "stock Shotcut|NpcFormationKey","stock Shotcut|VehicleSecondarySwitchKey","stock Shotcut|ProteusModeKey",
    "stock VoiceChat|FirstPersonKey","stock VoiceChat|GunshipGunnerKey","stock VoiceChat|SazabiMeleeKey",
    "stock Chat|PlayerJetBrakeKey","stock Chat|DreadnoughtDescendKey","stock Chat|SazabiDescendKey",
    "stock RideRescue|DreadnoughtStrafeRightKey","stock RideRescue|SazabiCannonKey",
    // on foot: the stock Dash on Shift is the jets' boost elsewhere only; the infantry right button: none
    // in a vehicle: the plugins' own across pages
    "FirstPersonKey|GunshipGunnerKey",          // plan §2 #8: V released / held (KeyTaken)
    "FirstPersonKey|SazabiMeleeKey",            // plan §2 #8
    "VehicleSecondarySwitchKey|ProteusModeKey", // plan §2 #4, §3.3 A (ProteusModeKey to G) waits on the user
    "PlayerJetSwitchKey|DrillLaunchKey",        // R: the drill tank's launch and its stores (both on R since the drill's launch)
    "PlayerJetSwitchKey|SazabiFunnelKey",       // R: the family 「武器切换」 (plan §3.2)
    "FreeLookKey|stock TanksAttack2",           // the middle button: the stock vehicles' second weapon vs free look (§1.2)
};

void Matrix() {
    const auto b=Binds();
    std::set<std::string> known(std::begin(kKnown),std::end(kKnown)),seen;
    for(std::size_t i=0;i<b.size();++i)
        for(std::size_t j=i+1;j<b.size();++j) {
            if(b[i].code!=b[j].code || !(b[i].ctx&b[j].ctx))continue;
            if(b[i].page==b[j].page && b[i].page>=kb::kFirstPage)continue;   // one page: unique (Catalog)
            if(b[i].page<kb::kFirstPage && b[j].page<kb::kFirstPage)continue; // the stock's own: its pages' business
            const std::string a=b[i].name+"|"+b[j].name,r=b[j].name+"|"+b[i].name;
            const std::string pair=known.count(a) ? a : known.count(r) ? r : a;
            seen.insert(pair);
            Check(known.count(pair)>0,"an overlap not in the known list: " + a);
        }
    for(const char* k:kKnown)Check(seen.count(k)>0,std::string("a listed overlap no longer happens (drop it from kKnown): ")+k);
    std::printf("conflict matrix: %zu overlaps, all listed\n",seen.size());
}

// --- The stubs
// The instructions the stubs are made of, by their first bytes: length, and (rel32 at) for jumps.
struct Ins { std::size_t len; int kind; std::uint64_t target; std::uint64_t imm; };   // kind 0 plain, 1 jcc, 2 jmp, 3 mov rax imm64, 4 call rax, 5 sub rsp, 6 add rsp
bool Decode(const unsigned char* p,std::size_t left,std::uint64_t at,Ins& o) {
    const auto rel=[&](std::size_t off,std::size_t len) { std::int32_t r;std::memcpy(&r,p+off,4);return at+len+static_cast<std::int64_t>(r); };
    o=Ins{0,0,0,0};
    if(p[0]==0x0F && (p[1]==0x82 || p[1]==0x83 || p[1]==0x86)){o={6,1,rel(2,6),p[1]};}
    else if(p[0]==0xE9)o={5,2,rel(1,5),0};
    else if(p[0]==0x48 && p[1]==0xB8){std::uint64_t v;std::memcpy(&v,p+2,8);o={10,3,0,v};}
    else if(p[0]==0xFF && p[1]==0xD0)o={2,4,0,0};
    else if(p[0]==0x48 && p[1]==0x83 && p[2]==0xEC)o={4,5,0,p[3]};
    else if(p[0]==0x48 && p[1]==0x83 && p[2]==0xC4)o={4,6,0,p[3]};
    else if(p[0]==0x83 && (p[1]==0xF8 || p[1]==0xFA))o.len=3;
    else if(p[0]==0x41 && p[1]==0x83)o.len=4;
    else if(p[0]==0x8B && p[1]==0xD8)o.len=2;
    else if(p[0]==0x48 && p[1]==0x8D && p[2]==0x0D)o.len=7;
    else if(p[0]==0x8B && p[1]==0x4C && p[2]==0x81)o.len=4;
    else if(p[0]==0x89 && p[1]==0x8E)o.len=6;
    else if((p[0]==0x89 || p[0]==0x8B) && p[1]==0x4C && p[2]==0x24)o.len=4;
    else if(p[0]==0x89 && p[1]==0x44 && p[2]==0x24)o.len=4;
    else if(p[0]==0x89 && p[1]==0xC1)o.len=2;
    else if(p[0]==0x8B && p[1]==0x12)o.len=2;
    else if((p[0]==0x44 && p[1]==0x89) || (p[0]==0x45 && p[1]==0x31) || (p[0]==0x41 && p[1]==0x8B) || (p[0]==0x49 && p[1]==0x89) ||
            (p[0]==0x48 && p[1]==0x8B))o.len=3;
    else if(p[0]==0x4C && (p[1]==0x8D || p[1]==0x8B))o.len=5;
    else if(p[0]==0x8A && p[1]==0x5C)o.len=4;
    return o.len && o.len<=left;
}
void Stubs() {
    const std::uint64_t base=0x7FF600000000ull,stubAt=0x7FF5F0000000ull;
    km::Callbacks cb{0x1111222233334444ull,0x5555666677778888ull,0x9999AAAABBBBCCCCull};
    for(int i=0;i<6;++i) {
        const km::Dispatch& d=km::kDispatches[i];
        km::Code c;c.at=stubAt+i*km::kStubSlot;
        km::BuildStub(i,c,base,cb);
        const std::string n=d.name;
        Check(c.ok && c.n<=km::kStubSlot,n + ": built, within its slot");
        std::vector<Ins> ins;
        std::size_t off=0;
        const std::size_t end=i==1 ? c.n-4*kb::kPages : c.n;   // the row count's rows follow its code
        while(off<end) {
            Ins o;
            if(!Decode(c.b+off,end-off,c.at+off,o)){Check(false,n + ": decodes at +" + std::to_string(off));break;}
            ins.push_back(o);off+=o.len;
        }
        Check(off==end,n + ": decodes to its end");
        std::vector<std::uint64_t> jcc;
        int calls=0,stack=0;
        bool aligned=true;
        std::set<std::uint64_t> fns;
        for(std::size_t k=0;k<ins.size();++k) {
            if(ins[k].kind==1)jcc.push_back(ins[k].target);
            if(ins[k].kind==3 && k+1<ins.size() && ins[k+1].kind==4){++calls;fns.insert(ins[k].imm);aligned=aligned && stack%16==0 && stack>=0x20;}
            if(ins[k].kind==5)stack+=static_cast<int>(ins[k].imm);
            if(ins[k].kind==6)stack-=static_cast<int>(ins[k].imm);
        }
        Check(jcc.size()==3 && jcc[0]==base+d.back && jcc[1]==base+d.def && jcc[2]==base+d.def,
              n + ": pages 0..10 to the stock jump table, outside 11..15 the stock default");
        Check(!ins.empty() && ins.back().kind==2 && ins.back().target==base+d.join,n + ": the plugin's page to the stock join");
        Check(stack==0 && aligned,n + ": its stack moves balanced, each call with aligned shadow space");
        const int want[]={0,0,1,2,1,1};
        Check(calls==want[i],n + ": " + std::to_string(want[i]) + " callback call(s)");
        if(i==2)Check(fns==std::set<std::uint64_t>{cb.label},n + ": the label");
        if(i==3)Check(fns==std::set<std::uint64_t>{cb.value,cb.label},n + ": the value and the label");
        if(i==4)Check(fns==std::set<std::uint64_t>{cb.dialog},n + ": the dialog's row");
        if(i==5)Check(fns==std::set<std::uint64_t>{cb.value},n + ": the value");
        if(i==1) {
            const std::size_t lea=3+6+3+6+3+6;   // after the head: lea rcx,[rip+rows]
            std::int32_t disp;std::memcpy(&disp,c.b+lea+3,4);
            Check(lea+7+static_cast<std::size_t>(disp)==c.n-4*kb::kPages,"row count: the lea finds its rows");
            for(int p=0;p<kb::kPages;++p) {
                std::int32_t rows;std::memcpy(&rows,c.b+c.n-4*kb::kPages+4*p,4);
                Check(rows==kb::RowsOf(kb::kFirstPage+p),"row count: page " + std::to_string(kb::kFirstPage+p));
            }
        }
        unsigned char jump[16];
        Check(km::SiteJump(base+d.site,c.at,d.size,jump) && jump[0]==0xE9,n + ": the site's jump");
        for(std::size_t k=5;k<d.size;++k)Check(jump[k]==0xCC,n + ": the site's rest int3");
    }
    unsigned char distant[16];
    Check(!km::SiteJump(0x10000000ull,0x7FF600000000ull,5,distant),"a stub out of rel32 reach: no jump");
    // the page table
    unsigned char stock[km::kStockRows*km::kRowBytes];
    for(std::size_t k=0;k<sizeof(stock);++k)stock[k]=static_cast<unsigned char>(k*7+1);
    unsigned char table[km::kRows*km::kRowBytes];
    km::BuildTable(stock,table);
    Check(!std::memcmp(table,stock,sizeof(stock)),"the table: the stock rows as read");
    for(int p=0;p<kb::kPages;++p) {
        const unsigned char* row=table+(km::kStockRows+p)*km::kRowBytes;
        const int n=kb::RowsOf(kb::kFirstPage+p);
        bool ok=row[n]==kb::kListEnd;
        for(int r=0;r<n;++r)ok=ok && row[r]==kb::SlotAt(kb::kFirstPage+p,r)->action;
        for(std::size_t r=n+1;r<km::kRowBytes;++r)ok=ok && row[r]==0;
        Check(ok,"the table: page " + std::to_string(kb::kFirstPage+p) + "'s actions, 0x4E, zeros");
    }
    unsigned char lea[km::kLeaSize];
    const unsigned char stockLea[km::kLeaSize]={0x48,0x8D,0x05,1,2,3,4};
    Check(km::LeaTo(0x7FF600000000ull,stockLea,0x7FF600001007ull,lea) && lea[0]==0x48 && lea[2]==0x05 && lea[3]==0x00 && lea[4]==0x10,
          "a lea pointed at the new table");
    Check(km::kSyncedRowsOurs[2]==km::kLastPage-5,"the all-sets rows: 5..15");
}

// The store's helpers on a fake store.
void Store() {
    std::vector<unsigned char> store(kb::kStoreSize,0);
    Check(!kb::Seeded(store.data(),kb::Owner::crew),"a fresh store: not seeded");
    kb::Seed(store.data(),kb::Owner::crew,[](const kb::Slot& s){ return s.defaultVk==0x54 ? 0 : s.defaultVk; });
    Check(kb::Seeded(store.data(),kb::Owner::crew) && !kb::Seeded(store.data(),kb::Owner::turret),"seeded: its marker only");
    bool all=true;
    for(const kb::Slot& s:kb::kSlots)
        for(int set=0;set<kb::kSyncedSets;++set) {
            const std::int32_t v=*kb::SlotPtr(store.data(),0,set,s.action);
            all=all && v==(s.owner!=kb::Owner::crew || s.defaultVk==0x54 ? 0 : kb::CodeOfVk(s.defaultVk));
        }
    Check(all,"seeded: the owner's slots in the four sets, an ini 0 unassigned, the other owner's untouched");
    Check(kb::SlotVk(store.data(),0x53)=='M' && kb::SlotVk(store.data(),0x4F)==0,"read back as virtual keys");
    *kb::SlotPtr(store.data(),0,0,0x53)=0x200;
    Check(kb::SlotVk(store.data(),0x53)==0,"a code no menu writes: no key");
    Check(*kb::SlotPtr(store.data(),1,0,0x53)==0,"player 1 untouched");
}
}  // namespace

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=3){std::printf("usage: keybind_test <EDF6VehicleCrew.ini> <EDF6AutoTurret.ini>\n");return 2;}
    Codes();
    Catalog(argv[1],argv[2]);
    Matrix();
    Stubs();
    Store();
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
