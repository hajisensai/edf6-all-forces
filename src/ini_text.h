// An ini file's text read and edited key by key, pure (no Windows, no game; tests/ini_text_test.cpp): what the room /
// HQ menu writes into EDF6VehicleCrew.ini (loadout_ini.h) and what the EDF4.1 outfit keeps in
// EDF6VehicleCrew.outfit41.ini (outfit41_logic.h), one reader and one writer for both.
//   - Get: a key's value in a section as GetPrivateProfileString finds it (the key trimmed, case-insensitive, the first
//     line of it in the section, the value trimmed);
//   - Rewrite: one key's line set (replaced in place, added after the section's last line, the section appended when
//     absent) or removed (an empty value), every other byte of the file kept (the BOM, CRLF or LF, comments, other keys
//     and their order). A UTF-16 file is refused (false): neither caller writes one.
//   - the keys of "one value per local player" / "per local player and class" (Player1, Colours1_0 ...): Key.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace crew::initext {
namespace detail {
inline constexpr char kBom[]="\xEF\xBB\xBF";
inline bool Space(char c) noexcept { return c==' ' || c=='\t'; }
inline char Lower(char c) noexcept { return c>='A' && c<='Z' ? static_cast<char>(c-'A'+'a') : c; }
inline bool SameText(std::string_view a,std::string_view b) noexcept {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(Lower(a[i])!=Lower(b[i]))return false;
    return true;
}
inline std::string_view TrimmedView(std::string_view s) noexcept {
    std::size_t a=0,b=s.size();
    while(a<b && Space(s[a]))++a;
    while(b>a && (Space(s[b-1]) || s[b-1]=='\r' || s[b-1]=='\n'))--b;
    return s.substr(a,b-a);
}
inline std::string Trimmed(const std::string& s) { return std::string(TrimmedView(s)); }
// "[name]" -> name; "" when the line is no section header.
inline std::string_view SectionView(std::string_view line) noexcept {
    const std::string_view t=TrimmedView(line);
    if(t.size()<2 || t.front()!='[')return {};
    const std::size_t close=t.find(']');
    return close==std::string_view::npos ? std::string_view{} : TrimmedView(t.substr(1,close-1));
}
inline std::string SectionOf(const std::string& line) { return std::string(SectionView(line)); }
// "key = value" -> key (GetPrivateProfileString's match: trimmed, case-insensitive); "" for a comment or no '='.
inline std::string_view KeyView(std::string_view line) noexcept {
    const std::string_view t=TrimmedView(line);
    if(t.empty() || t[0]==';' || t[0]=='#' || t[0]=='[')return {};
    const std::size_t eq=t.find('=');
    return eq==std::string_view::npos ? std::string_view{} : TrimmedView(t.substr(0,eq));
}
inline std::string KeyOf(const std::string& line) { return std::string(KeyView(line)); }
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
inline bool Ended(const std::string& line) noexcept { return !line.empty() && line.back()=='\n'; }
inline bool Blank(const std::string& line) { return TrimmedView(line).empty(); }
inline bool Utf16(std::string_view text) noexcept {
    return text.size()>=2 && (static_cast<unsigned char>(text[0])==0xFF || static_cast<unsigned char>(text[0])==0xFE);
}
}  // namespace detail

// `key`'s value in [`section`] of `text` (an empty `section`: in any section, or before the first); false when there is
// no such key (or the file is UTF-16).
inline bool Get(std::string_view text,std::string_view section,std::string_view key,std::string* value) {
    using namespace detail;
    if(Utf16(text))return false;
    if(text.substr(0,3)==kBom)text.remove_prefix(3);
    const bool any=section.empty();
    bool inside=any;
    for(std::size_t at=0;at<text.size();) {
        std::size_t end=text.find('\n',at);
        if(end==std::string_view::npos)end=text.size();
        const std::string_view line=text.substr(at,end-at);
        at=end+1;
        const std::string_view name=SectionView(line);
        if(!name.empty()){inside=any || SameText(name,section);continue;}
        if(!inside || !SameText(KeyView(line),key))continue;
        const std::string_view t=TrimmedView(line);
        if(value)*value=std::string(TrimmedView(t.substr(t.find('=')+1)));
        return true;
    }
    return false;
}

// `text` with `key`'s line in [`section`] set to `key=value` (`value` empty: every line of `key` there removed). The
// first line of `key` is replaced in place, later duplicates (which GetPrivateProfileString never reads) removed; an
// absent key is added after the section's last non-blank line; an absent section is appended. Every other line keeps its
// bytes, the file its line ending. False for a UTF-16 file.
inline bool Rewrite(const std::string& text,const std::string& section,const std::string& key,const std::string& value,std::string* out) {
    using namespace detail;
    if(Utf16(text))return false;
    const bool bom=text.compare(0,3,kBom)==0;
    const std::vector<std::string> lines=Lines(bom ? text.substr(3) : text);
    const std::string eol=Eol(text);
    const std::string mine=value.empty() ? std::string{} : key+"="+value+eol;
    std::vector<std::string> kept;
    bool inside=false,done=false;
    std::size_t sectionEnd=std::string::npos;   // kept index after the section's last non-blank line
    for(const std::string& line:lines) {
        const std::string_view name=SectionView(line);
        if(!name.empty())inside=SameText(name,section);
        if(inside && SameText(KeyView(line),key)) {
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

// The key of a value kept per local player (1-based: "Player1"), or per local player and class ("Colours1_0").
inline std::string Key(std::string_view base,int p) { return std::string(base)+std::to_string(p+1); }
inline std::string Key(std::string_view base,int p,int cls) { return Key(base,p)+"_"+std::to_string(cls); }
}  // namespace crew::initext
