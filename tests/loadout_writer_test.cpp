// The in-game loadout menu's saving, offline (src/loadout_ini.h, src/loadout_file.cpp): one SupportPreset_ /
// SupportVehicle_ line replaced, added or removed with every other byte kept (BOM, CRLF / LF, comments, other keys and
// their order), invalid values refused before anything is written, the result read back by the plugin's own reader
// (GetPrivateProfileStringW on a real file) and parsers; the atomic replace; the generator's status file and its hidden
// process (cmd.exe stands in for the installer).
#include "../src/loadout_ini.h"
#include "../src/loadout_file.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
using namespace crew;
const std::string kIni=
    "\xEF\xBB\xBF; EDF6VehicleCrew\r\n"
    "[VehicleCrew]\r\n"
    "Enabled=1\r\n"
    "; SupportPreset_SQUAD=rifle*4   (an example: a comment)\r\n"
    "SupportPreset_SQUAD=rifle*2\r\n"
    "Debug=0\r\n"
    "SupportVehicle_FIGHTER=GUNS\r\n"
    "\r\n"
    "[Other]\r\n"
    "SupportPreset_SQUAD=other section\r\n";

std::string Edit(const std::string& ini,LoadoutField f,const wchar_t* unit,int seats,const wchar_t* value,bool* ok=nullptr,
                 wchar_t* why=nullptr) {
    std::string out="unchanged";wchar_t local[200];
    const bool done=EditLoadoutIni(ini,f,unit,seats,value,&out,why ? why : local,200);
    if(ok)*ok=done;
    return out;
}

void Rewrites() {
    bool ok=false;
    std::string out=Edit(kIni,LoadoutField::preset,L"SQUAD",12,L"lance@1E3A8A*2,cannon",&ok);
    Check(ok,"a valid preset is saved");
    std::string want=kIni;
    want.replace(want.find("SupportPreset_SQUAD=rifle*2"),27,"SupportPreset_SQUAD=lance@1E3A8A*2,cannon");
    Check(out==want,"replaced in place: the BOM, the CRLF, the comment, the other keys, their order and the other section kept");
    out=Edit(kIni,LoadoutField::vehicle,L"TANK_CREWED",0,L"HE,AP:25",&ok);
    want=kIni;
    want.insert(want.find("\r\n\r\n[Other]")+2,"SupportVehicle_TANK_CREWED=HE,AP:25\r\n");
    Check(ok && out==want,"an absent key is added after the section's last line, before its blank line");
    out=Edit(kIni,LoadoutField::vehicle,L"FIGHTER",0,L"",&ok);
    want=kIni;
    want.erase(want.find("SupportVehicle_FIGHTER=GUNS\r\n"),29);
    Check(ok && out==want,"an empty value removes the key: the stock load");
    out=Edit(kIni,LoadoutField::preset,L"PLATOON",12,L"",&ok);
    Check(ok && out==kIni,"removing an absent key changes nothing");
}

// Stage 5: a crew's colours, the default's (no key suffix) and the mission crews'; refused where the parser refuses.
void Crews() {
    bool ok=false;wchar_t why[200];
    std::string out=Edit(kIni,LoadoutField::crew,L"TANK_CREWED",0,L"X:FFFFFF",&ok);
    std::string want=kIni;
    want.insert(want.find("\r\n\r\n[Other]")+2,"SupportCrewColour_TANK_CREWED=X:FFFFFF\r\n");
    Check(ok && out==want,"a crew colour added like any other key");
    out=Edit(kIni,LoadoutField::crew,L"",0,L"1E3A8A",&ok);
    Check(ok && out.find("\r\nSupportCrewColour=1E3A8A\r\n")!=std::string::npos,"the default's key has no suffix");
    out=Edit(kIni,LoadoutField::crew,kCrewMissionKey,0,L"FFFFFF",&ok);
    Check(ok && out.find("SupportCrewColour_MISSION=FFFFFF")!=std::string::npos,"the mission crews' key");
    Edit(kIni,LoadoutField::crew,L"SQUAD",12,L"1E3A8A",&ok,why);
    Check(!ok && std::wcsstr(why,L"没有乘员"),"an infantry squad has no crew: refused, said why");
    Edit(kIni,LoadoutField::crew,L"FIGHTER",0,L"1E3A8A:",&ok,why);
    Check(!ok && std::wcsstr(why,L"乘员颜色"),"a malformed crew colour refused by the plugin's parser");
    Check(LoadoutKey(LoadoutField::preset,L"").empty() && LoadoutKey(LoadoutField::vehicle,L"").empty(),"only a crew has a default key");
}

