// src/sgo_build_logic.h and src/soldier_classes_logic.h: SGO node blocks read back the way the game's accessors read
// them (docs/newclass-re/inject.md 1.2: E34D0 / E3550 / E3580 / E34E0 / E3470 / E34A0 / E3680 / E07D0 / E3420 / E1B60 /
// E3520 / E2450 and the category loop 0xE2C50), the classes 4 / 5 against the plan's contract, the refusals, the play
// count stub.
//
//   soldier_classes_test                       the checks
//   soldier_classes_test --extend IN.SGO OUT   IN's SoldierInit / SoldierWeaponCategory through the plugin's extension,
//                                              written as an SGO (tests/soldier_classes_native_audit.py reads it with
//                                              pylib/sgo.py)
#include "sgo_build_logic.h"
#include "soldier_classes_logic.h"
#include "soldier_classes.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace crew;
using namespace crew::sgob;

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) {
        ++failures;
        std::printf("FAIL %s\n",what);
    }
}

// The game's arithmetic, written out (not sgob::Read): a node {type, a, rel}, rel signed from the node itself.
std::int32_t I32(const unsigned char* p) {
    std::int32_t v;std::memcpy(&v,p,4);
    return v;
}
float F32(const unsigned char* p) {
    float v;std::memcpy(&v,p,4);
    return v;
}
const unsigned char* Kids(const unsigned char* node) { return node+I32(node+8); }   // movsxd [x+8]; add
const unsigned char* Kid(const unsigned char* node,int i) {
    if(I32(node)!=kArray || i<0 || i>=I32(node+4))return nullptr;
    return Kids(node)+i*12;
}
std::wstring Text(const unsigned char* node) {
    if(!node || I32(node)!=kString)return L"<not a string>";
    const auto* s=reinterpret_cast<const wchar_t*>(node+I32(node+8));
    std::wstring out(s);   // up to its NUL, as the game's wide string reads do
    if(out.size()!=static_cast<std::size_t>(I32(node+4)))return L"<length differs from a>";
    return out;
}

// The accessors on a SoldierInit node (`init` = what the redirected 0x528A0 returns, kept at cfg+0x20).
struct Accessors {
    const unsigned char* init;
    int Count() const { return I32(init+4); }                                           // E34D0
    const unsigned char* C(int cls) const { const unsigned char* e=Kids(init)+cls*12; return Kids(e); }
    int Type(int cls) const { return I32(C(cls)+8); }                                   // E3550
    std::wstring Name(int cls) const { return Text(C(cls)+0xC); }                       // E3580
    std::wstring Battle(int cls,int outfit) const { return Text(Kid(C(cls)+0x18,outfit)); }   // E34E0
    float HpBase(int cls) const { return F32(Kids(C(cls)+0x24)+8); }                    // E3470: [3][0]
    float HpPerArmor(int cls) const { return F32(Kids(C(cls)+0x24)+12+8); }             // E34A0: [3][1]
    int Slots(int cls) const { return I32(C(cls)+0x30+4); }                             // E3680: [C+0x34]
    const unsigned char* Slot(int cls,int s) const { return Kids(Kid(C(cls)+0x30,s)); } // E2450
    const unsigned char* Look(int cls) const { return C(cls)+0x3C; }                   // [5]
    std::wstring CustomColor(int cls) const { return Text(Kid(Look(cls),0)); }          // E07D0: [5][0]
    std::wstring Menu(int cls,int outfit) const { return Text(Kid(Kid(Look(cls),1),outfit)); }   // E3420: [5][1][outfit]
    std::wstring Rab(int cls) const { return Text(Kid(Look(cls),2)); }                  // E1B60: [5][2]
    int Images(int cls) const { return I32(Kid(Look(cls),3)+4); }                       // E3520: count of [5][3]
};

std::wstring W(const char* s) { return std::wstring(s,s+std::strlen(s)); }

