// 整备方案 (src/loadout_preset.h), offline: support keys recognized, captured from an ini as the plugin reads it, the
// edits a switch writes make the ini exactly the preset's (applied with the menu's own writer, loadout_ini.h), the book's
// file round-trips and refuses malformed text whole, mission memory follows deletes, and a random walk of edits keeps
// every book valid and serializable.
#include "../src/loadout_ini.h"
#include "../src/loadout_preset.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
using namespace crew::preset;

const std::string kIni=
    "\xEF\xBB\xBF; EDF6VehicleCrew\r\n"
    "[VehicleCrew]\r\n"
    "Enabled=1\r\n"
    "; SupportPreset_SQUAD=rifle*4\r\n"
    "SupportPreset_SQUAD=rifle*2\r\n"
    "SupportVehicle_FIGHTER = GUNS \r\n"
    "SupportCrewColour=\r\n"
    "SupportCrewColour_MISSION=FF0000\r\n"
    "SupportPreset_SQUAD=duplicate\r\n"
    "SupportPresetX=not a family\r\n"
    "[Other]\r\n"
    "SupportPreset_OTHER=other section\r\n";

void Keys() {
    SupportKey k;
    Check(ParseSupportKey("SupportPreset_SQUAD",&k) && k.family==Family::preset && k.unit=="SQUAD","preset key");
    Check(ParseSupportKey("SupportVehicle_OWN_MECH",&k) && k.family==Family::vehicle && k.unit=="OWN_MECH","vehicle key");
    Check(ParseSupportKey("SupportCrewColour",&k) && k.family==Family::crew && k.unit.empty(),"crew default key");
    Check(ParseSupportKey("SupportCrewColour_MISSION",&k) && k.family==Family::crew && k.unit=="MISSION","crew key");
    Check(!IsSupportKey("SupportPreset_"),"empty unit");
    Check(!IsSupportKey("SupportPreset_A-B"),"bad unit");
    Check(!IsSupportKey("SupportPresetX"),"no underscore");
    Check(!IsSupportKey("Enabled"),"other key");
    // Each family maps onto the menu writer's key spelling.
    const crew::LoadoutField f[]={crew::LoadoutField::preset,crew::LoadoutField::vehicle,crew::LoadoutField::crew};
    for(const char* key:{"SupportPreset_SQUAD","SupportVehicle_TANK","SupportCrewColour","SupportCrewColour_X1"}) {
        Check(ParseSupportKey(key,&k),"family key");
        std::wstring unit;
        for(char c:k.unit)unit+=static_cast<wchar_t>(c);
        const std::wstring w=crew::LoadoutKey(f[static_cast<int>(k.family)],unit.c_str());
        Check(crew::loadout_ini_detail::Ascii(w.c_str())==key,"same spelling as LoadoutKey");
    }
}

void Capture() {
    const std::vector<Entry> e=CaptureSupport(kIni);
    Check(e.size()==3,"three non-empty support keys in [VehicleCrew]");
    Check(e[0].key=="SupportCrewColour_MISSION" && e[0].value=="FF0000","sorted, crew");
    Check(e[1].key=="SupportPreset_SQUAD" && e[1].value=="rifle*2","the first of a duplicate");
    Check(e[2].key=="SupportVehicle_FIGHTER" && e[2].value=="GUNS","trimmed");
    Check(CaptureSupport("").empty(),"empty ini");
    Check(CaptureSupport("[vehiclecrew]\nSupportPreset_A=x").size()==1,"section case, no trailing newline");
}

// Applies `edits` with the menu's ini writer, as loadout_store.cpp SaveSupportLoadouts does (without the parsers' value
// check: the values here are opaque).
std::string Apply(const std::string& ini,const std::vector<Entry>& edits) {
    std::string text=ini,next;
    for(const Entry& e:edits) {
        Check(crew::RewriteIniKey(text,"VehicleCrew",e.key,e.value,&next),"rewrite");
        text.swap(next);
    }
    return text;
}