void Shapes() {
    bool ok=false;
    std::string out=Edit("Enabled=1\nDebug=0",LoadoutField::preset,L"SQUAD",12,L"rifle",&ok);
    Check(ok && out=="Enabled=1\nDebug=0\n[VehicleCrew]\nSupportPreset_SQUAD=rifle\n","no section: appended, the file's LF kept");
    out=Edit("[vehiclecrew]\n  supportpreset_squad = rifle\nX=1",LoadoutField::preset,L"SQUAD",12,L"sniper",&ok);
    Check(ok && out=="[vehiclecrew]\nSupportPreset_SQUAD=sniper\nX=1","section and key matched as the plugin's reader does (any case, spaces)");
    out=Edit("[VehicleCrew]\r\nSupportPreset_SQUAD=a\r\nSupportPreset_SQUAD=b\r\nY=2",LoadoutField::preset,L"SQUAD",12,L"rifle",&ok);
    Check(ok && out=="[VehicleCrew]\r\nSupportPreset_SQUAD=rifle\r\nY=2","a duplicate the reader never read goes");
    out=Edit("[VehicleCrew]\r\nX=1",LoadoutField::preset,L"SQUAD",12,L"rifle",&ok);
    Check(ok && out=="[VehicleCrew]\r\nX=1\r\nSupportPreset_SQUAD=rifle\r\n","added after an unterminated last line");
    out=Edit("",LoadoutField::preset,L"SQUAD",12,L"rifle",&ok);
    Check(ok && out=="[VehicleCrew]\r\nSupportPreset_SQUAD=rifle\r\n","an empty file gets the section, CRLF");
    std::string utf16("\xFF\xFE[\0V\0",6);
    Check(Edit(utf16,LoadoutField::preset,L"SQUAD",12,L"rifle",&ok)=="unchanged" && !ok,"a UTF-16 ini is refused, not mangled");
}

void Refusals() {
    bool ok=true;wchar_t why[200]{};
    Check(Edit(kIni,LoadoutField::preset,L"SQUAD",12,L"dragon",&ok,why)=="unchanged" && !ok && why[0],"an unknown kind refused, why named");
    Check(Edit(kIni,LoadoutField::preset,L"SQUAD",4,L"rifle*5",&ok)=="unchanged" && !ok,"more soldiers than seats refused");
    Check(Edit(kIni,LoadoutField::preset,L"TANK_CREWED",0,L"rifle",&ok)=="unchanged" && !ok,"a preset on a seatless entry refused");
    Check(Edit(kIni,LoadoutField::vehicle,L"FIGHTER",0,L"AP:25",&ok)=="unchanged" && !ok,"a tank round on a jet refused");
    Check(Edit(kIni,LoadoutField::vehicle,L"SQUAD",0,L"HE",&ok)=="unchanged" && !ok,"pylons on an entry without a vehicle refused");
    Check(Edit(kIni,LoadoutField::vehicle,L"TANK_CREWED",0,L"HE\r\nEnabled=0",&ok)=="unchanged" && !ok,"no line break smuggled in");
    Check(Edit(kIni,LoadoutField::preset,L"SQUAD",12,L"rifle\x3001rocket",&ok)=="unchanged" && !ok,"non-ASCII refused");
    Check(Edit(kIni,LoadoutField::preset,L"SQ=UAD",12,L"rifle",&ok)=="unchanged" && !ok,"a unit name that is not a key refused");
    Check(Edit(kIni,LoadoutField::preset,L"SQUAD",12,nullptr,&ok)=="unchanged" && !ok,"no value refused");
}

