// The hit prediction's own logic (src/hit_predict.h), offline: the intercept (lead, drop, flight time) and the window
// (its openings watched; fire / wait / skip from the time left, read or as it went before).
//   cmake --build build --target hit_predict_check && build\hit_predict_check.exe      (exit code 1 on a failure)
#include "../src/hit_predict.h"
#include <cmath>
#include <cstdio>

using namespace crew::hitpredict;

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}
bool Near(float a,float b,float tol=0.05f) { return std::fabs(a-b)<=tol; }

void Intercepts() {
    const float from[3]={0,0,0},at[3]={0,0,100},still[3]={0,0,0},across[3]={10,0,0};
    float aim[3];
    Check(Intercept(from,at,still,0.0f,0.0f,aim)==0.0f && aim[2]==100.0f,"hitscan: the point itself, no flight");
    float t=Intercept(from,at,still,100.0f,0.0f,aim);
    Check(Near(t,1.0f) && aim[0]==0.0f,"100 m at 100 m/s: 1 s, no lead on a still target");
    t=Intercept(from,at,across,100.0f,0.0f,aim);
    Check(t>1.0f && Near(aim[0],10.0f*t,0.01f),"a target crossing at 10 m/s: led by its move over the flight");
    t=Intercept(from,at,still,100.0f,9.8f,aim);
    Check(Near(aim[1],0.5f*9.8f*t*t,0.01f) && aim[1]>4.0f,"gravity: aimed over by the drop");
    Check(Intercept(from,at,nullptr,100.0f,0.0f,aim)>0.0f && aim[0]==0.0f,"no velocity known: the point itself, led by nothing");
}

void Windows() {
    // No window: fire when accepted, skip when not.
    Check(Decide(nullptr,0,500.0f,true,-1.0f)==Verdict::fire,"no window, accepted: fire");
    Check(Decide(nullptr,0,500.0f,false,-1.0f)==Verdict::skip,"no window, refused: skip");
    // The time left read: fire only when the round lands in it.
    Window w{};
    Observe(w,false,0);Observe(w,true,1000);   // seen shut, then its opening watched
    Check(Decide(&w,1000,500.0f,true,1500.0f)==Verdict::fire,"open, 1.5 s left, 0.5 s flight: fire");
    Check(Decide(&w,1000,2000.0f,true,1500.0f)==Verdict::skip,"open, 1.5 s left, 2 s flight: skip");
    Check(Decide(&w,1000,100.0f,false,1500.0f)==Verdict::skip,"open but the hit refused (the hull): skip");
    // Shut with no rhythm: skip.
    Observe(w,false,4000);
    Check(Decide(&w,4500,0.0f,true,-1.0f)==Verdict::skip,"shut, one opening watched: no rhythm, skip");
    Check(Near(w.openMs,3000.0f,1.0f),"open 1-4 s: open 3 s");
    // A second opening 10 s after the first: a period; shut again, it opens next at 20 s.
    Observe(w,true,11000);
    Check(Near(w.periodMs,10000.0f,1.0f),"openings 10 s apart: a period of 10 s");
    // Open: how long it stays open as before (3 s): fire early, skip when the round would land past 85% of it.
    Check(Decide(&w,11500,500.0f,true,-1.0f)==Verdict::fire,"0.5 s into a 3 s window, 0.5 s flight: fire");
    Check(Decide(&w,13000,1000.0f,true,-1.0f)==Verdict::skip,"2 s into a 3 s window, 1 s flight: shut before it lands, skip");
    Observe(w,false,14000);
    Check(Decide(&w,15000,0.0f,true,-1.0f)==Verdict::skip,"shut, next opening 6 s off: skip");
    Check(Decide(&w,19000,0.0f,true,-1.0f)==Verdict::wait,"shut, next opening 2 s off: wait (aimed, held)");
    Check(Decide(&w,21000,0.0f,true,-1.0f)==Verdict::wait,"overdue by 1 s of a 10 s period: still wait");
    Check(Decide(&w,32000,0.0f,true,-1.0f)==Verdict::skip,"overdue by more than a period: its rhythm gone, skip");
    // Running means ease toward new durations.
    Observe(w,true,21000);Observe(w,false,22000);
    Check(w.openMs<3000.0f && w.openMs>1000.0f,"a 1 s opening after 3 s ones: the mean between");
    // The first sight sets the state without counting an opening.
    Window f{};Observe(f,true,500);
    Check(f.seen && f.open && f.opens==0,"first seen open: open, not counted");
}
}  // namespace

int main() {
    Intercepts();
    Windows();
    std::printf("hit_predict_check: %d cases, %d failed\n",cases,failures);
    return failures ? 1 : 0;
}