void Switch() {
    const std::vector<Entry> now=CaptureSupport(kIni);
    const std::vector<Entry> to={{"SupportPreset_SQUAD","rifle*2"},{"SupportPreset_SNIPER","sniper*3"},
                                 {"SupportVehicle_FIGHTER","AAM_S*4"}};
    const std::vector<Entry> ed=SwitchEdits(now,to);
    Check(ed.size()==3,"remove one, add one, change one, keep one");
    Check(ed[0].key=="SupportCrewColour_MISSION" && ed[0].value.empty(),"absent key removed");
    Check(ed[1].key=="SupportPreset_SNIPER" && ed[1].value=="sniper*3","added");
    Check(ed[2].key=="SupportVehicle_FIGHTER" && ed[2].value=="AAM_S*4","changed");
    const std::string after=Apply(kIni,ed);
    std::vector<Entry> got=CaptureSupport(after);
    std::vector<Entry> want=to;
    std::sort(want.begin(),want.end(),[](const Entry& a,const Entry& b){return a.key<b.key;});
    Check(got.size()==want.size(),"ini now has the preset's keys");
    for(std::size_t i=0;i<got.size();++i)Check(got[i].key==want[i].key && got[i].value==want[i].value,"same values");
    Check(after.find("Enabled=1")!=std::string::npos && after.find("SupportPreset_OTHER=other section")!=std::string::npos,
          "other keys and sections kept");
    Check(SwitchEdits(got,want).empty(),"switching again writes nothing");
}

Book Sample() {
    Book b;
    Player pl;
    pl.cls=1;
    pl.weapon[0][0]="AssultRifle01";
    pl.weapon[1][0]="pRapier01";
    pl.weapon[1][5]="";
    Check(AddPreset(b,"近战突击",{{"SupportVehicle_FIGHTER","AAM_S*4"},{"SupportPreset_SQUAD","rifle*2"}},pl)==0,"add 1");
    Check(AddPreset(b,"防空",{},Player{})==1,"add 2 without player");
    b.current=1;
    Check(RememberMission(b,"EDF6\\M013_5",0),"remember");
    Check(RememberMission(b,"edf5/m001",1),"remember 2");
    return b;
}