// A stock-like SoldierInit (CONFIG.SGO's layout; the values are made up but the Ranger's HP).
Value StockElement(int type,const char* name,int slots) {
    std::vector<Value> battle,menu,dds,slotList;
    for(int i=0;i<4;++i) {
        battle.push_back(Str(W("app:/object/") + W(name) + L"_0" + static_cast<wchar_t>(L'1'+i) + L".sgo"));
        menu.push_back(Str(W("APP:/MENUOBJECT/") + W(name) + L"_0" + static_cast<wchar_t>(L'1'+i) + L".SGO"));
        dds.push_back(Str(W("SoldierType_") + W(name) + L"_0" + static_cast<wchar_t>(L'1'+i) + L".dds"));
    }
    for(int s=0;s<slots;++s)
        slotList.push_back(Array({Str(L"Weapon_Slot1"),Str(L"eDefault"),Array({Int(type*10),Int(type*10+1)}),Int(0),Int(s==slots-1)}));
    return Array({Int(type),Str(W("SoldierType_")+W(name)),Array(std::move(battle)),Array({Float(200.0f),Float(0.56f)}),
                  Array(std::move(slotList)),Array({Str(L"App:/DefaultPackage/CustomColor.sgo"),Array(std::move(menu)),
                                                    Str(L"App:/UI/TypeSelectImage.rab"),Array(std::move(dds))})});
}
Value StockInit() {
    return Array({StockElement(0,"Ranger",4),StockElement(1,"WingDiver",4),StockElement(2,"AirRaider",5),StockElement(3,"Fencer",6)});
}
Value StockCategories() {
    std::vector<Value> rows;
    for(int i=0;i<4;++i)rows.push_back(Array({Int(i),Str(W("Weapon_Cat")+static_cast<wchar_t>(L'0'+i))}));
    rows.push_back(Array({Int(334),Str(L"")}));   // an empty name: a = 0
    return Array(std::move(rows));
}

void TestBuilder() {
    Value tree=Array({Int(-7),Float(1.5f),Str(L"abc"),Str(L""),Raw({1,2,3}),Array({}),Array({Str(L"abc"),Array({Int(1)})})});
    const std::vector<unsigned char> block=Write(tree);
    Check(!block.empty() && block.size()%16==0,"a block is laid out, padded to 16");
    const unsigned char* root=block.data();
    Check(I32(root)==kArray && I32(root+4)==7 && I32(root+8)==12,"the root array's children follow it");
    Check(I32(Kid(root,0)+8)==-7,"int at +8");
    Check(F32(Kid(root,1)+8)==1.5f,"float at +8");
    Check(Text(Kid(root,2))==L"abc","string at node + rel, NUL-terminated");
    Check(I32(Kid(root,3)+4)==0 && Text(Kid(root,3)).empty(),"an empty string: a = 0, rel at a real NUL");
    Check(Kids(Kid(root,2))==Kids(Kid(Kid(root,6),0)),"equal strings are one");
    const unsigned char* raw=Kid(root,4);
    Check(I32(raw)==kRaw && I32(raw+4)==3 && (Kids(raw)-root)%16==0 && Kids(raw)[2]==3,"raw block, 16-aligned");
    Check(I32(Kid(root,5))==kArray && I32(Kid(root,5)+4)==0,"empty array");
    Value back;
    Check(Read(root,back) && back==tree,"Read(Write(tree)) == tree");
    // position independent: the same tree from a copy anywhere
    std::vector<unsigned char> moved(block.size()+40,0xCC);
    std::memcpy(moved.data()+40,block.data(),block.size());
    Check(Read(moved.data()+40,back) && back==tree,"the block read from another address");
    // a negative rel (a node after what it points at) reads as the game reads it
    std::vector<unsigned char> neg(48,0);
    const std::int32_t s[3]={kString,1,-12},n[3]={kInt,4,9};
    std::memcpy(neg.data()+24,s,12);
    neg[12]='x';
    std::memcpy(neg.data()+36,n,12);
    Check(Read(neg.data()+24,back) && back.text==L"x","rel is signed");
    const std::int32_t bad[3]={5,0,0};
    std::memcpy(neg.data(),bad,12);
    Check(!Read(neg.data(),back),"an unknown node type is refused");
    Limits small;small.depth=1;
    Check(!Read(root,back,small),"a tree past the depth limit is refused");
}

