// Compile as a tiny shared library for proteus_damage_native_audit.py. The code
// under test is the production header, including its object/weak/team readers.
#include "../src/proteus_damage_gate.h"
#include <cstring>

namespace {
bool Read(const void* p,std::size_t offset,void* out,std::size_t size) noexcept {
    if(!p)return false;
    std::memcpy(out,static_cast<const unsigned char*>(p)+offset,size);
    return true;
}
}
extern "C" __declspec(dllexport) bool ProteusDamageEligible(const void* object,const void* gdi,const void* team) noexcept {
    crew::proteus_damage_gate::Facts facts{};
    return crew::proteus_damage_gate::ReadFacts(&Read,object,gdi,team,facts) &&
           crew::proteus_damage_gate::Eligible(facts);
}