void File() {
    const Book b=Sample();
    const std::string text=Serialize(b);
    Book r;std::string why;
    Check(Parse(text,&r,&why),"round trip parses");
    Check(Serialize(r)==text,"round trip is byte-identical");
    Check(r.current==1 && r.presets.size()==2 && r.presets[0].name=="近战突击","fields back");
    Check(r.presets[0].support[0].key=="SupportPreset_SQUAD","support sorted on add");
    Check(r.presets[0].player==b.presets[0].player && r.presets[1].player.cls==-1,"player back");
    Check(MissionPreset(r,"edf6/m013_5")==0 && MissionPreset(r,"EDF6/M013_5")==0,"mission key normalized");
    Check(MissionPreset(r,"EDF6/M999")==-1,"unknown mission");
    Book empty;
    Check(Parse("",&empty,&why) && empty.presets.empty() && empty.current==-1,"empty file");

    const char* bad[]={
        "Name=x\r\n",                                                    // before any section
        "[Book]\r\nCurrent=9\r\n",                                       // out of range
        "[Book]\r\nCurrent=1\r\n",                                       // no such preset
        "[Preset2]\r\nName=x\r\n",                                       // not from 1
        "[Preset1]\r\n",                                                 // no name
        "[Preset1]\r\nName=a\r\nName=b\r\n",                             // name twice
        "[Preset1]\r\nName=a\r\nWeapons0=a,b,c,d,e,f\r\n",              // weapons without class
        "[Preset1]\r\nName=a\r\nClass=0\r\nWeapons0=a,b,c\r\n",          // not 6
        "[Preset1]\r\nName=a\r\nClass=0\r\nWeapons0=a,b,c,d,e,f,g\r\n",  // 7
        "[Preset1]\r\nName=a\r\nClass=6\r\n",                            // class (0..5)
        "[Preset1]\r\nName=a\r\nClass=5\r\nWeapons6=a,b,c,d,e,f\r\n",    // no class 6
        "[Book]\r\nFormat=1\r\n",                                        // version 1 has no Format line
        "[Book]\r\nFormat=3\r\n",                                        // a newer plugin's book
        "[Book]\r\nFormat=2\r\nFormat=2\r\n",                            // twice
        "[Preset1]\r\nName=a\r\nS.Enabled=1\r\n",                        // not a support key
        "[Preset1]\r\nName=a\r\nS.SupportPreset_A=x\r\nS.SupportPreset_A=y\r\n",
        "[Preset1]\r\nName=a\r\nS.SupportPreset_A=\r\n",                 // empty value
        "[Preset1]\r\nName=a\r\nS.SupportPreset_A=\xE4\xB8\xAD\r\n",     // non-ASCII value
        "[Preset1]\r\nName=\xFF\r\n",                                    // not UTF-8
        "[Preset1]\r\nName=a\r\nColour=1\r\n",                           // unknown key
        "[Preset1]\r\nName=a\r\n[Missions]\r\nedf6/m1=1\r\nEDF6/M1=1\r\n", // duplicate after normalizing
        "[Strange]\r\n",
        "[Book\r\n",
    };
    for(const char* t:bad) {
        Book keep=Sample();
        Check(!Parse(t,&keep,&why) && !why.empty(),t);
        Check(keep.presets.size()==2,"refused parse leaves the book");
    }
    Check(text.find("[Book]\r\nFormat=2\r\n")!=std::string::npos,"the book says its format");
    // the new classes (4 载具手, 5 刻印剑士) round trip
    {
        Book nb;
        Player pl;
        pl.cls=5;
        pl.weapon[4][0]="ZW_VehicleNone";pl.weapon[4][1]="ZW_Gyrojet01";
        pl.weapon[5][0]="ZW_MeleeNone";pl.weapon[5][1]="ZW_RuneCarbine01";pl.weapon[5][2]="ZW_SupportNone";
        Check(AddPreset(nb,"剑士",{},pl)==0,"a preset of class 5");
        Book back;
        Check(Parse(Serialize(nb),&back,&why) && back.presets[0].player==pl,"class 4 / 5 weapons back");
        Check(Serialize(nb).find("Weapons5=ZW_MeleeNone,ZW_RuneCarbine01,ZW_SupportNone,,,")!=std::string::npos,"Weapons5 line");
    }
    // migration: a version 1 book (no Format line, classes 0..3) reads as it is and is written back as version 2
    {
        const std::string v1="[Book]\r\nCurrent=1\r\n\r\n[Preset1]\r\nName=a\r\nClass=3\r\n"
                             "Weapons0=AssultRifle01,,,,,\r\nWeapons1=,,,,,\r\nWeapons2=,,,,,\r\nWeapons3=hShield01,,,,,\r\n";
        Book m;
        Check(Parse(v1,&m,&why),"version 1 parses");
        Check(m.current==0 && m.presets[0].player.cls==3 && m.presets[0].player.weapon[3][0]=="hShield01" &&
              m.presets[0].player.weapon[0][0]=="AssultRifle01","version 1 contents");
        for(int s=0;s<kSlots;++s)
            Check(m.presets[0].player.weapon[4][s].empty() && m.presets[0].player.weapon[5][s].empty(),
                  "version 1: the new classes left as they are");
        const std::string v2=Serialize(m);
        Book again;
        Check(v2.find("Format=2")!=std::string::npos && Parse(v2,&again,&why) && Serialize(again)==v2,
              "written back as version 2");
    }
    Book dropped;
    Check(Parse("[Preset1]\r\nName=a\r\n[Missions]\r\nedf6/m1=2\r\nedf6/m2=1\r\n",&dropped,&why),"mission to a missing preset");
    Check(dropped.missions.size()==1 && MissionPreset(dropped,"edf6/m2")==0,"dropped, not an error");
}

