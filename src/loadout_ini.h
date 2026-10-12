// The in-game loadout menu's ini writer, pure part (no Windows, no game): one SupportPreset_<KEY> / SupportVehicle_<KEY>
// line of EDF6VehicleCrew.ini [VehicleCrew] replaced, added or removed, every other byte of the file kept as it was (the
// BOM, CRLF or LF, comments, other keys and their order). The value is checked with the very parsers the plugin reads it
// with (support_loadout.h ParseSupportPreset / ParseVehicleLoadout, as support_config.cpp does) before anything is
// written, so the menu never saves a value the plugin would refuse. src/loadout_store.cpp writes the result atomically
// and reloads the config; tests/loadout_writer_test.cpp runs this offline. Design: docs/feature-2026-10-12-room-loadout-menu.md.
#pragma once
#include "support_loadout.h"
#include <cstddef>
#include <cwchar>
#include <string>
#include <vector>

namespace crew {
enum class LoadoutField : std::uint8_t { preset, vehicle };   // SupportPreset_<KEY>, SupportVehicle_<KEY>

namespace loadout_ini_detail {
inline constexpr char kBom[]="\xEF\xBB\xBF";
inline bool Space(char c) noexcept { return c==' ' || c=='\t'; }
inline char Lower(char c) noexcept { return c>='A' && c<='Z' ? static_cast<char>(c-'A'+'a') : c; }
inline bool SameText(const std::string& a,const std::string& b) noexcept {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(Lower(a[i])!=Lower(b[i]))return false;
    return true;
}
inline std::string Trimmed(const std::string& s) {
    std::size_t a=0,b=s.size();
    while(a<b && Space(s[a]))++a;
    while(b>a && (Space(s[b-1]) || s[b-1]=='\r' || s[b-1]=='\n'))--b;
    return s.substr(a,b-a);
}
// "[name]" -> name; "" when the line is no section header.
inline std::string SectionOf(const std::string& line) {
    const std::string t=Trimmed(line);
    if(t.size()<2 || t.front()!='[')return {};
    const std::size_t close=t.find(']');
    return close==std::string::npos ? std::string{} : Trimmed(t.substr(1,close-1));
}
// "key = value" -> key (GetPrivateProfileString's match: trimmed, case-insensitive); "" for a comment or no '='.
inline std::string KeyOf(const std::string& line) {
    const std::string t=Trimmed(line);
    if(t.empty() || t[0]==';' || t[0]=='#' || t[0]=='[')return {};
    const std::size_t eq=t.find('=');
    return eq==std::string::npos ? std::string{} : Trimmed(t.substr(0,eq));
}
// The file as lines, each with its own end ("\r\n", "\n" or none for the last).
inline std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    for(std::size_t at=0;at<text.size();) {
        const std::size_t nl=text.find('\n',at);
        const std::size_t end=nl==std::string::npos ? text.size() : nl+1;
        out.push_back(text.substr(at,end-at));
        at=end;
    }
    return out;
}
inline const char* Eol(const std::string& text) noexcept { return text.find("\r\n")!=std::string::npos || text.find('\n')==std::string::npos ? "\r\n" : "\n"; }
// A text already checked to be ASCII (LoadoutKey, ValidLoadoutValue), as bytes.
inline std::string Ascii(const wchar_t* text) {
    std::string out;
    for(;*text;++text)out+=static_cast<char>(*text&0x7F);
    return out;
}
inline bool Ended(const std::string& line) noexcept { return !line.empty() && line.back()=='\n'; }
inline bool Blank(const std::string& line) { return Trimmed(line).empty(); }
}  // namespace loadout_ini_detail

// The ini key of `unit`'s field ("SupportPreset_SQUAD"); empty when `unit` is not [A-Za-z0-9_]+ (at most 64).
inline std::wstring LoadoutKey(LoadoutField field,const wchar_t* unit) {
    if(!unit || !*unit || std::wcslen(unit)>64)return {};
    for(const wchar_t* p=unit;*p;++p)
        if(!((*p>=L'A' && *p<=L'Z') || (*p>=L'a' && *p<=L'z') || (*p>=L'0' && *p<=L'9') || *p==L'_'))return {};
    return std::wstring(field==LoadoutField::preset ? L"SupportPreset_" : L"SupportVehicle_")+unit;
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
    const VehicleBody body=VehicleBodyOfKey(unit);
    if(body==VehicleBody::none){Say(why,capacity,L"该单位没有可配置的挂点");return false;}
    VehicleLoadout l;
    return ParseVehicleLoadout(value,body,&l,why,capacity);
}

// `text` (the ini's bytes, UTF-8 or ANSI, with or without BOM) with `key`'s line in [`section`] set to `key=value`
// (`value` empty: every line of `key` there removed). The first line of `key` is replaced in place, later duplicates
// (which GetPrivateProfileString never reads) removed; an absent key is added after the section's last non-blank line;
// an absent section is appended. Every other line keeps its bytes, the file its line ending. False for a UTF-16 file.
inline bool RewriteIniKey(const std::string& text,const std::string& section,const std::string& key,const std::string& value,std::string* out) {
    using namespace loadout_ini_detail;
    if(text.size()>=2 && (static_cast<unsigned char>(text[0])==0xFF || static_cast<unsigned char>(text[0])==0xFE))return false;
    const bool bom=text.compare(0,3,kBom)==0;
    const std::vector<std::string> lines=Lines(bom ? text.substr(3) : text);
    const std::string eol=Eol(text);
    const std::string mine=value.empty() ? std::string{} : key+"="+value+eol;
    std::vector<std::string> kept;
    bool inside=false,done=false;
    std::size_t sectionEnd=std::string::npos;   // kept index after the section's last non-blank line
    for(const std::string& line:lines) {
        const std::string name=SectionOf(line);
        if(!name.empty())inside=SameText(name,section);
        if(inside && SameText(KeyOf(line),key)) {
            if(!done && !mine.empty())kept.push_back(Ended(line) ? mine : mine.substr(0,mine.size()-eol.size()));
            done=true;
            if(!mine.empty())sectionEnd=kept.size();
            continue;
        }
        kept.push_back(line);
        if(inside && !Blank(line))sectionEnd=kept.size();
    }
    if(!done && !mine.empty()) {
        if(sectionEnd==std::string::npos) {
            if(!kept.empty() && !Ended(kept.back()))kept.back()+=eol;
            kept.push_back("["+section+"]"+eol);
            sectionEnd=kept.size();
        } else if(!Ended(kept[sectionEnd-1]))kept[sectionEnd-1]+=eol;
        kept.insert(kept.begin()+static_cast<std::ptrdiff_t>(sectionEnd),mine);
    }
    std::string joined=bom ? std::string(kBom) : std::string{};
    for(const std::string& line:kept)joined+=line;
    *out=joined;
    return true;
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
}  // namespace crew
