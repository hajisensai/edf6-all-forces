// The ini text read and rewritten key by key (src/ini_text.h), offline: what the room menu writes into
// EDF6VehicleCrew.ini (loadout_ini.h RewriteIniKey is it) and how the EDF4.1 outfit keeps its own file
// (outfit41_logic.h Store / Parse: only its keys written, every other line of the file kept).
// cmake --build build --target ini_text_test && build\ini_text_test.exe
#include "ini_text.h"
#include "outfit41_logic.h"
#include <cstdio>
#include <string>

namespace {
using namespace crew;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
std::string Rewritten(const std::string& text,const char* section,const char* key,const char* value) {
    std::string out;
    Expect(initext::Rewrite(text,section,key,value,&out),"rewritten");
    return out;
}

void Reading() {
    const std::string text="\xEF\xBB\xBF; comment\r\n[A]\r\n Key = one \r\nkey=two\r\n[B]\r\nKey=three\r\nOther=\r\n";
    std::string v;
    Expect(initext::Get(text,"A","KEY",&v) && v=="one","the first line of the key, trimmed, case-insensitive, past the BOM");
    Expect(initext::Get(text,"b","key",&v) && v=="three","another section's");
    Expect(initext::Get(text,"B","Other",&v) && v.empty(),"an empty value is a value");
    Expect(!initext::Get(text,"C","Key",&v),"no such section");
    Expect(!initext::Get(text,"A","Missing",&v),"no such key");
    Expect(initext::Get(text,{},"Key",&v) && v=="one","any section: the first");
    Expect(initext::Get("Loose=1\n[S]\n",{},"loose",&v) && v=="1","any section: before the first header too");
    Expect(!initext::Get(std::string("\xFF\xFEK\0=\0",6),"","K",&v),"a UTF-16 file is not read");
    Expect(initext::Key("Player",0)=="Player1" && initext::Key("Colours",1,3)=="Colours2_3","per player / per player and class keys");
}

void Writing() {
    const std::string text="; mine\n[VehicleCrew]\nA=1\n\n[Other]\nA=9\n";
    Expect(Rewritten(text,"VehicleCrew","A","2")=="; mine\n[VehicleCrew]\nA=2\n\n[Other]\nA=9\n","replaced in place, LF kept");
    Expect(Rewritten(text,"VehicleCrew","B","3")=="; mine\n[VehicleCrew]\nA=1\nB=3\n\n[Other]\nA=9\n","added after the section's last line");
    Expect(Rewritten(text,"VehicleCrew","A","")=="; mine\n[VehicleCrew]\n\n[Other]\nA=9\n","an empty value removes it");
    Expect(Rewritten(text,"New","K","v")=="; mine\n[VehicleCrew]\nA=1\n\n[Other]\nA=9\n[New]\nK=v\n","an absent section appended");
    Expect(Rewritten("","S","K","v")=="[S]\r\nK=v\r\n","a new file: CRLF");
    Expect(Rewritten("[S]\r\nK=1\r\nk=2\r\n","S","K","3")=="[S]\r\nK=3\r\n","later duplicates removed");
    std::string out;
    Expect(!initext::Rewrite(std::string("\xFF\xFE",2),"S","K","v",&out),"a UTF-16 file is refused");
}

// The EDF4.1 outfit's file through the same reader and writer: the player's own lines kept.
void OutfitFile() {
    using namespace crew::outfit41;
    State s;
    s.on[0]=true;
    s.has[1][2]=true;
    std::memset(s.own[1][2],0x5A,kColourBlock);
    s.swap[1].cls=2;s.swap[1].outfit=3;s.swap[1].others=0x0123456789ABCDEFull;
    std::memset(s.swap[1].backup,0x11,kColourBlock);
    const std::string was="; my note\r\n[Outfit41]\r\nPlayer1=0\r\nColours1_0="+Hex(s.own[1][2],kColourBlock)+"\r\n[Mine]\r\nX=1\r\n";
    std::string text;
    Expect(Store(was,s,&text),"stored");
    Expect(text.find("; my note\r\n")==0 && text.find("[Mine]\r\nX=1\r\n")!=std::string::npos,"the file's other lines kept");
    Expect(text.find("Colours1_0=")==std::string::npos,"a block no longer known removed");
    Expect(text.find("Player1=1\r\n")!=std::string::npos && text.find("Player2=0\r\n")!=std::string::npos,"the choices");
    const State back=Parse(text);
    Expect(back.on[0] && !back.on[1] && back.has[1][2] && back.own[1][2][7]==0x5A,"read back: the choice and the colours");
    Expect(back.swap[1].cls==2 && back.swap[1].outfit==3 && back.swap[1].others==0x0123456789ABCDEFull &&
           back.swap[1].backup[0]==0x11,"read back: the swap with its backup");
    State none=s;none.swap[1]=Swap{};
    std::string text2;
    Expect(Store(text,none,&text2) && text2.find("Swap2=")==std::string::npos && text2.find("Backup2=")==std::string::npos,
           "a swap ended: its keys removed");
    Expect(Format(State{})=="[Outfit41]\r\nPlayer1=0\r\nPlayer2=0\r\n","a new file of an empty state");
}
}  // namespace

int main() {
    Reading();
    Writing();
    OutfitFile();
    std::printf("ini_text_test: %d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