void Edits() {
    Book b=Sample();
    Check(CopyPreset(b,0,"近战突击 副本") && b.presets.size()==3 && b.presets[2].support==b.presets[0].support,"copy");
    Check(!RenamePreset(b,0,"") && !RenamePreset(b,0," a") && RenamePreset(b,0,"突击"),"rename");
    Check(StorePreset(b,1,{{"SupportPreset_A","x"}},Player{}) && b.presets[1].name=="防空" && b.presets[1].support.size()==1,
          "store keeps the name");
    Check(!StorePreset(b,1,{{"Enabled","1"}},Player{}),"store refuses a non-support key");
    Check(DeletePreset(b,0),"delete");
    Check(b.presets.size()==2 && b.current==0,"current follows");
    Check(MissionPreset(b,"edf6/m013_5")==-1 && MissionPreset(b,"edf5/m001")==0,"missions follow");
    for(int i=static_cast<int>(b.presets.size());i<kPresetMost;++i)Check(AddPreset(b,"p"+std::to_string(i),{},Player{})>=0,"fill");
    Check(AddPreset(b,"full",{},Player{})==-1,"at most 8");
    Check(!RememberMission(b,"a=b",0) && !RememberMission(b,"x",99),"bad memory refused");
}

void RandomWalk() {
    std::mt19937 rng(20261124);
    const char* keys[]={"SupportPreset_SQUAD","SupportPreset_SNIPER","SupportVehicle_TANK","SupportVehicle_OWN_MECH",
                        "SupportCrewColour","SupportCrewColour_MISSION"};
    std::string ini="[VehicleCrew]\r\nEnabled=1\r\n";
    Book b;
    for(int step=0;step<20000;++step) {
        const int op=static_cast<int>(rng()%7);
        const int n=static_cast<int>(b.presets.size());
        const int at=n ? static_cast<int>(rng()%static_cast<unsigned>(n)) : -1;
        std::vector<Entry> sup;
        for(const char* k:keys)if(rng()%2)sup.push_back(Entry{k,"v"+std::to_string(rng()%3)});
        Player pl;
        if(rng()%2){pl.cls=static_cast<int>(rng()%4);pl.weapon[pl.cls][rng()%6]="W"+std::to_string(rng()%9);}
        switch(op) {
        case 0: AddPreset(b,"p"+std::to_string(step),sup,pl); break;
        case 1: DeletePreset(b,at); break;
        case 2: StorePreset(b,at,sup,pl); break;
        case 3: RememberMission(b,"edf6/m"+std::to_string(rng()%50),at); break;
        case 4: CopyPreset(b,at,"c"+std::to_string(step)); break;
        case 5: if(at>=0){b.current=at;ini=Apply(ini,SwitchEdits(CaptureSupport(ini),b.presets[static_cast<std::size_t>(at)].support));
                          ++checks;if(!(CaptureSupport(ini)==b.presets[static_cast<std::size_t>(at)].support)){std::fprintf(stderr,"FAIL switch step %d\n",step);std::exit(1);}}
                break;
        default: RenamePreset(b,at,"r"+std::to_string(step)); break;
        }
        if(step%97==0) {
            Book r;std::string why;
            ++checks;
            if(!Parse(Serialize(b),&r,&why) || Serialize(r)!=Serialize(b)){std::fprintf(stderr,"FAIL walk step %d: %s\n",step,why.c_str());std::exit(1);}
            for(const auto& m:b.missions)Check(m.second>=0 && m.second<static_cast<int>(b.presets.size()),"memory in range");
            Check(b.current>=-1 && b.current<static_cast<int>(b.presets.size()),"current in range");
        }
    }
}
}  // namespace

int main() {
    Keys();
    Capture();
    Switch();
    File();
    Edits();
    RandomWalk();
    std::printf("loadout_preset: %d checks passed\n",checks);
    return 0;
}
