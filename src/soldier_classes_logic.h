// The two new player classes' data (pure logic; src/soldier_classes.cpp, tests/soldier_classes_test.cpp):
// the SoldierInit elements 4 / 5 and the SoldierWeaponCategory rows they need, as CONFIG.SGO's stock elements are laid
// out (docs/newclass-re/inject.md 1.2, docs/plan-2026-11-28-new-classes-impl.md 1), and the checks that the stock
// tables are the ones these are appended to.
//
// A SoldierInit element: [type, name key, [battle SGO x4 outfits], [HP base, HP per armor], [slot...],
//   [CustomColor SGO, [menu SGO x4], select image rab, [select image dds x4]]]
// a slot: [slot name key, default weapon, [weapon category...], repeat flag, support flag]
// a SoldierWeaponCategory row: [category, name key]
#pragma once
#include "sgo_build_logic.h"
#include <cstdint>
#include <string>
#include <vector>

namespace crew::zwclass {
constexpr int kStockClassCount=4;   // Ranger, Wing Diver, Air Raider, Fencer (0..3)
constexpr int kClassCount=6;        // + ZwVehicler (4), ZwRuneBlade (5)
constexpr int kVehicler=4;
constexpr int kRuneBlade=5;
constexpr int kOutfits=4;           // the save's outfits (-1..3 picks one of four)

struct Category {
    std::int32_t id;
    const wchar_t* nameKey;
};
inline constexpr Category kCategories[]={
    {400,L"WeaponCategory_ZwVehicle"},
    {401,L"WeaponCategory_ZwPistol"},
    {500,L"WeaponCategory_ZwMelee"},
    {501,L"WeaponCategory_ZwRanged"},
    {502,L"WeaponCategory_ZwSupport"},
};

struct Slot {
    const wchar_t* nameKey;
    const wchar_t* defaultWeapon;
    std::int32_t category;
    std::int32_t repeat;    // [3]: 0 on every stock slot but none of ours repeats a category
    std::int32_t support;   // [4]: 1 like the Air Raider's vehicle slot and the support slots
};

struct ClassDef {
    std::int32_t type;
    const wchar_t* internalName;    // the factory name (xgs_scene_object_class), module B
    const wchar_t* nameKey;         // [1], the text table's key
    const wchar_t* fileStem;        // zw_vehicler: app:/object/<stem>_0N.sgo, APP:/MENUOBJECT/<stem>_0N.SGO
    const wchar_t* customColor;     // [5][0]
    float hpBase;                   // [3][0]  NOTE placeholder (the plan sets none): the Ranger's
    float hpPerArmor;               // [3][1]
    Slot slots[3];
    int slotCount;
};

// The select screen image ([5][2] / [5][3]): the stock rab and dds names of the class's own (the plan names none; the
// stock select screen is replaced by the armory, module D).
inline constexpr wchar_t kSelectImageRab[]=L"App:/UI/TypeSelectImage.rab";

inline constexpr ClassDef kClasses[]={
    {kVehicler,L"ZwVehicler",L"SoldierType_ZwVehicler",L"zw_vehicler",L"App:/DefaultPackage/CustomColorZwVehicler.sgo",
     200.0f,0.56f,
     {{L"ZW_SlotVehicle",L"ZW_VehicleNone",400,0,1},
      {L"ZW_SlotPistol",L"ZW_Gyrojet01",401,0,0},
      {nullptr,nullptr,0,0,0}},
     2},
    {kRuneBlade,L"ZwRuneBlade",L"SoldierType_ZwRuneBlade",L"zw_runeblade",L"App:/DefaultPackage/CustomColorZwRuneBlade.sgo",
     200.0f,0.56f,
     {{L"ZW_SlotMelee",L"ZW_MeleeNone",500,0,0},
      {L"ZW_SlotRanged",L"ZW_RuneCarbine01",501,0,0},
      {L"ZW_SlotSupport",L"ZW_SupportNone",502,0,1}},
     3},
};
static_assert(sizeof(kClasses)/sizeof(kClasses[0])==kClassCount-kStockClassCount);

inline const ClassDef* Find(int type) noexcept {
    for(const ClassDef& c:kClasses)
        if(c.type==type)return &c;
    return nullptr;
}

inline std::wstring Numbered(const wchar_t* prefix,const wchar_t* stem,int n,const wchar_t* suffix) {
    std::wstring s=prefix;
    s+=stem;
    s+=L"_0";
    s+=static_cast<wchar_t>(L'0'+n);
    s+=suffix;
    return s;
}
inline std::wstring BattleSgo(const ClassDef& c,int outfit) { return Numbered(L"app:/object/",c.fileStem,outfit+1,L".sgo"); }
inline std::wstring MenuSgo(const ClassDef& c,int outfit) {
    std::wstring stem=c.fileStem;   // the stock menu entries are upper case paths with lower case stems
    return Numbered(L"APP:/MENUOBJECT/",stem.c_str(),outfit+1,L".SGO");
}
inline std::wstring SelectDds(const ClassDef& c,int n) {
    // SoldierType_ZwVehicler_01.dds: the stock SoldierType_Ranger_01.dds with the class's name key
    std::wstring s=c.nameKey;
    s+=L"_0";
    s+=static_cast<wchar_t>(L'0'+n+1);
    s+=L".dds";
    return s;
}

inline sgob::Value Element(const ClassDef& c) {
    using namespace sgob;
    std::vector<Value> battle,menu,dds,slots;
    for(int i=0;i<kOutfits;++i) {
        battle.push_back(Str(BattleSgo(c,i)));
        menu.push_back(Str(MenuSgo(c,i)));
        dds.push_back(Str(SelectDds(c,i)));
    }
    for(int i=0;i<c.slotCount;++i) {
        const Slot& s=c.slots[i];
        slots.push_back(Array({Str(s.nameKey),Str(s.defaultWeapon),Array({Int(s.category)}),Int(s.repeat),Int(s.support)}));
    }
    return Array({Int(c.type),Str(c.nameKey),Array(std::move(battle)),Array({Float(c.hpBase),Float(c.hpPerArmor)}),
                  Array(std::move(slots)),Array({Str(c.customColor),Array(std::move(menu)),Str(kSelectImageRab),Array(std::move(dds))})});
}

namespace detail {
inline bool IsArray(const sgob::Value& v,std::size_t n) { return v.type==sgob::kArray && v.items.size()==n; }
inline bool AllOf(const sgob::Value& v,std::int32_t type) {
    for(const sgob::Value& i:v.items)
        if(i.type!=type)return false;
    return true;
}
// One stock element, as the accessors read it (inject.md 1.2): six items, [0] its index.
inline bool StockElement(const sgob::Value& e,int index) {
    using namespace sgob;
    if(!IsArray(e,6) || e.items[0].type!=kInt || static_cast<std::int32_t>(e.items[0].bits)!=index)return false;
    if(e.items[1].type!=kString || !IsArray(e.items[2],kOutfits) || !AllOf(e.items[2],kString))return false;
    if(!IsArray(e.items[3],2) || !AllOf(e.items[3],kFloat) || e.items[4].type!=kArray)return false;
    for(const Value& slot:e.items[4].items)
        if(!IsArray(slot,5) || slot.items[0].type!=kString || slot.items[1].type!=kString || slot.items[2].type!=kArray ||
           slot.items[3].type!=kInt || slot.items[4].type!=kInt)
            return false;
    const Value& look=e.items[5];
    return look.type==kArray && look.items.size()>=4 && look.items[0].type==kString && IsArray(look.items[1],kOutfits) &&
           look.items[2].type==kString && look.items[3].type==kArray;
}
}  // namespace detail

// SoldierInit with elements 4 / 5 appended. Null (with why) when `stock` is not the four stock classes laid out as the
// accessors read them: a CONFIG.SGO another mod changed is not extended.
inline const char* ExtendSoldierInit(const sgob::Value& stock,sgob::Value& out) {
    if(stock.type!=sgob::kArray)return "SoldierInit is not an array";
    if(stock.items.size()!=static_cast<std::size_t>(kStockClassCount))return "SoldierInit does not have the four stock classes";
    for(int i=0;i<kStockClassCount;++i)
        if(!detail::StockElement(stock.items[static_cast<std::size_t>(i)],i))return "a SoldierInit element is not laid out as the game reads it";
    out=stock;
    for(const ClassDef& c:kClasses)out.items.push_back(Element(c));
    return nullptr;
}

// SoldierWeaponCategory with kCategories appended. Null (with why) when a row is not [int, string] or one of ours is
// there already.
inline const char* ExtendCategories(const sgob::Value& stock,sgob::Value& out) {
    using namespace sgob;
    if(stock.type!=kArray || stock.items.empty())return "SoldierWeaponCategory is not an array of rows";
    for(const Value& row:stock.items) {
        if(!detail::IsArray(row,2) || row.items[0].type!=kInt || row.items[1].type!=kString)
            return "a SoldierWeaponCategory row is not [category, name key]";
        for(const Category& c:kCategories)
            if(static_cast<std::int32_t>(row.items[0].bits)==c.id)return "SoldierWeaponCategory has one of the new categories already";
    }
    out=stock;
    for(const Category& c:kCategories)out.items.push_back(Array({Int(c.id),Str(c.nameKey)}));
    return nullptr;
}

// Why a mode data load must keep the stock four even though the reads are redirected (null: classes 4 / 5 may be added).
// Each is a module installed at load before this one (module_table.h kBeforeGameStatus): without the save's stand-in a
// class past 3 reaches MAIN.GST, and the stock game indexes SoldierInit with it unchecked; without the factories a
// class 4 / 5 SGO names a class the game cannot make (no player, the dynamic_cast at 0x5A4065 fails); without the
// per-type patches the HUD's nameplate read and the damage colour index past their tables.
constexpr const char* MissingPrerequisite(bool save,bool factories,bool typeTables) noexcept {
    return !save ? "the save's stand-in (soldier save) is not installed"
         : !factories ? "the factories ZwVehicler / ZwRuneBlade (soldier factory) are not registered"
         : !typeTables ? "the per-type tables (soldier type patches) do not hold types 4 / 5"
         : nullptr;
}

// Counts of per-class code that has room (or cases) for the stock four only: the armor statistics, the key config
// menu, the play counts (docs/newclass-re/count.md).
constexpr int StockClassesOf(int count) noexcept { return count<kStockClassCount?count:kStockClassCount; }

// The play count stub (0x70FC2D, the 9 bytes `mov edx,[rcx+rdx+0x6E90]; test edx,edx` replaced by a jump here): a
// class past the stock four goes to the loop's next row (`skip`, 0x70FD82); the rest (and -1) back to the je at `back`
// (0x70FC36) with the flags of the test, as the game had them.
constexpr std::size_t kPlayCountStubSize=42;
inline void PlayCountStub(std::uint64_t back,std::uint64_t skip,unsigned char (&out)[kPlayCountStubSize]) noexcept {
    const unsigned char head[]={
        0x8B,0x94,0x11,0x90,0x6E,0x00,0x00,   // mov edx,[rcx+rdx+0x6E90]
        0x83,0xFA,kStockClassCount,           // cmp edx,4
        0x7D,0x10,                            // jge skip (signed: -1 stays the game's)
        0x85,0xD2,                            // test edx,edx
        0xFF,0x25,0x00,0x00,0x00,0x00,        // jmp [rip]: back
    };
    std::size_t at=0;
    for(unsigned char b:head)out[at++]=b;
    for(int i=0;i<8;++i)out[at++]=static_cast<unsigned char>(back>>(8*i));
    const unsigned char jump[]={0xFF,0x25,0x00,0x00,0x00,0x00};   // skip: jmp [rip]
    for(unsigned char b:jump)out[at++]=b;
    for(int i=0;i<8;++i)out[at++]=static_cast<unsigned char>(skip>>(8*i));
}
}  // namespace crew::zwclass