void TestClasses() {
    const Value stock=StockInit();
    Value six;
    Check(zwclass::ExtendSoldierInit(stock,six)==nullptr,"the stock four are extended");
    const std::vector<unsigned char> block=Write(six);
    const Accessors a{block.data()};
    Check(a.Count()==kSoldierClassCount && a.Count()==6,"E34D0: six classes");
    for(int c=0;c<4;++c) {
        Check(a.Type(c)==c,"a stock class keeps its type");
        Check(a.Slots(c)==static_cast<int>(stock.items[static_cast<std::size_t>(c)].items[4].items.size()),"a stock class keeps its slots");
    }
    Check(a.Name(0)==L"SoldierType_Ranger" && a.Battle(3,2)==L"app:/object/Fencer_03.sgo","the stock elements are copied whole");
    Check(a.Type(4)==4 && a.Type(5)==5,"E3550: 4 / 5");
    Check(a.Name(4)==L"SoldierType_ZwVehicler" && a.Name(5)==L"SoldierType_ZwRuneBlade","E3580: the name keys");
    Check(a.Battle(4,0)==L"app:/object/zw_vehicler_01.sgo" && a.Battle(4,3)==L"app:/object/zw_vehicler_04.sgo","E34E0: ZwVehicler");
    Check(a.Battle(5,0)==L"app:/object/zw_runeblade_01.sgo" && a.Battle(5,3)==L"app:/object/zw_runeblade_04.sgo","E34E0: ZwRuneBlade");
    Check(a.Menu(4,1)==L"APP:/MENUOBJECT/zw_vehicler_02.SGO" && a.Menu(5,3)==L"APP:/MENUOBJECT/zw_runeblade_04.SGO","E3420: menu SGOs");
    Check(a.CustomColor(4)==L"App:/DefaultPackage/CustomColorZwVehicler.sgo" &&
          a.CustomColor(5)==L"App:/DefaultPackage/CustomColorZwRuneBlade.sgo","E07D0: CustomColor");
    Check(a.Rab(4)==L"App:/UI/TypeSelectImage.rab" && a.Images(4)==4 && a.Images(5)==4,"E1B60 / E3520: the select image");
    Check(a.HpBase(4)==200.0f && a.HpPerArmor(5)==0.56f,"E3470 / E34A0");
    Check(a.Slots(4)==2 && a.Slots(5)==3,"E3680: 2 / 3 slots");
    struct Want {
        int cls,slot;
        const wchar_t* name;
        const wchar_t* def;
        int cat,repeat,support;
    };
    const Want want[]={
        {4,0,L"ZW_SlotVehicle",L"ZW_VehicleNone",400,0,1},{4,1,L"ZW_SlotPistol",L"ZW_Gyrojet01",401,0,0},
        {5,0,L"ZW_SlotMelee",L"ZW_MeleeNone",500,0,0},{5,1,L"ZW_SlotRanged",L"ZW_RuneCarbine01",501,0,0},
        {5,2,L"ZW_SlotSupport",L"ZW_SupportNone",502,0,1},
    };
    for(const Want& w:want) {   // E2450: [0] name, [1] default, [2] categories (+0x18, count +0x1C), [3], [4]
        const unsigned char* s=a.Slot(w.cls,w.slot);
        Check(Text(s)==w.name && Text(s+12)==w.def,"E2450: slot name / default weapon");
        Check(I32(s+0x1C)==1 && I32(Kids(s+0x18)+8)==w.cat,"E2450: the slot's one category");
        Check(I32(s+0x24+8)==w.repeat && I32(s+0x30+8)==w.support,"E2450: repeat / support flags");
    }
    Value back;
    Check(Read(block.data(),back) && back==six,"the block reads back as the tree");
    Check(IsNewClass(4) && IsNewClass(5) && !IsNewClass(3) && !IsNewClass(6) && !IsNewClass(-1),"IsNewClass");
    Check(zwclass::Find(4) && std::wcscmp(zwclass::Find(4)->internalName,L"ZwVehicler")==0 &&
          std::wcscmp(zwclass::Find(5)->internalName,L"ZwRuneBlade")==0 && !zwclass::Find(3),"internal names");
}

