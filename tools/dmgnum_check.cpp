// The damage numbers' colours and the allies' numbers (src/dmgnum.h) checked offline: a hit is friendly fire when the
// damage function said so (before any weak spot), a weak spot when its part multiplier is above 1 or the victim is a
// weak-spot part object, normal otherwise (armour, an unreadable NaN); the colours follow the ini (0xRRGGBB), the
// switch off gives the stock white back for the player's own and the ally colour for an ally's normal hit; only the
// NPCs', the friendly objects' and the unmanned vehicles' hits are handed to the player's screen; a zero is not; the
// shim's bytes put its two absolute addresses where src/dmgnum.cpp writes them and hand Hit the damage function's rsp.
//   cmake --build build --target dmgnum_check && build\dmgnum_check.exe      (exit code 1 on a failure)
#include "../src/dmgnum.h"
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace dmgnum;
int failures=0,cases=0;
void Check(bool ok,const char* what) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}
bool Same(const Rgba& a,const Rgba& b) {
    return std::fabs(a.r-b.r)<1e-6f && std::fabs(a.g-b.g)<1e-6f && std::fabs(a.b-b.b)<1e-6f && std::fabs(a.a-b.a)<1e-6f;
}

void Kinds() {
    Check(Classify(false,1.0f,false)==Kind::normal,"no part: normal");
    Check(Classify(false,0.5f,false)==Kind::normal,"armour (below 1): normal");
    Check(Classify(false,1.0005f,false)==Kind::normal,"1 within the float's noise: normal");
    Check(Classify(false,1.5f,false)==Kind::weak,"part multiplier 1.5: weak spot");
    Check(Classify(false,10.0f,false)==Kind::weak,"part multiplier 10: weak spot");
    Check(Classify(false,1.0f,true)==Kind::weak,"a weak-spot part object: weak spot");
    Check(Classify(true,1.0f,false)==Kind::friendly,"friendly fire");
    Check(Classify(true,3.0f,true)==Kind::friendly,"friendly fire on a weak part: still friendly fire");
    Check(Classify(false,std::numeric_limits<float>::quiet_NaN(),false)==Kind::normal,"NaN multiplier: normal");
    Check(Classify(false,std::numeric_limits<float>::infinity(),false)==Kind::weak,"inf multiplier: weak (above 1)");
}

void WeakParts() {
    Check(WeakPartClass(".?AVEDF6_GrandMotherParts_Weak@@"),"the GrandMother weak part");
    Check(!WeakPartClass(".?AVEDF6_GrandMotherParts_Gate@@"),"the GrandMother gate is not a weak part");
    Check(!WeakPartClass(".?AVEDF6_GrandMotherParts_Weak@@X"),"a longer name is not the class");
    Check(!WeakPartClass(".?AVEDF6_GrandMotherParts_Weak"),"a prefix is not the class");
    Check(!WeakPartClass(""),"an empty name");
    Check(!WeakPartClass(nullptr),"no class");
}

