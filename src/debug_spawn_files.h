// The debug-spawn catalog's file contract, shared by constexpr checks and the
// runtime file lookup. Only safe relative OBJECT/WEAPON paths under Mods are addressable.
#pragma once
#include <cstddef>

namespace crew::debugspawn {
enum class ModDirectory { invalid, object, weapon };
struct ModFileRef {
    ModDirectory directory=ModDirectory::invalid;
    const wchar_t* name=nullptr;
    constexpr explicit operator bool() const noexcept { return directory!=ModDirectory::invalid; }
};
constexpr bool FilePrefix(const wchar_t* text,const wchar_t* prefix) noexcept {
    if(!text || !prefix)return false;
    for(std::size_t i=0;prefix[i];++i)if(!text[i] || text[i]!=prefix[i])return false;
    return true;
}
constexpr bool PlainModFilename(const wchar_t* text) noexcept {
    if(!text || !text[0])return false;
    std::size_t segment=0,n=0;
    for(;text[n];++n) {
        if(n>=241)return false; // the canonical prefix adds seven chars (248 total)
        const wchar_t c=text[n];
        if(c==L'/' || c==L'\\') {
            if(segment==0 || text[n-1]==L'.')return false;
            segment=0;continue;
        }
        if(segment>=128 || (segment==0 && c==L'.'))return false;
        if(!((c>=L'A' && c<=L'Z') || (c>=L'a' && c<=L'z') || (c>=L'0' && c<=L'9') || c==L'_' || c==L'-' || c==L'.'))return false;
        if(c==L'.' && text[n+1]==L'.')return false;
        ++segment;
    }
    return segment>0 && text[n-1]!=L'.';
}

constexpr ModFileRef ParseModFile(const wchar_t* text) noexcept {
    if(!text)return {};
    ModDirectory directory=ModDirectory::object;
    const wchar_t* name=text;
    if(FilePrefix(text,L"OBJECT/"))name=text+7;
    else if(FilePrefix(text,L"WEAPON/")){directory=ModDirectory::weapon;name=text+7;}
    if(!PlainModFilename(name))return {};
    return {directory,name};
}
}  // namespace crew::debugspawn
