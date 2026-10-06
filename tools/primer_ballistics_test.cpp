// Verify the actual landing point of the tail stinger, independent of its angle solver.
#include "../src/primer_pose.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0;

void Landing(float across,float up,float most) {
    constexpr float speed=90.0f,gravity=14.7f;
    float angle=0.0f;
    if(!primer::LobElevation(across,up,speed,gravity,most,&angle)) {
        std::fprintf(stderr,"no trajectory to (%.1f, %.1f)\n",across,up);
        ++failures;
        return;
    }
    const double time=across/(speed*std::cos(static_cast<double>(angle)));
    const double height=speed*std::sin(static_cast<double>(angle))*time-0.5*gravity*time*time;
    if(angle>most || std::fabs(height-up)>0.01) {
        std::fprintf(stderr,"trajectory to (%.1f, %.1f): angle %.6f, height %.6f\n",across,up,angle,height);
        ++failures;
    }
}

void Unreachable(float across,float up,float most) {
    float angle=0.0f;
    if(primer::LobElevation(across,up,90.0f,14.7f,most,&angle)) {
        std::fprintf(stderr,"accepted unreachable target (%.1f, %.1f), limit %.2f\n",across,up,most);
        ++failures;
    }
}
}

int main() {
    Landing(140.0f,0.0f,1.25f);   // high arc exceeds the mount limit: use the low arc, never a clamped angle
    Landing(500.0f,0.0f,1.25f);
    Landing(140.0f,-45.0f,1.25f);
    Landing(140.0f,45.0f,1.25f);
    Landing(2.0f,0.0f,1.25f);
    float high=0.0f;
    if(!primer::LobElevation(500.0f,0.0f,90.0f,14.7f,1.25f,&high) || high<=primer::kPi/4.0f)++failures;
    Unreachable(600.0f,0.0f,1.25f);
    Unreachable(100.0f,100.0f,0.5f);   // physically reachable, but neither arc is within the mount limit
    Unreachable(0.0f,0.0f,1.25f);
    if(!failures)std::puts("primer ballistics: all landing and reachability checks passed");
    return failures ? 1 : 0;
}
