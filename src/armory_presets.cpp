// 整备方案 at run time: armory_presets.h. EDF.dll 0x678CCB46 RVAs (docs/loadout-re.md §1, §8; docs/armory-re.md §4).
#include "armory_presets.h"
#include "crew.h"
#include "loadout_store.h"
#include "player_slots.h"
#include "room_preview.h"
#include "weapon_equip.h"
#include <Windows.h>
#include <cstdint>
#include <array>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace crew::armorypresets {
namespace {

preset::Book book;
bool readable=false;   // the last Load succeeded: the book may be written

void Say(wchar_t* why,std::size_t cap,const wchar_t* text) noexcept { if(why && cap)wcsncpy_s(why,cap,text,_TRUNCATE); }

std::wstring Wide(const std::string& s) {
    if(s.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(n<=0)return {};
    std::wstring out(static_cast<std::size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}
std::string Utf8(const wchar_t* s) {
    if(!s || !*s)return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,nullptr,0,nullptr,nullptr);
    if(n<=1)return {};
    std::string out(static_cast<std::size_t>(n-1),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,out.data(),n,nullptr,nullptr);
    return out;
}

// EDF6VehicleCrew.ini -> EDF6VehicleCrew.presets.ini, beside it.
bool BookPath(wchar_t* out,std::size_t cap) noexcept {
    const wchar_t* ini=ConfigPath();
    if(!ini || !*ini || wcscpy_s(out,cap,ini))return false;
    wchar_t* dot=wcsrchr(out,L'.');
    wchar_t* slash=wcsrchr(out,L'\\');
    if(!dot || (slash && dot<slash))return false;
    *dot=0;
    return wcscat_s(out,cap,L".presets.ini")==0;
}

bool Save(wchar_t* why,std::size_t cap) noexcept {
    wchar_t path[MAX_PATH];
    if(!BookPath(path,std::size(path))){Say(why,cap,L"no path for EDF6VehicleCrew.presets.ini");return false;}
    try {
        if(ReplaceFileAtomic(path,preset::Serialize(book)))return true;
    } catch(...) {}
    Say(why,cap,L"EDF6VehicleCrew.presets.ini could not be written");
    Log("ARMORY the preset book could not be written (%lu)",GetLastError());
    return false;
}

// The support keys of EDF6VehicleCrew.ini now.
bool ReadSupport(std::vector<preset::Entry>* out,wchar_t* why,std::size_t cap) {
    std::string ini;
    const wchar_t* path=ConfigPath();
    if(!path || !ReadFileBytes(path,&ini)){Say(why,cap,L"EDF6VehicleCrew.ini could not be read");return false;}
    *out=preset::CaptureSupport(ini);
    return true;
}

// A weapon name's row as weapon_equip reads it (the one reader of the runtime weapon table).
playerslots::RowInfo Lookup(const std::string& name) {
    playerslots::RowInfo info;
    const std::wstring w=Wide(name);
    if(w.empty())return info;
    const int row=wequip::FindRow(w.c_str());
    wequip::RowFacts f;
    if(row<0 || !wequip::ReadRow(row,&f))return info;
    info.row=row;
    info.category=f.category;
    info.owned=wequip::Owned(row);
    return info;
}

// The preset's player written into GameStatus through weapon_equip (the one writer of GS+0x6E98 / +0x6E90), then the 3D
// view told of the class's slots 0..3 (room_preview, the one msg 4 sender); false when there is nothing to write into.
bool WritePlayer(const preset::Player& p) {
    if(p.cls<0)return true;   // the preset does not hold the player's loadout
    if(wequip::RowCount()<0)return false;   // not ready or no weapon table yet
    const playerslots::Plan plan=playerslots::PlanWrites(p,[](const std::string& n){ return Lookup(n); });
    std::array<std::array<int,wlist::kSlotsMost>,playerslots::kClasses> want{};
    for(auto& c:want)c.fill(-1);
    for(int i=0;i<plan.n;++i)want[static_cast<std::size_t>(plan.write[i].cls)][static_cast<std::size_t>(plan.write[i].slot)]=
        plan.write[i].row;
    int written=0,refused=0;
    std::array<int,wlist::kSlotsMost> shown{};
    shown.fill(-1);
    for(int c=0;c<playerslots::kClasses;++c){
        const wlist::ClassPlan r=wequip::SetClassSlots(0,c,want[static_cast<std::size_t>(c)]);
        written+=r.changed;
        for(std::size_t s=0;s<r.verdict.size();++s)
            if(want[static_cast<std::size_t>(c)][s]>=0 && r.verdict[s]!=wlist::Verdict::ok && r.verdict[s]!=wlist::Verdict::same)
                ++refused;
        if(c==plan.cls)shown=r.slots;
    }
    if(plan.cls>=0 && !wequip::SetClass(0,plan.cls))return false;
    for(int s=0;s<4;++s)
        if(shown[static_cast<std::size_t>(s)]>=0)roompreview::ShowPlayerWeapon(plan.cls,s,shown[static_cast<std::size_t>(s)]);
    Log("ARMORY the player's loadout switched: class %d, %d slots written (%d not in the weapon table, %d not owned, "
        "%d of a category the slot does not take, %d refused by the select rules (level cap / duplicate): left as they were)",
        plan.cls,written,plan.unknown,plan.notOwned,plan.wrongSlot,refused);
    return true;
}

// The support key edits (SwitchEdits) written through SaveSupportLoadouts: all or none.
bool WriteSupport(const std::vector<preset::Entry>& edits,int index,wchar_t* why,std::size_t cap) {
    if(edits.empty())return true;
    if(static_cast<int>(edits.size())>kLoadoutSaveMost){Say(why,cap,L"too many support keys in one switch");return false;}
    std::vector<std::wstring> units(edits.size()),values(edits.size());
    std::vector<LoadoutSave> lines(edits.size());
    for(std::size_t i=0;i<edits.size();++i) {
        preset::SupportKey key;
        if(!preset::ParseSupportKey(edits[i].key,&key)){Say(why,cap,L"a support key the preset holds is malformed");return false;}
        units[i]=Wide(key.unit);
        values[i]=Wide(edits[i].value);
        const LoadoutField field=key.family==preset::Family::preset ? LoadoutField::preset :
                                 key.family==preset::Family::vehicle ? LoadoutField::vehicle : LoadoutField::crew;
        lines[i]=LoadoutSave{field,units[i].c_str(),values[i].c_str()};
    }
    int failed=-1;
    if(SaveSupportLoadouts(lines.data(),static_cast<int>(lines.size()),&failed,why,cap))return true;
    Log("ARMORY switch to preset %d refused at %d (%s): %ls (no support key written)",index+1,failed,
        failed>=0 ? edits[static_cast<std::size_t>(failed)].key.c_str() : "-",why ? why : L"");
    return false;
}

// A switch is all or nothing across its three stores: the book (Current=), EDF6VehicleCrew.ini's support keys and the
// player's weapons in GameStatus. Everything that can refuse is checked first (the ini readable, the edits well formed,
// the weapon table there when the preset holds the player); then the book is written first (the one write most likely
// to fail: a locked file), then the ini (all or none), then the player. A later step failing undoes the earlier ones:
// the ini written back to what it was, the book back to its old Current=. The variant generator starts only once all
// three hold the new preset.
bool SwitchTo(int index,wchar_t* why,std::size_t cap) {
    if(index<0 || index>=static_cast<int>(book.presets.size())){Say(why,cap,L"no such preset");return false;}
    const preset::Preset& to=book.presets[static_cast<std::size_t>(index)];
    std::vector<preset::Entry> now;
    if(!ReadSupport(&now,why,cap))return false;
    const std::vector<preset::Entry> edits=preset::SwitchEdits(now,to.support);
    for(const preset::Entry& e:edits) {
        preset::SupportKey key;
        if(!preset::ParseSupportKey(e.key,&key)){Say(why,cap,L"a support key the preset holds is malformed");return false;}
    }
    if(static_cast<int>(edits.size())>kLoadoutSaveMost){Say(why,cap,L"too many support keys in one switch");return false;}
    preset::Player was;
    if(to.player.cls>=0 && !ReadPlayer(&was)) {
        Say(why,cap,L"the player's loadout cannot be read or written yet (no weapon table): nothing switched");
        return false;
    }
    const int wasCurrent=book.current;
    book.current=index;
    if(!Save(why,cap)){book.current=wasCurrent;return false;}
    const auto undoBook=[&]() {
        book.current=wasCurrent;
        wchar_t again[160]{};
        if(!Save(again,std::size(again)))
            Log("ARMORY the book could not be put back to preset %d either: %ls (it names preset %d)",wasCurrent+1,again,index+1);
    };
    if(!WriteSupport(edits,index,why,cap)){undoBook();return false;}
    if(!WritePlayer(to.player)) {
        Say(why,cap,L"the player's loadout could not be written: the switch was undone");
        std::vector<preset::Entry> after;
        wchar_t back[160]{};
        if(!ReadSupport(&after,back,std::size(back)) || !WriteSupport(preset::SwitchEdits(after,now),index,back,std::size(back)))
            Log("ARMORY preset %d: the support keys could not be put back: %ls",index+1,back);
        if(to.player.cls>=0 && !WritePlayer(was))Log("ARMORY preset %d: the player's old loadout could not be put back",index+1);
        undoBook();
        return false;
    }
    if(!edits.empty()) {
        wchar_t gen[160]{};
        if(!StartVariantGeneration(gen,std::size(gen)))Log("ARMORY the variant generator not started: %ls",gen);
    }
    Log("ARMORY switched to preset %d (%zu support keys changed)",index+1,edits.size());
    return true;
}

}  // namespace

bool ReadPlayer(preset::Player* out) noexcept {
    try {
        *out=preset::Player{};
        if(wequip::RowCount()<0)return false;   // weapon_equip refused or no weapon table yet
        const int cls=wequip::CurrentClass(0);
        if(cls<0)return false;
        out->cls=cls;
        for(int c=0;c<playerslots::kClasses;++c) {
            std::array<int,wlist::kSlotsMost> rows{};
            if(!wequip::ReadEquipped(0,c,&rows))return false;
            for(int s=0;s<playerslots::SlotsOf(c);++s) {
                const int id=rows[static_cast<std::size_t>(s)];
                wequip::RowFacts f;
                if(id<0 || !wequip::ReadRow(id,&f) || !f.id[0])continue;
                // a name that filled RowFacts::id may be cut short: stored, it would never be found again
                if(wcslen(f.id)+1>=std::size(f.id)){Log("ARMORY row %d's name is too long to store: slot left empty",id);continue;}
                out->weapon[c][s]=Utf8(f.id);
            }
        }
        return true;
    } catch(...) { return false; }
}

bool Load(wchar_t* why,std::size_t cap) noexcept {
    try {
        book=preset::Book{};
        readable=false;
        wchar_t path[MAX_PATH];
        if(!BookPath(path,std::size(path))){Say(why,cap,L"no path for EDF6VehicleCrew.presets.ini");return false;}
        if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_FILE_NOT_FOUND){readable=true;return true;}
        std::string text;
        if(!ReadFileBytes(path,&text)){Say(why,cap,L"EDF6VehicleCrew.presets.ini could not be read");return false;}
        std::string reason;
        if(!preset::Parse(text,&book,&reason)) {
            book=preset::Book{};
            Log("ARMORY EDF6VehicleCrew.presets.ini refused: %s (presets off until it reads)",reason.c_str());
            const std::wstring w=Wide(reason);
            Say(why,cap,w.empty() ? L"EDF6VehicleCrew.presets.ini is malformed" : w.c_str());
            return false;
        }
        readable=true;
        return true;
    } catch(...) { Say(why,cap,L"out of memory");return false; }
}