// A whole menu save at once (EditLoadoutIniAll): the same bytes as the edits one after another; one refused edit (anywhere
// in the list) and nothing at all is applied, the refused one named.
void Batch() {
    const LoadoutEdit edits[]={
        {LoadoutField::preset,L"SQUAD",12,L"lance@1E3A8A*2,cannon"},{LoadoutField::vehicle,L"TANK_CREWED",0,L"HE,AP:25"},
        {LoadoutField::vehicle,L"FIGHTER",0,L""},{LoadoutField::crew,L"",0,L"1E3A8A"},{LoadoutField::crew,L"TANK_CREWED",0,L"X:FFFFFF"},
        {LoadoutField::preset,L"SQUAD",12,L"sniper*3"}};   // the same key twice: the last wins, as one save after another
    std::string one=kIni;
    for(const LoadoutEdit& e:edits){bool ok=false;one=Edit(one,e.field,e.unit,e.seats,e.value,&ok);Check(ok,"each edit alone is valid");}
    std::string all="unchanged";int failed=7;wchar_t why[200]{};
    Check(EditLoadoutIniAll(kIni,edits,6,&all,&failed,why,200) && all==one && failed==-1,
          "all edits in one pass: byte for byte what saving them one by one gives");
    for(int bad=0;bad<6;++bad) {
        LoadoutEdit broken[6];
        for(int i=0;i<6;++i)broken[i]=edits[i];
        broken[bad].value=broken[bad].field==LoadoutField::preset ? L"dragon" : broken[bad].field==LoadoutField::vehicle ?
                          L"AP:25\r\nEnabled=0" : L"1E3A8A:";
        all="unchanged";failed=-1;why[0]=0;
        Check(!EditLoadoutIniAll(kIni,broken,6,&all,&failed,why,200) && all=="unchanged" && failed==bad && why[0],
              "one refused edit: none applied, it is named and why");
    }
    LoadoutEdit unknown[2]={edits[0],{LoadoutField::preset,L"SQ=UAD",12,L"rifle"}};
    Check(!EditLoadoutIniAll(kIni,unknown,2,&all,&failed,why,200) && failed==1,"a bad key refuses the whole save");
    std::string utf16("\xFF\xFE[\0V\0",6);all="unchanged";
    Check(!EditLoadoutIniAll(utf16,edits,1,&all,&failed,why,200) && all=="unchanged" && failed==-1,"a UTF-16 ini: nothing applied");
    Check(EditLoadoutIniAll(kIni,edits,0,&all,&failed,why,200) && all==kIni,"nothing to save: the text as it was");
}

std::wstring TempPath(const wchar_t* name) {
    wchar_t dir[MAX_PATH];GetTempPathW(MAX_PATH,dir);
    wchar_t path[MAX_PATH];_snwprintf_s(path,_TRUNCATE,L"%lsedf6vc_loadout_writer_%lu_%ls",dir,GetCurrentProcessId(),name);
    return path;
}

// The production chain: written atomically, read back by GetPrivateProfileStringW, parsed by the plugin's parsers.
void RoundTrip() {
    const std::wstring path=TempPath(L"rt.ini");
    Check(ReplaceFileAtomic(path.c_str(),kIni),"the ini written");
    std::string bytes,out;
    Check(ReadFileBytes(path.c_str(),&bytes) && bytes==kIni,"read back byte for byte");
    bool ok=false;
    out=Edit(bytes,LoadoutField::preset,L"SQUAD",12,L"lance@1E3A8A:X*2,cannon",&ok);
    Check(ok && ReplaceFileAtomic(path.c_str(),out),"the edit replaces the file");
    out=Edit(out,LoadoutField::vehicle,L"STRIKE",0,L"AGM:6,AAM_S:2",&ok);
    Check(ok && ReplaceFileAtomic(path.c_str(),out),"a second edit");
    wchar_t text[256];
    GetPrivateProfileStringW(L"VehicleCrew",L"SupportPreset_SQUAD",L"",text,256,path.c_str());
    SupportPreset p;wchar_t why[160];
    Check(ParseSupportPreset(text,12,&p,why,160) && p.count==3 && p.kind[0]==SupportWeapon::wingLance &&
          p.look[1].primary==0x1E3A8A && p.look[1].secondary==kStockColour && p.kind[2]==SupportWeapon::fencerCannon,
          "the plugin's reader and parser get the preset back");
    GetPrivateProfileStringW(L"VehicleCrew",L"SupportVehicle_STRIKE",L"",text,256,path.c_str());
    VehicleLoadout l;
    Check(ParseVehicleLoadout(text,VehicleBody::strike,&l,why,160) && l.count==2 && l.store[0]==4 && l.store[1]==1,
          "the plugin's reader and parser get the pylons back");
    Check(GetPrivateProfileIntW(L"VehicleCrew",L"Enabled",0,path.c_str())==1,"the other keys still read");
    Check(ReadFileBytes(path.c_str(),&bytes) && bytes.compare(0,3,"\xEF\xBB\xBF")==0,"the BOM survived both saves");
    Check(GetFileAttributesW((path+L".tmp-"+std::to_wstring(GetCurrentProcessId())).c_str())==INVALID_FILE_ATTRIBUTES,
          "no temporary file left");
    const std::wstring missing=TempPath(L"no\\such\\dir.ini");
    Check(!ReplaceFileAtomic(missing.c_str(),"x"),"an unwritable path fails, nothing half-written");
    DeleteFileW(path.c_str());
}

