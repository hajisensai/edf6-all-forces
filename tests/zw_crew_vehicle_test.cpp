// A support Vehicler's own vehicle (src/zw_crew_vehicle_logic.h): when its soldier asks, how a refusal waits, when it
// gives up, and the ticket its request and plan carry. Pure, no game.
#include "zw_crew_vehicle_logic.h"
#include "zw_vehicle_catalog.h"
#include <cstdio>

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL: %s\n",what);}
}
namespace z=crew::zwcrew;

void Asking() {
    z::Entry e{};
    Check(z::Step(e,0,true)==z::Act::none,"never noted: nothing asked");
    z::Note(e,1000);
    Check(e.phase==z::Phase::waiting && e.tries==0,"noted: waiting, no try yet");
    Check(z::Step(e,1000+z::kSettleMs-1,true)==z::Act::none,"on its feet, not long enough: waits");
    // Aboard a transport the clock starts again.
    Check(z::Step(e,5000,false)==z::Act::none && e.nextAt==5000+z::kSettleMs,"aboard: the clock starts again");
    Check(z::Step(e,5000+z::kSettleMs-1,true)==z::Act::none,"down, not long enough yet");
    Check(z::Step(e,5000+z::kSettleMs,true)==z::Act::ask && e.phase==z::Phase::asked && e.tries==1,"on its feet long enough: asks");
    Check(z::Step(e,99999,true)==z::Act::none,"asked: nothing more until an answer");
    z::Made(e);
    Check(e.phase==z::Phase::made && z::Step(e,999999,true)==z::Act::none,"made: one vehicle a soldier, never again");
    z::Refused(e,1);
    Check(e.phase==z::Phase::made,"a late refusal does not undo a made vehicle");
}

void Retrying() {
    z::Entry e{};
    z::Note(e,0);
    std::uint64_t now=z::kSettleMs;
    for(int i=0;i<z::kMostTries;++i) {
        Check(z::Step(e,now,true)==z::Act::ask,"each try: asks");
        z::Refused(e,now);
        Check(e.phase==z::Phase::waiting && e.nextAt==now+z::kRetryMs,"refused: waits kRetryMs");
        Check(z::Step(e,now+z::kRetryMs-1,true)==z::Act::none,"not before the retry time");
        now+=z::kRetryMs;
    }
    Check(z::Step(e,now,true)==z::Act::giveUp && e.phase==z::Phase::givenUp,"out of tries: gives up once");
    Check(z::Step(e,now+100000,true)==z::Act::none,"given up: nothing more");
    Check(e.tries==z::kMostTries,"exactly kMostTries asks");
}

void Tickets() {
    z::Ticket t{};
    for(int slot=0;slot<z::kMostCrews;++slot)
        for(std::uint16_t g:{std::uint16_t{1},std::uint16_t{2},std::uint16_t{0x7FFF},std::uint16_t{0xFFFF}}) {
            const auto w=z::Encode(z::kCrewKind,slot,g);
            Check(w!=0 && z::Decode(w,&t) && t.kind==z::kCrewKind && t.slot==slot && t.generation==g,"a ticket round-trips (never 0)");
        }
    Check(!z::Decode(0,&t),"0 (a start's variant): no ticket");
    Check(!z::Decode(z::Encode(0,3,0),&t),"generation 0: no ticket");
    Check(!z::Decode(z::Encode(0,z::kMostCrews,1),&t),"a slot past the table: no ticket");
    Check(!z::Decode(z::Encode(0,1,1)|(std::uint64_t{1}<<40),&t),"bits above 32: no ticket");
    Check(z::NextGeneration(0)==1 && z::NextGeneration(1)==2 && z::NextGeneration(0xFFFF)==1,"generations skip 0");
    // The crew's kind is a Vehicler's vehicle, the start-unlocked tank.
    Check(z::kCrewKind>=0 && z::kCrewKind<crew::kZwVehicleCount,"the crew's kind is in the catalog");
    Check(crew::ZwVehicleKindOfRow(L"ZW_V1_HSTVL")==z::kCrewKind,"the crew's kind is ZW_V1_HSTVL");
}
}  // namespace

int main() {
    Asking();
    Retrying();
    Tickets();
    if(failures){std::printf("%d of %d checks failed\n",failures,checks);return 1;}
    std::printf("zw_crew_vehicle: all %d checks passed\n",checks);
    return 0;
}