void Fill(armory::Presets* out) noexcept {
    *out=armory::Presets{};
    const int n=static_cast<int>(book.presets.size());
    out->count=n<armory::kPresetMost ? n : armory::kPresetMost;
    out->current=book.current<out->count ? book.current : -1;
    for(int i=0;i<out->count;++i) {
        try {
            const std::wstring w=Wide(book.presets[static_cast<std::size_t>(i)].name);
            armory::CopyText(out->names[i],armory::kTextMost,w.c_str());
        } catch(...) { out->names[i][0]=0; }
    }
}

int MissionPreset(const wchar_t* key) noexcept {
    if(!readable || !key || !*key)return -1;
    try { return preset::MissionPreset(book,Utf8(key)); } catch(...) { return -1; }
}

bool RememberMission(const wchar_t* key,int index,wchar_t* why,std::size_t cap) noexcept {
    if(!readable){Say(why,cap,L"EDF6VehicleCrew.presets.ini did not read: presets off");return false;}
    try {
        const preset::Book before=book;
        if(!preset::RememberMission(book,Utf8(key),index)){Say(why,cap,L"refused (no such preset, a bad key or the book full)");return false;}
        if(!Save(why,cap)){book=before;return false;}
        Log("ARMORY mission %ls starts with preset %d",key,index+1);
        return true;
    } catch(...) { Say(why,cap,L"out of memory");return false; }
}

