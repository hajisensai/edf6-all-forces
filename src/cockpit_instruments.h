// Live cockpit geometry. The fields' numbers are pylib/cockpit_fields.py's (src/cockpit_fields.inc, generated); the digit
// segment order is shared with pylib/cockpit_instruments.py.
#pragma once
#include "fp_view.h"
#include "cockpit_fields.inc"
#include <cwchar>

namespace crew {
namespace instruments {
// Fields appended after power (a model built before them keeps its numbers). The weapons screen's: arm the picked
// weapon's round class (hud_cue.h CockpitRound: its colour, tag and picture), mag its rounds over its magazine (share;
// unavailable: no magazine read), load its loading (share; available only while it loads), empty 1 when it is spent for
// good, pick its cell; for each of the kRows cells (a window over the seat's weapons, the picked one in it) row + i its
// rounds (share: its magazine's fill, or its loading while it loads: one bar a cell), rowRound + i its round class. roll: the bank, degrees,
// right wing down positive (flight only, as climb and pitch).
// kRows and Field: src/cockpit_fields.inc.
struct Readings { float value[count]; float share[count]; bool contact[6]; float at[6][2]; int language=0; bool flight=false; };

// Native cabin fragments retain their original animated parent outside the local
// seat's first-person draw. This gate does not depend on live instrument readings.
inline fpview::M4 ShellPose(const fpview::M4& original,unsigned mask,int seat) noexcept {
    if(seat<0 || seat>=31 || !(mask&(1u<<seat)))return original;
    const float origin[3]={0,0,0};
    return fpview::Collapse(origin);
}

// The panoramic cockpit (pylib/cockpit_kits_mech.py panoramic pods; docs/feature-panoramic-cockpit.md). A panoramic
// seat's kit rides vc_pan_<seat> instead of the vehicle's own bone, so the local rider's draw can fold every bone of
// the vehicle's own (its hull: nothing of it stands between the eye and the world) and keep the seat and its screens.
// Its pod's walls are the screen: tiles vc_boot_<seat>_<step>_<kind> (kind 0 the dark tile, 1 its glow, a hair nearer
// the eye). On a vehicle's first start (crew.cpp VehiclePowered) the boot runs once, kBootEnd seconds, never skipped:
// all dark, the live screens light at kBootScreens, then each step's tiles flash and give way to the world, steps
// kBootFirst..kBootFirst+kBootSpan apart (pylib's BOOT_STEPS is kBootSteps: the steps spread outward from ahead).
// `t` is the seconds since the boot began (a seat that does not boot: past kBootEnd).
constexpr int kBootSteps=12;
constexpr float kBootScreens=0.6f,kBootFirst=1.0f,kBootSpan=1.8f,kBootGlow=0.14f,kBootEnd=3.0f;
constexpr float BootStepAt(int step) noexcept { return kBootFirst+kBootSpan*static_cast<float>(step)/static_cast<float>(kBootSteps-1); }
constexpr bool BootScreensOn(float t) noexcept { return t>=kBootScreens; }
// A tile of `step` shown at `t`: the dark one until its step, the glow round it.
constexpr bool BootShown(float t,int step,int kind) noexcept {
    if(step<0 || step>=kBootSteps)return false;
    const float at=BootStepAt(step);
    return kind==0 ? t<at : (t>at-kBootGlow && t<at+kBootGlow);
}
static_assert(BootShown(0.0f,0,0) && !BootShown(0.0f,0,1),"the boot begins dark");
static_assert(!BootShown(kBootEnd,kBootSteps-1,0) && !BootShown(kBootEnd,kBootSteps-1,1),"the boot is over by kBootEnd");
static_assert(kBootScreens<BootStepAt(0)-kBootGlow,"the screens light before the first tile");

// A panoramic pod keeps the vehicle's arms in its rider's view (the user, 2026-11-26: 「看不到机甲的手臂，不知道攻击打到
//哪里」): the bones of an arm chain are not folded with the hull. Balam's (jowan / kawan the upper / fore arm, hand,
// fing* its claw) and the smaller mechs' (Begaruta, Nix: jowan / kawan / weapon_hand / weapon_lowerArm /
// weapon_upperArm; the old robot: arm / arm_slide and the weapon_ ones). Not the shoulder armour (shoulderArmor*,
// kata_): it stands beside the eye.
inline bool PanoramicArm(const wchar_t* name) noexcept {
    if(!name)return false;
    static constexpr const wchar_t* kArm[]={L"jowan_",L"kawan_",L"hand_",L"fing",L"arm_",L"weapon_hand_",
                                            L"weapon_lowerArm_",L"weapon_upperArm_"};
    for(const wchar_t* a:kArm)if(std::wcsncmp(name,a,std::wcslen(a))==0)return true;
    return false;
}

// The seat's two control sticks (pylib/cockpit_kits_mech.py _sticks): bone vc_i_<seat>_<i>_j_<hand>_<degrees>_0 at
// a stick's foot (rows: x the eye's left, y up the shaft, z ahead), hand 0 the left stick (the seat's left stick: the
// move), 1 the right (its right stick: the aim). `x` right positive and `y` forward negative (the seat block's,
// seat+0x2C0 / +0x2C4, +0x2D0 / +0x2D4, each -1..1): the stick leans ahead by -y and to the right by x, `degrees` at
// full throw.
inline fpview::M4 StickPose(const fpview::M4& original,float x,float y,int degrees) noexcept {
    const float most=static_cast<float>(degrees)*0.01745329252f;
    const float ahead=-fpview::Clamp(std::isfinite(y) ? y : 0.0f,-1.0f,1.0f)*most;
    const float right=fpview::Clamp(std::isfinite(x) ? x : 0.0f,-1.0f,1.0f)*most;
    const float ax[3]={1,0,0},az[3]={0,0,1};
    // Row vectors: the foot's frame turned about its x (the tip toward +z), then its z (the tip toward -x: right).
    return fpview::Mul(fpview::Mul(fpview::Rotation(ax,std::cos(ahead),std::sin(ahead)),
                                   fpview::Rotation(az,std::cos(right),std::sin(right))),original);
}
// A stick's lean follows its input over kStickLag seconds (a mouse's right stick is one frame's move: it would twitch).
constexpr float kStickLag=0.08f;
inline float StickFollow(float now,float want,float dt) noexcept {
    if(!std::isfinite(want))want=0.0f;
    if(!std::isfinite(now) || !(dt>0.0f))return std::isfinite(now) ? now : 0.0f;
    return now+(want-now)*(1.0f-std::exp(-dt/kStickLag));
}

// One weapon of the seat, for the weapons screen: its round class (hud_cue.h CockpitRound), rounds, magazine fill (< 0
// no magazine read), loading (< 0 not loading: hud_cue.h CellLoad) and whether it is spent for good (no rounds, no
// reload).
struct ArmRow { int round=-1; int rounds=-1; float magazine=-1.0f; float loading=-1.0f; bool spent=false; };

// The first of the kRows weapons the screen's cells show: all of them when they fit, else a window round the picked one.
constexpr int RowWindow(int n,int picked) noexcept {
    return n<=kRows || picked<1 ? 0 : picked-1>n-kRows ? n-kRows : picked-1;
}

// The weapons screen's readings from the seat's weapons in the order the screen lists them, `picked` the one it fires.
// A weapon loading shows its loading in place of its magazine's fill (the bar is the one gauge of "when can I fire");
// without a magazine read a listed weapon's bar is full while it has rounds. The old fields (slot, ammo) stay filled for
// a model built before this screen.
inline void ArmReadings(Readings& r,const ArmRow* rows,int n,int picked) noexcept {
    auto unit=[](float v){return v<0.0f ? 0.0f : v>1.0f ? 1.0f : v;};
    if(!rows || n<=0 || picked<0 || picked>=n)return;
    const ArmRow& a=rows[picked];
    const bool loading=a.loading>=0.0f;
    r.value[slot]=static_cast<float>(picked+1);
    r.value[ammo]=a.rounds>=0 ? static_cast<float>(a.rounds) : -1.0f;
    r.value[arm]=a.round>=0 ? static_cast<float>(a.round) : -1.0f;
    r.value[mag]=a.magazine>=0.0f && a.rounds>=0 ? static_cast<float>(a.rounds) : -1.0f;
    r.share[mag]=a.magazine>=0.0f && !loading ? unit(a.magazine) : 0.0f;
    r.value[load]=loading ? unit(a.loading)*100.0f : -1.0f;r.share[load]=loading ? unit(a.loading) : 0.0f;
    r.value[empty]=a.spent ? 1.0f : -1.0f;
    const int first=RowWindow(n,picked);
    r.value[pick]=n>1 ? static_cast<float>(picked-first) : -1.0f;
    for(int i=0;i<kRows;++i) {
        const int k=first+i;
        const bool listed=n>1 && k<n;
        const ArmRow& w=rows[listed ? k : 0];
        const bool busy=listed && w.loading>=0.0f;
        r.value[row+i]=listed ? static_cast<float>(w.rounds>0 ? w.rounds : 0) : -1.0f;
        r.share[row+i]=!listed ? 0.0f : busy ? unit(w.loading) : w.magazine>=0.0f ? unit(w.magazine) : w.rounds>0 ? 1.0f : 0.0f;
        r.value[rowRound+i]=listed && w.round>=0 ? static_cast<float>(w.round) : -1.0f;
    }
}

// Readings of the whole vehicle, not of one seat (its weapons, its armour): the speed, height, heading, climb, pitch
// and bank are the same from every seat, so a neighbour's compass card, altimeter, attitude card and their numbers
// show them live too (a neighbour's weapons screen does not show this seat's weapons).
constexpr bool VehicleField(int field) noexcept {
    return field==speed || field==altitude || field==heading || field==climb || field==pitch || field==roll;
}

// How a widget bone (mode, field, place as its name gives them; `fields` how many the name had) of `mine` or a neighbour
// seat is posed: skip (left as the model has it), neutral (posed without readings: hidden or at rest) or live (posed with
// the readings when they are ready). l's field is a language and t's a contact, not a reading.
enum class SeatPose { skip, neutral, live };
inline SeatPose SeatPoseOf(bool mine,wchar_t mode,int field,int place,int fields) noexcept {
    if(mine)return SeatPose::live;
    if(mode!=L'l' && mode!=L't' && VehicleField(field))return SeatPose::live;
    if(mode==L'l')return place==0 ? SeatPose::live : SeatPose::neutral;
    const bool rangedNeedle=mode==L'n' && fields>=5;
    const bool neutralWidget=mode==L'D' || mode==L'B' || mode==L'T' || mode==L'Z';
    // Adjacent stations visible from this seat use the same language, while their new health/percent widgets remain
    // neutral without that seat's live data. A weapons screen's picture / tag / frame (e) has no neutral look: another
    // seat's is hidden. A compass card / altimeter hand / attitude card (h, w, g) reads only vehicle fields (above);
    // one on another field is hidden as well.
    if(mode==L'c' || mode==L'p' || mode==L'v' || mode==L'u' || mode==L'e' || mode==L'r' || mode==L'q' || mode==L'a'
        || mode==L'h' || mode==L'w' || mode==L'g' || rangedNeedle || neutralWidget)return SeatPose::neutral;
    return SeatPose::skip;
}

// The bank of a seat's eye frame `m` (rows left, up, ahead in the world: vc_eye_<seat>), degrees, right wing down
// positive; -1 for a degenerate frame. The attitude cards' roll (firstperson.cpp FirstPersonInstrumentReadings) and the
// head-up glass's bank scale (hud.cpp HeadUpGlass) read it the same way.
inline float BankOf(const float* m) noexcept {
    return std::isfinite(m[1]+m[5]) && (m[1]!=0.0f || m[5]!=0.0f) ? std::atan2(m[1],m[5])/0.01745329252f : -1.0f;
}

inline bool Available(const Readings& r,int field) noexcept {
    return field>=0 && field<count && std::isfinite(r.value[field]) &&
        ((field!=hp && field!=hull) || std::isfinite(r.share[field])) &&
        ((field==climb || field==pitch || field==roll) ? r.flight : r.value[field]>=0);
}

// pad: leading zeros lit (a heading's 074).
inline bool Segment(float value,int place,int segment,bool pad=false) noexcept {
    constexpr unsigned masks[]={0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
    if(place<0 || place>4 || segment<0 || segment>6)return false;
    if(!std::isfinite(value) || value<0.0f || value>99999.0f)return segment==6; // unavailable / overflow: dashes
    int div=1;for(int i=0;i<place;++i)div*=10;
    const int number=static_cast<int>(value+0.5f);
    if(place && number<div && !pad)return false;
    return (masks[(number/div)%10]&(1u<<segment))!=0;
}

inline fpview::M4 Pose(const fpview::M4& original,wchar_t mode,int field,int place,int segment,
                      const Readings& readings,bool ready) noexcept {
    if(mode==L'D' || mode==L'B' || mode==L'T' || mode==L'Z')mode+=L'a'-L'A'; // new widgets are neutral outside their owning seat
    if(mode==L'l') {
        const bool available=place==0 || (place>0 && place<=count && std::isfinite(readings.value[place-1]) && readings.value[place-1]>=0);
        return ready && field>=0 && field<4 && field==readings.language && available ? original : fpview::Collapse(original.m+12);
    }
    if(field<0 || field>=count)return original;
    if(mode==L'v')return ready && Available(readings,field) ? original : fpview::Collapse(original.m+12);
    if(mode==L'u')return !ready || !Available(readings,field) ? original : fpview::Collapse(original.m+12);
    // e_<field>_<value>: shown while the field reads `value` (one of a set of pictures, tags, frames).
    if(mode==L'e')return ready && Available(readings,field) && static_cast<int>(readings.value[field]+0.5f)==place ?
        original : fpview::Collapse(original.m+12);
    // Clockwise by `degrees` as the face is seen (the needles' sense) about the bone's own axis.
    auto turned=[&](float degrees,const fpview::M4& base){
        const float a=degrees*0.01745329252f,axis[3]={0,0,1};
        return fpview::Mul(fpview::Rotation(axis,std::cos(a),-std::sin(a)),base);
    };
    // h_<field>_<only>: a compass card turned so the field's heading stands under the lubber line at the top (1: only on
    // the ground, 2: only in flight: a card that gives way to the attitude indicator).
    if(mode==L'h') {
        if(!ready || !Available(readings,field) || (place==1 && readings.flight) || (place==2 && !readings.flight))
            return fpview::Collapse(original.m+12);
        return turned(-readings.value[field],original);
    }
    // w_<field>_<per>: a needle round the whole dial once each `per` (an altimeter's hand).
    if(mode==L'w') {
        if(!ready || !Available(readings,field) || place<=0)return fpview::Collapse(original.m+12);
        return turned(360.0f*readings.value[field]/static_cast<float>(place),original);
    }
    // g_<pitch>_<k>_<limit>: an attitude indicator's horizon: down by the nose's pitch (k / 10000 of the bone's unit a
    // degree, at most limit / 10000), then turned against the bank about the instrument's centre. Flight only.
    if(mode==L'g') {
        if(!ready || !Available(readings,field) || !Available(readings,roll))return fpview::Collapse(original.m+12);
        const float most=static_cast<float>(segment)*1e-4f;
        const float drop=fpview::Clamp(-readings.value[field]*static_cast<float>(place)*1e-4f,-most,most);
        // Dropped along the card's own vertical (perpendicular to its horizon), then the card turned: v T R O.
        return fpview::Mul(fpview::Translate(0,drop,0),turned(-readings.value[roll],original));
    }
    if(mode==L'c') {
        const float share=readings.share[field];
        const bool known=ready && std::isfinite(share) && std::isfinite(readings.value[field]) && readings.value[field]>=0;
        const int level=share>.55f ? 0 : share>.25f ? 1 : 2;
        return known && place==level ? original : fpview::Collapse(original.m+12);
    }
    if(mode==L't') {
        if(field>=6 || !ready || !readings.contact[field])return fpview::Collapse(original.m+12);
        return fpview::Mul(fpview::Translate(readings.at[field][0],readings.at[field][1],0),original);
    }
    const float v=ready ? readings.value[field] : -1.0f;
    if(mode==L'd' || mode==L'p' || mode==L'z') {
        const float number=mode==L'p' && v>=0 && std::isfinite(v) ? fpview::Clamp(readings.share[field],0,1)*100 : v;
        return Segment(number,place,segment,mode==L'z') ? original : fpview::Collapse(original.m+12);
    }
    if(mode==L'b') {
        const float s=ready ? readings.share[field] : 0.0f;
        const float k=std::isfinite(s) ? fpview::Clamp(s,0.0f,1.0f) : 0.0f;
        fpview::M4 out=original;
        for(int i=0;i<3;++i)out.m[i]*=k;
        return out;
    }
    if(mode==L'n' || mode==L'r' || mode==L'q' || mode==L'a') {
        if(!ready || !Available(readings,field))return fpview::Collapse(original.m+12);
        const float maximum=place>0 ? static_cast<float>(place) : field==speed ? 2000.0f : field==altitude ? 3000.0f : 360.0f;
        const float share=mode==L'q' ? readings.share[field] : mode==L'a' ? (v+maximum)/(2*maximum) : v/maximum;
        if(!std::isfinite(share))return fpview::Collapse(original.m+12);
        const float a=(-135.0f+270.0f*fpview::Clamp(share,0.0f,1.0f))*0.01745329252f;
        const float axis[3]={0,0,1};
        return fpview::Mul(fpview::Rotation(axis,std::cos(a),-std::sin(a)),original);
    }
    return original;
}
} // namespace instruments
} // namespace crew
