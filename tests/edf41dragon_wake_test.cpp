// EDF6 DragonSmall's get-up rule in its knock-down state (src/edf41dragon_wake.h), against the 2026-10-17 M166 run of
// EDF4.1's giant dragon: 60 s down with no hit (20:05:20 -> 20:06:20) is the cap, not a rest. No game needed.
#include "../src/edf41dragon_wake.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
namespace dw=crew::edf41dragon;

int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
}  // namespace

int main() {
    // the constants as EDF.dll and EDF41.exe hold them (1.5 s, 2000, 60 s)
    Check(dw::kWakeAfter==1.5f && dw::kSettled==2000.0f && dw::kWakeCap==60.0f,"constants");
    // never before 1.5 s, at rest or not (strictly over: the class's comiss / jbe)
    Check(dw::WakeReason(0.0f,0.0f)==dw::Wake::TooSoon,"at once");
    Check(dw::WakeReason(1.5f,0.0f)==dw::Wake::TooSoon,"1.5 s exactly");
    Check(!dw::Wakes41(1.5f,0.0f),"4.1: 1.5 s exactly");
    // after 1.5 s: at rest (sum under 2000) gets up, in both games
    Check(dw::WakeReason(1.6f,1999.0f)==dw::Wake::Settled && dw::Wakes41(1.6f,1999.0f),"at rest");
    Check(dw::WakeReason(1.6f,2000.0f)==dw::Wake::Unsettled && !dw::Wakes41(1.6f,2000.0f),"2000 exactly is not at rest");
    // a ragdoll that does not come to rest: down until the cap in EDF6, for good in 4.1
    Check(dw::WakeReason(59.9f,5000.0f)==dw::Wake::Unsettled,"moving, under the cap");
    Check(dw::WakeReason(60.0f,5000.0f)==dw::Wake::Unsettled,"60 s exactly");
    Check(dw::WakeReason(60.1f,5000.0f)==dw::Wake::Capped && !dw::Wakes41(60.1f,5000.0f),"the cap");
    // a NaN sum (a broken body) never counts as at rest
    const float nan=std::strtof("nan",nullptr);
    Check(dw::WakeReason(2.0f,nan)==dw::Wake::Unsettled && !dw::Wakes41(2.0f,nan),"NaN sum");
    Check(dw::WakeReason(nan,0.0f)==dw::Wake::TooSoon,"NaN clock");
    // the log's words
    Check(std::strcmp(dw::WakeName(dw::Wake::Capped),"60 s cap: gets up")==0,"names");
    std::printf("edf41dragon_wake: %d checks passed\n",checks);
    return 0;
}
