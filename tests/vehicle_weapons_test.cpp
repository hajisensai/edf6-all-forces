// The vehicles' weapon SGOs queued with them (src/vehicle_weapons_logic.h): the object path's name, the SGO scan, the
// stock rows (src/vehicle_weapons.inc) of the hitch2 SYNC LOAD list's stock vehicles, the choice between them, the repeats.
//   vehicle_weapons_test                the checks
//   vehicle_weapons_test --scan FILE    each weapon file the scan finds in FILE (an SGO), a line each (UTF-8; the
//                                       native audit tests/vehicle_weapons_native_audit.py runs it on the real SGOs)
#include "vehicle_weapons_logic.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures=0;
void Expect(bool ok,const char* what) {
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
using namespace crew::vehicle_weapons;
using List=std::vector<std::wstring>;

List Scan(const std::vector<unsigned char>& bytes) {
    List out;
    ScanSgo(bytes.data(),bytes.size(),[&](std::wstring_view w){out.emplace_back(w);});
    return out;
}
List Of(std::wstring_view name) {
    List out;
    WeaponsOf(name,nullptr,0,[&](std::wstring_view w){out.emplace_back(w);});
    return out;
}
bool Holds(const List& l,std::wstring_view w) {
    for(const auto& x:l)if(Same(x,w))return true;
    return false;
}
void PutText(std::vector<unsigned char>& b,const wchar_t* s,bool nul=true) {
    for(const wchar_t* c=s;*c;++c){b.push_back(static_cast<unsigned char>(*c&0xFF));b.push_back(static_cast<unsigned char>(*c>>8));}
    if(nul){b.push_back(0);b.push_back(0);}
}

void Names() {
    Expect(ObjectName(L"app:/object/VEHICLE404_BIGTANK.sgo")==L"VEHICLE404_BIGTANK","a catalog row's path");
    Expect(ObjectName(L"app:/Object/V505_TANK_MISSION.SGO")==L"V505_TANK_MISSION","a support hull's path (any case)");
    Expect(ObjectName(L"app:/object/edf6vc_lo_tank_0000000000000001.sgo")==L"edf6vc_lo_tank_0000000000000001","a variant's");
    for(const wchar_t* none:{L"app:/weapon/v_fuel01.sgo",L"app:/object/.sgo",L"app:/object/x.mab",L"object/x.sgo",L""})
        Expect(ObjectName(none).empty(),"not an object SGO path");
}

void ScanSgoBytes() {
    std::vector<unsigned char> b={0x53,0x47,0x4F,0x00,1,2,3};   // a header, then an odd offset
    PutText(b,L"app:/weapon/edf6vc_katyusha_rockets.sgo");
    PutText(b,L"vehicle_setup");
    b.push_back(7);                                               // the next string at the other parity
    PutText(b,L"APP:/WEAPON/V_404BIGTANK_mainCannon.SGO");
    PutText(b,L"app:/object/VEHICLE404_BIGTANK.sgo");             // not a weapon
    PutText(b,L"app:/weapon/v_404bigtank_gatling.mab");           // not an SGO
    PutText(b,L"app:/weapon/.sgo");                               // no name
    std::wstring longName=L"app:/weapon/";longName.append(200,L'x');longName+=L".sgo";
    PutText(b,longName.c_str());                                  // over kPathMost
    PutText(b,L"app:/weapon/edf6vc_katyusha_rockets.sgo");        // a repeat: the caller drops it
    PutText(b,L"app:/weapon/v_fuel01.sgo",false);                 // cut off by the end of the file: not taken
    const List l=Scan(b);
    Expect(l.size()==3,"three weapon strings (the repeat too), the decoys and the unended one not");
    Expect(l.size()==3 && l[0]==L"app:/weapon/edf6vc_katyusha_rockets.sgo" && l[1]==L"APP:/WEAPON/V_404BIGTANK_mainCannon.SGO",
           "in their order and their own spelling");
    Expect(Scan({}).empty() && Scan({0x61,0}).empty(),"nothing in an empty or short file");
}

// The stock vehicles behind the hitch2 SYNC LOAD list's V_* weapons that are stock catalog rows / support hulls
// (the others are the range's derived Mods rows, read from Mods/OBJECT: tests/vehicle_weapons_native_audit.py).
void StockRows() {
    struct Want { const wchar_t* vehicle; std::vector<const wchar_t*> weapons; };
    const Want wants[]={
        {L"VEHICLE404_BIGTANK",{L"app:/weapon/v_404bigtank_maincannon.sgo",L"app:/weapon/v_404bigtank_subcannon.sgo",
                                L"app:/weapon/v_404bigtank_gatling.sgo",L"app:/weapon/v_404bigtank_sidegrenade_l.sgo",
                                L"app:/weapon/v_404bigtank_sidegrenade_r.sgo"}},
        {L"VEHICLE403_TANK_MISSION",{L"app:/weapon/v_403tank_machinegun.sgo"}},
        {L"V510_MASER_MISSION",{L"app:/weapon/v_510_maser_thunder_mission.sgo"}},
        {L"VEHICLE407_BIGBEGARUTA",{L"app:/weapon/v_407bigbegaruta_cannon.sgo",L"app:/weapon/v_407bigbegaruta_missile.sgo"}},
        {L"V505_TANK_MISSION",{L"app:/weapon/v_505tank_cannon01.sgo"}},
        {L"V503_BIKE",{L"app:/weapon/v_503_bike_gun_l.sgo",L"app:/weapon/v_503_bike_gun_r.sgo",L"app:/weapon/v_fuel01.sgo"}},
    };
    for(const auto& w:wants) {
        const List l=Of(w.vehicle);
        bool all=!l.empty();
        for(const wchar_t* x:w.weapons)all=all && Holds(l,x);
        char what[160];std::snprintf(what,sizeof(what),"stock %ls: its hitch2 weapons are in its list",w.vehicle);
        Expect(all,what);
    }
    Expect(Of(L"vehicle404_bigtank").size()==5,"a stock name in any case; the Titan's five");
    Expect(Of(L"V512_KEITRUCK_BGP").empty() && Stock(L"V512_KEITRUCK_BGP"),"an unarmed support hull: a row, no weapons");
    for(const auto& r:kStock) {
        int n=0;bool ok=true;
        EachListed(r.weapons,[&](std::wstring_view w){++n;ok=ok && WeaponPath(w);});
        if(!ok){char what[160];std::snprintf(what,sizeof(what),"%ls: every listed file a weapon SGO",r.name);Expect(false,what);}
    }
}

void Choice() {
    int n=0;
    Expect(WeaponsOf(L"EDF6TR_NOT_STOCK",nullptr,0,[&](std::wstring_view){++n;})==-1 && !n,"neither Mods nor stock: -1");
    std::vector<unsigned char> b;PutText(b,L"app:/weapon/edf6vc_bmpt_atgmf_4.sgo");
    List l;
    Expect(WeaponsOf(L"VEHICLE404_BIGTANK",b.data(),b.size(),[&](std::wstring_view w){l.emplace_back(w);})==1 &&
           l.size()==1 && l[0]==L"app:/weapon/edf6vc_bmpt_atgmf_4.sgo","a Mods copy is read, not the stock row");
}

void Repeats() {
    static Queued q;
    Expect(q.Add(L"app:/weapon/v_fuel01.sgo") && !q.Add(L"APP:/WEAPON/V_FUEL01.SGO") && q.count==1,"once, in any case");
    // The bike and the sidecar share the bike's guns and fuel: the second vehicle queues none of them again.
    int first=0,second=0;
    for(const auto& w:Of(L"V503_BIKE"))first+=q.Add(w);
    for(const auto& w:Of(L"V613_BIKE"))second+=q.Add(w);
    Expect(first==2 && second==0,"shared weapons queued once a mission");
}

int ScanFile(const char* file) {
    FILE* in=nullptr;
    if(fopen_s(&in,file,"rb") || !in){std::fprintf(stderr,"cannot read %s\n",file);return 2;}
    std::vector<unsigned char> b;unsigned char buf[65536];
    for(std::size_t n;(n=std::fread(buf,1,sizeof(buf),in))>0;)b.insert(b.end(),buf,buf+n);
    std::fclose(in);
    for(const auto& w:Scan(b)) {
        std::string s;
        for(wchar_t c:w)s.push_back(c<0x80 ? static_cast<char>(c) : '?');
        std::printf("%s\n",s.c_str());
    }
    return 0;
}
}  // namespace

int main(int argc,char** argv) {
    if(argc==3 && !std::strcmp(argv[1],"--scan"))return ScanFile(argv[2]);
    Names();
    ScanSgoBytes();
    StockRows();
    Choice();
    Repeats();
    if(failures){std::printf("vehicle_weapons_test: %d failures\n",failures);return 1;}
    std::printf("vehicle_weapons_test: OK (%zu stock vehicles)\n",std::size(kStock));
    return 0;
}
