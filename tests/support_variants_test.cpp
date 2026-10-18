// The mission's variant preload set and the hello's filter (src/support_variants_logic.h; docs/feature-2026-10-12-room-
// loadout-menu.md 阶段 R): exactly this machine's configuration + the room's host list + the pending list's newest
// names, never a directory's worth; each part bounded so the worst case of all three fits kSupportVariantsMost; names
// that are not variant files (paths, other files) never enter; the hello's filter names the host's list's files alone
// when there is one; the false-positive rate of the 256-bit filter at the sizes that matter.
#include "../src/support_variants_logic.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace crew;
using namespace crew::variants;
int checks=0,failures=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

std::wstring Soldier(int kind,bool leader,std::int32_t primary) {
    SupportLook look;look.primary=primary;
    wchar_t f[96];SupportLookFile(static_cast<SupportWeapon>(kind),leader,look,f,96);
    return f;
}
std::vector<std::wstring> Soldiers(int n,std::int32_t base) {
    std::vector<std::wstring> out;
    for(int i=0;i<n;++i)out.push_back(Soldier(i%kSupportWeaponCount,(i/kSupportWeaponCount)%2==1,base+i));
    return out;
}
std::string Lines(const std::vector<std::wstring>& names,const char* eol="\r\n") {
    std::string s;
    for(const auto& n:names){for(wchar_t c:n)s+=static_cast<char>(c);s+=eol;}
    return s;
}

MissionSet set;   // big: not on the stack
HostSet host;

