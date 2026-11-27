// edf::Matches (edf/patch.h) in a file of its own: it reads, writes nothing, and the fixtures over a fake image stand in
// their own (tests/enemy_control_pvp_berserker_core_test.cpp: its SetTeam entry is a jump to the fixture's) while still
// linking the patch primitives.
#include "edf/patch.h"
#include <cstring>

namespace edf {
bool Matches(const unsigned char* image,std::size_t rva,const unsigned char* bytes,std::size_t size) noexcept {
    return image && std::memcmp(image+rva,bytes,size)==0;
}
}  // namespace edf