void TestCategories() {
    Value out;
    Check(zwclass::ExtendCategories(StockCategories(),out)==nullptr,"the categories are extended");
    const std::vector<unsigned char> block=Write(out);
    const unsigned char* root=block.data();
    Check(I32(root+4)==5+5,"five rows appended");
    const int ids[]={400,401,500,501,502};
    const wchar_t* keys[]={L"WeaponCategory_ZwVehicle",L"WeaponCategory_ZwPistol",L"WeaponCategory_ZwMelee",
                           L"WeaponCategory_ZwRanged",L"WeaponCategory_ZwSupport"};
    for(int i=0;i<5;++i) {   // 0xE2C50: [0] the category, [1] its name key (the map keeps node+rel)
        const unsigned char* row=Kids(Kid(root,5+i));
        Check(I32(row+8)==ids[i] && Text(row+12)==keys[i],"a new category row");
    }
    Check(Text(Kids(Kid(root,4))+12).empty(),"a stock row's empty name stays empty");
}

void TestRefusals() {
    Value out,stock=StockInit();
    Value five=stock;five.items.push_back(StockElement(4,"Other",1));
    Check(zwclass::ExtendSoldierInit(five,out)!=nullptr,"five classes are not extended");
    Value shuffled=stock;std::swap(shuffled.items[0],shuffled.items[1]);
    Check(zwclass::ExtendSoldierInit(shuffled,out)!=nullptr,"an element out of order is refused");
    Value shape=stock;shape.items[2].items[3].items.pop_back();
    Check(zwclass::ExtendSoldierInit(shape,out)!=nullptr,"an element without both HP floats is refused");
    Value slot=stock;slot.items[1].items[4].items[0].items[2]=Int(3);
    Check(zwclass::ExtendSoldierInit(slot,out)!=nullptr,"a slot without a category array is refused");
    Check(zwclass::ExtendSoldierInit(Int(4),out)!=nullptr,"not an array");
    Value cats=StockCategories();cats.items.push_back(Array({Int(501),Str(L"x")}));
    Check(zwclass::ExtendCategories(cats,out)!=nullptr,"a category of ours already there is refused");
    Value row=StockCategories();row.items[0].items.pop_back();
    Check(zwclass::ExtendCategories(row,out)!=nullptr,"a row that is not [int, string] is refused");
}

void TestCounts() {
    Check(zwclass::StockClassesOf(6)==4 && zwclass::StockClassesOf(4)==4 && zwclass::StockClassesOf(3)==3 &&
          zwclass::StockClassesOf(0)==0,"the clamp to the stock four");
    unsigned char stub[zwclass::kPlayCountStubSize];
    const std::uint64_t back=0x1122334455667788ull,skip=0x0102030405060708ull;
    zwclass::PlayCountStub(back,skip,stub);
    const unsigned char head[]={0x8B,0x94,0x11,0x90,0x6E,0x00,0x00,0x83,0xFA,0x04,0x7D};
    Check(std::memcmp(stub,head,sizeof(head))==0,"the stub reloads edx and compares it with 4 (jge: signed)");
    const std::size_t skipAt=12+static_cast<std::size_t>(stub[11]);
    Check(stub[12]==0x85 && stub[13]==0xD2,"test edx,edx before going back (the je reads its flags)");
    std::uint64_t b,s;
    std::memcpy(&b,stub+20,8);
    std::memcpy(&s,stub+skipAt+6,8);
    Check(stub[14]==0xFF && stub[15]==0x25 && I32(stub+16)==0 && b==back,"jmp [rip] back");
    Check(skipAt==28 && stub[28]==0xFF && stub[29]==0x25 && I32(stub+30)==0 && s==skip,"jge lands on jmp [rip] skip");
    Check(zwclass::kPlayCountStubSize==42,"42 bytes");
}

