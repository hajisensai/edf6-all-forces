// Live cockpit geometry. Field order and digit segment order are shared with pylib/cockpit_instruments.py.
#pragma once
#include "fp_view.h"

namespace crew {
namespace instruments {
// The weapons screen's fields are appended (a model built before them keeps its numbers): arm the picked weapon's
// picture (hud_cue.h CockpitArmGroup), shell its round's tag (reticle.h Shell, none unavailable), mag its rounds over its
// magazine (share; unavailable: no magazine read), load its loading (share; available only while it loads), empty 1 when
// it is spent for good, pick its row; row + i weapon i's rounds (share: its magazine's fill), rowLoad + i its loading.
constexpr int kRows=4;
enum Field { hp,hull,fuel,speed,altitude,heading,slot,ammo,warn,climb,pitch,power,
             arm,shell,mag,load,empty,pick,row,rowLoad=row+kRows,count=rowLoad+kRows };
struct Readings { float value[count]; float share[count]; bool contact[6]; float at[6][2]; int language=0; bool flight=false; };

// Native cabin fragments retain their original animated parent outside the local
// seat's first-person draw. This gate does not depend on live instrument readings.
inline fpview::M4 ShellPose(const fpview::M4& original,unsigned mask,int seat) noexcept {
    if(seat<0 || seat>=31 || !(mask&(1u<<seat)))return original;
    const float origin[3]={0,0,0};
    return fpview::Collapse(origin);
}

// One weapon of the seat, for the weapons screen: its picture's group, rounds, magazine fill (< 0 no magazine read),
// loading (< 0 not loading: hud_cue.h CellLoad), round tag (reticle.h Shell as an int, 0 none) and whether it is spent
// for good (no rounds, no reload).
struct ArmRow { int group=-1; int rounds=-1; float magazine=-1.0f; float loading=-1.0f; int shell=0; bool spent=false; };

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
    r.value[arm]=a.group>=0 ? static_cast<float>(a.group) : -1.0f;
    r.value[shell]=a.shell>0 ? static_cast<float>(a.shell) : -1.0f;
    r.value[mag]=a.magazine>=0.0f && a.rounds>=0 ? static_cast<float>(a.rounds) : -1.0f;
    r.share[mag]=a.magazine>=0.0f && !loading ? unit(a.magazine) : 0.0f;
    r.value[load]=loading ? unit(a.loading)*100.0f : -1.0f;r.share[load]=loading ? unit(a.loading) : 0.0f;
    r.value[empty]=a.spent ? 1.0f : -1.0f;
    r.value[pick]=n>1 && picked<kRows ? static_cast<float>(picked) : -1.0f;
    for(int i=0;i<kRows;++i) {
        const bool listed=n>1 && i<n;
        const ArmRow& w=rows[listed ? i : 0];
        const bool busy=listed && w.loading>=0.0f;
        r.value[row+i]=listed ? static_cast<float>(w.rounds>0 ? w.rounds : 0) : -1.0f;
        r.share[row+i]=!listed || busy ? 0.0f : w.magazine>=0.0f ? unit(w.magazine) : w.rounds>0 ? 1.0f : 0.0f;
        r.value[rowLoad+i]=busy ? unit(w.loading)*100.0f : -1.0f;r.share[rowLoad+i]=busy ? unit(w.loading) : 0.0f;
    }
}

inline bool Available(const Readings& r,int field) noexcept {
    return field>=0 && field<count && std::isfinite(r.value[field]) &&
        ((field!=hp && field!=hull) || std::isfinite(r.share[field])) &&
        ((field==climb || field==pitch) ? r.flight : r.value[field]>=0);
}

inline bool Segment(float value,int place,int segment) noexcept {
    constexpr unsigned masks[]={0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
    if(place<0 || place>4 || segment<0 || segment>6)return false;
    if(!std::isfinite(value) || value<0.0f || value>99999.0f)return segment==6; // unavailable / overflow: dashes
    int div=1;for(int i=0;i<place;++i)div*=10;
    const int number=static_cast<int>(value+0.5f);
    if(place && number<div)return false;
    return (masks[(number/div)%10]&(1u<<segment))!=0;
}

inline fpview::M4 Pose(const fpview::M4& original,wchar_t mode,int field,int place,int segment,
                      const Readings& readings,bool ready) noexcept {
    if(mode==L'D' || mode==L'B' || mode==L'T')mode+=L'a'-L'A'; // new widgets are neutral outside their owning seat
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
    if(mode==L'd' || mode==L'p') {
        const float number=mode==L'p' && v>=0 && std::isfinite(v) ? fpview::Clamp(readings.share[field],0,1)*100 : v;
        return Segment(number,place,segment) ? original : fpview::Collapse(original.m+12);
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