bool Apply(const armory::Effects& fx,wchar_t* why,std::size_t cap) noexcept {
    if(fx.op==armory::Op::none)return true;
    if(!readable){Say(why,cap,L"EDF6VehicleCrew.presets.ini did not read: presets off");return false;}
    try {
        const preset::Book before=book;
        bool ok=false;
        const std::string name=Utf8(fx.name);
        switch(fx.op) {
        case armory::Op::switchTo: return SwitchTo(fx.index,why,cap);
        case armory::Op::store:
        case armory::Op::add: {
            std::vector<preset::Entry> support;
            if(!ReadSupport(&support,why,cap))return false;
            // The player's loadout unreadable (no weapon table yet, weapon_equip refused): refused, never stored as "no
            // player" (that would wipe the weapons a stored preset held).
            preset::Player now;
            if(!ReadPlayer(&now)){Say(why,cap,L"the player's loadout could not be read (no weapon table): not stored");return false;}
            if(fx.op==armory::Op::store)ok=preset::StorePreset(book,fx.index,std::move(support),now);
            else {
                const int at=preset::AddPreset(book,name,std::move(support),now);
                if(at>=0)book.current=at;   // it holds what is in force now
                ok=at>=0;
            }
            break;
        }
        case armory::Op::copy: ok=preset::CopyPreset(book,fx.index,name); break;
        case armory::Op::rename: ok=preset::RenamePreset(book,fx.index,name); break;
        case armory::Op::remove: ok=preset::DeletePreset(book,fx.index); break;
        default: break;
        }
        if(!ok){Say(why,cap,L"refused (the book full, the name empty or too long)");return false;}
        if(!Save(why,cap)){book=before;return false;}
        Log("ARMORY preset op %d on %d done",static_cast<int>(fx.op),fx.index+1);
        return true;
    } catch(...) { Say(why,cap,L"out of memory");return false; }
}

}  // namespace crew::armorypresets