void Colours() {
    Check(Same(FromRgb(0xFFFFFF),kStockFill),"0xFFFFFF is the stock white");
    Check(Same(FromRgb(0x000000),Rgba{0.0f,0.0f,0.0f,1.0f}),"0x000000 opaque black");
    Check(Same(FromRgb(0xFF8000),Rgba{1.0f,128.0f/255.0f,0.0f,1.0f}),"0xFF8000 order R,G,B");
    Check(Same(FromRgb(0xFF123456),FromRgb(0x123456)),"bits above 24 ignored");
    Colors c;
    c.weak=0xFFC800;c.friendly=0xFF4040;c.ally=0x80C0FF;
    Check(Same(Fill(c,Kind::normal,false),kStockFill),"mine, normal: stock white");
    Check(Same(Fill(c,Kind::weak,false),FromRgb(0xFFC800)),"mine, weak: the weak colour");
    Check(Same(Fill(c,Kind::friendly,false),FromRgb(0xFF4040)),"mine, friendly: the friendly colour");
    Check(Same(Fill(c,Kind::normal,true),FromRgb(0x80C0FF)),"an ally's normal: the ally colour");
    Check(Same(Fill(c,Kind::weak,true),FromRgb(0xFFC800)),"an ally's weak spot: the weak colour");
    Check(Same(Fill(c,Kind::friendly,true),FromRgb(0xFF4040)),"an ally's friendly fire: the friendly colour");
    c.tint=false;
    Check(Same(Fill(c,Kind::weak,false),kStockFill),"tint off: mine stock white");
    Check(Same(Fill(c,Kind::friendly,false),kStockFill),"tint off: mine friendly stock white");
    Check(Same(Fill(c,Kind::friendly,true),FromRgb(0x80C0FF)),"tint off: an ally's in the ally colour");
    const Colors d;
    Check(d.tint && d.weak==0xFFC800 && d.friendly==0xFF4040 && d.ally==0xFFFFFF,"the defaults (the ini's)");
    Check(Same(kStockSecond,Rgba{0.0f,0.0f,0.0f,1.0f}),"the stock second colour");
}

void Allies() {
    using dmgstat::Side;
    Check(!AllyNumber(Side::me),"mine: the stock shows it");
    Check(!AllyNumber(Side::myVehicle),"my vehicle's: the stock shows it");
    Check(AllyNumber(Side::squad),"an NPC soldier's");
    Check(AllyNumber(Side::support),"an unmanned vehicle's");
    Check(AllyNumber(Side::ally),"a friendly object's");
    Check(!AllyNumber(Side::teammate),"another player's: their screen");
    Check(!AllyNumber(Side::enemy),"an enemy's");
    Check(!AllyNumber(Side::world),"the world's");
    Check(WorthShowing(-12.0f) && WorthShowing(5.0f),"a hit and a heal");
    Check(!WorthShowing(0.0f) && !WorthShowing(-0.0f),"a zero");
    Check(!WorthShowing(std::numeric_limits<float>::quiet_NaN()),"NaN");
}

std::uint32_t Rel32(unsigned at) {
    return static_cast<std::uint32_t>(kStub[at])|static_cast<std::uint32_t>(kStub[at+1])<<8|
           static_cast<std::uint32_t>(kStub[at+2])<<16|static_cast<std::uint32_t>(kStub[at+3])<<24;
}

void Stub() {
    Check(kStub[kStubHook-2]==0x48 && kStub[kStubHook-1]==0xB8,"mov rax,imm64 before the Hit slot");
    Check(kStub[kStubHook+8]==0xFF && kStub[kStubHook+9]==0xD0,"call rax after it");
    Check(kStub[kStubBack-6]==0xFF && kStub[kStubBack-5]==0x25 && Rel32(kStubBack-4)==0,"jmp [rip+0] before the back slot");
    // push rcx (8) + sub rsp,38h: the damage function's rsp is rsp+40h, the 5th / 6th arguments at +20h / +28h.
    Check(kStub[0]==0x51 && kStub[1]==0x48 && kStub[2]==0x83 && kStub[3]==0xEC && kStub[4]==0x38,"push rcx; sub rsp,38h");
    Check((8+0x38)%16==0,"the call's rsp is 16-aligned (the damage function's is)");
    Check(kStub[25]==0x4C && kStub[26]==0x8D && kStub[27]==0x4C && kStub[28]==0x24 && kStub[29]==8+0x38,"lea r9,[rsp+40h]");
    Check(kStub[8]==0x24 && kStub[9]==0x20 && kStub[14]==0x24 && kStub[15]==0x28,"5th at [rsp+20h], 6th at [rsp+28h]");
    Check(kStub[42]==0x48 && kStub[43]==0x83 && kStub[44]==0xC4 && kStub[45]==0x38 && kStub[46]==0x59,"add rsp,38h; pop rcx");
}
}  // namespace

int main() {
    Kinds();
    WeakParts();
    Colours();
    Allies();
    Stub();
    std::printf("dmgnum_check: %d/%d passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
