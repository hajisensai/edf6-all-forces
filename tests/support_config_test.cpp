// The out-of-mission support configuration (src/support_config.cpp): parse, validation and its fallbacks, and the
// production load through a real ini file (GetPrivateProfileStringW), as plugin.cpp LoadConfig does.
#include "../src/support_config.cpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>

namespace crew {
const wchar_t* const keys[]={L"INTERCEPTOR",L"FIGHTER",L"HELI",L"SQUAD",L"PLATOON",L"TANK_CREWED"};
int SupportCallCount() noexcept {return 6;}
const wchar_t* SupportCallKey(int i) noexcept {return i>=0 && i<6 ? keys[i] : nullptr;}
std::string lastLog;
void Log(const char* fmt,...) noexcept {
    char line[1024];va_list a;va_start(a,fmt);vsnprintf(line,sizeof(line),fmt,a);va_end(a);lastLog=line;
}
}
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
std::map<std::wstring,std::wstring> ini;
std::size_t Read(void*,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept {
    out[0]=0;const auto it=ini.find(key);if(it==ini.end())return 0;
    wcsncpy_s(out,capacity,it->second.c_str(),_TRUNCATE);return std::wcslen(out);
}
crew::SupportConfig Parse(){return crew::ParseSupportConfig(&Read,nullptr,&crew::SupportCallKey,crew::SupportCallCount());}
}

int main() {
    using namespace crew;
    SupportConfig c=Parse();
    Check(!c.disabled && c.Enabled(0) && c.Enabled(5) && !c.problems[0],"no settings: every unit callable, nothing reported");
    Check(c.squad==SupportWeapon::rifle && c.leader==SupportWeapon::rifle && c.platoon[1]==SupportWeapon::rocket &&
          c.platoon[2]==SupportWeapon::sniper && c.vehicleCrew==SupportWeapon::rifle && c.aircraftCrew==SupportWeapon::rifle,
          "defaults: rifles, the platoon's second squad rockets and third snipers");
    ini={{L"SupportDisabled",L" fighter ， heli,TANK_CREWED "},{L"SupportSquadWeapon",L"Shotgun"},{L"SupportSquadLeaderWeapon",L"狙击"},
         {L"SupportPlatoonWeapons",L"flame, rifle,rocket"},{L"SupportVehicleCrewWeapon",L"rocket"},{L"SupportAircraftCrewWeapon",L"火焰"},
         {L"SupportAircraftCount_FIGHTER",L"2"},{L"SupportAircraftCount_HELI",L" 0 "}};
    c=Parse();
    Check(!c.Enabled(1) && !c.Enabled(2) && !c.Enabled(5) && c.Enabled(0) && c.Enabled(3) && !c.problems[0],
          "keys case-insensitive, trimmed, full-width comma accepted");
    Check(c.squad==SupportWeapon::shotgun && c.leader==SupportWeapon::sniper && c.platoon[0]==SupportWeapon::flame &&
          c.platoon[1]==SupportWeapon::rifle && c.platoon[2]==SupportWeapon::rocket && c.vehicleCrew==SupportWeapon::rocket &&
          c.aircraftCrew==SupportWeapon::flame,"english (any case) and chinese weapon names");
    Check(c.aircraft[1]==2 && c.aircraft[2]==0,"aircraft counts per unit, 0 = the call's own");
    ini={{L"SupportDisabled",L"FIGHTER,BOGUS,TANK"},{L"SupportSquadWeapon",L"laser"},{L"SupportPlatoonWeapons",L"rifle,rifle"},
         {L"SupportAircraftCount_FIGHTER",L"12"},{L"SupportAircraftCount_HELI",L"two"}};
    c=Parse();
    Check(!c.Enabled(1) && c.Enabled(5),"unknown units ignored, known ones still disabled");
    Check(c.squad==SupportWeapon::rifle && c.platoon[0]==SupportWeapon::rifle && c.platoon[1]==SupportWeapon::rocket,
          "invalid weapon and a two-squad platoon keep the defaults");
    Check(c.aircraft[1]==0 && c.aircraft[2]==0,"out of range / non-numeric counts keep the call's own");
    const std::wstring problems=c.problems;
    Check(problems.find(L"BOGUS")!=std::wstring::npos && problems.find(L"TANK")!=std::wstring::npos &&
          problems.find(L"SupportSquadWeapon=laser")!=std::wstring::npos && problems.find(L"SupportPlatoonWeapons")!=std::wstring::npos &&
          problems.find(L"SupportAircraftCount_FIGHTER=12")!=std::wstring::npos && problems.find(L"SupportAircraftCount_HELI=two")!=std::wstring::npos,
          "every invalid setting is named with its value");
    ini={{L"SupportPlatoonWeapons",L"rifle,rocket,sniper,flame"}};
    c=Parse();Check(c.problems[0] && c.platoon[2]==SupportWeapon::sniper,"four platoon weapons is invalid, not silently truncated");
    // The production path: a real ini file, read with GetPrivateProfileStringW, published for the dispatcher.
    wchar_t path[MAX_PATH];GetTempPathW(MAX_PATH,path);wcscat_s(path,L"edf6_support_config_test.ini");
    FILE* f=nullptr;_wfopen_s(&f,path,L"wb");Check(f!=nullptr,"temp ini written");
    const char text[]="[VehicleCrew]\r\nSupportDisabled=SQUAD\r\nSupportSquadLeaderWeapon=rocket\r\nSupportAircraftCount_HELI=3\r\n"
                      "SupportVehicleCrewWeapon=nonsense\r\n";
    std::fwrite(text,1,sizeof(text)-1,f);std::fclose(f);
    Check(SupportCfg().Enabled(3),"before any load: the defaults are published");
    LoadSupportConfig(path);
    Check(!SupportCfg().Enabled(3) && SupportCfg().leader==SupportWeapon::rocket && SupportCfg().aircraft[2]==3 &&
          SupportCfg().vehicleCrew==SupportWeapon::rifle && SupportCfg().problems[0],"the ini file is read and published");
    Check(lastLog.find("CONFIG support")==0 && lastLog.find("INVALID")!=std::string::npos && lastLog.find("nonsense")!=std::string::npos,
          "the load logs the configuration and names the invalid value");
    DeleteFileW(path);
    SupportWeapon w;
    Check(!ParseSupportWeapon(L"",&w) && !ParseSupportWeapon(nullptr,&w) && ParseSupportWeapon(L"SNIPER",&w) && w==SupportWeapon::sniper,
          "weapon names: empty refused, case-insensitive");
    Check(SupportSoldierResource(SupportWeapon::rifle,true)==2 && IsSupportLeaderResource(SupportSoldierResource(SupportWeapon::flame,true)) &&
          !IsSupportLeaderResource(SupportSoldierResource(SupportWeapon::flame,false)) && !IsSupportSoldierResource(0x503) &&
          !IsSupportSoldierResource(0x10000) && SupportSoldierWeapon(SupportSoldierResource(SupportWeapon::shotgun,false))==SupportWeapon::shotgun,
          "soldier resource ids: rifle keeps protocol v2 ids, every variant decodes, others are no soldier");
    std::printf("support_config_test: %d checks passed\n",checks);
}
