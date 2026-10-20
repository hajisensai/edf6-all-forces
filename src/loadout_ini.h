// The in-game loadout menu's ini writer, pure part (no Windows, no game): one SupportPreset_<KEY> / SupportVehicle_<KEY> /
// SupportCrewColour[_<KEY>] line of EDF6VehicleCrew.ini [VehicleCrew] replaced, added or removed, every other byte of the file kept as it was (the
// BOM, CRLF or LF, comments, other keys and their order). The value is checked with the very parsers the plugin reads it
// with (support_loadout.h ParseSupportPreset / ParseVehicleLoadout / ParseCrewColour, as support_config.cpp does) before anything is
// written, so the menu never saves a value the plugin would refuse. src/loadout_store.cpp writes the result atomically
// and reloads the config; tests/loadout_writer_test.cpp runs this offline. Design: docs/feature-2026-10-12-room-loadout-menu.md.
#pragma once
#include "ini_text.h"
#include "support_loadout.h"
#include <cstddef>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace crew {
// SupportPreset_<KEY>, SupportVehicle_<KEY>, SupportCrewColour_<KEY> (an empty KEY: SupportCrewColour, every crew's default).
enum class LoadoutField : std::uint8_t { preset, vehicle, crew };

namespace loadout_ini_detail {
using namespace initext::detail;   // the ini text's lines, sections and keys (ini_text.h)
// A text already checked to be ASCII (LoadoutKey, ValidLoadoutValue), as bytes.
inline std::string Ascii(const wchar_t* text) {
    std::string out;
    for(;*text;++text)out+=static_cast<char>(*text&0x7F);
    return out;
}
}  // namespace loadout_ini_detail

// The ini key of `unit`'s field ("SupportPreset_SQUAD"); empty when `unit` is not [A-Za-z0-9_]+ (at most 64). A crew's
// with an empty `unit`: the default's, SupportCrewColour.
inline std::wstring LoadoutKey(LoadoutField field,const wchar_t* unit) {
    if(field==LoadoutField::crew && unit && !*unit)return L"SupportCrewColour";
    if(!unit || !*unit || std::wcslen(unit)>64)return {};
    for(const wchar_t* p=unit;*p;++p)
        if(!((*p>=L'A' && *p<=L'Z') || (*p>=L'a' && *p<=L'z') || (*p>=L'0' && *p<=L'9') || *p==L'_'))return {};
    return std::wstring(field==LoadoutField::preset ? L"SupportPreset_" : field==LoadoutField::vehicle ? L"SupportVehicle_" :
                        L"SupportCrewColour_")+unit;
}

// `value` for `unit`'s field, checked as the plugin will read it. `seats`: the entry's (SupportCallSeats; a preset only).
// Only printable ASCII is written (the menu's values are; the ini is read by GetPrivateProfileStringW in the ANSI code
// page). Empty is valid: the key goes, the stock load.
inline bool ValidLoadoutValue(LoadoutField field,const wchar_t* unit,int seats,const wchar_t* value,wchar_t* why,std::size_t capacity) noexcept {
    using loadout_detail::Say;
    if(!value){Say(why,capacity,L"没有值");return false;}
    for(const wchar_t* p=value;*p;++p)
        if(*p<0x20 || *p>0x7E){Say(why,capacity,L"只能写 ASCII 可见字符");return false;}
    if(std::wcslen(value)>240){Say(why,capacity,L"太长");return false;}
    if(field==LoadoutField::preset) {
        SupportPreset p;
        return ParseSupportPreset(value,seats,&p,why,capacity);
    }
    if(field==LoadoutField::crew) {
        if(unit && *unit && !HasCrew(unit)){Say(why,capacity,L"该单位没有乘员，只有小队（颜色用 SupportPreset_）");return false;}
        CrewColour c;
        return ParseCrewColour(value,&c,why,capacity);
    }
    const VehicleBody body=VehicleBodyOfKey(unit);
    if(body==VehicleBody::none){Say(why,capacity,L"该单位没有可配置的挂点");return false;}
    VehicleLoadout l;
    return ParseVehicleLoadout(value,body,&l,why,capacity);
}

// `text` with `key`'s line in [`section`] set (ini_text.h Rewrite: the first replaced in place, duplicates removed, an
// absent key added after the section's last line, an empty `value` removes it, every other byte kept). False for UTF-16.
inline bool RewriteIniKey(const std::string& text,const std::string& section,const std::string& key,const std::string& value,std::string* out) {
    return initext::Rewrite(text,section,key,value,out);
}

// The whole edit: `unit`'s field checked (ValidLoadoutValue), then `ini` rewritten ([VehicleCrew]). False: nothing to
// write, `why` says why.
inline bool EditLoadoutIni(const std::string& ini,LoadoutField field,const wchar_t* unit,int seats,const wchar_t* value,
                           std::string* out,wchar_t* why,std::size_t capacity) {
    const std::wstring key=LoadoutKey(field,unit);
    if(key.empty()){loadout_detail::Say(why,capacity,L"单位名无效");return false;}
    if(!ValidLoadoutValue(field,unit,seats,value,why,capacity))return false;
    if(RewriteIniKey(ini,"VehicleCrew",loadout_ini_detail::Ascii(key.c_str()),loadout_ini_detail::Ascii(value),out))return true;
    loadout_detail::Say(why,capacity,L"ini 是 UTF-16 编码，菜单不改写它");
    return false;
}

// One save of the menu: many keys at once (room_ui.cpp Save; loadout_store.h SaveSupportLoadouts).
struct LoadoutEdit { LoadoutField field; const wchar_t* unit; int seats; const wchar_t* value; };
// Every edit checked first (its key, ValidLoadoutValue); only when all pass, each applied in turn to the text in memory
// (RewriteIniKey: the same bytes as one EditLoadoutIni after another). False: *out untouched, nothing to write; `*failed`
// the edit refused (-1: the file itself, a UTF-16 ini), `why` says why.
inline bool EditLoadoutIniAll(const std::string& ini,const LoadoutEdit* edits,int count,std::string* out,int* failed,
                              wchar_t* why,std::size_t capacity) {
    if(failed)*failed=-1;
    if(count<0 || (count && !edits)){loadout_detail::Say(why,capacity,L"没有要保存的项");return false;}
    for(int i=0;i<count;++i) {
        const LoadoutEdit& e=edits[i];
        if(LoadoutKey(e.field,e.unit).empty()){if(failed)*failed=i;loadout_detail::Say(why,capacity,L"单位名无效");return false;}
        if(!ValidLoadoutValue(e.field,e.unit,e.seats,e.value,why,capacity)){if(failed)*failed=i;return false;}
    }
    std::string text=ini,next;
    for(int i=0;i<count;++i) {
        const LoadoutEdit& e=edits[i];
        const std::wstring key=LoadoutKey(e.field,e.unit);
        if(!RewriteIniKey(text,"VehicleCrew",loadout_ini_detail::Ascii(key.c_str()),loadout_ini_detail::Ascii(e.value),&next)) {
            loadout_detail::Say(why,capacity,L"ini 是 UTF-16 编码，菜单不改写它");
            return false;
        }
        text.swap(next);
    }
    *out=std::move(text);
    return true;
}
}  // namespace crew
