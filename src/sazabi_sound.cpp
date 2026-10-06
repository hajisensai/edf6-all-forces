// The Sazabi's sounds (sazabi_sound.h). Not made yet: every call is silent until the clips are (vsynth.h, jetaudio.cpp).
#include "sazabi_sound.h"

namespace crew {
void SazabiSfx(SzSfx which,const float* pos) noexcept { (void)which;(void)pos; }
void SazabiLoop(SzLoop which,const float* pos,const float* vel,float level) noexcept { (void)which;(void)pos;(void)vel;(void)level; }
void SazabiSoundTick() noexcept {}
}  // namespace crew
