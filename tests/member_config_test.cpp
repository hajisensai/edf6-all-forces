// A room member's support loadout line (src/member_config.h): what it writes, that it reads back to the same
// configuration, the entries left out past the budget, and that anything else is refused whole.
#include "../src/support_config.cpp"
#include "../src/member_config.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace crew {
const wchar_t* const keys[]={L"INTERCEPTOR",L"FIGHTER",L"HELI",L"SQUAD",L"PLATOON",L"TANK_CREWED"};
int SupportCallCount() noexcept {return 6;}
const wchar_t* SupportCallKey(int i) noexcept {return i>=0 && i<6 ? keys[i] : nullptr;}
int SupportCallSeats(int i) noexcept {return i==3 || i==4 ? 12 : 0;}
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",why);}}
std::map<std::wstring,std::wstring> ini;
std::size_t Read(void*,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept {
    out[0]=0;const auto it=ini.find(key);if(it==ini.end())return 0;
    wcsncpy_s(out,capacity,it->second.c_str(),_TRUNCATE);return std::wcslen(out);
}
crew::SupportConfig FromIni(){return crew::ParseSupportConfig(&Read,nullptr,&crew::SupportCallKey,6,&crew::SupportCallSeats);}
std::string Line(const crew::SupportConfig& c,std::size_t capacity=4096,int* leftOut=nullptr) {
    static char out[4096];
    crew::memberconfig::Encode(c,6,out,capacity,leftOut);
    return out;
}
bool Reads(const std::string& line,crew::SupportConfig* out=nullptr) {
    crew::SupportConfig c;
    const bool ok=crew::memberconfig::Decode(line.c_str(),6,&crew::SupportCallSeats,&c);
    if(out)*out=c;
    return ok;
}
}

int main() {
    using namespace crew;
    // Nothing but defaults: the empty line, which reads as the defaults.
    ini.clear();
    Check(Line(FromIni()).empty(),"the defaults: an empty line");
    SupportConfig back;
    Check(Reads("",&back) && back.squad==SupportWeapon::rifle && !back.preset[3].count,"the empty line reads as the defaults");

    // A full configuration.
    ini={{L"SupportDisabled",L"HELI"},{L"SupportSquadWeapon",L"shotgun"},{L"SupportSquadLeaderWeapon",L"狙击"},
         {L"SupportPlatoonWeapons",L"flame,rifle,rocket"},{L"SupportAircraftCrewWeapon",L"火焰"},
         {L"SupportAircraftCount_FIGHTER",L"3"},
         {L"SupportPreset_SQUAD",L"rifle@1E3A8A*2,rocket,rifle+aHandGrenade01"},
         {L"SupportVehicle_TANK_CREWED",L"HE,AP:25"},{L"SupportVehicle_FIGHTER",L"AAM_M:4,AAM_S:2"},
         {L"SupportVehicle_OWN_PFIGHTER",L"MK82:12"},
         {L"SupportCrewColour",L"FF0000"},{L"SupportCrewColour_MISSION",L"00FF00:0000FF"},{L"SupportCrewColour_TANK_CREWED",L"123456"}};
    const SupportConfig full=FromIni();
    Check(!full.problems[0],"the test's ini reads without a problem");
    const std::string line=Line(full);
    std::printf("  line: %s\n",line.c_str());
    Check(line=="Q=shotgun|L=sniper|T=flame,rifle,rocket|R=flame|N1=3|P3=rifle@1E3A8A*2,rocket,rifle+aHandGrenade01|"
                "V1=AAM_M:4,AAM_S:2|V5=HE,AP:25|C=FF0000|M=00FF00:0000FF|C5=123456",
          "the line as documented: only what differs, the documented order, the ini's spelling");
    Check(line.find("HELI")==std::string::npos && line.find("MK82")==std::string::npos,
          "never SupportDisabled (the host's rule) nor a player's own summons (EDF6X_VCPVEH)");
    Check(Reads(line,&back),"it reads back");
    Check(back.squad==SupportWeapon::shotgun && back.leader==SupportWeapon::sniper && back.platoon[0]==SupportWeapon::flame &&
          back.aircraftCrew==SupportWeapon::flame && back.vehicleCrew==SupportWeapon::rifle && back.aircraft[1]==3,
          "the weapons and the aircraft count");
    Check(back.preset[3].count==4 && back.preset[3].look[0].primary==0x1E3A8A && back.preset[3].look[3].pack==full.preset[3].look[3].pack &&
          back.preset[3].look[3].pack!=0,"the preset, its colours and its backpack weapon");
    Check(back.vehicle[5].On() && back.vehicle[5].count==full.vehicle[5].count && back.vehicle[1].On(),"the vehicles' pylons");
    Check(back.crewDefault.set && back.crewMission.set && back.crew[5].set && back.crew[5].look.primary==0x123456,"the crews' colours");
    Check(!back.disabled && !back.playerVehicle[0].On(),"read: nothing disabled, no own summons (not carried)");
    Check(Line(back)==line,"and writes the same line again");

    // Past the budget: whole entries left out, the last first; what is left still reads as itself.
    int leftOut=0;
    const std::string cut=Line(full,60,&leftOut);
    Check(!cut.empty() && cut.size()<60 && leftOut>0 && line.compare(0,cut.size(),cut)==0 && Reads(cut,&back) && Line(back)==cut,
          "a small budget: a prefix of whole entries, the rest counted, and it reads as itself");
    int none=0;Line(full,4096,&none);
    Check(none==0,"room enough: nothing left out");

    // Anything else: refused whole (that member's calls use the defaults).
    const char* refused[]={
        "Z=1","Q=rifle","Q=shotgun|Q=shotgun","Q=shotgun|L=sniper|Q=shotgun","L=sniper|Q=shotgun","P9=rifle","P03=rifle","P3=tank",
        "P0=rifle","Q=","Q","=rifle","Q=shotgun|","|Q=shotgun","N1=9","N1=0","V3=HE,AP:25","V5=HE,AP:999","C3=FF0000","M=GG0000",
        "Q=shotgun;L=sniper","Q=shot\tgun","Q=..\\..\\x","Q=shotgun|P3=rifle@1E3A8A,rifle@1E3A8A","SupportDisabled=FIGHTER",
        "V1=HE,AP:25","P3=rifle*13"};
    int n=0;
    for(const char* r:refused){if(!Reads(r,&back) && !back.preset[3].count && back.squad==SupportWeapon::rifle)++n;else std::printf("  accepted: %s\n",r);}
    Check(n==static_cast<int>(sizeof(refused)/sizeof(refused[0])),
          "unknown / duplicate / default / out-of-order / out-of-range / malformed / unseated / crewless entries refused whole");
    Check(!Reads(std::string(5000,'Q')),"junk refused");
    Check(!crew::memberconfig::Decode(nullptr,6,&SupportCallSeats,&back),"no line: refused");

    std::printf("member_config: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