void Names() {
    MissionSet& s=set;s.count=0;
    Expect(s.Add(L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO") && s.Add(L"edf6vc_npc_rifle_1e3a8a_x.sgo") && s.count==1,
           "names are upper case and unique (Windows matches either)");
    Expect(s.Has(L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO") && !std::wcscmp(s.name[0],L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"),"kept upper case");
    for(const wchar_t* bad:{L"..\\EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO\\..\\x",L"V505_TANK_MISSION.SGO",
                            L"EDF6VC_JET_FIGHTER.SGO",L"EDF6VC_NPC_RIFLE_X_X.SGO",L"",static_cast<const wchar_t*>(nullptr)})
        Expect(!s.Add(bad),"not a variant file name: never added (no path, no other file)");
    Expect(s.count==1,"nothing else came in");
}

void Pending() {
    std::vector<std::wstring> got;
    const auto take=[&](const wchar_t* f){got.push_back(f);};
    const std::vector<std::wstring> names=Soldiers(200,0x100000);
    std::string text=Lines(names);
    Expect(PendingNames(text.data(),text.size(),kPendingPreloadMost,take)==kPendingPreloadMost && got.size()==kPendingPreloadMost,
           "the pending list's newest 128 names");
    Expect(got.front()==names[200-kPendingPreloadMost] && got.back()==names.back(),"oldest first, ending with the newest line");
    got.clear();
    text="\xEF\xBB\xBF"+Lines({names[0]},"\n")+"..\\..\\Windows\\x.SGO\r\nrubbish\r\n\r\n  "+Lines({names[1]},"")+
         "\r\n"+Lines({names[0]})+"edf6vc_npc_rifle_abcdef_x.sgo";
    Expect(PendingNames(text.data(),text.size(),kPendingPreloadMost,take)==3,"only variant names; a name twice counts once");
    Expect(got.size()==3 && got[0]==names[1] && got[1]==names[0] && got[2]==L"EDF6VC_NPC_RIFLE_ABCDEF_X.SGO",
           "BOM, LF, blanks, spaces, lower case handled; the repeated name at its newest place");
    got.clear();
    Expect(PendingNames(nullptr,0,kPendingPreloadMost,take)==0 && PendingNames("",0,kPendingPreloadMost,take)==0 && got.empty(),
           "no pending list: nothing");
}

void Mission() {
    // The worst case of every part: all of them, distinct, fit.
    const std::vector<std::wstring> configured=Soldiers(kConfiguredVariantsMost,0x200000);
    const std::vector<std::wstring> hosts=Soldiers(kHostVariantsMost,0x400000);
    const std::string pending=Lines(Soldiers(kPendingPreloadMost,0x600000));
    MissionParts p=MissionVariants(&set,[&](auto&& put){for(const auto& f:configured)put(f.c_str());},
                                   [&](auto&& put){for(const auto& f:hosts)put(f.c_str());},pending.data(),pending.size());
    Expect(p.configured==kConfiguredVariantsMost && p.host==kHostVariantsMost && p.pending==kPendingPreloadMost && !p.dropped &&
           set.count==kSupportVariantsMost,"every part at its bound: the whole set fits, nothing dropped");
    // More than a part's bound (a source the plugin never gives): the excess counted, the other parts unharmed.
    const std::vector<std::wstring> tooMany=Soldiers(kConfiguredVariantsMost+5,0x700000);
    p=MissionVariants(&set,[&](auto&& put){for(const auto& f:tooMany)put(f.c_str());},
                      [&](auto&& put){for(const auto& f:hosts)put(f.c_str());},pending.data(),pending.size());
    Expect(p.dropped==5 && p.host==kHostVariantsMost && p.pending==kPendingPreloadMost,"a part over its bound cannot crowd out the others");
    // Overlaps count once; a host's name the configuration has is no new entry.
    p=MissionVariants(&set,[&](auto&& put){put(configured[0].c_str());put(configured[1].c_str());},
                      [&](auto&& put){put(configured[1].c_str());put(hosts[0].c_str());},nullptr,0);
    Expect(set.count==3 && p.configured==2 && p.host==1 && p.pending==0,"the union: each file once");
    // A directory full of other generated files is no part of it: only what is named.
    Expect(!set.Has(hosts[1].c_str()),"nothing beyond the three sources");
}

void BackpackFallback() {
    SupportPreset preset;wchar_t why[128];
    Expect(ParseSupportPreset(L"rifle@1E3A8A+aHandGrenade01*4",12,&preset,why,128),"packed preset parses");
    const auto configured=[&](auto&& put){ForEachPresetVariant(preset,put);};
    const MissionParts parts=MissionVariants(&set,configured,[](auto&&){},nullptr,0);
    Expect(parts.configured==4 && !parts.host && !parts.pending && !parts.dropped && set.count==4,
           "without any pending names the configuration preloads both pack and colour-only leader/member resources");
    Expect(set.Has(L"EDF6VC_NPC_RIFLE_L_1E3A8A_X_AHANDGRENADE01.SGO") && set.Has(L"EDF6VC_NPC_RIFLE_1E3A8A_X_AHANDGRENADE01.SGO") &&
           set.Has(L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO") && set.Has(L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"),
           "the complete and old-peer resources match the generator's four names");
}

void Bloom() {
    MissionSet& ready=set;ready.count=0;host.count=0;
    const std::vector<std::wstring> own=Soldiers(40,0x800000),theirs=Soldiers(10,0x900000);
    for(const auto& f:own)ready.Add(f.c_str());
    for(const auto& f:theirs){ready.Add(f.c_str());host.Add(f.c_str());}
    host.Add(Soldier(0,false,0xABCDEF).c_str());   // the host lists one this machine lacks
    unsigned char all[kVariantBloomBytes],only[kVariantBloomBytes];
    HelloBloom(ready,nullptr,all);
    HelloBloom(ready,&host,only);
    bool every=true,hostOnes=true;
    for(const auto& f:own)every&=BloomHas(all,VariantHash(f.c_str()));
    for(const auto& f:theirs)hostOnes&=BloomHas(only,VariantHash(f.c_str()))&&BloomHas(all,VariantHash(f.c_str()));
    Expect(every && hostOnes,"no host list: every ready file; with one: each of its files ready here");
    int bits=0,bitsAll=0;
    for(std::size_t i=0;i<kVariantBloomBytes;++i)
        for(int b=0;b<8;++b){bits+=(only[i]>>b)&1;bitsAll+=(all[i]>>b)&1;}
    Expect(bits<=30 && bits<bitsAll,"with a host list only its files set bits (at most 3 each)");
    HostSet none;
    unsigned char empty[kVariantBloomBytes];
    HelloBloom(ready,&none,empty);
    Expect(!std::memcmp(empty,all,kVariantBloomBytes),"an empty host list: as without one");
    // The false-positive rate of 256 bits / 3 probes: (1 - e^(-3n/256))^3. Measured over names never added.
    struct Row { int n; double most; };
    for(const Row r:{Row{10,0.01},Row{30,0.06},Row{64,0.18},Row{128,0.60}}) {
        unsigned char f[kVariantBloomBytes]{};
        const std::vector<std::wstring> in=Soldiers(r.n,0xA00000);
        for(const auto& x:in)BloomAdd(f,VariantHash(x.c_str()));
        const std::vector<std::wstring> out=Soldiers(4000,0xC00000);
        int hits=0;
        for(const auto& x:out)hits+=BloomHas(f,VariantHash(x.c_str()));
        const double rate=hits/4000.0,theory=std::pow(1.0-std::exp(-3.0*r.n/256.0),3.0);
        std::printf("  bloom n=%d: false positives %.3f (theory %.3f)\n",r.n,rate,theory);
        Expect(rate<=r.most,"the false-positive rate within its documented bound");
    }
}
}  // namespace

int main() {
    Names();
    Pending();
    Mission();
    BackpackFallback();
    Bloom();
    std::printf("support_variants: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
