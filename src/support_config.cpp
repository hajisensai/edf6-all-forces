#include "support_config.h"
#include <windows.h>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <new>

namespace crew {
void Log(const char* fmt,...) noexcept;
namespace {
struct WeaponName { SupportWeapon weapon; const wchar_t* name; const wchar_t* label; };
constexpr WeaponName kWeaponNames[]={
    {SupportWeapon::rifle,L"rifle",L"步枪"},{SupportWeapon::flame,L"flame",L"火焰"},
    {SupportWeapon::rocket,L"rocket",L"火箭"},{SupportWeapon::shotgun,L"shotgun",L"霰弹"},
    {SupportWeapon::sniper,L"sniper",L"狙击"},
};
static_assert(sizeof(kWeaponNames)/sizeof(kWeaponNames[0])==static_cast<std::size_t>(kSupportWeaponCount));

void Trim(const wchar_t*& begin,const wchar_t*& end) noexcept {
    while(begin<end && std::iswspace(*begin))++begin;
    while(end>begin && std::iswspace(end[-1]))--end;
}
bool Separator(wchar_t c) noexcept { return c==L',' || c==L'，' || c==L';' || c==L'、'; }
// Calls `each(token)` for every non-empty, trimmed, comma separated token; false when one is longer than 63.
template<class Each> bool Tokens(const wchar_t* text,Each each) noexcept {
    for(const wchar_t* p=text;*p;) {
        const wchar_t* end=p;while(*end && !Separator(*end))++end;
        const wchar_t *a=p,*b=end;Trim(a,b);
        if(b>a) {
            if(b-a>63)return false;
            wchar_t token[64];std::wmemcpy(token,a,static_cast<std::size_t>(b-a));token[b-a]=0;
            if(!each(token))return false;
        }
        p=*end ? end+1 : end;
    }
    return true;
}
void Problem(SupportConfig& c,const wchar_t* key,const wchar_t* value,const wchar_t* fallback) noexcept {
    const std::size_t used=std::wcslen(c.problems);
    if(used+4>=std::size(c.problems))return;
    _snwprintf_s(c.problems+used,std::size(c.problems)-used,_TRUNCATE,L"%ls%ls=%ls 无效，改用 %ls",used ? L"；" : L"",key,value,fallback);
}
void ReadWeapon(SupportIniRead read,void* context,SupportConfig& c,const wchar_t* key,SupportWeapon& out) noexcept {
    wchar_t text[64]{};
    if(!read(context,key,text,std::size(text)))return;
    const wchar_t *a=text,*b=text+std::wcslen(text);Trim(a,b);
    if(a==b)return;
    wchar_t value[64]{};std::wmemcpy(value,a,static_cast<std::size_t>(b-a));
    SupportWeapon weapon;
    if(ParseSupportWeapon(value,&weapon))out=weapon;
    else Problem(c,key,value,SupportWeaponName(out));
}
}

bool ParseSupportWeapon(const wchar_t* text,SupportWeapon* out) noexcept {
    if(!text || !out)return false;
    for(const auto& w:kWeaponNames)if(!_wcsicmp(text,w.name) || !std::wcscmp(text,w.label)){*out=w.weapon;return true;}
    return false;
}
const wchar_t* SupportWeaponName(SupportWeapon weapon) noexcept {
    const auto i=static_cast<int>(weapon);
    return i>=0 && i<kSupportWeaponCount ? kWeaponNames[i].name : L"rifle";
}
const wchar_t* SupportWeaponLabel(SupportWeapon weapon) noexcept {
    const auto i=static_cast<int>(weapon);
    return i>=0 && i<kSupportWeaponCount ? kWeaponNames[i].label : L"步枪";
}

static int KeyIndexOf(SupportKeyOf keyOf,int units,const wchar_t* key) noexcept {
    for(int i=0;keyOf && i<units;++i)if(const wchar_t* k=keyOf(i);k && !_wcsicmp(k,key))return i;
    return -1;
}

SupportConfig ParseSupportConfig(SupportIniRead read,void* context,SupportKeyOf keyOf,int units) noexcept {
    SupportConfig c;
    if(!read)return c;
    if(units>kSupportConfigUnits)units=kSupportConfigUnits;
    wchar_t text[1024]{};
    if(read(context,L"SupportDisabled",text,std::size(text))) {
        std::uint64_t disabled=0;bool bad=false;
        wchar_t unknown[128]{};
        const bool lengthOk=Tokens(text,[&](const wchar_t* token) noexcept {
            const int i=KeyIndexOf(keyOf,units,token);
            if(i<0 || i>=units) {
                bad=true;const std::size_t used=std::wcslen(unknown);
                _snwprintf_s(unknown+used,std::size(unknown)-used,_TRUNCATE,L"%ls%ls",used ? L"," : L"",token);
                return true;
            }
            disabled|=std::uint64_t{1}<<i;return true;
        });
        if(!lengthOk)Problem(c,L"SupportDisabled",L"(过长的单位名)",L"按已识别的停用");
        else if(bad)Problem(c,L"SupportDisabled",unknown,L"忽略这些未知单位");
        c.disabled=disabled;
    }
    ReadWeapon(read,context,c,L"SupportSquadWeapon",c.squad);
    ReadWeapon(read,context,c,L"SupportSquadLeaderWeapon",c.leader);
    ReadWeapon(read,context,c,L"SupportVehicleCrewWeapon",c.vehicleCrew);
    ReadWeapon(read,context,c,L"SupportAircraftCrewWeapon",c.aircraftCrew);
    if(read(context,L"SupportPlatoonWeapons",text,std::size(text))) {
        SupportWeapon three[3]{};int n=0;bool valid=true;
        if(!Tokens(text,[&](const wchar_t* token) noexcept {
            SupportWeapon w;
            if(n>=3 || !ParseSupportWeapon(token,&w))return false;
            three[n++]=w;return true;
        }))valid=false;   // a bad, fourth or over-long token
        if(valid && n==3)for(int i=0;i<3;++i)c.platoon[i]=three[i];
        else if(n || !valid) {
            wchar_t fallback[64];
            _snwprintf_s(fallback,_TRUNCATE,L"%ls,%ls,%ls（需正好三个）",SupportWeaponName(c.platoon[0]),
                         SupportWeaponName(c.platoon[1]),SupportWeaponName(c.platoon[2]));
            Problem(c,L"SupportPlatoonWeapons",text,fallback);
        }
    }
    // SupportAircraftCount_<key>: 0 (the call's own) or 1..kSupportAircraftMost; the dispatcher also keeps a plan
    // within its 16 units (an aircraft and its real crew).
    for(int i=0;keyOf && i<units;++i) {
        const wchar_t* unit=keyOf(i);
        if(!unit)continue;
        wchar_t key[96],value[32]{};
        _snwprintf_s(key,_TRUNCATE,L"SupportAircraftCount_%ls",unit);
        if(!read(context,key,value,std::size(value)))continue;
        wchar_t* end=nullptr;
        const long n=std::wcstol(value,&end,10);
        while(end && *end && std::iswspace(*end))++end;
        if(end && !*end && end!=value && n>=0 && n<=kSupportAircraftMost)c.aircraft[i]=static_cast<std::uint8_t>(n);
        else {
            wchar_t fallback[48];_snwprintf_s(fallback,_TRUNCATE,L"默认数量（允许 0-%d）",kSupportAircraftMost);
            Problem(c,key,value,fallback);
        }
    }
    return c;
}

namespace {
const SupportConfig kDefaultSupport{};
std::atomic<const SupportConfig*> publishedSupport{&kDefaultSupport};
struct IniFile { const wchar_t* path; };
std::size_t ReadIni(void* context,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept {
    const auto* ini=static_cast<const IniFile*>(context);
    return GetPrivateProfileStringW(L"VehicleCrew",key,L"",out,static_cast<DWORD>(capacity),ini->path);
}
}

void LoadSupportConfig(const wchar_t* iniPath) noexcept {
    if(!iniPath)return;
    IniFile ini{iniPath};
    auto* next=new(std::nothrow) SupportConfig(ParseSupportConfig(&ReadIni,&ini,&SupportCallKey,SupportCallCount()));
    if(!next)return;
    char problems[512]{};
    if(next->problems[0])WideCharToMultiByte(CP_UTF8,0,next->problems,-1,problems,sizeof(problems),nullptr,nullptr);
    Log("CONFIG support disabled=%016llX squad=%ls leader=%ls platoon=%ls/%ls/%ls vehicleCrew=%ls aircraftCrew=%ls%s%s",
        static_cast<unsigned long long>(next->disabled),SupportWeaponName(next->squad),SupportWeaponName(next->leader),
        SupportWeaponName(next->platoon[0]),SupportWeaponName(next->platoon[1]),SupportWeaponName(next->platoon[2]),
        SupportWeaponName(next->vehicleCrew),SupportWeaponName(next->aircraftCrew),problems[0] ? " INVALID: " : "",problems);
    publishedSupport.store(next,std::memory_order_release);
}
const SupportConfig& SupportCfg() noexcept { return *publishedSupport.load(std::memory_order_acquire); }
}