// --extend: IN's members through the extension, written as an SGO {SoldierInit, SoldierWeaponCategory}.
int Extend(const char* in,const char* outPath) {
    std::ifstream f(in,std::ios::binary);
    const std::vector<unsigned char> file{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
    if(file.size()<0x20 || std::memcmp(file.data(),"SGO\0",4)!=0) {
        std::printf("%s: not a little-endian SGO\n",in);
        return 2;
    }
    const auto u32=[&](std::size_t at) { return static_cast<std::uint32_t>(I32(file.data()+at)); };
    const std::uint32_t dataOff=u32(0xC),nameCount=u32(0x10),nameOff=u32(0x14);
    const unsigned char* init=nullptr;
    const unsigned char* cats=nullptr;
    for(std::uint32_t i=0;i<nameCount;++i) {
        const std::size_t e=nameOff+i*8;
        const auto* name=reinterpret_cast<const wchar_t*>(file.data()+e+I32(file.data()+e));
        const unsigned char* node=file.data()+dataOff+u32(e+4)*12;
        if(std::wcscmp(name,L"SoldierInit")==0)init=node;
        if(std::wcscmp(name,L"SoldierWeaponCategory")==0)cats=node;
    }
    Value initStock,catsStock,initOut,catsOut;
    if(!init || !cats || !Read(init,initStock) || !Read(cats,catsStock)) {
        std::printf("%s: no readable SoldierInit / SoldierWeaponCategory\n",in);
        return 2;
    }
    if(const char* why=zwclass::ExtendSoldierInit(initStock,initOut)) {
        std::printf("refused: %s\n",why);
        return 3;
    }
    if(const char* why=zwclass::ExtendCategories(catsStock,catsOut)) {
        std::printf("refused: %s\n",why);
        return 3;
    }
    const std::vector<unsigned char> blocks[2]={Write(initOut),Write(catsOut)};
    // header, two member nodes (the blocks' roots, rel moved), the blocks, the name table, the names
    std::vector<unsigned char> out(0x20+2*12,0);
    std::size_t at[2];
    for(int i=0;i<2;++i) {
        while(out.size()%16)out.push_back(0);
        at[i]=out.size();
        out.insert(out.end(),blocks[i].begin(),blocks[i].end());
        std::memcpy(out.data()+0x20+i*12,blocks[i].data(),8);
        const std::int32_t rel=I32(blocks[i].data()+8)+static_cast<std::int32_t>(at[i]-(0x20+i*12));
        std::memcpy(out.data()+0x20+i*12+8,&rel,4);
    }
    const std::size_t names=out.size();
    out.resize(names+16,0);
    const wchar_t* keys[2]={L"SoldierInit",L"SoldierWeaponCategory"};
    for(int i=0;i<2;++i) {
        const std::size_t text=out.size();
        const std::size_t len=std::wcslen(keys[i]);
        out.resize(text+(len+1)*2,0);
        std::memcpy(out.data()+text,keys[i],len*2);
        const std::int32_t entry[2]={static_cast<std::int32_t>(text-(names+i*8)),i};
        std::memcpy(out.data()+names+i*8,entry,8);
    }
    const std::uint32_t header[6]={0,1,2,0x20,2,static_cast<std::uint32_t>(names)};   // magic, version, count, data, names
    std::memcpy(out.data(),"SGO\0",4);
    std::memcpy(out.data()+4,header+1,20);
    std::ofstream o(outPath,std::ios::binary);
    o.write(reinterpret_cast<const char*>(out.data()),static_cast<std::streamsize>(out.size()));
    return o?0:2;
}
}  // namespace

void TestPrerequisites() {
    Check(zwclass::MissingPrerequisite(true,true,true)==nullptr,"all three installed: classes 4 / 5 added");
    Check(zwclass::MissingPrerequisite(false,true,true)!=nullptr,"no save stand-in: the stock four");
    Check(zwclass::MissingPrerequisite(true,false,true)!=nullptr,"no factories: the stock four");
    Check(zwclass::MissingPrerequisite(true,true,false)!=nullptr,"no per-type tables: the stock four");
}

int main(int argc,char** argv) {
    if(argc==4 && std::strcmp(argv[1],"--extend")==0)return Extend(argv[2],argv[3]);
    TestBuilder();
    TestClasses();
    TestCategories();
    TestRefusals();
    TestCounts();
    TestPrerequisites();
    std::printf("soldier_classes_test: %d/%d checks passed\n",checks-failures,checks);
    return failures?1:0;
}