void StatusText() {
    GenerateStatus s=ParseGenerateStatus("\xEF\xBB\xBFstate=ok\r\nmade=3\r\nfailed=0\r\nmessage=\xE5\xB7\xB2\xE7\x94\x9F\xE6\x88\x90 3\r\n");
    Check(s.state==GenerateState::ok && s.made==3 && s.failed==0 && !std::wcscmp(s.message,L"已生成 3"),"the status file read (UTF-8 message)");
    Check(ParseGenerateStatus("state=failed\nfailed=2\nmessage=x").state==GenerateState::failed,"failed, LF lines");
    Check(ParseGenerateStatus("state=running\n").state==GenerateState::running,"running");
    Check(ParseGenerateStatus("").state==GenerateState::idle && ParseGenerateStatus("state=what").state==GenerateState::idle,
          "nothing or nonsense: idle");
}

void WaitDone(VariantGenerator& g) {
    // Until it is done, however long: VariantGenerator starts its child BELOW_NORMAL_PRIORITY_CLASS (it must not take
    // the game's CPU), so on a machine busy at normal priority (ctest -j) the stand-in cmd.exe waits for the CPU: 0.7 s
    // alone, 90 s under 16 busy normal-priority processes (2026-10-25, the whole length of the load). No wall-clock
    // bound here: a stand-in that never ends is the test's CTest TIMEOUT (CMakeLists.txt).
    while(g.Running())Sleep(20);
    Check(!g.Running(),"the stand-in finished");
}

void Process() {
    wchar_t cmd[MAX_PATH];GetSystemDirectoryW(cmd,MAX_PATH);wcscat_s(cmd,L"\\cmd.exe");
    const std::wstring status=TempPath(L"status.txt");
    DeleteFileW(status.c_str());
    VariantGenerator g;
    Check(g.Status(status.c_str()).state==GenerateState::idle,"never started, no file: idle");
    wchar_t why[200]{};
    const std::wstring ok=L"/c (echo state=ok& echo made=2& echo message=done)>\""+status+L"\"";
    Check(g.Start(cmd,ok.c_str(),why,200),"the stand-in starts hidden");
    Check(!g.Start(cmd,ok.c_str(),why,200) || !g.Running(),"a second start is refused while one runs");
    WaitDone(g);
    GenerateStatus s=g.Status(status.c_str());
    Check(s.state==GenerateState::ok && s.made==2,"its status file read once it is done");
    const std::wstring crash=L"/c (echo state=running)>\""+status+L"\" & exit 7";
    Check(g.Start(cmd,crash.c_str(),why,200),"started again after the first finished");
    WaitDone(g);
    s=g.Status(status.c_str());
    Check(s.state==GenerateState::failed && std::wcsstr(s.message,L"7"),"exited still `running`: failed, the exit code named");
    VariantGenerator fresh;
    Check(fresh.Status(status.c_str()).state==GenerateState::failed,"a stale `running` nobody writes: failed, not running forever");
    Check(!fresh.Start(L"C:\\no\\such\\generator.exe",L"x",why,200) && why[0],"a missing generator refused, why named");
    // A new run may die before Python writes its first `running` status (or exit busy without touching the file).
    // Leave a prior final result with an explicitly old timestamp, then run a real child that writes nothing.
    for(const char* state:{"ok","failed"}) {
        Check(ReplaceFileAtomic(status.c_str(),std::string("state=")+state+"\nmade=99\nmessage=previous run\n"),
              "a prior final result written");
        HANDLE old=CreateFileW(status.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                               nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        const FILETIME past{1,0};
        Check(old!=INVALID_HANDLE_VALUE && SetFileTime(old,nullptr,nullptr,&past),"the prior result predates the new child");
        CloseHandle(old);
        Check(g.Start(cmd,L"/c exit 3",why,200),"a child starts then exits busy without a status write");
        WaitDone(g);
        s=g.Status(status.c_str());
        Check(s.state==GenerateState::failed && s.made==0 && std::wcsstr(s.message,L"另一个生成器"),
              "a stale final result is not attributed to the new busy child");
        Check(g.Start(cmd,L"/c exit 0",why,200),"a child exits zero without writing a current final state");
        WaitDone(g);
        s=g.Status(status.c_str());
        Check(s.state==GenerateState::failed && s.made==0 && std::wcsstr(s.message,L"0"),
              "even zero exit needs a final state written by this run");
    }
    const std::wstring badOk=L"/c (echo state=ok)>\""+status+L"\" & exit 7";
    Check(g.Start(cmd,badOk.c_str(),why,200),"a child writes ok but exits nonzero");
    WaitDone(g);
    s=g.Status(status.c_str());
    Check(s.state==GenerateState::failed && std::wcsstr(s.message,L"7"),"nonzero exit cannot claim success");
    DeleteFileW(status.c_str());
}
}  // namespace

int main() {
    Rewrites();
    Crews();
    Shapes();
    Refusals();
    Batch();
    RoundTrip();
    StatusText();
    Process();
    std::printf("loadout_writer: %d checks passed\n",checks);
    return 0;
}
