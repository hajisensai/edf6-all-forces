// The real 0x5FC280(axis,true) builds old/current bone samples from angle-rate and angle.
// A stabilizer correction must preserve the old sample without feeding correction into next frame's motor rate.
#include "../src/stab.h"
#include <cstdio>
#include <initializer_list>
int main() {
    int checked=0;
    for(float before:{-3.13f,-0.4f,0.0f,0.7f,3.13f})for(float own:{-0.02f,0.0f,0.02f})for(float correction:{-0.025f,0.025f}) {
        float axis[4]={-crew::tcam::kPi,crew::tcam::kPi,crew::tcam::Wrap(before+own),own};
        const float corrected=crew::tcam::Wrap(axis[2]+correction);
        float mappedBefore=0,mappedAfter=0;
        crew::stab::Remap(axis,before,corrected,[&](float* a) noexcept {mappedBefore=crew::tcam::Wrap(a[2]-a[3]);mappedAfter=a[2];});
        if(std::fabs(crew::tcam::Wrap(mappedBefore-before))>1e-6f || mappedAfter!=corrected || axis[3]!=own)return 1;
        // Sample every render interpolation phase of all three Titan turrets; no correction-sized jump at phase zero.
        for(int phase=0;phase<=10;++phase) {
            const float t=static_cast<float>(phase)/10.0f;
            const float drawn=crew::tcam::Wrap(mappedBefore+crew::tcam::Wrap(mappedAfter-mappedBefore)*t);
            const float want=crew::tcam::Wrap(before+crew::tcam::Wrap(corrected-before)*t);
            if(std::fabs(crew::tcam::Wrap(drawn-want))>1e-6f)return 2;
            ++checked;
        }
    }
    std::printf("stab_interpolation_test: %d interpolation samples passed; native motor rates preserved\n",checked);
}
