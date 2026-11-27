// The vehicle HUD's ground-vehicle parts (split from hud.cpp; the HUD's overview is hud.cpp's head): the friendly
// vehicles' readouts and the carriers' panel, the payload and seat cues, the loadout strip, the drill, the Sazabi, the
// Dreadnought, the Balam's marks, the turret's aim, and the stock vehicles' HUD and ammo (vhud.cpp gathers it).
#include "hud_internal.h"
#include "config.h"
#include "plugin_log.h"

namespace crew {
namespace hudi {

// The snapshot's entries nearest the player (as of the snapshot), nearest first.
int Nearest(const Snapshot& s,Data* out,int most) noexcept {
    const float* me=s.me;
    float dist[kMaxShown];
    int n=0;
    for(int i=0;i<s.count && i<kEntries;++i) {
        const Data& d=s.d[i];
        const float dx=d.pos[0]-me[0],dy=d.pos[1]-me[1],dz=d.pos[2]-me[2],r=dx*dx+dy*dy+dz*dz;
        int at=n;
        while(at>0 && dist[at-1]>r)--at;
        if(at>=most)continue;
        for(int k=(n<most ? n : most-1);k>at;--k){out[k]=out[k-1];dist[k]=dist[k-1];}
        out[at]=d;dist[at]=r;
        if(n<most)++n;
    }
    return n;
}

const float* HpColour(float share) noexcept { return share>0.6f ? kGreen : share>0.3f ? kYellow : kRed; }
const float* HullColour(float share) noexcept { return share>0.5f ? kCyan : share>0.25f ? kAmber : kRed; }

// The readouts: a line each over its vehicle, the HP bar under it (bars now, the line later).
constexpr float kReadoutFar=400.0f;   // a readout's bar reads at its smallest from this far on
int Readouts(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,Line* lines,int at,
             const Snapshot& snap,int* shown,ULONGLONG now) noexcept {
    Data best[kMaxShown];
    int most=Cfg().vehicleHudCount;
    most=most<0 ? 0 : most>kMaxShown ? kMaxShown : most;
    const int n=Nearest(snap,best,most);
    *shown=0;
    for(int i=0;i<n && LineSink{lines,&at}.Room(1,__func__);++i) {
        const Data& d=best[i];
        const float over[3]={d.pos[0],d.pos[1]+d.top,d.pos[2]};
        float sx,sy,depth;
        if(!Project(vp,over,width,height,&sx,&sy,&depth))continue;
        const float share=d.hpMax>0.0f ? Unit(d.hp/d.hpMax) : 0.0f;
        const float k=DepthScale(depth,kReadoutFar),ks=k*s;
        Line& l=lines[at];
        wchar_t kind[24];
        hudtext::WordTo(d.kind,kind,_countof(kind));
        Format(l,L"%ls %d%%",kind,static_cast<int>(std::lround(share*100.0f)));
        if(d.guns>=0)Append(l,Tr(Tx::readoutGuns),d.guns);
        if(d.missiles>=0)Append(l,Tr(Tx::readoutMissiles),d.missiles);
        if(d.drones>=0)Append(l,Tr(Tx::readoutDrones),d.drones);
        if(d.leaving)Append(l,L"%ls",Tr(Tx::readoutRtb));
        else if(d.fuel>=0.0f) {
            const int sec=static_cast<int>(d.fuel);
            Append(l,Tr(Tx::readoutFuel),sec/60,sec%60);
        }
        l.scale=kLineScale*k;l.rgba=d.leaving || (d.fuel>=0.0f && d.fuel<20.0f) ? kWarn : kWhite;
        l.w=l.h=0.0f;
        if(text)MeasureAll(*text,&l,1);
        const float bw=80.0f*ks,bh=7.0f*ks;
        Bar(drawer,ctx,sx-bw*0.5f,sy-bh,bw,bh,share,TrailOf(d.key,share,now),HpColour(share),ks);
        l.x=sx-l.w*0.5f;l.y=sy-bh-3.0f*ks-l.h;
        ++at;++*shown;
    }
    return at;
}

// A carrier's panel at the screen's right, from `top` down: its quads now, its lines later. Returns its bottom.
float Panel(void* drawer,void* ctx,Text* text,const CarrierPanel& p,int index,int count,float width,float top,float s,
            Line* lines,int* at) noexcept {
    const int first=*at;
    if(!LineSink{lines,at}.Room(2+p.parts,__func__))return top;
    Line* const title=&lines[first];
    Line* const hull=&lines[first+1];
    if(count>1)Format(*title,Tr(Tx::carrierTitleN),index+1);
    else Format(*title,L"%ls",Tr(Tx::carrierTitle));
    title->scale=kTitleScale;title->rgba=kTitle;
    const float hullShare=p.hullMax>0.0f ? p.hull/p.hullMax : 0.0f;
    Format(*hull,Tr(Tx::carrierHull),static_cast<int>(std::lround(hullShare*100.0f)),p.hull,p.hullMax);
    hull->scale=kLineScale;hull->rgba=kWhite;
    for(int k=0;k<p.parts;++k) {
        Line& l=lines[first+2+k];
        const auto& part=p.part[k];
        wchar_t name[24];
        hudtext::WordTo(part.name,name,_countof(name));
        if(part.down) {
            const int sec=static_cast<int>(part.repairSec);
            Format(l,Tr(Tx::carrierPartDown),name,sec/60,sec%60);
            l.rgba=kDown;
        } else {
            Format(l,L"%ls   %d%%",name,static_cast<int>(std::lround(part.max>0.0f ? part.hp*100.0f/part.max : 0.0f)));
            l.rgba=kWhite;
        }
        l.scale=kLineScale;
    }
    const int n=2+p.parts;
    for(int i=0;i<n;++i)lines[first+i].w=lines[first+i].h=0.0f;
    if(text)MeasureAll(*text,&lines[first],n);
    const float pad=8.0f*s,gap=4.0f*s,hullH=12.0f*s,partH=5.0f*s;
    float textW=0.0f;
    for(int i=0;i<n;++i)textW=lines[first+i].w>textW ? lines[first+i].w : textW;
    const float pw=textW+2.0f*pad>280.0f*s ? textW+2.0f*pad : 280.0f*s;
    const float x0=width-24.0f*s-pw,x=x0+pad,bw=pw-2.0f*pad;
    float y=top+pad;
    // The lines' places and the bars under them.
    title->x=x;title->y=y;y+=title->h+gap;
    hull->x=x;hull->y=y;y+=hull->h+gap*0.5f;
    const float hullY=y;y+=hullH+gap*1.5f;
    float partY[4]{};
    for(int k=0;k<p.parts;++k) {
        Line& l=lines[first+2+k];
        l.x=x;l.y=y;y+=l.h+gap*0.5f;
        partY[k]=y;y+=partH+gap;
    }
    const float bottom=y+pad-gap;
    Rect(drawer,ctx,x0,top,x0+pw,bottom,kPanel);
    Rect(drawer,ctx,x0,top,x0+3.0f*s,bottom,kCyan);           // the edge
    Rect(drawer,ctx,x0,top,x0+pw,top+2.0f*s,kCyan);
    Bar(drawer,ctx,x,hullY,bw,hullH,hullShare,hullShare,HullColour(hullShare),s);
    for(int k=0;k<p.parts;++k) {
        const auto& part=p.part[k];
        const float share=part.down || part.max<=0.0f ? 0.0f : part.hp/part.max;
        Bar(drawer,ctx,x,partY[k],bw,partH,share,share,part.down ? kRed : kTeal,s);
    }
    *at=first+n;
    return bottom;
}
// A seat with its own secondary (the BMPT) or weapons on their own fire keys (payload.cpp OwnKey): each control's round
// now with the one key that switches it, the primary's (PlayerJetSwitchKey / LB) and the own secondary's
// (VehicleSecondarySwitchKey / -Button, XInput bits), a control with one choice showing none (never "the last used":
// each key switches its own control, payload.cpp Switch); each extra fire key with the weapon it fires (an auto
// turret marked so); the auto turrets' switch and whether they are on.
void PayloadControls(Line& line,const PayloadReadout& p) noexcept {
    Format(line,L"");
    auto add=[&](Tx text,const wchar_t* key,const wchar_t* name){if(line.text[0])Append(line,L"   ");Append(line,Tr(text),key,name);};
    wchar_t key[32];
    if(p.choices>1 && p.picked>=0 && p.picked<p.count) {
        if(p.keys)KeyName(Cfg().playerJetSwitchKey,key,32);else SeatButtonName(p.switchButton,key,32);
        add(Tx::controlPrimaryRounds,key,p.entry[p.picked].name);
    }
    if(p.ownChoices>1 && p.ownPicked>=0 && p.ownPicked<p.count) {
        if(p.keys)KeyName(Cfg().vehicleSecondarySwitchKey,key,32);else PadBitsName(p.ownSwitchButton,key,32);
        add(Tx::controlOwnSecondary,key,p.entry[p.ownPicked].name);
    }
    for(int i=0;i<p.count && i<kMostPayload;++i) {
        const int k=p.entry[i].key;
        if(k==kSeatTriggerKey) {   // a gunner seat's own weapon: its attack button
            wchar_t name[48];
            std::swprintf(name,48,L"%ls%ls",p.entry[i].name,p.entry[i].autoTurret ? Tr(Tx::controlAutoMark) : L"");
            if(line.text[0])Append(line,L"   ");
            Append(line,Tr(Tx::controlSeatFire),name);
            continue;
        }
        if(k<3)continue;
        if(p.keys)KeyName(FireKeyVk(static_cast<unsigned char>(k)),key,32);else PadBitsName(FireKeyButton(static_cast<unsigned char>(k)),key,32);
        wchar_t name[48];
        std::swprintf(name,48,L"%ls%ls",p.entry[i].name,p.entry[i].autoTurret ? Tr(Tx::controlAutoMark) : L"");
        add(Tx::controlFireKey,key,name);
    }
    if(p.autoTurrets>0) {
        if(p.keys)KeyName(Cfg().vehicleAutoTurretKey,key,32);else PadBitsName(Cfg().vehicleAutoTurretButton,key,32);
        if(line.text[0])Append(line,L"   ");
        Append(line,Tr(p.autoOn ? Tx::controlAutoTurretsOn : Tx::controlAutoTurretsOff),key);
    }
}

// The mouse's aim: a hollow cyan square at `at` (the jets' and the helis', heliaim.h).
void AimSquare(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at) noexcept {
    float sx,sy,depth;
    if(!Project(vp,at,width,height,&sx,&sy,&depth))return;
    const float r=14.0f*s,t=2.0f*s;
    Rect(drawer,ctx,sx-r,sy-r,sx+r,sy-r+t,kCyan);Rect(drawer,ctx,sx-r,sy+r-t,sx+r,sy+r,kCyan);
    Rect(drawer,ctx,sx-r,sy-r,sx-r+t,sy+r,kCyan);Rect(drawer,ctx,sx+r-t,sy-r,sx+r,sy+r,kCyan);
}

// The mouse's aim on the keyboard and mouse: a hollow square where it aims, a small dot where the plane flies (with the
// flight HUD on, its flight path marker shows that: FighterHud).
void AimMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept {
    AimSquare(drawer,ctx,vp,width,height,s,j.aim);
    float sx,sy,depth;
    if(!Cfg().playerJetFlightHud && Project(vp,j.path,width,height,&sx,&sy,&depth)) {
        const float r=4.0f*s;
        Rect(drawer,ctx,sx-r,sy-r,sx+r,sy+r,kWhite);
    }
}

// An impact point (CCIP) at `at`: a yellow cross with a gap at its centre. False when it is off the screen.
bool ImpactCross(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at,float* sx,float* sy) noexcept {
    float depth;
    if(!Project(vp,at,width,height,sx,sy,&depth))return false;
    const float x=*sx,y=*sy,r=16.0f*s,g=5.0f*s,t=2.0f*s;
    Rect(drawer,ctx,x-r,y-t*0.5f,x-g,y+t*0.5f,kYellow);Rect(drawer,ctx,x+g,y-t*0.5f,x+r,y+t*0.5f,kYellow);
    Rect(drawer,ctx,x-t*0.5f,y-r,x+t*0.5f,y-g,kYellow);Rect(drawer,ctx,x-t*0.5f,y+g,x+t*0.5f,y+r,kYellow);
    return true;
}

// The selected unguided store's map impact (bomb or rocket CCIP; guided stores keep their lock mark).
void ImpactMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept {
    float sx,sy;
    if(j.hasImpact)ImpactCross(drawer,ctx,vp,width,height,s,j.impact,&sx,&sy);
}

// The Katyusha's impact point (launcher.cpp): the cross where a rocket fired now lands, a dotted ring round it as far
// as the ripple spreads, and under the cross the range, the flight time and the launcher's elevation (text later).
void LauncherMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const LauncherReadout& l,
                   Line* lines,int* at) noexcept {
    // The Andryusha's fuse line sits under the impact line; with no impact on the screen (out of reach, behind the
    // camera) under the screen's centre, so the fuse and its switch are seen wherever the rider aims.
    float fuseX=width*0.5f,fuseY=height*0.5f+40.0f*s;
    if(l.reach) {
        for(int i=0;i<l.rings && i<kLauncherRing;++i) {
            float x,y,depth;
            if(!Project(vp,l.ring[i],width,height,&x,&y,&depth))continue;
            const float r=2.5f*s;
            Rect(drawer,ctx,x-r,y-r,x+r,y+r,kYellow);
        }
        float sx,sy;
        if(ImpactCross(drawer,ctx,vp,width,height,s,l.impact,&sx,&sy) && LineSink{lines,at}.Room(1,__func__)) {
            Line& line=lines[(*at)++];
            Format(line,Tr(Tx::launcherLine),static_cast<int>(std::lround(l.range)),l.flight,static_cast<int>(std::lround(l.elevation)));
            line.scale=kLineScale;line.rgba=kYellow;line.w=line.h=0.0f;
            if(text)MeasureAll(*text,&line,1);
            line.x=sx-line.w*0.5f;line.y=sy+20.0f*s;
            fuseX=sx;fuseY=line.y+line.h+4.0f*s;
        }
    }
    if(!l.fuse || !LineSink{lines,at}.Room(1,__func__))return;
    wchar_t key[32]=L"";
    if(l.keys)KeyName(Cfg().vehicleSecondarySwitchKey,key,32);else PadBitsName(Cfg().vehicleSecondarySwitchButton,key,32);
    Line& fuse=lines[(*at)++];
    Format(fuse,Tr(l.fuse==2 ? Tx::andryushaFuseAirburst : Tx::andryushaFuseImpact),key);
    fuse.scale=kLineScale;fuse.rgba=l.fuse==2 ? kGreen : kYellow;fuse.w=fuse.h=0.0f;
    if(text)MeasureAll(*text,&fuse,1);
    fuse.x=fuseX-fuse.w*0.5f;fuse.y=fuseY;
}

// The artillery's high camera toggle (highcam.cpp, the user 2026-10-05): a line low on the screen naming its key,
// green while the high view is on.
void HighCamHint(Text* text,float width,float height,float s,bool on,bool keys,Line* lines,int* at) noexcept {
    if(!LineSink{lines,at}.Room(1,__func__))return;
    wchar_t key[32]=L"R3";
    if(keys)KeyName(Cfg().highCamKey,key,32);
    else if(Cfg().highCamButton!=0x80)std::swprintf(key,32,Tr(Tx::padButton),Cfg().highCamButton);
    if((keys && Cfg().highCamKey<=0) || (!keys && Cfg().highCamButton<=0))return;
    Line& l=lines[(*at)++];
    Format(l,Tr(on ? Tx::highCamOn : Tx::highCamOff),key);
    l.scale=kLineScale*0.85f;l.rgba=on ? kGreen : kWhite;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    l.x=(width-l.w)*0.5f;l.y=height*0.86f+2.0f*s;
}

// The seats of the vehicle the player sits in (seatswitch.cpp, the user 2026-10-06 / 2026-10-07): a line low on the
// screen, each seat's number, what it is and who holds it, the player's in brackets. The prompt's moment (p.hints) adds
// the keys that move them; amber a moment after a refused press (the seat named taken, or no free seat), grey online with
// SeatSwitchOnline off. Outside it (SeatList: the whole ride) the bare list, dimmed.
// The seats line's tail in the prompt's moment: the keys that move the player, or why they cannot (online).
void SeatKeys(Line& l,const SeatPrompt& p) noexcept {
    if(p.locked)Append(l,L"%ls",Tr(Tx::seatOnlineLocked));
    else if(p.keys) {
        wchar_t key[32];
        KeyName(Cfg().seatNextKey,key,32);
        if(Cfg().seatNextKey>0)Append(l,Tr(Tx::seatNextKey),key);
        if(Cfg().seatNumberKeys)Append(l,Tr(Tx::seatPick),p.seats<9 ? p.seats : 9);
    } else if(Cfg().seatButton==0x02)Append(l,Tr(Tx::seatNextKey),L"B");
    else if(Cfg().seatButton>0) {
        wchar_t button[32];
        std::swprintf(button,32,Tr(Tx::padButton),Cfg().seatButton);
        Append(l,Tr(Tx::seatNextKey),button);
    }
}

void SeatLine(Text* text,float width,float height,const SeatPrompt& p,Line* lines,int* at) noexcept {
    if(!LineSink{lines,at}.Room(1,__func__) || p.seats<2)return;
    Line& l=lines[(*at)++];
    Format(l,L"%ls",Tr(Tx::seats));
    const wchar_t* const holders[]={L"-",Tr(Tx::holderYou),Tr(Tx::holderNpc),Tr(Tx::holderTaken)};
    for(int i=0;i<p.seats && i<kMostSeatsShown;++i) {
        const Tx what=i==0 ? (p.aircraft ? Tx::seatPilot : Tx::seatDriver) : p.gun[i] ? Tx::seatGun : Tx::seatOther;
        Append(l,i==p.at ? L"  [%d %ls %ls]" : L"  %d %ls %ls",i+1,Tr(what),holders[static_cast<int>(p.holder[i])&3]);
    }
    if(p.hints)SeatKeys(l,p);
    if(p.refused==-2)Append(l,L"%ls",Tr(Tx::seatNoFree));
    else if(p.refused>=0)Append(l,Tr(Tx::seatTaken),p.refused+1);
    alignas(16) static const float kGrey[4]={0.7f,0.7f,0.7f,0.9f};
    alignas(16) static const float kDim[4]={1.0f,1.0f,1.0f,0.7f};
    l.scale=kLineScale*0.85f;l.rgba=!p.hints ? kDim : p.locked ? kGrey : p.refused!=-1 ? kWarn : kWhite;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    l.x=(width-l.w)*0.5f;l.y=height*0.82f;
}

// The controller's validated mouse command is an open circle; free look holds it in cyan.
// The actual gun/impact is a separate mark. Stock HUD already supplies that mark, so
// `square` only requests the legacy barrel-deviation square when that HUD is absent.
void TurretMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const TurretCamReadout& r,bool square) noexcept {
    if(r.physicalOnly || r.high)return;
    float sx,sy;
    // r.aim is the controller's validated mouse command/held point. Never use the
    // physical barrel endpoint r.gun as a surrogate mouse cursor.
    if(r.aimValid && (r.decoupled || r.freeLook) && sight::ToScreen(vp,r.aim,1.0f,width,height,&sx,&sy))
        Arc(drawer,ctx,sx,sy,14.0f*s,0.0f,6.283185307f,2.0f*s,28,r.freeLook ? kCyan : kWhite);
    if(square && r.aimValid && !r.onTarget && sight::ToScreen(vp,r.gun,1.0f,width,height,&sx,&sy)) {
        const float h=9.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-h,sy-h,sx+h,sy-h+t,kWhite);Rect(drawer,ctx,sx-h,sy+h-t,sx+h,sy+h,kWhite);
        Rect(drawer,ctx,sx-h,sy-h,sx-h+t,sy+h,kWhite);Rect(drawer,ctx,sx+h-t,sy-h,sx+h,sy+h,kWhite);
    }

}

const wchar_t* ThermalModeWord(thermal::Mode mode) noexcept {
    return Tr(mode==thermal::Mode::whiteHot ? Tx::thermalWhiteHot : mode==thermal::Mode::blackHot ? Tx::thermalBlackHot : Tx::thermalOptical);
}
const wchar_t* ThermalReasonWord(thermal::Reason reason) noexcept {
    switch(reason) {
    case thermal::Reason::unsupportedSeat: return Tr(Tx::thermalUnsupportedSeat);
    case thermal::Reason::notInSight: return Tr(Tx::thermalEnterSight);
    case thermal::Reason::hooksUnavailable:
    case thermal::Reason::unsupportedSurface: return Tr(Tx::thermalFallback);
    case thermal::Reason::waitingFrame: return Tr(Tx::thermalWaiting);
    case thermal::Reason::optical:
    case thermal::Reason::active: return L"";
    }
    return L"";
}
// The mode the player chose (the key's answer at once, in or out of the sight), then why it is not drawn yet when it is not.
// A press on a seat without the equipment is answered too (status.rejected), never silently dropped.
void ThermalControlText(Line& line,const thermal::Status& status) noexcept {
    // A pad seat's toggle is its seat-button chord (ini VehicleThermalButton), a keyboard seat's the key.
    wchar_t key[32];
    if(status.pad)SeatChordName(Cfg().thermalButton,key,_countof(key));else KeyName(Cfg().thermalKey,key,_countof(key));
    if(!status.eligible){Format(line,Tr(Tx::thermalControl),key,Tr(Tx::thermalUnsupportedSeat));return;}
    // A failed render (no hooks, unsupported surface) shows what is on screen, optical, with the fallback; a mode that
    // only waits (for the sight, for the first composite) shows the choice the key made.
    const bool failed=status.reason==thermal::Reason::unsupportedSurface || status.reason==thermal::Reason::hooksUnavailable;
    Format(line,Tr(Tx::thermalControl),key,ThermalModeWord(failed ? status.effective : status.requested));
    const wchar_t* reason=ThermalReasonWord(status.reason);
    const bool idle=status.requested==thermal::Mode::optical && status.reason==thermal::Reason::notInSight;   // nothing waits
    if(reason[0] && !idle)Append(line,L" / %ls",reason);
}
void ThermalControlHint(Text* text,float width,float height,float s,const thermal::Status& status,Line* lines,int* at) noexcept {
    if((!status.eligible && !status.rejected) || !LineSink{lines,at}.Room(1,__func__))return;
    Line& line=lines[(*at)++];ThermalControlText(line,status);
    line.scale=kLineScale*0.65f;line.rgba=!status.eligible || status.reason==thermal::Reason::unsupportedSurface ||
        status.reason==thermal::Reason::hooksUnavailable ? kAmber : kCyan;line.w=line.h=0.0f;
    if(text) {
        MeasureAll(*text,&line,1);
        const float most=std::fmin(720.0f*s,width-36.0f*s);
        if(line.w>most){line.scale*=most/line.w;MeasureAll(*text,&line,1);}
    }
    line.x=18.0f*s;line.y=height*0.3f+26.0f*s;
}

// Optical contacts are sampled on the game thread; drawing only projects the immutable HUD snapshot.
// Recognition remains native LOS-based; the image label reports the thermal compositor's effective mode.
void VehicleContacts(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                     const VehicleSensorReadout& sensor,const thermal::Status& thermalStatus,Line* lines,int* at) noexcept {
    if(sensor.kind!=VehicleSensorKind::optical)return;
    const float r=(sensor.gunship ? 14.0f : 11.0f)*s,t=std::fmax(1.0f,1.4f*s),arm=5.0f*s;
    for(int i=0;i<sensor.count && i<kVehicleContacts;++i) {
        const auto& contact=sensor.contact[i];
        float x,y,depth;
        if(!std::isfinite(contact.range) || contact.range<=0.0f ||
           !Project(vp,contact.at,width,height,&x,&y,&depth) ||
           x<r || x>width-r || y<r || y>height-r)continue;
        if(sensor.gunship) {
            float hx,hy;scopeview::RectHalf(width,height,&hx,&hy);
            if(x-r<width*0.5f-hx || x+r>width*0.5f+hx || y-r<height*0.5f-hy || y+r>height*0.5f+hy)continue;
        }
        for(int side=-1;side<=1;side+=2)for(int up=-1;up<=1;up+=2) {
            const float bx=x+static_cast<float>(side)*r,by=y+static_cast<float>(up)*r;
            Rect(drawer,ctx,side<0 ? bx : bx-arm,by-t*0.5f,side<0 ? bx+arm : bx,by+t*0.5f,kCyan);
            Rect(drawer,ctx,bx-t*0.5f,up<0 ? by : by-arm,bx+t*0.5f,up<0 ? by+arm : by,kCyan);
        }
    }
    Label(text,lines,at,18.0f*s,height*0.3f,0,kLineScale*0.65f,kCyan,
          Tr(Tx::vehicleSensorContacts),ThermalModeWord(thermalStatus.effective),sensor.count,static_cast<int>(std::lround(sensor.range)));
}

// Mark the real seat locator before the player is within the stock prompt's short reach. Large carriers place
// this beside their outer hull/wing: an origin-centred hint would direct the player to the wrong place.
void EntranceMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                  const BoardingEntrance& e,Line* lines,int* at) noexcept {
    float x,y,depth;
    if(!Project(vp,e.at,width,height,&x,&y,&depth) || x<0.0f || x>width || y<0.0f || y>height)return;
    const float* colour=e.inReach ? kHud : kCyan;
    Arc(drawer,ctx,x,y,13.0f*s,0.0f,kTurn,2.0f*s,24,colour);
    Seg(drawer,ctx,x-20.0f*s,y,x-14.0f*s,y,2.0f*s,colour);
    Seg(drawer,ctx,x+14.0f*s,y,x+20.0f*s,y,2.0f*s,colour);
    const float tx=vec::Clamp(x,160.0f*s,width-160.0f*s),ty=vec::Clamp(y+30.0f*s,30.0f*s,height-30.0f*s);
    const int metres=static_cast<int>(std::ceil(e.distance));
    // Kept on the screen as the 2D line was (clamped in): its place moved there, the plate standing on it.
    float on[3];ScreenShifted(vp,width,height,e.at,tx-x,ty-y,on);
    const std::uint64_t of=WorldKey(kWorldEntrance,e.of);
    const float dx=0.0f,dy=0.0f;
    if(e.hail && e.coming)WorldLabel(text,lines,at,vp,width,height,on,of,dx,dy,1,kLineScale,colour,Tr(Tx::boardingHailing),metres);
    else if(e.hail){wchar_t key[32];KeyName(Cfg().playerJetHailKey,key,32);WorldLabel(text,lines,at,vp,width,height,on,of,dx,dy,1,kLineScale,colour,Tr(Tx::boardingHail),key,metres);}
    else if(e.inReach)WorldLabel(text,lines,at,vp,width,height,on,of,dx,dy,1,kLineScale,colour,L"%ls",Tr(Tx::boardingReady));
    else WorldLabel(text,lines,at,vp,width,height,on,of,dx,dy,1,kLineScale,colour,Tr(Tx::boardingEntry),metres);
}
constexpr float kCellW=112.0f,kCellH=48.0f;   // px at 1080 lines: a strip's cell
constexpr float kRows3[]={-4.0f,0.0f,4.0f};   // a glyph's three rows (the pod's rockets, the gun's rounds)

// A store's silhouette round (cx, cy), the nose to the right, `k` px a unit (some 36 x 14 units).
void StoreGlyph(void* drawer,void* ctx,float cx,float cy,float k,hudcue::StoreIcon icon,const float* rgba) noexcept {
    using hudcue::StoreIcon;
    auto box=[&](float u0,float v0,float u1,float v1){Rect(drawer,ctx,cx+u0*k,cy+v0*k,cx+u1*k,cy+v1*k,rgba);};
    auto tri=[&](float bu,float bv,float tu,float tv,float half){Tri(drawer,ctx,cx+bu*k,cy+bv*k,cx+tu*k,cy+tv*k,half*k,rgba);};
    auto seg=[&](float u0,float v0,float u1,float v1,float t){Seg(drawer,ctx,cx+u0*k,cy+v0*k,cx+u1*k,cy+v1*k,t*k,rgba);};
    // A missile: a body of length `len` and radius `r`, its nose cone, tail fins of `fin`, canards (front fins) or mid
    // wings (`mid`, an air-to-ground's).
    auto missile=[&](float len,float r,float fin,bool canards,bool mid){
        const float tail=-len*0.5f,cone=len*0.5f-r*2.0f;
        box(tail,-r,cone,r);tri(cone,0.0f,len*0.5f,0.0f,r);
        tri(tail+3.0f,-r,tail,-r-fin,1.6f);tri(tail+3.0f,r,tail,r+fin,1.6f);
        if(canards){tri(cone-3.0f,-r,cone-4.5f,-r-fin*0.5f,1.0f);tri(cone-3.0f,r,cone-4.5f,r+fin*0.5f,1.0f);}
        if(mid){tri(1.0f,-r,-4.0f,-r-fin*1.2f,3.5f);tri(1.0f,r,-4.0f,r+fin*1.2f,3.5f);}
    };
    switch(icon) {
    case StoreIcon::aamShort: missile(24.0f,1.4f,3.5f,true,false);break;
    case StoreIcon::aam: missile(30.0f,1.6f,4.0f,false,true);break;
    case StoreIcon::aamLong: missile(34.0f,2.3f,4.5f,false,true);break;
    case StoreIcon::agm: missile(26.0f,2.6f,3.0f,false,true);break;
    case StoreIcon::agmLight: missile(20.0f,1.6f,2.5f,true,false);break;
    case StoreIcon::bomb:   // Mk 82: a fat body, its ogive nose, the tail cone and its box fins
        box(-7.0f,-3.8f,8.0f,3.8f);tri(8.0f,0.0f,15.0f,0.0f,3.8f);tri(-7.0f,0.0f,-12.0f,0.0f,3.8f);
        seg(-15.0f,-5.0f,-15.0f,5.0f,1.6f);seg(-15.0f,-5.0f,-10.0f,-3.0f,1.4f);seg(-15.0f,5.0f,-10.0f,3.0f,1.4f);
        break;
    case StoreIcon::rocket:   // Hydra 70: the pod's outline, its tubes, the rockets' tips out of its front
        seg(-14.0f,-6.0f,12.0f,-6.0f,1.6f);seg(-14.0f,6.0f,12.0f,6.0f,1.6f);seg(-14.0f,-6.0f,-14.0f,6.0f,1.6f);
        seg(12.0f,-6.0f,12.0f,6.0f,1.6f);seg(-14.0f,-2.0f,12.0f,-2.0f,0.8f);seg(-14.0f,2.0f,12.0f,2.0f,0.8f);
        for(const float v:kRows3)tri(12.0f,v*1.0f,16.0f,v*1.0f,1.4f);
        break;
    case StoreIcon::drone: // deployed aircraft, not a falling munition
        seg(-14.0f,0.0f,14.0f,0.0f,2.5f);tri(6.0f,0.0f,17.0f,0.0f,2.5f);
        tri(2.0f,0.0f,-8.0f,-8.0f,3.0f);tri(2.0f,0.0f,-8.0f,8.0f,3.0f);
        seg(-12.0f,-4.0f,-12.0f,4.0f,2.0f);break;
    case StoreIcon::energy: // emitter and parallel beam rays
        box(-15.0f,-5.0f,-8.0f,5.0f);
        for(const float r:kRows3)seg(-7.0f,r*3.0f,16.0f,r*3.0f,r==0.0f ? 2.0f : 1.0f);
        break;
    case StoreIcon::charge: // detonation action
        seg(-12.0f,-6.0f,12.0f,6.0f,1.6f);seg(-12.0f,6.0f,12.0f,-6.0f,1.6f);
        seg(0.0f,-9.0f,0.0f,9.0f,1.6f);seg(-16.0f,0.0f,16.0f,0.0f,1.6f);break;
    case StoreIcon::shell:   // a tank gun's round: the case, its driving band, the ogive and its fuze
        box(-14.0f,-3.6f,-2.0f,3.6f);box(-15.5f,-4.2f,-14.0f,4.2f);
        box(-1.0f,-3.4f,4.0f,3.4f);box(-2.0f,-3.9f,-1.0f,3.9f);
        tri(4.0f,0.0f,12.5f,0.0f,3.4f);box(12.0f,-0.7f,14.0f,0.7f);
        break;
    case StoreIcon::gun:   // the gun's rounds: three bullets
        for(const float r:kRows3){const float v=r*1.125f;box(-10.0f,v-1.4f,2.0f,v+1.4f);tri(2.0f,v,7.0f,v,1.4f);box(-12.0f,v-1.6f,-10.5f,v+1.6f);}
        break;
    }
}
struct LoadoutGrid { int columns,rows; float cell,width,height; };
LoadoutGrid LoadoutGridOf(Text* text,float width,float s,const LoadCell* cells,int n) noexcept {
    n=n<8 ? n : 8;
    if(n<=0)return {};
    const float available=std::fmax(1.0f,width-32.0f*s);
    float cell=kCellW*s;
    for(int i=0;i<n;++i) {
        Line l{};Format(l,L"%ls",cells[i].text);l.scale=kLineScale*0.75f;
        if(text)MeasureAll(*text,&l,1);
        cell=std::fmax(cell,l.w+20.0f*s);
    }
    cell=std::fmin(cell,available);
    int columns=static_cast<int>(available/cell);
    columns=columns<1 ? 1 : columns>4 ? 4 : columns;
    if(columns>n)columns=n;
    const int rows=(n+columns-1)/columns;
    return {columns,rows,cell,cell*columns,(kCellH*rows+8.0f*(rows-1))*s};
}
LoadoutDock LoadoutDockOf(Text* text,float width,float height,float s,const LoadCell* cells,int n,int footers) noexcept {
    if(text && !text->made && textOk && text->mgr)MakeText(*text);
    float fitted=s;
    LoadoutGrid grid{};
    // The lower dock owns at most 36% of the viewport: cells, the switch banner
    // and all control rows. Long translated names determine columns before fit.
    for(int pass=0;pass<5;++pass) {
        Text local{};Text* t=text;
        if(text){local=*text;local.s*=fitted/s;t=&local;}
        grid=LoadoutGridOf(t,width,fitted,cells,n);
        const float need=grid.height+(24.0f+72.0f+24.0f*footers+8.0f)*fitted;
        if(need<=height*kLoadoutRegionShare)break;
        fitted*=height*kLoadoutRegionShare/need;
    }
    Text local{};Text* t=text;
    if(text){local=*text;local.s*=fitted/s;t=&local;}
    grid=LoadoutGridOf(t,width,fitted,cells,n);
    const float footer=height-(24.0f+24.0f*footers)*fitted;
    const float y=footer-8.0f*fitted-grid.height;
    return {fitted,y,footer,y-72.0f*fitted,y-8.0f*fitted};
}

// A measured grid, at most four columns, without a minimum width that can push
// the first/last cell off screen. Every row includes its own text and glyphs.
float LoadoutStrip(void* drawer,void* ctx,Text* text,float width,float y,float s,const LoadCell* cells,int n,Line* lines,int* at) noexcept {
    n=n<8 ? n : 8;
    const LoadoutGrid grid=LoadoutGridOf(text,width,s,cells,n);
    if(!grid.columns)return 0.0f;
    for(int i=0;i<n;++i) {
        const int row=i/grid.columns,col=i%grid.columns;
        const int left=n-row*grid.columns,columns=grid.columns<left ? grid.columns : left;
        const float x=(width-grid.cell*columns)*0.5f+col*grid.cell,cy=y+row*(kCellH+8.0f)*s;
        const float cx=x+grid.cell*0.5f,x1=x+grid.cell,h=kCellH*s,t=2.0f*s;
        const int line=*at;
        Label(text,lines,at,cx,cy+37.0f*s,1,kLineScale*0.75f,cells[i].rgba,L"%ls",cells[i].text);
        if(text && line<*at && lines[line].w>grid.cell-16.0f*s) {
            Line& l=lines[line];l.scale*=(grid.cell-16.0f*s)/l.w*0.97f;MeasureAll(*text,&l,1);
            l.x=cx-l.w*0.5f;l.y=cy+37.0f*s-l.h*0.5f;
        }
        const LoadCell& c=cells[i];
        if(c.picked) {
            Rect(drawer,ctx,x+2.0f*s,cy,x1-2.0f*s,cy+h,kPanel);
            Seg(drawer,ctx,x+2.0f*s,cy,x1-2.0f*s,cy,t,kCyan);Seg(drawer,ctx,x+2.0f*s,cy+h,x1-2.0f*s,cy+h,t,kCyan);
            Seg(drawer,ctx,x+2.0f*s,cy,x+2.0f*s,cy+h,t,kCyan);Seg(drawer,ctx,x1-2.0f*s,cy,x1-2.0f*s,cy+h,t,kCyan);
        }
        StoreGlyph(drawer,ctx,cx,cy+15.0f*s,1.4f*s,c.icon,c.rgba);
        // Its loading bar under the name, as the stock weapon gauge's (Bar: the HUD's gauge style), amber while it fills;
        // ready, gone.
        if(c.load>=0.0f)Bar(drawer,ctx,x+12.0f*s,cy+h-4.5f*s,grid.cell-24.0f*s,3.0f*s,c.load,c.load,kAmber,s);
    }
    return grid.height;
}

// The picked store large, over the strip, for kSwitchMs after a switch: its picture twice the size, its name and rounds.
// The panel is as wide as its text needs (a weapon's word is up to twice as wide in Chinese or Japanese: the text ran out
// of a fixed 300 px), and it covers the HUD's text drawn under it so far (a pitch ladder's degrees showed through it).
void LoadoutBanner(void* drawer,void* ctx,Text* text,float width,float bottom,float s,const LoadCell& c,Line* lines,int* at) noexcept {
    const int first=*at;
    Label(text,lines,at,0.0f,0.0f,0,kTitleScale,kCyan,L"%ls",c.text);
    const float textX=116.0f*s,right=24.0f*s;
    if(text && *at>first && lines[first].w>width-32.0f*s-textX-right) {
        Line& l=lines[first];l.scale*=std::fmax(1.0f,width-32.0f*s-textX-right)/l.w*0.97f;MeasureAll(*text,&l,1);
    }
    const float textW=*at>first ? lines[first].w : 0.0f;
    const float w=std::fmax(300.0f*s,textX+textW+right),h=64.0f*s,x0=(width-w)*0.5f,y0=bottom-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kCyan);
    StoreGlyph(drawer,ctx,x0+58.0f*s,y0+h*0.5f,2.6f*s,c.icon,kCyan);
    for(int i=0;i<first;++i) {   // what is under the panel
        Line& l=lines[i];
        if(l.x<x0+w && x0<l.x+l.w && l.y<y0+h && y0<l.y+l.h)l.text[0]=L'\0';
    }
    if(*at>first){lines[first].x=x0+textX;lines[first].y=y0+(h-lines[first].h)*0.5f;}
}

// The player's drill tank (DrillCue, the user 2026-10-05: the RPM on the HUD; its heat next to it): the RPM and its
// bar, amber while it spins up or down, green at the top, "DRILLING" while it touches something; the heat's bar beside
// it, yellow, amber past 70%, red past 90%; overheated, all red and "OVERHEAT" (it turns again once cooled).
const float* HeatColour(float heat,bool over) noexcept { return over || heat>=0.9f ? kRed : heat>=0.7f ? kAmber : kYellow; }
// The drill's state word (nullptr: none): overheated, launched (out / on its way back), or biting.
const wchar_t* DrillState(const DrillCue& c) noexcept {
    if(c.overheated)return Tr(Tx::overheat);
    if(c.flying)return Tr(c.returning ? Tx::drillReturning : Tx::drillLaunched);
    return c.touching && c.rpm>0.0f ? Tr(Tx::drilling) : nullptr;
}
void DrillLaunchHint(Line& line,const DrillCue& c) noexcept {
    if(!Cfg().drillLaunch)return;
    wchar_t binding[64];
    if(c.keys)KeyName(Cfg().drillLaunchKey,binding,_countof(binding));
    else SeatButtonName(Cfg().drillLaunchButton,binding,_countof(binding));
    Append(line,L"    ");Append(line,Tr(Tx::drillLaunchHint),binding);
}
void DrillPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const DrillCue& c,Line* lines,int* at) noexcept {
    if(!LineSink{lines,at}.Room(1,__func__) || !(c.maxRpm>0.0f))return;
    Line& l=lines[(*at)++];
    const float share=Unit(c.rpm/c.maxRpm),heat=Unit(c.heat);
    const bool top=share>=0.99f;
    wchar_t state[32]=L"";
    if(const wchar_t* const word=DrillState(c))std::swprintf(state,32,L"    %ls",word);
    Format(l,Tr(Tx::drillLine),static_cast<int>(std::lround(c.rpm)),static_cast<int>(std::lround(heat*100.0f)),state);
    DrillLaunchHint(l,c);
    l.scale=kTitleScale;
    l.rgba=c.overheated ? kRed : top ? kGreen : share>0.0f ? kAmber : kCyan;
    l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    const float pad=8.0f*s,gap=5.0f*s,rpmW=220.0f*s,heatW=120.0f*s,between=16.0f*s,barH=10.0f*s,barsW=rpmW+between+heatW;
    const float lineH=l.h>0.0f ? l.h : 24.0f*s,w=(l.w>barsW ? l.w : barsW)+2.0f*pad,h=pad+lineH+gap+barH+pad;
    const float x0=(width-w)*0.5f,y0=height*0.80f-h,bx=(width-barsW)*0.5f,by=y0+pad+lineH+gap;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,c.overheated ? kRed : top ? kGreen : kCyan);
    l.x=(width-l.w)*0.5f;l.y=y0+pad;
    Bar(drawer,ctx,bx,by,rpmW,barH,share,share,top ? kGreen : kAmber,s);
    Bar(drawer,ctx,bx+rpmW+between,by,heatW,barH,heat,heat,HeatColour(heat,c.overheated),s);
}

// --- The player's Sazabi (sazabi.h SazabiCue, published by sazabi.cpp; README 沙扎比): its own HUD (the user, 2026-10-07:
// "我们自制的hud … 需要做的都要做"), drawn while the player drives it (the stock vehicle weapon gauge given way:
// HudPublish's SetStockGaugeCover). Positions in design px (1080 lines; times the HUD's scale `s`) from the screen's
// centre C:
// - The reticle (SazabiReticle): a pink ring, four ticks and a dot at C when the camera is the Sazabi's own
//   (cue.centred: the centre IS the aim), else where the aim point projects; red, thicker and ringed once the missiles'
//   lock is complete. Under it (+44) the range to what the centre's ray meets ("412 m"; "--- m" when it meets nothing).
// - Within kSazabiKeepOut of C nothing else: the flight's gauges stand round it. Left (SazabiThrusterArc): the
//   thrusters' charge as an arc of radius kSazabiArcR over +-kSazabiArcHalf round the left, filled from its lower end
//   (blue; amber under kSazabiLowThrust; red on a red track overheated; brighter with a glow while boosting), its share
//   and its name (blinking OVERHEAT overheated) right-aligned kSazabiSide left of C. Right (SazabiAltSpeed): a dim
//   bracket arc mirroring it, the altitude and the speed (km/h) left-aligned kSazabiSide right of C, a climb arrow after
//   the altitude past kSazabiClimbShown m/s.
// - Under right (from (C+kSazabiSpecialX, C+kSazabiSpecialY)): the special weapon the secondary fires (SazabiSpecial) on a
//   panel with a pink edge: its name (the missiles' count), its gauge (the next salvo, the lock's progress under it; the
//   funnels' pips, filled docked and hollow out, the next launch under them; the cannon's charge in pink, else its
//   cooldown) (the special used last: every one has its own key), and the bindings of the melee, the guard and the
//   dash (ini SazabiMeleeKey, SazabiGuardKey, SazabiDashKey; a pad's buttons) (the user, 2026-10-09: 「缺少近战」「缺少冲刺」:
//   nothing on the screen said where they were).
// - Lower left (SazabiPanel; its left kSazabiLeft left of C, its bottom at 0.80 H; held on a narrow viewport, and moved
//   down to the screen's foot when the flight's gauges would meet it): the name, the state tags lit on that row (GUARD,
//   AIR, TOMAHAWK, LOCK, OVERHEAT), then a row per system, its name at the left and its gauge at the right: the
//   thrusters, the beam rifle's next shot, the shield missiles (their count, the next salvo, the lock under it), the mega
//   particle cannon (pink charging, else its cooldown), the funnels' pips; each row its binding ([key]: the thrusters'
//   the dash's, the rifle's and the missiles' the fire buttons). A gauge full is green: ready.
// - The missiles' lock on its target as the jets' (LockAt: a yellow square closing in while it locks, the red diamond).
alignas(16) const float kSazabiRed[4]={1.0f,0.3f,0.35f,1.0f};
alignas(16) const float kSazabiPink[4]={1.0f,0.45f,0.75f,1.0f};
alignas(16) const float kSazabiLocked[4]={1.0f,0.18f,0.22f,1.0f};
alignas(16) const float kSazabiFrame[4]={0.8f,0.9f,1.0f,0.4f};      // the right bracket, the hints
alignas(16) const float kSazabiTrack[4]={0.0f,0.0f,0.0f,0.5f};      // an arc gauge's empty track
alignas(16) const float kSazabiHotTrack[4]={0.6f,0.05f,0.05f,0.55f};  // the thrusters' track overheated
alignas(16) const float kSazabiThrust[4]={0.3f,0.72f,1.0f,0.85f};
alignas(16) const float kSazabiBoost[4]={0.62f,0.93f,1.0f,1.0f};
alignas(16) const float kSazabiPick[4]={1.0f,0.45f,0.75f,0.16f};    // the selected special's band
alignas(16) const float kSazabiHint[4]={0.85f,0.9f,0.95f,0.8f};
constexpr float kSazabiLowThrust=0.25f,kSazabiLeft=880.0f,kSazabiKeepOut=120.0f,kSazabiArcR=160.0f,kSazabiArcW=8.0f;
constexpr float kSazabiArcHalf=36.0f*kDeg,kSazabiSide=178.0f,kSazabiClimbShown=2.0f;
constexpr float kSazabiSpecialX=140.0f,kSazabiSpecialY=108.0f;
constexpr float kSazabiFlightReach=330.0f;   // how far left of C the flight's gauges may reach (their text, in any language)
constexpr float kSazabiFlightFoot=108.0f;    // and how far under C (the thrusters' arc's lower end, its edge)
constexpr int kSazabiFunnels=6,kSazabiRows=5,kSazabiTags=5,kSazabiSpecials=3;
// A row's gauge: a bar of `share` in `fill` (or the funnels' pips: `pips` of them in the pack), and with `sub` >= 0 a
// thin bar of `sub` in `subFill` under it.
struct SazabiGauge { float share; const float* fill; int pips; float sub; const float* subFill; };
const float* ReadyColour(float share) noexcept { return share>=1.0f ? kGreen : kTeal; }
bool SazabiLockDone(const SazabiCue& c) noexcept { return c.hasLock && c.missileLock>=1.0f; }
int SazabiSpecialOf(const SazabiCue& c) noexcept { return c.special<0 ? 0 : c.special>=kSazabiSpecials ? kSazabiSpecials-1 : c.special; }
int SazabiPack(const SazabiCue& c) noexcept {   // the funnels in the pack
    return kSazabiFunnels-(c.funnelsOut<0 ? 0 : c.funnelsOut>kSazabiFunnels ? kSazabiFunnels : c.funnelsOut);
}
// Where the reticle stands (see the top): the screen's centre on the Sazabi's camera, else the aim point projected.
bool SazabiAimAt(const float* vp,float width,float height,const SazabiCue& c,float* x,float* y) noexcept {
    if(c.centred){*x=width*0.5f;*y=height*0.5f;return true;}
    float depth;
    return c.hasAim && Project(vp,c.aim,width,height,x,y,&depth);
}
void SazabiReticle(void* drawer,void* ctx,const float* vp,float width,float height,float s,const SazabiCue& c) noexcept {
    float x,y;
    if(!SazabiAimAt(vp,width,height,c,&x,&y))return;
    const bool locked=SazabiLockDone(c);
    const float* const ink=locked ? kSazabiLocked : kSazabiPink;
    const float r=18.0f*s,t=(locked ? 3.0f : 2.0f)*s,in=r+3.0f*s,out=in+(locked ? 10.0f : 7.0f)*s,dot=(locked ? 3.0f : 2.0f)*s;
    Arc(drawer,ctx,x,y,r,0.0f,kTurn,t,32,ink);
    Rect(drawer,ctx,x-out,y-t*0.5f,x-in,y+t*0.5f,ink);Rect(drawer,ctx,x+in,y-t*0.5f,x+out,y+t*0.5f,ink);
    Rect(drawer,ctx,x-t*0.5f,y-out,x+t*0.5f,y-in,ink);Rect(drawer,ctx,x-t*0.5f,y+in,x+t*0.5f,y+out,ink);
    Rect(drawer,ctx,x-dot,y-dot,x+dot,y+dot,ink);
    if(locked)Arc(drawer,ctx,x,y,out+4.0f*s,0.0f,kTurn,1.5f*s,40,ink);
}
// The aim assist's enemy (cue.hasAssist, sazabi_assist.h): four pink corner brackets round its lock point, the shots'
// point; held by the lock-on (cue.lockOn) they are red, thicker and tighter, with a dot on the point.
void SazabiAssistMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const SazabiCue& c) noexcept {
    float x,y,depth;
    if(!c.hasAssist || !Project(vp,c.assist,width,height,&x,&y,&depth))return;
    const float* const ink=c.lockOn ? kSazabiLocked : kSazabiPink;
    const float h=(c.lockOn ? 20.0f : 26.0f)*s,arm=(c.lockOn ? 11.0f : 9.0f)*s,t=(c.lockOn ? 3.0f : 2.0f)*s;
    for(int i=0;i<4;++i) {
        const float sx=i&1 ? 1.0f : -1.0f,sy=i&2 ? 1.0f : -1.0f,cx=x+sx*h,cy=y+sy*h;
        Rect(drawer,ctx,std::fmin(cx,cx-sx*arm),cy-t*0.5f,std::fmax(cx,cx-sx*arm),cy+t*0.5f,ink);
        Rect(drawer,ctx,cx-t*0.5f,std::fmin(cy,cy-sy*arm),cx+t*0.5f,std::fmax(cy,cy-sy*arm),ink);
    }
    if(c.lockOn)Rect(drawer,ctx,x-2.5f*s,y-2.5f*s,x+2.5f*s,y+2.5f*s,ink);
}
// The range under the reticle (see the top).
void SazabiRange(Text* text,const float* vp,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    float x,y;
    if(!SazabiAimAt(vp,width,height,c,&x,&y))return;
    const float* const ink=SazabiLockDone(c) ? kSazabiLocked : kSazabiPink,ly=y+44.0f*s,scale=kLineScale*0.8f;
    if(c.aimHit && std::isfinite(c.aimRange))
        Label(text,lines,at,x,ly,1,scale,ink,L"%d m",static_cast<int>(std::lround(vec::Clamp(c.aimRange,0.0f,99999.0f))));
    else Label(text,lines,at,x,ly,1,scale,ink,L"--- m");
}
// The flight's gauges round C (see the top).
// Left of C (see the top): the thrusters' arc, its share and its name (or OVERHEAT).
void SazabiThrusterArc(void* drawer,void* ctx,Text* text,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    const float cx=width*0.5f,cy=height*0.5f,r=kSazabiArcR*s,t=kSazabiArcW*s,span=2.0f*kSazabiArcHalf,from=kTurn*0.5f-kSazabiArcHalf;
    const float share=Unit(c.thruster);
    const bool low=share<kSazabiLowThrust,blink=(GetTickCount64()/250)%2==0;
    const float* const fill=c.overheat ? kRed : low ? kAmber : c.boosting ? kSazabiBoost : kSazabiThrust;
    Arc(drawer,ctx,cx,cy,r,from,span,t+2.0f*s,24,kBarEdge);
    Arc(drawer,ctx,cx,cy,r,from,span,t,24,c.overheat ? kSazabiHotTrack : kSazabiTrack);
    if(share>0.0f)Arc(drawer,ctx,cx,cy,r,from,span*share,t,1+static_cast<int>(share*23.0f),fill);
    if(c.boosting && share>0.0f)Arc(drawer,ctx,cx,cy,r+t*0.5f+3.0f*s,from,span*share,1.5f*s,1+static_cast<int>(share*23.0f),kSazabiBoost);
    for(int i=1;i<4;++i) {   // its quarters
        const float a=from+span*static_cast<float>(i)*0.25f,ca=std::cos(a),sa=std::sin(a);
        Seg(drawer,ctx,cx+(r-t*0.5f)*ca,cy+(r-t*0.5f)*sa,cx+(r+t*0.5f)*ca,cy+(r+t*0.5f)*sa,1.5f*s,kBarTick);
    }
    const float lx=cx-kSazabiSide*s,up=cy-13.0f*s,down=cy+15.0f*s;
    Label(text,lines,at,lx,up,2,kLineScale*1.1f,fill,L"%d%%",static_cast<int>(std::lround(share*100.0f)));
    if(c.overheat)Label(text,lines,at,lx,down,2,kLineScale*0.8f,blink ? kRed : kWhite,L"%ls",Tr(Tx::overheat));
    else Label(text,lines,at,lx,down,2,kLineScale*0.7f,kSazabiHint,L"%ls",Tr(Tx::sazabiThruster));
}
// Right of C (see the top): the bracket, the altitude and its climb, the speed.
void SazabiAltSpeed(void* drawer,void* ctx,Text* text,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    const float cx=width*0.5f,cy=height*0.5f,r=kSazabiArcR*s,span=2.0f*kSazabiArcHalf;
    const float rx=cx+kSazabiSide*s,up=cy-13.0f*s,down=cy+15.0f*s;
    Arc(drawer,ctx,cx,cy,r,-kSazabiArcHalf,span,2.0f*s,24,kSazabiFrame);
    for(int i=0;i<=4;++i) {
        const float a=-kSazabiArcHalf+span*static_cast<float>(i)*0.25f,ca=std::cos(a),sa=std::sin(a);
        Seg(drawer,ctx,cx+r*ca,cy+r*sa,cx+(r+6.0f*s)*ca,cy+(r+6.0f*s)*sa,1.5f*s,kSazabiFrame);
    }
    const int alt=static_cast<int>(std::lround(vec::Clamp(std::isfinite(c.altitude) ? c.altitude : 0.0f,0.0f,99999.0f)));
    const int kmh=static_cast<int>(std::lround(vec::Clamp(std::isfinite(c.speed) ? c.speed*3.6f : 0.0f,0.0f,99999.0f)));
    const int altAt=*at;
    Label(text,lines,at,rx,up,0,kLineScale*0.85f,kWhite,Tr(Tx::sazabiAltitude),alt);
    Label(text,lines,at,rx,down,0,kLineScale*0.85f,c.boosting ? kSazabiBoost : kWhite,Tr(Tx::sazabiSpeed),kmh);
    if(*at>altAt && std::isfinite(c.climb) && std::fabs(c.climb)>kSazabiClimbShown) {   // the climb's arrow after the altitude
        const Line& l=lines[altAt];
        const float ax=l.x+l.w+12.0f*s,ay=up,h=7.0f*s,dir=c.climb>0.0f ? -1.0f : 1.0f;
        Tri(drawer,ctx,ax,ay-dir*h,ax,ay+dir*h,h,c.climb>0.0f ? kGreen : kAmber);
    }
}
// The funnels' pips across (x, y, w, h): the first `filled` in the pack, the rest hollow (flying).
void FunnelPips(void* drawer,void* ctx,float x,float y,float w,float h,int filled,float s) noexcept {
    const float gap=4.0f*s,pw=(w-gap*static_cast<float>(kSazabiFunnels-1))/static_cast<float>(kSazabiFunnels),t=1.5f*s;
    for(int i=0;i<kSazabiFunnels;++i) {
        const float px=x+static_cast<float>(i)*(pw+gap);
        Rect(drawer,ctx,px-t,y-t,px+pw+t,y+h+t,kBarEdge);
        if(i<filled){Rect(drawer,ctx,px,y,px+pw,y+h,kSazabiPink);Rect(drawer,ctx,px,y,px+pw,y+h*0.35f,kBarShine);continue;}
        Rect(drawer,ctx,px,y,px+pw,y+t,kSazabiPink);Rect(drawer,ctx,px,y+h-t,px+pw,y+h,kSazabiPink);
        Rect(drawer,ctx,px,y,px+t,y+h,kSazabiPink);Rect(drawer,ctx,px+pw-t,y,px+pw,y+h,kSazabiPink);
    }
}
// The rows' names and gauges from the cue (see the top): `rows` gets the names, `g` the gauges.
// A binding's name for the HUD: the key on the keyboard and mouse, else the pad's button (`button` < 0: the pad's stock
// fire button, named `pad`).
void SazabiBinding(bool keys,int vk,int button,const wchar_t* pad,wchar_t* out,int size) noexcept {
    if(keys)KeyName(vk,out,size);
    else if(button<0)wcsncpy_s(out,size,pad,_TRUNCATE);
    else SeatButtonName(button,out,size);
}
// `l`'s text followed by its binding.
void SazabiKeyed(Line& l,bool keys,int vk,int button,const wchar_t* pad) noexcept {
    wchar_t key[32],label[96];
    SazabiBinding(keys,vk,button,pad,key,32);
    if(!key[0])return;   // a pad's stock fire button: not named (the game's own binding)
    wcsncpy_s(label,96,l.text,_TRUNCATE);
    Format(l,L"%ls [%ls]",label,key);
}
void SazabiRowsOf(const SazabiCue& c,Line* rows,SazabiGauge* g) noexcept {
    const bool low=c.thruster<kSazabiLowThrust,charging=c.cannonCharge>0.0f,empty=c.missiles<=0;
    const float salvo=empty ? 0.0f : Unit(c.missileReady),lock=Unit(c.missileLock);
    const int pack=SazabiPack(c);
    Format(rows[0],L"%ls",Tr(Tx::sazabiThruster));
    rows[0].rgba=c.overheat ? kRed : low ? kAmber : kWhite;
    g[0]=SazabiGauge{Unit(c.thruster),c.overheat ? kRed : low ? kAmber : c.boosting ? kSazabiBoost : kCyan,-1,-1.0f,nullptr};
    Format(rows[1],L"%ls",Tr(Tx::sazabiRifle));
    rows[1].rgba=c.rifleReady>=1.0f ? kGreen : kWhite;
    g[1]=SazabiGauge{Unit(c.rifleReady),ReadyColour(c.rifleReady),-1,-1.0f,nullptr};
    Format(rows[2],Tr(Tx::sazabiMissiles),c.missiles>0 ? c.missiles : 0);
    rows[2].rgba=empty ? kRed : salvo>=1.0f ? kGreen : kWhite;
    g[2]=SazabiGauge{salvo,ReadyColour(salvo),-1,lock,lock>=1.0f ? kRed : kYellow};
    Format(rows[3],L"%ls",Tr(Tx::sazabiCannon));
    rows[3].rgba=charging ? kSazabiPink : c.cannonReady>=1.0f ? kGreen : kWhite;
    g[3]=charging ? SazabiGauge{Unit(c.cannonCharge),kSazabiPink,-1,-1.0f,nullptr}
                  : SazabiGauge{Unit(c.cannonReady),ReadyColour(c.cannonReady),-1,-1.0f,nullptr};
    Format(rows[4],L"%ls",Tr(Tx::sazabiFunnels));
    rows[4].rgba=pack>0 ? kWhite : kAmber;
    g[4]=SazabiGauge{0.0f,nullptr,pack,Unit(c.funnelReady),ReadyColour(c.funnelReady)};
    const Config& k=Cfg();
    SazabiKeyed(rows[0],c.keys,k.sazabiDashKey,0x01,L"A");
    SazabiKeyed(rows[1],c.keys,VK_LBUTTON,-1,L"");
    SazabiKeyed(rows[2],c.keys,VK_RBUTTON,-1,L"");
    SazabiKeyed(rows[3],c.keys,k.sazabiCannonKey,k.sazabiCannonButton,L"");
    SazabiKeyed(rows[4],c.keys,k.sazabiFunnelKey,k.sazabiFunnelButton,L"");
}
// The state tags lit (see the top) into `tags`: how many.
int SazabiTagsOf(const SazabiCue& c,Line* tags) noexcept {
    int n=0;
    auto tag=[&](bool on,Tx text,const float* rgba){if(on){Format(tags[n],L"%ls",Tr(text));tags[n++].rgba=rgba;}};
    tag(c.guard,Tx::sazabiGuard,kCyan);
    tag(c.air,Tx::sazabiAir,kTeal);
    tag(c.swinging,Tx::sazabiTomahawk,kAmber);
    tag(SazabiLockDone(c),Tx::sazabiLock,kRed);
    tag(c.overheat,Tx::overheat,kRed);
    return n;
}
void SazabiPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    if(!LineSink{lines,at}.Room(1+kSazabiRows+kSazabiTags,__func__))return;
    Line* const title=&lines[*at];
    Line* const rows=title+1;
    Line* const tags=rows+kSazabiRows;
    SazabiGauge g[kSazabiRows];
    Format(*title,L"%ls",Tr(Tx::sazabiTitle));
    title->scale=kTitleScale;title->rgba=kSazabiRed;
    SazabiRowsOf(c,rows,g);
    const int nTags=SazabiTagsOf(c,tags),n=1+kSazabiRows+nTags;
    for(int i=1;i<n;++i)title[i].scale=i<=kSazabiRows ? kLineScale : kLineScale*0.8f;
    for(int i=0;i<n;++i)title[i].w=title[i].h=0.0f;
    if(text)MeasureAll(*text,title,n);
    const float pad=10.0f*s,gap=7.0f*s,col=18.0f*s,tagGap=12.0f*s,barW=160.0f*s,barH=12.0f*s,subH=5.0f*s,subGap=2.0f*s,mark=16.0f*s;
    const float titleH=title->h>0.0f ? title->h : 30.0f*s;
    float nameW=0.0f,tagsW=0.0f,rowH[kSazabiRows],rowsH=0.0f;
    for(int i=0;i<kSazabiRows;++i) {
        nameW=rows[i].w>nameW ? rows[i].w : nameW;
        const float lineH=rows[i].h>0.0f ? rows[i].h : 24.0f*s,gauge=barH+(g[i].sub>=0.0f ? subGap+subH : 0.0f);
        rowH[i]=lineH>gauge ? lineH : gauge;
        rowsH+=rowH[i]+(i ? gap : 0.0f);
    }
    float tagH=0.0f;
    for(int i=0;i<nTags;++i){tagsW+=tagGap+tags[i].w;tagH=std::fmax(tagH,tags[i].h>0.0f ? tags[i].h : 18.0f*s);}
    // the tags on the title row while they fit beside the name over the rows' width, else a row of their own under it
    const float rowW=mark+nameW+col+barW;
    const bool wrap=nTags>0 && title->w+tagsW>rowW;
    const float titleW=wrap ? std::fmax(title->w,tagsW-tagGap) : title->w+tagsW,headH=titleH+(wrap ? gap+tagH : 0.0f);
    const float w=(rowW>titleW ? rowW : titleW)+2.0f*pad,h=pad+headH+gap+rowsH+pad;
    const float x0=std::fmax(width*0.5f-kSazabiLeft*s,16.0f*s);
    float y0=height*0.80f-h;
    // the flight's gauges left of C reach to kSazabiFlightReach: a panel that would meet them goes to the screen's foot
    if(x0+w>width*0.5f-kSazabiFlightReach*s && y0<height*0.5f+kSazabiFlightFoot*s)y0=std::fmax(height-16.0f*s-h,height*0.5f+kSazabiFlightFoot*s);
    const float bx=x0+w-pad-barW;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kSazabiRed);
    Rect(drawer,ctx,x0,y0,x0+3.0f*s,y0+h,kSazabiRed);
    title->x=x0+pad;title->y=y0+pad;
    float x=wrap ? x0+pad-tagGap : x0+w-pad-tagsW;   // the tags at the title row's right end, or a row under it
    const float tagsBottom=wrap ? title->y+titleH+gap+tagH : title->y+titleH;
    for(int i=0;i<nTags;++i){x+=tagGap;tags[i].x=x;tags[i].y=tagsBottom-tags[i].h;x+=tags[i].w;}
    float y=y0+pad+headH+gap;
    for(int i=0;i<kSazabiRows;++i) {
        const float lineH=rows[i].h>0.0f ? rows[i].h : 24.0f*s,gauge=barH+(g[i].sub>=0.0f ? subGap+subH : 0.0f);
        const float gy=y+(rowH[i]-gauge)*0.5f;
        rows[i].x=x0+pad+mark;rows[i].y=y+(rowH[i]-lineH)*0.5f;
        if(g[i].pips>=0)FunnelPips(drawer,ctx,bx,gy,barW,barH,g[i].pips,s);
        else Bar(drawer,ctx,bx,gy,barW,barH,g[i].share,g[i].share,g[i].fill,s);
        if(g[i].sub>=0.0f)Bar(drawer,ctx,bx,gy+barH+subGap,barW,subH,g[i].sub,g[i].sub,g[i].subFill,s);
        y+=rowH[i]+gap;
    }
    *at+=n;
}
// The special weapon's readout under right of C (see the top).
void SazabiSpecial(void* drawer,void* ctx,Text* text,float width,float height,float s,const SazabiCue& c,Line* lines,int* at) noexcept {
    constexpr int kHints=3;
    if(!LineSink{lines,at}.Room(1+kHints,__func__))return;
    Line& name=lines[*at];
    Line* const hints=&lines[*at+1];
    const int special=SazabiSpecialOf(c);
    const bool locked=SazabiLockDone(c);
    if(special==0)Format(name,Tr(Tx::sazabiMissiles),c.missiles>0 ? c.missiles : 0);
    else Format(name,L"%ls",Tr(special==1 ? Tx::sazabiFunnels : Tx::sazabiCannon));
    name.rgba=special==0 && c.missiles<=0 ? kRed : special==0 && locked ? kSazabiLocked : kSazabiPink;
    name.scale=kLineScale*0.9f;
    const Config& k=Cfg();
    const int vk[kHints]={k.sazabiMeleeKey,k.sazabiGuardKey,k.sazabiDashKey},button[kHints]={k.sazabiMeleeButton,k.sazabiGuardButton,0x01};
    const Tx what[kHints]={Tx::sazabiMeleeKey,Tx::sazabiGuardKey,Tx::sazabiDashKey};
    for(int i=0;i<kHints;++i) {
        wchar_t key[32];
        SazabiBinding(c.keys,vk[i],button[i],L"",key,32);
        Format(hints[i],Tr(what[i]),key);
        hints[i].rgba=kSazabiHint;hints[i].scale=kLineScale*0.65f;hints[i].w=hints[i].h=0.0f;
    }
    name.w=name.h=0.0f;
    if(text)MeasureAll(*text,&name,1+kHints);
    const float pad=7.0f*s,gap=5.0f*s,barW=180.0f*s,barH=9.0f*s,subH=4.0f*s,subGap=2.0f*s;
    const float nameH=name.h>0.0f ? name.h : 22.0f*s,hintH=hints[0].h>0.0f ? hints[0].h : 16.0f*s;
    const bool sub=special==0 ? c.hasLock : special==1;   // the lock's progress; the next launch
    const float gaugeH=barH+(sub ? subGap+subH : 0.0f);
    const float inner=std::fmax(barW,name.w),w=inner+2.0f*pad+3.0f*s,h=pad+nameH+gap+gaugeH+gap+kHints*hintH+pad;
    const float x0=width*0.5f+kSazabiSpecialX*s,y0=height*0.5f+kSazabiSpecialY*s,x=x0+3.0f*s+pad;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+3.0f*s,y0+h,kSazabiPink);
    name.x=x;name.y=y0+pad;
    const float gy=y0+pad+nameH+gap;
    if(special==0) {
        const float salvo=c.missiles>0 ? Unit(c.missileReady) : 0.0f,lock=Unit(c.missileLock);
        Bar(drawer,ctx,x,gy,barW,barH,salvo,salvo,ReadyColour(salvo),s);
        if(sub)Bar(drawer,ctx,x,gy+barH+subGap,barW,subH,lock,lock,lock>=1.0f ? kRed : kYellow,s);
    } else if(special==1) {
        FunnelPips(drawer,ctx,x,gy,barW,barH,SazabiPack(c),s);
        const float ready=Unit(c.funnelReady);
        Bar(drawer,ctx,x,gy+barH+subGap,barW,subH,ready,ready,ReadyColour(ready),s);
    } else {
        const bool charging=c.cannonCharge>0.0f;
        const float share=Unit(charging ? c.cannonCharge : c.cannonReady);
        Bar(drawer,ctx,x,gy,barW,barH,share,share,charging ? kSazabiPink : ReadyColour(share),s);
    }
    for(int i=0;i<kHints;++i){hints[i].x=x;hints[i].y=gy+gaugeH+gap+static_cast<float>(i)*hintH;}
    *at+=1+kHints;
}
// The whole of it (see the top), the panel last: its lines after the rest's.
void SazabiHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const SazabiCue& c,
               Line* lines,int* at) noexcept {
    float x,y;
    if(c.hasLock)LockAt(drawer,ctx,vp,width,height,s,c.missileLock>=1.0f ? 2 : 1,c.lock,Unit(c.missileLock),&x,&y);
    SazabiAssistMark(drawer,ctx,vp,width,height,s,c);
    SazabiReticle(drawer,ctx,vp,width,height,s,c);
    SazabiRange(text,vp,width,height,s,c,lines,at);
    SazabiThrusterArc(drawer,ctx,text,width,height,s,c,lines,at);
    SazabiAltSpeed(drawer,ctx,text,width,height,s,c,lines,at);
    SazabiSpecial(drawer,ctx,text,width,height,s,c,lines,at);
    SazabiPanel(drawer,ctx,text,width,height,s,c,lines,at);
}

// The Dreadnought's own HUD (dreadnought.cpp DreadnoughtCue; the user, 2026-10-21: 「独立的hud显示呢？」), for the local
// player in any of its seats: a panel at the screen's left (the ship and the seat they hold, its way and throttle, its
// keel's height and climb, its heading, its hull and boost, its turrets by who trains them, its hangar), and the keys
// low in the middle. The stock gauges give way to it (HudPublish).
constexpr float kDnLeft=24.0f,kDnWidth=430.0f,kDnRow=26.0f,kDnBar=180.0f,kDnBarH=11.0f;
alignas(16) const float kDnGold[4]={0.95f,0.78f,0.35f,1.0f};
alignas(16) const float kDnIdle[4]={0.6f,0.65f,0.7f,0.55f};
void DnThrottle(void* drawer,void* ctx,float x,float y,float w,float h,float throttle,bool boosting,float s) noexcept {
    constexpr float kAstern=0.35f;   // dreadnought.cpp kReverse
    const float zero=x+w*kAstern/(1.0f+kAstern);
    const float e=s>0.5f ? s : 0.5f;
    Rect(drawer,ctx,x-e,y-e,x+w+e,y+h+e,kBarEdge);
    Rect(drawer,ctx,x,y,x+w,y+h,kBarBack);
    const float at=zero+(throttle>=0.0f ? (x+w-zero)*Unit(throttle) : -(zero-x)*Unit(-throttle/kAstern));
    Rect(drawer,ctx,std::fmin(zero,at),y,std::fmax(zero,at),y+h,throttle<0.0f ? kAmber : boosting ? kCyan : kGreen);
    Rect(drawer,ctx,zero-e,y-2.0f*s,zero+e,y+h+2.0f*s,kWhite);
}
void DreadnoughtHud(void* drawer,void* ctx,Text* text,float width,float height,float s,const DreadnoughtCue& c,Line* lines,
                    int* at) noexcept {
    const float pad=12.0f*s,row=kDnRow*s,barW=kDnBar*s,barH=kDnBarH*s;
    const int rows=8;
    const float w=kDnWidth*s,h=2.0f*pad+row*(rows+0.4f);
    const float x0=kDnLeft*s,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kDnGold);
    Rect(drawer,ctx,x0,y0,x0+3.0f*s,y0+h,kDnGold);
    const float tx=x0+pad,bx=x0+w-pad-barW;
    float y=y0+pad+row*0.5f;
    wchar_t seat[32];
    if(c.seat==0)wcscpy_s(seat,Tr(Tx::dnSeatBridge));
    else std::swprintf(seat,32,Tr(Tx::dnSeatTurret),c.seat);
    Label(text,lines,at,tx,y,0,kTitleScale,kDnGold,L"%ls",Tr(Tx::dnTitle));
    Label(text,lines,at,x0+w-pad,y,2,kLineScale,kWhite,L"%ls",seat);
    y+=row*1.2f;
    Label(text,lines,at,tx,y,0,kLineScale,kWhite,Tr(Tx::dnSpeed),static_cast<int>(std::lround(c.speed*3.6f)));
    DnThrottle(drawer,ctx,bx,y-barH*0.5f,barW,barH,c.throttle,c.boosting,s);
    y+=row;
    const int pct=static_cast<int>(std::lround(std::fabs(c.throttle)*(c.throttle<0.0f ? 100.0f/0.35f : 100.0f)));
    Label(text,lines,at,tx,y,0,kLineScale*0.9f,c.throttle<0.0f ? kAmber : kWhite,Tr(c.throttle<0.0f ? Tx::dnAstern : Tx::dnThrottle),pct);
    Label(text,lines,at,x0+w-pad,y,2,kLineScale*0.9f,kWhite,Tr(Tx::dnHeading),static_cast<int>(std::lround(c.heading))%360);
    y+=row;
    Label(text,lines,at,tx,y,0,kLineScale*0.9f,kWhite,Tr(Tx::dnAltitude),c.keel>=0.0f ? static_cast<int>(std::lround(c.keel)) : 0,c.climb);
    y+=row;
    const float hull=c.hpMax>0.0f ? Unit(c.hp/c.hpMax) : 0.0f;
    Label(text,lines,at,tx,y,0,kLineScale*0.9f,kWhite,Tr(Tx::dnHull),static_cast<int>(std::lround(hull*100.0f)));
    Bar(drawer,ctx,bx,y-barH*0.5f,barW,barH,hull,hull,HullColour(hull),s);
    y+=row;
    Label(text,lines,at,tx,y,0,kLineScale*0.9f,c.boosting ? kCyan : kWhite,L"%ls",Tr(Tx::dnBoost));
    Bar(drawer,ctx,bx,y-barH*0.5f,barW,barH,c.boost,c.boost,c.boost>0.25f ? kCyan : kAmber,s);
    y+=row;
    // the turrets: a box each, filled by who trains it, outlined bright when on its aim
    Label(text,lines,at,tx,y,0,kLineScale*0.9f,kWhite,L"%ls",Tr(Tx::dnTurrets));
    const int n=c.turrets>0 && c.turrets<=8 ? c.turrets : 8;
    const float box=16.0f*s,gap=(barW-box*static_cast<float>(n))/static_cast<float>(n>1 ? n-1 : 1);
    for(int i=0;i<n;++i) {
        const unsigned bit=1u<<i;
        const float* fill=(c.mine&bit) ? kGreen : (c.others&bit) ? kCyan : (c.commanded&bit) ? kAmber : kDnIdle;
        const float bx0=bx+static_cast<float>(i)*(box+gap),by0=y-box*0.5f;
        Rect(drawer,ctx,bx0,by0,bx0+box,by0+box,(c.onAim&bit) ? kWhite : kBarEdge);
        Rect(drawer,ctx,bx0+2.0f*s,by0+2.0f*s,bx0+box-2.0f*s,by0+box-2.0f*s,fill);
        if(c.seat==1+i)Rect(drawer,ctx,bx0,by0+box+2.0f*s,bx0+box,by0+box+4.0f*s,kGreen);
    }
    y+=row;
    wchar_t hangar[96];
    std::swprintf(hangar,96,Tr(Tx::dnHangar),c.stowed,c.out);
    if(c.lifting>0)wcscat_s(hangar,Tr(Tx::dnHangarLift));
    else if(c.flightWait>0.5f){const std::size_t k=wcslen(hangar);std::swprintf(hangar+k,96-k,Tr(Tx::dnHangarWait),c.flightWait);}
    Label(text,lines,at,tx,y,0,kLineScale*0.9f,kWhite,L"%ls",hangar);
    y+=row;
    Label(text,lines,at,tx,y,0,kLineScale*0.75f,kDnIdle,L"%ls",Tr(Tx::dnLegend));
    // the keys, low in the middle
    const Config& k=Cfg();
    wchar_t seats[32],view[32],hangarKey[32],descend[32],boost[32],ahead[32];
    if(c.keys){KeyName(k.seatNextKey,seats,32);KeyName(k.firstPersonKey,view,32);KeyName(k.dreadnoughtHangarKey,hangarKey,32);
               KeyName(k.dreadnoughtDescendKey,descend,32);KeyName(k.dreadnoughtBoostKey,boost,32);KeyName(k.dreadnoughtAheadKey,ahead,32);}
    else{SeatButtonName(k.dreadnoughtSeatButton,seats,32);wcscpy_s(view,L"V");SeatButtonName(k.dreadnoughtHangarButton,hangarKey,32);
         SeatButtonName(k.dreadnoughtDescendButton,descend,32);SeatButtonName(k.dreadnoughtBoostButton,boost,32);
         SeatButtonName(k.dreadnoughtAheadButton,ahead,32);}
    // the look ahead only where it works: its key set, in the third person (first person looks from the seat's eye)
    wchar_t aheadHint[48]{};
    if(!c.firstPerson && (c.keys ? k.dreadnoughtAheadKey : k.dreadnoughtAheadButton)!=0)
        std::swprintf(aheadHint,_countof(aheadHint),Tr(Tx::dnKeysAhead),ahead);
    if(c.seat==0) {
        if(c.keys)Label(text,lines,at,width*0.5f,height*0.905f,1,kLineScale*0.8f,kWhite,Tr(Tx::dnKeysFly),descend,boost);
        else Label(text,lines,at,width*0.5f,height*0.905f,1,kLineScale*0.8f,kWhite,Tr(Tx::dnPadFly),descend,boost);
        Label(text,lines,at,width*0.5f,height*0.935f,1,kLineScale*0.8f,kWhite,Tr(Tx::dnKeysBridge),hangarKey,seats,view,aheadHint);
    } else Label(text,lines,at,width*0.5f,height*0.935f,1,kLineScale*0.8f,kWhite,Tr(Tx::dnKeysGun),seats,view,aheadHint);
}

// A carrier's world bars (see the top): the hull's over its tower, each deck part's over its place.
constexpr float kCarrierFar=1500.0f,kPartFar=600.0f;
void CarrierBars(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const CarrierPanel& p,
                 Line* lines,int* at,ULONGLONG now) noexcept {
    float sx,sy,depth;
    if(LineSink{lines,at}.Room(1,__func__) && Project(vp,p.at,width,height,&sx,&sy,&depth)) {
        const float k=DepthScale(depth,kCarrierFar),ks=k*s;
        const float share=p.hullMax>0.0f ? Unit(p.hull/p.hullMax) : 0.0f;
        Line& l=lines[(*at)++];
        Format(l,Tr(Tx::carrierBadge),static_cast<int>(std::lround(share*100.0f)));
        l.scale=kTitleScale*k;l.rgba=kTitle;l.w=l.h=0.0f;
        if(text)MeasureAll(*text,&l,1);
        const float bw=240.0f*ks,bh=12.0f*ks;
        Bar(drawer,ctx,sx-bw*0.5f,sy-bh,bw,bh,share,TrailOf(p.key,share,now),HullColour(share),ks);
        l.x=sx-l.w*0.5f;l.y=sy-bh-4.0f*ks-l.h;
    }
    for(int k=0;k<p.parts && k<4 && LineSink{lines,at}.Room(1,__func__);++k) {
        const auto& part=p.part[k];
        if(!Project(vp,part.at,width,height,&sx,&sy,&depth))continue;
        const float f=DepthScale(depth,kPartFar),fs=f*s;
        const float share=part.down || part.max<=0.0f ? 0.0f : Unit(part.hp/part.max);
        Line& l=lines[(*at)++];
        wchar_t name[24];
        hudtext::WordTo(part.name,name,_countof(name));
        if(part.down) {
            const int sec=static_cast<int>(part.repairSec);
            Format(l,Tr(Tx::carrierPartDownShort),name,sec/60,sec%60);
            l.rgba=kDown;
        } else {
            Format(l,L"%ls",name);
            l.rgba=kWhite;
        }
        l.scale=kLineScale*f;l.w=l.h=0.0f;
        if(text)MeasureAll(*text,&l,1);
        const float bw=90.0f*fs,bh=6.0f*fs;
        // Each part's trail its own: the carrier's key and the part's index (a value, never read through).
        const void* const key=static_cast<const unsigned char*>(p.key)+1+k;
        Bar(drawer,ctx,sx-bw*0.5f,sy-bh,bw,bh,share,TrailOf(key,share,now),part.down ? kRed : kTeal,fs);
        l.x=sx-l.w*0.5f;l.y=sy-bh-3.0f*fs-l.h;
    }
}

// EDF6AutoTurret's turret the player is at (turretaim.cpp, common/edf/aimlink.h; the user, 2026-10-06: "a lock box on
// the target I pick"). The lock as the jets' (LockAt: a yellow square closing in while its track settles, then the red
// diamond). Low on the screen one line: the auto-aim's state, the key that flips it and, while on, the lock binding.
// One auto-aim since 2026-10-25 (the user: "这个自动瞄准是不是多余了一个版本，删掉一个"): the lead-circle mode, its circle,
// its bore cross and its two lines ("自动瞄准 开" over "[Z] 自动瞄准 关", read as two auto-aims) went; StockMarks' own lead
// mark shows where to aim. Off by default since the same day (the user: "怎么自动开启自动瞄准了，而且也没啥提示欸……自动瞄准不要
// 开启，开启以后应该有被瞄准敌人的高亮圈之类的"): AUTO-AIM: OFF (white) and the key that turns it on, or AUTO-AIM: ON (green)
// with the key that turns it off and the lock's; for kSwitchMs after a flip (`flipped`) the new state large over the
// screen's middle on a panel; while on and nothing is locked, a green ring (TargetRing) on the enemy the gun is turned
// onto, else on the one a lock press would take now.
constexpr float kTargetRing=26.0f;   // px at 1080 lines
// A ring with four ticks out of it round `p` (the auto-aim's target with no lock).
void TargetRing(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* p) noexcept {
    float x,y,depth;
    if(!Project(vp,p,width,height,&x,&y,&depth))return;
    const float r=kTargetRing*s,t=2.0f*s,tick=8.0f*s;
    Arc(drawer,ctx,x,y,r,0.0f,kTurn,t,24,kGreen);
    Seg(drawer,ctx,x-r-tick,y,x-r,y,t,kGreen);Seg(drawer,ctx,x+r,y,x+r+tick,y,t,kGreen);
    Seg(drawer,ctx,x,y-r-tick,x,y-r,t,kGreen);Seg(drawer,ctx,x,y+r,x,y+r+tick,t,kGreen);
}
// The side's letter in strokes, `h` px high, its top left at (x, y).
void FistLetter(void* drawer,void* ctx,int side,float x,float y,float h,float t,const float* rgba) noexcept {
    const float w=h*0.6f;
    Seg(drawer,ctx,x,y,x,y+h,t,rgba);
    if(side==bargaaim::kLeft){Seg(drawer,ctx,x,y+h,x+w,y+h,t,rgba);return;}
    Seg(drawer,ctx,x,y,x+w*0.7f,y,t,rgba);Seg(drawer,ctx,x+w*0.7f,y,x+w,y+h*0.25f,t,rgba);
    Seg(drawer,ctx,x+w,y+h*0.25f,x+w*0.7f,y+h*0.5f,t,rgba);Seg(drawer,ctx,x+w*0.7f,y+h*0.5f,x,y+h*0.5f,t,rgba);
    Seg(drawer,ctx,x+w*0.35f,y+h*0.5f,x+w,y+h,t,rgba);
}
// Corner brackets round (x, y), `half` px from it.
void Brackets(void* drawer,void* ctx,float x,float y,float half,float t,const float* rgba) noexcept {
    const float arm=half*0.45f;
    for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2) {
        const float cx=x+sx*half,cy=y+sy*half;
        Seg(drawer,ctx,cx,cy,cx-sx*arm,cy,t,rgba);Seg(drawer,ctx,cx,cy,cx,cy-sy*arm,t,rgba);
    }
}
// What the fist's mark came out as (the Debug=1 "BARGA draw" line).
struct FistDrawn { char how; float x,y; bool target; };   // how: 'r' a reticle on the screen, 'e' a chevron at the edge, '-' none
FistDrawn FistMarkDraw(void* drawer,void* ctx,const float* vp,float width,float height,float s,int side,
                       const bargaaim::FistMark& f) noexcept {
    alignas(16) static const float kDimCyan[4]={0.3f,0.85f,1.0f,0.45f};
    alignas(16) static const float kDimGreen[4]={0.35f,1.0f,0.35f,0.45f};
    alignas(16) static const float kDimRed[4]={1.0f,0.3f,0.25f,0.5f};
    if(!f.on)return {'-',-1.0f,-1.0f,false};
    const float* const rgba=f.target ? (f.next ? kRed : kDimRed)
                          : side==bargaaim::kLeft ? (f.next ? kCyan : kDimCyan) : (f.next ? kGreen : kDimGreen);
    const float t=(f.next ? 3.0f : 2.0f)*s,letter=14.0f*s;
    float x,y,depth;
    if(f.target && Project(vp,f.targetAt,width,height,&x,&y,&depth))Brackets(drawer,ctx,x,y,18.0f*s,t,rgba);
    if(Project(vp,f.at,width,height,&x,&y,&depth)) {
        const float gap=5.0f*s,tick=gap+9.0f*s;
        Seg(drawer,ctx,x-tick,y,x-gap,y,t,rgba);Seg(drawer,ctx,x+gap,y,x+tick,y,t,rgba);
        Seg(drawer,ctx,x,y-tick,x,y-gap,t,rgba);Seg(drawer,ctx,x,y+gap,x,y+tick,t,rgba);
        FistLetter(drawer,ctx,side,x+tick*0.6f+2.0f*s,y-tick*0.6f-letter,letter,t,rgba);
        return {'r',x,y,f.target};
    }
    float c[4];
    Clip(vp,f.at,c);
    const float size=12.0f*s;
    const auto e=bargaaim::ToEdge(c[0],c[1],width,height,size*2.5f);
    // A chevron: its tip at the edge point, its arms back along the direction.
    const float bx=e.x-e.dx*size,by=e.y-e.dy*size,px=-e.dy*size*0.8f,py=e.dx*size*0.8f;
    Seg(drawer,ctx,bx+px,by+py,e.x,e.y,t,rgba);Seg(drawer,ctx,e.x,e.y,bx-px,by-py,t,rgba);
    FistLetter(drawer,ctx,side,e.x-e.dx*size*2.2f-letter*0.3f,e.y-e.dy*size*2.2f-letter*0.5f,letter,t,rgba);
    return {'e',e.x,e.y,f.target};
}
void BargaAimMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const bargaaim::Readout& r) noexcept {
    const float t=2.0f*s;
    // Debug=1: what of it is on the screen, every kBargaDrawLogMs (the draw's half of the game thread's "BARGA v=" line).
    constexpr ULONGLONG kBargaDrawLogMs=500;
    static ULONGLONG logged=0;
    const ULONGLONG now=GetTickCount64();
    const bool log=Cfg().debug && now-logged>=kBargaDrawLogMs;
    if(log)logged=now;
    FistDrawn drawn[2];
    for(int side=0;side<2;++side)drawn[side]=FistMarkDraw(drawer,ctx,vp,width,height,s,side,r.fist[side]);
    int contacts=0,contactsShown=0,roundsShown=0;
    for(int i=0;i<r.spheres && i<bargaaim::kMostSpheres;++i) {
        const auto& sp=r.sphere[i];
        float sx,sy;
        if(sp.touch && sp.ground){++contacts;contactsShown+=ImpactCross(drawer,ctx,vp,width,height,s,sp.groundAt,&sx,&sy) ? 1 : 0;}
        if(sp.target){++contacts;contactsShown+=ImpactCross(drawer,ctx,vp,width,height,s,sp.targetAt,&sx,&sy) ? 1 : 0;}
    }
    const int n=!r.cannon || r.rounds<=0 ? 0 : r.rounds<bargaaim::kMostRounds ? r.rounds : bargaaim::kMostRounds;
    if(log)
        Log("BARGA draw %.0fx%.0f L=%c%s%s (%.0f,%.0f) R=%c%s%s (%.0f,%.0f) spheres %d contacts %d/%d rounds %d",
            width,height,drawn[0].how,r.fist[0].next ? "*" : "",drawn[0].target ? " hit" : "",drawn[0].x,drawn[0].y,
            drawn[1].how,r.fist[1].next ? "*" : "",drawn[1].target ? " hit" : "",drawn[1].x,drawn[1].y,r.spheres,contactsShown,contacts,n);
    if(n<=0)return;
    // Only the rounds that land: one that meets nothing ends its flight in the air (RoundLands' finite end, up to
    // km out), no point on the ground to mark, so the line breaks there and it has no ring or cross.
    const float* const rgba=r.bursting ? kRed : kAmber;
    int landedCount=0;
    for(int i=0;i<n;) {
        if(!r.landed[i]){++i;continue;}
        int end=i;
        while(end<n && r.landed[end])++end;
        roundsShown+=WorldPolyline(drawer,ctx,vp,width,height,r.at+i,end-i,t*1.5f,rgba);
        landedCount+=end-i;
        i=end;
    }
    if(log)Log("BARGA draw rounds %d landed of %d, %d on the screen",landedCount,n,roundsShown);
    for(int i=0;i<n;++i) {
        if(!r.landed[i] || (i%bargaaim::kRingEvery!=0 && i!=n-1))continue;
        if(r.blast>0.0f)WorldRing(drawer,ctx,vp,width,height,r.at[i],r.blast,t,rgba);
    }
    float sx,sy;
    if(r.landed[0])ImpactCross(drawer,ctx,vp,width,height,s,r.at[0],&sx,&sy);   // the next round
}
void TurretAimMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                    const edf::aimlink::TurretReadoutV1& r,Line* lines,int* at,float controlsBottom) noexcept {
    namespace link=edf::aimlink;
    const bool on=r.mode!=link::Mode::manual;
    float x,y;
    if(on && r.lock!=link::Lock::none)LockAt(drawer,ctx,vp,width,height,s,r.lock==link::Lock::locked ? 2 : 1,r.at,r.lockProgress,&x,&y);
    else if(on && r.target)TargetRing(drawer,ctx,vp,width,height,s,r.at);
    // The state (on: green, a tank driver's whose lock its gunners fight GUNNERS: AUTO-AIM, white; off: white), the key
    // that flips it (a pad with no AutoAimButton: where to set one) and, while on, the lock's binding; red while locked.
    wchar_t text1[200];
    wcscpy_s(text1,Tr(!on ? Tx::autoAimStateOff : r.ownGun ? Tx::autoAimStateOn : Tx::gunnersAuto));
    std::size_t n=wcslen(text1);
    if(r.keys ? r.modeKey>0 : r.modeButton>0) {
        wchar_t flip[32];
        Binding(r.keys,r.modeKey,r.modeButton,flip,32);
        std::swprintf(text1+n,200-n,Tr(on ? Tx::autoAimTurnOff : Tx::autoAimTurnOn),L"   ",flip);
    } else if(!r.keys) {
        std::swprintf(text1+n,200-n,Tr(Tx::autoAimPadUnset),L"   ");
    }
    n=wcslen(text1);
    if(on && (r.keys ? r.lockKey>0 : r.lockButton>0)) {
        wchar_t lock[32];
        Binding(r.keys,r.lockKey,r.lockButton,lock,32);
        std::swprintf(text1+n,200-n,Tr(Tx::lockKey),L"   ",lock,Tr(r.lock==link::Lock::none ? Tx::lockWord : Tx::nextWord));
    }
    const float* const colour=!on ? kWhite : r.lock==link::Lock::locked ? kRed : r.ownGun ? kGreen : kWhite;
    Label(text,lines,at,width*0.5f,controlsBottom>=0.0f ? controlsBottom : height*0.905f,1,kLineScale,colour,L"%ls",text1);
}
// The new auto-aim state large over the screen's middle on a panel, for kSwitchMs after a flip: drawn whenever the
// turret's readout is there, with the line above or without it (the optic, the overhead view, the gunner's sight, first
// person: the flip's key works there too, and its only answer must not be hidden).
void AutoAimBanner(void* drawer,void* ctx,Text* text,float width,float height,float s,bool on,Line* lines,int* at) noexcept {
    const float by=height*0.30f;
    const float* const banner=on ? kGreen : kWhite;
    Label(text,lines,at,width*0.5f,by,1,kTitleScale,banner,L"%ls",Tr(on ? Tx::autoAimBannerOn : Tx::autoAimBannerOff));
    const float w=(*at>0 && lines[*at-1].w>0.0f ? lines[*at-1].w : 420.0f*s)+40.0f*s,h=48.0f*s;
    Rect(drawer,ctx,(width-w)*0.5f,by-h*0.5f,(width+w)*0.5f,by+h*0.5f,kPanel);
    Rect(drawer,ctx,(width-w)*0.5f,by-h*0.5f,(width+w)*0.5f,by-h*0.5f+3.0f*s,banner);
}

// --- The stock vehicles' HUD (vhud.cpp gathers it; ini StockVehicleHud; docs/hud-re.md §7): for the stock vehicle the
// player drives or mans, in the helis' green and drawn with the same quads and text (exclusive full screen too):
//  - each weapon's impact point (StockMarks): a gun's or a cannon's the helis' boresight and pipper where its round
//    meets the map, or with an enemy under the view the jets' gun sight on it (the pipper where the round passes it, its
//    lead mark where it is then: pipper on the mark, a hit; dim out of the round's reach); neither (the sky): the
//    boresight alone with its label (roundaim.h GunSight), a grenade's or a mortar's (and any round flying longer than kLobSec) the
//    artillery's yellow cross as the Katyusha's (LauncherMarks) with its range and flight time, the rockets' the helis'
//    diamond, a missile's lock the helis' (LockAt; no lock: a dim ring round its boresight and its LockonRange); its
//    label and range beside it; the selected store's label in brackets. Weapons that land together (a pair of guns)
//    are drawn once;
//  - the heading tape at the top (the gun's heading, a caret under it for the hull's: StockTape);
//  - left of the bottom centre (StockBlock): the hull / turret indicator (the hull's outline and the gun's line, up the
//    camera's look: which way the hull points against where the player looks and aims), the vehicle's kind and seat,
//    its speed and HP with the HP bar (STAB on that line while the gun stabilizer holds the seat's gun), a line per
//    weapon (rounds of the magazine; RELOAD and its share and seconds; EMPTY when it never reloads), and over it the warning (a missile, a lock, the hull critical, out of ammo);
//  - the threat ring of the aircraft (ThreatRing) round the screen's middle. ---
constexpr float kLobSec=2.5f;        // s: a round in the air longer than this is lobbed (the cross, with its flight time)
const char kStockHpKey=0;            // the HP bar's damage trail's key (an address of our own: never a vehicle's)

// `a` - `b` in degrees, in (-180, 180].
float HdgDiff(float a,float b) noexcept {
    float d=std::fmod(a-b,360.0f);
    if(d>180.0f)d-=360.0f;
    if(d<=-180.0f)d+=360.0f;
    return d;
}

// The heading tape for `hdg` (the gun's) with a caret under it at `hull`'s heading when within the tape (HeadingTape's
// look: a tick every 5 degrees, the tens numbered).
void StockTape(void* drawer,void* ctx,Text* text,float width,float height,float s,float hdg,float hull,Line* lines,int* at) noexcept {
    const float cx=width*0.5f,base=height*0.11f,k=kTapePx*s,t=2.0f*s;
    Seg(drawer,ctx,cx-kTapeHalf*k,base,cx+kTapeHalf*k,base,t,kHudDim);
    for(int d=static_cast<int>(std::ceil((hdg-kTapeHalf)/5.0f))*5;static_cast<float>(d)<=hdg+kTapeHalf;d+=5) {
        const float x=cx+(static_cast<float>(d)-hdg)*k;
        const bool ten=d%10==0;
        Seg(drawer,ctx,x,base,x,base-(ten ? 10.0f : 5.0f)*s,t,kHud);
        if(ten)Label(text,lines,at,x,base-22.0f*s,1,kLineScale*0.8f,kHud,L"%03d",((d%360)+360)%360);
    }
    Tri(drawer,ctx,cx,base+12.0f*s,cx,base+2.0f*s,6.0f*s,kHud);
    Label(text,lines,at,cx,base+26.0f*s,1,kLineScale,kHud,L"%03d",static_cast<int>(std::lround(hdg))%360);
    if(hull<0.0f)return;
    const float off=HdgDiff(hull,hdg);
    if(std::fabs(off)>kTapeHalf)return;
    const float x=cx+off*k;
    Tri(drawer,ctx,x,base+14.0f*s,x,base+4.0f*s,5.0f*s,kAmber);
    Label(text,lines,at,x,base+40.0f*s,1,kLineScale*0.7f,kAmber,L"%ls",Tr(Tx::hullMark));
}

// A weapon's label (its round's class: rounds.cpp, vhud.cpp) in the HUD's language, bracketed when selected.
void ArmName(const StockArm& a,bool selected,wchar_t* out,std::size_t size) noexcept {
    wchar_t word[24];
    hudtext::WordTo(a.label,word,_countof(word));
    _snwprintf_s(out,size,_TRUNCATE,selected ? L"[%ls]" : L"%ls",word);
}

// One weapon's mark (see above); `name` its label (bracketed when selected).
// A direct-fire gun's sight: the reticle its style draws (reticle.h: a tank gun's chevron, lead stadia and ballistic
// range ladder; a marksman's duplex and mil dots; a machine gun's ring; an anti-aircraft gun's lead rings; a beam's
// open cross) on the gun's line (the boresight, where the gun points: it follows the camera a moment late), its angles
// from the view's own projection (the magnification is in it); over the left of it the gun, over the right its
// rangefinder (the map hit or the ranged target: the pipper's `range`). The pipper and the lead mark stay StockMark's.
void GunReticle(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,
                const wchar_t* name,Line* lines,int* at) noexcept {
    float cx,cy;
    if(!sight::ToScreen(vp,a.bore,0.0f,width,height,&cx,&cy))return;
    const reticle::View v{cx,cy,FocalAt(vp,a.bore,width,height,cx,cy),s,kLineScale};
    reticle::Aim aim{};
    aim.style=a.reticle;aim.shell=a.shell;aim.speed=a.roundSpeed;
    LadderMarks(vp,width,height,a.ladder,&aim);
    reticle::Sketch k{};
    reticle::Draw(aim,v,&k);
    Render(drawer,ctx,text,s,k,lines,at);
    const float in=(a.reticle==reticle::Style::cannon ? reticle::kStadiaIn : 40.0f)*s;
    const float over=cy-16.0f*s,note=kLineScale*0.85f;
    Label(text,lines,at,cx-in,over,2,note,kHud,L"%ls",name);
    const float measured=a.ranged ? a.targetRange : a.range;
    if(!fcs::KitOf(a.fcs).rangefinder)return;   // a telescope: the stadia and the ladder are the range
    if(measured>0.0f && (a.hit || a.ranged))
        Label(text,lines,at,cx+in,over,0,note,a.ranged && !a.inReach ? kHudDim : kHud,Tr(Tx::sightRange),static_cast<int>(std::lround(measured)));
    else Label(text,lines,at,cx+in,over,0,note,kHudDim,L"%ls",Tr(Tx::sightNoRange));
}

// --- Looking through a magnified optic (scopereticle.h; the user, 2026-10-11: the Z sight redone, realistic) ---
// The fire-control computer's data line, inside the bottom of the field either side of the lower post, in LED digits
// (a Leopard 2's EMES / an M1's GPS show theirs under the image): left the weapon now laid and its rounds, under it the
// magnification; right the laser range (four digits, ---- with no return), under it the gun's state.
void ScopeData(Text* text,Line* lines,int* at,float s,const scope::Field& f,const StockArm& a,float zoom,int zero) noexcept {
    const float y=f.cy+f.r*scope::kDataLine,gap=16.0f*s,row=24.0f*s;
    const float measured=a.ranged ? a.targetRange : a.range;
    const bool ranged=measured>0.0f && (a.hit || a.ranged);
    wchar_t word[40];
    if(a.name[0])_snwprintf_s(word,_countof(word),_TRUNCATE,L"%ls",a.name);
    else hudtext::WordTo(a.label,word,_countof(word));
    Label(text,lines,at,f.cx-gap,y,2,kLineScale*0.9f,kLed,L"%ls  %d",word,a.ammo>0 ? a.ammo : 0);
    const bool laser=fcs::KitOf(a.fcs).rangefinder;   // a telescope has no range to show (firecontrol.h): the stadia are it
    if(laser && ranged)Label(text,lines,at,f.cx+gap,y,0,kLineScale*1.15f,a.ranged && !a.inReach ? kLedDim : kLed,L"%04d",
                    static_cast<int>(std::lround(measured>9999.0f ? 9999.0f : measured)));
    else if(laser)Label(text,lines,at,f.cx+gap,y,0,kLineScale*1.15f,kLedDim,L"----");
    Label(text,lines,at,f.cx-gap,y+row,2,kLineScale*0.8f,kLed,L"%.0fx",zoom);
    if(a.ammo>0)Label(text,lines,at,f.cx+gap,y+row,0,kLineScale*0.8f,kLed,L"%ls",Tr(Tx::scopeReady));
    else if(a.canReload)Label(text,lines,at,f.cx+gap,y+row,0,kLineScale*0.8f,kLedDim,Tr(Tx::scopeLoading),a.reloadSec>0.0f ? a.reloadSec : 0.0f);
    else Label(text,lines,at,f.cx+gap,y+row,0,kLineScale*0.8f,kLedDim,L"%ls",Tr(Tx::scopeEmpty));
    // The range setting (sightzoom.h: the wheel), the line of sight laid under the bore for it; ---- the bore line.
    if(zero>0)Label(text,lines,at,f.cx-gap,y+2.0f*row,2,kLineScale*0.8f,kLed,Tr(Tx::scopeZero),zero);
    else Label(text,lines,at,f.cx-gap,y+2.0f*row,2,kLineScale*0.8f,kLedDim,L"%ls",Tr(Tx::scopeZeroBore));
}
// What the optic shows of the weapon the gunner has laid (the seat's fire-control pick): its reticle etched across the
// field round the bore's point, a guided round's lock as the tracker shows it, a rocket's impact point, the predicted
// ground intersections of the barrels (the 1x HUD's crosses: the computer's), and the data line.
void ScopeArm(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,
              const scope::Field& f,float zoom,int zero,Line* lines,int* at) noexcept {
    // The reticle is on the optic's glass: the field's centre, the line of sight (the camera looks along it; laid under the
    // bore by the range setting, so the bore's own point stands above it and the range marks hang from the set range).
    const float cx=f.cx,cy=f.cy;
    float bx=cx,by=cy;   // the focal length is measured at the bore's own point (the turned probe's distance from it)
    float focal=sight::ToScreen(vp,a.bore,0.0f,width,height,&bx,&by) ? FocalAt(vp,a.bore,width,height,bx,by) : 0.0f;
    if(!(focal>0.0f))focal=CentreFocal(vp,width,height);
    const reticle::View v{cx,cy,focal,s,kLineScale};
    reticle::Aim aim{};
    aim.style=a.reticle;aim.shell=a.shell;aim.speed=a.roundSpeed;
    LadderMarks(vp,width,height,a.ladder,&aim);
    const scope::Look look=a.kind==RoundKind::homing ? scope::Look::seeker : a.kind==RoundKind::rocket ? scope::Look::rocket :
                           scope::LookOf(a.reticle);
    reticle::Sketch k{};
    scope::Draw(look,aim,v,f,&k);
    Render(drawer,ctx,text,s,k,lines,at);
    float x,y;
    if(a.kind==RoundKind::homing)LockAt(drawer,ctx,vp,width,height,s,a.lock,a.at,a.lockProgress,&x,&y);
    else if(a.kind==RoundKind::rocket && a.hit && sight::ToScreen(vp,a.at,1.0f,width,height,&x,&y))RocketDiamond(drawer,ctx,x,y,s,kLit);
    else if(a.ranged && !a.physicalOnly)FcsLead(drawer,ctx,text,vp,width,height,s,a,a.inReach ? kLit : kLedDim,lines,at);
    if(a.kind!=RoundKind::homing)PhysicalPaths(drawer,ctx,text,vp,width,height,s,a.path,a.paths,false,lines,at);
    ScopeData(text,lines,at,s,f,a,zoom,zero);
}
void PublishScopeDrawn(const void* soldier) noexcept {
    const scopehold::Drawn d{soldier,soldier ? GetTickCount64() : 0};
    AcquireSRWLockExclusive(&scopeDrawnLock);
    scopeDrawn=d;
    ReleaseSRWLockExclusive(&scopeDrawnLock);
}

// `reticle`: this gun's sight is GunReticle (the seat's sight gun: StockMarks), its pipper unlabelled (the reticle
// reads its range).
void StockMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,const wchar_t* name,
               bool reticle,Line* lines,int* at) noexcept {
    if(scopeNow.on && !a.lofted){ScopeArm(drawer,ctx,text,vp,width,height,s,a,scopeNow.field,scopeNow.zoom,scopeNow.zero,lines,at);return;}
    float x,y;
    const float note=kLineScale*0.85f;
    const int metres=static_cast<int>(std::lround(a.range));
    if(a.kind==RoundKind::homing) {
        if(LockAt(drawer,ctx,vp,width,height,s,a.lock,a.at,a.lockProgress,&x,&y))
            Label(text,lines,at,x,y+44.0f*s,1,note,a.lock==2 ? kRed : kYellow,L"%ls %d m",name,metres);
        else if(!a.lock) {
            wchar_t line[48];
            _snwprintf_s(line,_countof(line),_TRUNCATE,L"%ls %d m",name,metres);
            SeekerGate(drawer,ctx,text,vp,width,height,s,a.bore,line,lines,at);
        }
        return;
    }
    if(a.kind==RoundKind::rocket) {
        if(sight::ToScreen(vp,a.at,1.0f,width,height,&x,&y) || sight::ToScreen(vp,a.bore,0.0f,width,height,&x,&y)) {
            const float* c=a.hit ? kHud : kHudDim;
            RocketDiamond(drawer,ctx,x,y,s,c);
            if(a.hit)Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,c,L"%ls %d m",name,metres);
            else Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,c,L"%ls",name);
        }
        return;
    }
    if(reticle && !a.physicalOnly)GunReticle(drawer,ctx,text,vp,width,height,s,a,name,lines,at);
    else Boresight(drawer,ctx,vp,width,height,s,a.bore);
    if(a.ranged && !a.physicalOnly)FcsLead(drawer,ctx,text,vp,width,height,s,a,a.inReach ? kHud : kHudDim,lines,at);
    PhysicalPaths(drawer,ctx,text,vp,width,height,s,a.path,a.paths,!reticle,lines,at);
}
// --- The Proteus (proteus.cpp; README 普罗透斯): its part of the stock vehicle HUD. In the block (StockBlock) up to four
// lines with a bar under some: the stance (and the stagger's progress), the shield (its state, HP and deployed its heat;
// the HP bar under it), the field (its allies), the driver's missile launcher; the bindings named where the driver has a
// press to make. The cannons and the launcher themselves are listed with the seat's weapons (vhud.cpp, borrowed mounts).
// On the hull ring the standing shield's arc; on the ground the field's edge. ---
constexpr int kProteusLines=4;
const char kBarrierKey=0;            // the shield's HP bar's damage trail's key (an address of our own)
struct ProteusLine { Line* line; bool bar; float share; const float* fill; const void* trailKey; };

// A binding's name: the mouse's buttons by name (GetKeyNameText has none for them), the keys as KeyName, a pad's button.
void ProteusBinding(bool keys,int key,int button,wchar_t* out,int size) noexcept {
    static const Tx kMouse[]={Tx::count,Tx::mouseLeft,Tx::mouseRight,Tx::count,Tx::mouseMiddle,Tx::mouseX1,Tx::mouseX2};
    if(keys && key>0 && key<=6){wcscpy_s(out,static_cast<rsize_t>(size),kMouse[key]==Tx::count ? L"?" : Tr(kMouse[key]));return;}
    Binding(keys,key,button,out,size);
}

// The block's Proteus lines (see above) into `pl` (`n` of them): the lines are `lines`' next ones.
int ProteusLinesOf(const ProteusReadout& p,Line* lines,ProteusLine* pl) noexcept {
    wchar_t mode[24],shield[24],launcher[24];
    ProteusBinding(p.keys,p.modeKey,p.modeButton,mode,24);
    ProteusBinding(p.keys,p.shieldKey,p.shieldButton,shield,24);
    ProteusBinding(p.keys,p.launcherKey,0,launcher,24);
    if(!p.keys)wcscpy_s(launcher,L"LT");
    const bool blink=(GetTickCount64()/125)%2==0;
    int n=0;
    const auto add=[&](bool bar,float share,const float* fill)->Line& {
        Line& l=lines[n];
        l.scale=kLineScale*0.85f;l.rgba=kHud;l.text[0]=L'\0';
        pl[n]=ProteusLine{&l,bar,share,fill,nullptr};
        ++n;
        return l;
    };
    // The stance.
    {
        const bool stagger=p.mode==proteus::Mode::deploying || p.mode==proteus::Mode::stowing;
        Line& l=add(stagger,p.stagger,kAmber);
        switch(p.mode) {
            case proteus::Mode::walk: Format(l,L"%ls",Tr(Tx::proteusWalk));if(p.driver)Append(l,Tr(Tx::proteusDeployKey),mode);break;
            case proteus::Mode::deploying: Format(l,Tr(Tx::proteusDeploying),static_cast<int>(std::lround(p.stagger*100.0f)));l.rgba=kAmber;break;
            case proteus::Mode::deployed: Format(l,L"%ls",Tr(Tx::proteusDeployed));if(p.driver)Append(l,Tr(Tx::proteusStowKey),mode);l.rgba=kCyan;break;
            case proteus::Mode::stowing: Format(l,Tr(Tx::proteusStowing),static_cast<int>(std::lround(p.stagger*100.0f)));l.rgba=kAmber;break;
        }
    }
    // The shield: its state and HP (deployed its heat too), the HP bar under it.
    {
        const bool deployed=p.mode==proteus::Mode::deployed;
        Line& l=add(true,p.shield,HullColour(p.shield));
        pl[n-1].trailKey=&kBarrierKey;
        const Tx state=!p.shieldReady ? Tx::shieldOffline : p.broken ? Tx::shieldBroken : deployed && p.overheated ? Tx::overheat :
                       p.shieldUp ? Tx::shieldUp : Tx::shieldOff;
        const int hp=static_cast<int>(std::lround(p.shield*100.0f));
        if(deployed)Format(l,Tr(Tx::shieldHeat),Tr(state),hp,static_cast<int>(std::lround(p.heat*100.0f)));
        else Format(l,Tr(Tx::frontShield),Tr(state),hp);
        if(p.driver && p.shieldReady)Append(l,L"   [%ls]",shield);
        if(p.priority)Append(l,L"%ls",Tr(Tx::alliesFocus));
        l.rgba=!p.shieldReady ? kHudDim : p.broken || (deployed && p.overheated) ? (blink ? kRed : kWhite) : p.shieldUp ? kCyan : kHudDim;
    }
    if(p.mode==proteus::Mode::deployed) {   // the field
        Line& f=add(false,0.0f,nullptr);
        Format(f,Tr(Tx::field),static_cast<int>(std::lround(p.fieldRadius)),p.allies);
        f.rgba=kTeal;
    }
    if(p.driver) {   // the launcher, the driver's while its own seat is empty
        Line& l=add(false,0.0f,nullptr);
        if(p.launcher){Format(l,Tr(Tx::launcherKey),launcher);l.rgba=kYellow;}
        else if(p.mode!=proteus::Mode::deployed){Format(l,L"%ls",Tr(Tx::launcherDeployFirst));l.rgba=kHudDim;}
        else --n;   // a gunner of its own works it
    }
    return n;
}

// The field's edge on the ground (see above).
void ProteusMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const ProteusReadout& p,Line* lines,
                  int* at) noexcept {
    for(int i=0;i<p.ringCount && i<kProteusRing;++i) {
        if(i%2)continue;   // dashed
        float x0,y0,x1,y1;
        const int k=(i+1)%p.ringCount;
        if(sight::ToScreen(vp,p.ring[i],1.0f,width,height,&x0,&y0) && sight::ToScreen(vp,p.ring[k],1.0f,width,height,&x1,&y1))
            Seg(drawer,ctx,x0,y0,x1,y1,2.0f*s,kTeal);
    }
    (void)text;(void)lines;(void)at;
}


// The marks of every aimed weapon but the Katyusha's (launcher.cpp's), those landing on another's drawn once.
// The seat's sight gun (GunReticle's): the picked weapon when its style is a gun sight (reticle.h GunStyle: not a
// flamethrower's, a grenade's, a missile's) with a ladder (a beam needs none); -1 for none.
// Whether arm `i` can carry a gun sight of its own (the style, the ladder).
bool SightCapable(const StockHudReadout& r,int i) noexcept {
    if(i<0 || i>=r.arms || i>=kStockArms)return false;
    const StockArm& a=r.arm[i];
    return a.aimed && !a.physicalOnly && !a.lofted && !a.lobbed && a.kind==RoundKind::arc && reticle::GunStyle(a.reticle) &&
           (a.shell==reticle::Shell::beam || a.ladder.ticks>0);
}
int SightGun(const StockHudReadout& r) noexcept {
    return SightCapable(r,r.sight) ? r.sight : -1;
}
// A gun on the seat's other trigger keeps its own full sight (the user, 2026-10-14: 「切换武器的时候会把瞄具取消到。比如泰坦
// 用机枪的时候，镇魂炮的瞄具就没了」): which trigger was pulled last only says which one is picked (its name bracketed,
// the magnified scope's field), not which gun may be aimed. One fired with the pick (coFired) is the pick's marks.
// A missile / rocket picked keeps the screen to its own marks (StockMarks' rule): the other gun only its bore cross then.
bool OtherTriggerSight(const StockHudReadout& r,int i) noexcept {
    const bool pickIsGun=r.sight<0 || r.sight>=r.arms || r.sight>=kStockArms || r.arm[r.sight].kind==RoundKind::arc;
    return pickIsGun && i!=r.sight && i>=0 && i<r.arms && i<kStockArms && r.arm[i].armed && !r.arm[i].coFired &&
           SightCapable(r,i);
}

// The bore cross of a gun on the seat's other trigger (StockMarks), dimmer than the pick's, its name under it.
void OtherTriggerMark(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,
                      Line* lines,int* at) noexcept {
    float x,y;
    if(!sight::ToScreen(vp,a.bore,0.0f,width,height,&x,&y))return;
    const float t=2.0f*s,r=8.0f*s,g=3.0f*s;
    Seg(drawer,ctx,x-r,y,x-g,y,t,kHudDim);Seg(drawer,ctx,x+g,y,x+r,y,t,kHudDim);
    Seg(drawer,ctx,x,y-r,x,y-g,t,kHudDim);Seg(drawer,ctx,x,y+g,x,y+r,t,kHudDim);
    wchar_t name[32];
    ArmName(a,false,name,_countof(name));
    Label(text,lines,at,x,y+(r+12.0f*s),1,kLineScale*0.7f,kHudDim,L"%ls",name);
}

void StockMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,
                const StockExtras& x,Line* lines,int* at) noexcept {
    const int sightGun=x.high ? -1 : SightGun(r);
    // One selected weapon owns the fire-control marks. A missile/rocket selection must not leave the main gun's
    // optical reticle underneath it; lofted launchers have their dedicated impact/spread marks.
    const int selected=r.sight>=0 && r.sight<r.arms && r.sight<kStockArms ? r.sight : -1;
    for(int i=0;i<r.arms && i<kStockArms;++i) {
        const StockArm& a=r.arm[i];
        if(i!=selected && !a.coFired && !a.armed)continue;
        if(!a.aimed || a.lofted)continue;
        if(x.high || i!=selected) {
            // A gun on the other trigger (the main gun while the coaxial gun is picked, and the other way round): its own
            // bore cross, moving with its barrel, named, as well as where it lands (the user, 2026-10-12: 「泰坦的炮口准星
            // 不会随着炮管动 ... 应该两个准星都同时显示才对」). Not in a magnified scope (that field is the pick's).
            if(!x.high && !scopeNow.on && OtherTriggerSight(r,i)) {
                wchar_t name[32];
                ArmName(a,false,name,_countof(name));
                StockMark(drawer,ctx,text,vp,width,height,s,a,name,true,lines,at);   // its own sight, unbracketed
                continue;
            }
            if(!x.high && !a.coFired && !scopeNow.on && a.kind==RoundKind::arc)OtherTriggerMark(drawer,ctx,text,vp,width,height,s,a,lines,at);
            PhysicalPaths(drawer,ctx,text,vp,width,height,s,a.path,a.paths,i==selected,lines,at);
            continue;
        }
        wchar_t name[32];
        ArmName(a,i==r.selected,name,_countof(name));
        StockMark(drawer,ctx,text,vp,width,height,s,a,name,i==sightGun,lines,at);
    }
}

// The hull / turret indicator at (cx, cy): up is the way the camera looks (`up` its heading); the hull an outline turned
// by its heading off it, the gun a line from the middle by its heading off it (headings < 0: none); `stops` (a Nix:
// the torso's twist limits as headings, each < 0: none) two amber ticks outside the ring.
void HullTurret(void* drawer,void* ctx,float cx,float cy,float s,float up,float hull,float gun,const float* stops) noexcept {
    const float r=kIndicatorR*s,t=2.0f*s;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,t,24,kHudDim);
    Tri(drawer,ctx,cx,cy-r+6.0f*s,cx,cy-r-2.0f*s,4.0f*s,kHudDim);   // the look's mark at the top
    for(int k=0;stops && k<2;++k) {
        if(stops[k]<0.0f)continue;
        const float a=HdgDiff(stops[k],up)*kDeg,fx=std::sin(a),fy=-std::cos(a);
        Seg(drawer,ctx,cx+fx*(r+2.0f*s),cy+fy*(r+2.0f*s),cx+fx*(r+9.0f*s),cy+fy*(r+9.0f*s),t,kAmber);
    }
    if(hull>=0.0f) {
        const float a=HdgDiff(hull,up)*kDeg,fx=std::sin(a),fy=-std::cos(a),rx=-fy,ry=fx;   // its nose and right on the screen
        const float l=0.62f*r,w=0.38f*r;
        const float corner[4][2]={{l,w},{l,-w},{-l,-w},{-l,w}};
        float px[4],py[4];
        for(int k=0;k<4;++k){px[k]=cx+fx*corner[k][0]+rx*corner[k][1];py[k]=cy+fy*corner[k][0]+ry*corner[k][1];}
        for(int k=0;k<4;++k)Seg(drawer,ctx,px[k],py[k],px[(k+1)%4],py[(k+1)%4],t,kHud);
        Tri(drawer,ctx,cx+fx*l*0.55f,cy+fy*l*0.55f,cx+fx*l*0.95f,cy+fy*l*0.95f,w*0.5f,kHud);   // its front
    }
    if(gun>=0.0f) {
        const float a=HdgDiff(gun,up)*kDeg,fx=std::sin(a),fy=-std::cos(a);
        Arc(drawer,ctx,cx,cy,0.2f*r,0.0f,kTurn,t,10,kCyan);
        Seg(drawer,ctx,cx+fx*0.2f*r,cy+fy*0.2f*r,cx+fx*1.05f*r,cy+fy*1.05f*r,3.0f*s,kCyan);
    }
}

// A weapon's line: its label (bracketed when selected), the rounds of the magazine, its reload; its colour by its state.
void ArmLine(Line& l,const StockArm& a,bool selected) noexcept {
    wchar_t name[32];
    ArmName(a,selected,name,_countof(name));
    Format(l,L"%ls",name);
    if(a.ammoMax>1)Append(l,L" %d/%d",a.ammo>0 ? a.ammo : 0,a.ammoMax);
    else if(a.ammo>1)Append(l,L" %d",a.ammo);   // no magazine read (WeaponStatusOk false): the rounds alone
    else if(a.ammo>0)Append(l,L" %ls",Tr(Tx::armReady));
    if(a.ammo<=0 && a.reload<1.0f) {
        Append(l,L" ");
        Append(l,Tr(Tx::armReload),static_cast<int>(std::lround(a.reload*100.0f)));
        if(a.reloadSec>=0.0f)Append(l,L" %.1fs",a.reloadSec);
        l.rgba=kAmber;
    } else if(a.ammo<=0) {
        Append(l,L" %ls",Tr(a.canReload ? Tx::armReloadWord : Tx::armEmpty));
        l.rgba=a.canReload ? kAmber : kRed;
    } else if(a.cycleSec>=0.0f) {   // the next round going in (vhud.cpp Cycle)
        Append(l,L" %ls %.1fs",Tr(Tx::armReloadWord),a.cycleSec);
        l.rgba=kAmber;
    } else l.rgba=selected ? kCyan : kHud;
    l.scale=kLineScale*0.85f;
}

// The EMC's line (EmcCue): charging, its share and an amber bar filling; the beam out, BEAM and the seconds left, its bar
// emptying, white; after it, REARM; ready, the beams its rounds still make, green (a charge let go drains on its bar); no rounds, EMPTY, red.
void EmcLine(Line& l,const EmcCue& c,float* bar,const float** colour) noexcept {
    const float beam=Cfg().emcBeamSec>0.0f ? Cfg().emcBeamSec : 1.0f;
    if(c.firing) {
        Format(l,Tr(Tx::emcBeam),c.beamLeft);
        *bar=Unit(c.beamLeft/beam);*colour=kWhite;
    } else if(c.charging) {
        Format(l,Tr(Tx::emcCharge),static_cast<int>(std::lround(Unit(c.charge)*100.0f)));
        *bar=Unit(c.charge);*colour=kAmber;
    } else if(c.empty) {
        Format(l,L"%ls",Tr(Tx::emcEmpty));
        *bar=0.0f;*colour=kRed;
    } else if(c.rearm>0.0f) {
        Format(l,Tr(Tx::emcRearm),c.rearm);
        *bar=0.0f;*colour=kHudDim;
    } else {
        Format(l,Tr(Tx::emcReady),c.beams);
        *bar=Unit(c.charge);*colour=kGreen;
    }
    l.rgba=*colour;
    l.scale=kLineScale*0.85f;
}

// A heading (sight::HeadingOf's degrees) of world yaw `yaw` (nix.h's: rad, atan2(x, z)).
float HeadingOfYaw(float yaw) noexcept {
    const float d[3]={std::sin(yaw),0.0f,std::cos(yaw)};
    return sight::HeadingOf(d);
}

// The stock armor gauge's numbers where stockgauge.cpp hides it (the user, 2026-10-09: "上了载具以后…隐藏", then "让护甲
// 显示在咱们的hud不就行了"): a line and a bar each, as the hull's in StockBlock (its 150 x 6 px bar under its line). The
// player's armor red at the stock gauge's own low mark (armorhud::kLowArmor, 0x827723), else the HUD's colour; the
// vehicle's durability coloured as StockBlock's hull (HpColour). In StockBlock the armor's line goes under the hull's
// bar; a vehicle whose HUD shows no durability (the aircraft, a stock heli's strip, the Sazabi) has both in ArmorPanel.
constexpr float kDurabilityBarW=150.0f,kDurabilityBarH=6.0f;   // px at 1080 lines: StockBlock's barW / barH
void DurabilityLine(Line& l,Tx what,const armorhud::Durability& d,Text* text) noexcept {
    Format(l,Tr(what),static_cast<int>(std::lround(d.hp)),static_cast<int>(std::lround(d.most)));
    l.scale=kLineScale;l.rgba=d.low ? kRed : kHud;l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
}
const float* DurabilityFill(const armorhud::Durability& d,bool armor) noexcept { return armor ? (d.low ? kRed : kHud) : HpColour(d.share); }
void ArmorPanel(void* drawer,void* ctx,Text* text,float s,const armorhud::Readout& r,Line* lines,int* at) noexcept {
    const int rows=r.hasHull ? 2 : 1;
    if(!LineSink{lines,at}.Room(rows,__func__))return;
    const float lineH=18.0f*s,gap=3.0f*s,barW=kDurabilityBarW*s,barH=kDurabilityBarH*s,x=kArmorPanelX*s;
    float y=kArmorPanelY*s;
    const armorhud::Durability* const d[2]={r.hasHull ? &r.hull : &r.armor,&r.armor};
    const Tx what[2]={r.hasHull ? Tx::hullRow : Tx::armorRow,Tx::armorRow};
    for(int i=0;i<rows;++i) {
        const bool armor=!r.hasHull || i==1;
        Line& l=lines[(*at)++];
        DurabilityLine(l,what[i],*d[i],text);
        l.x=x;l.y=y;y+=(l.h>0.0f ? l.h : lineH)+gap;
        Bar(drawer,ctx,x,y,barW,barH,d[i]->share,d[i]->share,DurabilityFill(*d[i],armor),s);
        y+=barH+gap*2.0f;
    }
}

// The block left of the bottom centre (see above). A Nix: the ring's hull is its legs, its gun the torso (nix.cpp), the
// twist's limits ticked and its angle on the info line. The drill tank: its drill's RPM and heat a line under the
// weapons (DrillPanel's colours), OVERHEAT the warning. The EMC (emc.cpp): its charged beam a line under the weapons
// and a bar under it (EmcLine). The readouts of a kind (the Nix's, the EMC's, the Proteus's) are taken only when they are
// this vehicle's (their position on it). Out of lines (a crowded frame): the head always (it carries the warnings), then
// as many weapons as fit, then the extras that fit whole; never the whole block dropped (with the stock gauge hidden,
// stockgauge.cpp, it is the vehicle's only readout).
void StockBlock(void* drawer,void* ctx,Text* text,float width,float height,float s,const StockHudReadout& r,const StockExtras& x,
                Line* lines,int* at,float bottom=-1.0f,bool compact=false) noexcept {
    if(!LineSink{lines,at}.Room(3,__func__))return;   // the head's three lines first
    int room=LineSink{lines,at}.Free()-3;
    const bool armorOn=x.armor && room>0;   // the hidden stock armor gauge's numbers: next after the head (ArmorRows)
    room-=armorOn ? 1 : 0;
    const int want=compact ? 0 : r.arms<kStockArms ? r.arms : kStockArms;
    const int arms=want<room ? want : room;
    room-=arms;
    const bool drillOn=x.drill && x.drill->maxRpm>0.0f && room>0;
    room-=drillOn ? 1 : 0;
    const EmcCue* const emc=x.emc && vec::Dist(x.emc->pos,r.pos)<2.0f && room>0 ? x.emc : nullptr;   // this vehicle's
    room-=emc ? 1 : 0;
    const ProteusReadout* const prot=x.proteus && vec::Dist(x.proteus->pos,r.pos)<2.0f ? x.proteus : nullptr;   // this vehicle's
    const bool protLines=prot && room>=kProteusLines;
    Line& warn=lines[(*at)++];
    Line& title=lines[(*at)++];
    Line& info=lines[(*at)++];
    Line* const armor=armorOn ? &lines[(*at)++] : nullptr;
    Line* const arm=&lines[*at];
    *at+=arms;
    Line* const drill=drillOn ? &lines[(*at)++] : nullptr;
    Line* const emcLine=emc ? &lines[(*at)++] : nullptr;
    ProteusLine pl[kProteusLines]{};
    const int prots=protLines ? ProteusLinesOf(*prot,&lines[*at],pl) : 0;
    *at+=prots;
    const bool nix=x.nix && vec::Dist(x.nix->at,r.pos)<2.0f;   // the Nix readout is this vehicle's
    bool missile=false,locked=false,dry=r.arms>0;
    for(int i=0;i<r.threats && i<kStockThreats;++i){missile=missile || r.threatKind[i]==2;locked=locked || r.threatKind[i]==1;}
    for(int i=0;i<r.arms && i<kStockArms;++i)dry=dry && r.arm[i].ammo<=0 && !r.arm[i].canReload;
    const float hp=r.hpMax>0.0f ? Unit(r.hp/r.hpMax) : 0.0f;
    const bool blink=(GetTickCount64()/125)%2==0;
    if(missile){Format(warn,L"%ls",Tr(Tx::missileBang));warn.rgba=blink ? kRed : kWhite;}
    else if(locked){Format(warn,L"%ls",Tr(Tx::locked));warn.rgba=kYellow;}
    else if(hp<0.25f && r.hpMax>0.0f){Format(warn,L"%ls",Tr(Tx::hullCritical));warn.rgba=blink ? kRed : kWhite;}
    else if(drill && x.drill->overheated){Format(warn,L"%ls",Tr(Tx::drillOverheat));warn.rgba=blink ? kRed : kWhite;}
    else if(FuelLow(r.fuel)){Format(warn,L"%ls",Tr(Tx::lowFuel));warn.rgba=kAmber;}
    else if(dry){Format(warn,L"%ls",Tr(Tx::noAmmo));warn.rgba=kAmber;}
    else{Format(warn,L"");warn.rgba=kHud;}
    const char* const kind=prot ? "PROTEUS" : r.kind;
    if(r.seat==0)Format(title,L"%hs",kind);
    else Format(title,Tr(Tx::stockGunner),kind,r.seat);
    Format(info,Tr(Tx::stockInfo),static_cast<int>(std::lround(r.speed*3.6f)),static_cast<int>(std::lround(hp*100.0f)));
    if(r.gear!=kNoDriveGear){Append(info,L"    ");Append(info,Tr(Tx::driveGear),drivegear::Name(r.gear));}   // drivegear.cpp
    if(nix){Append(info,L"    ");Append(info,Tr(Tx::twist),static_cast<int>(std::lround(-x.nix->twist*57.2957795f)));}   // right positive, as headings
    if(r.stab)Append(info,L"    %ls",Tr(r.stab==2 ? Tx::stabLag : Tx::stab));   // the gun stabilizer holds it (LAG: the hull outruns its drive)
    wchar_t fuel[32];
    FuelText(fuel,_countof(fuel),r.fuel);   // a bike's tank (the stock gauge's FUEL panel gone: stockgauge.cpp)
    if(fuel[0])Append(info,L"    %ls",fuel);
    warn.scale=kTitleScale;title.scale=info.scale=kLineScale;title.rgba=info.rgba=kHud;
    for(int i=0;i<arms;++i)ArmLine(arm[i],r.arm[i],i==r.selected);
    if(drill) {
        const DrillCue& c=*x.drill;
        const float share=Unit(c.rpm/c.maxRpm);
        wchar_t state[32]=L"";
        if(const wchar_t* const word=DrillState(c))std::swprintf(state,32,L"  %ls",word);
        Format(*drill,Tr(Tx::drillLineShort),static_cast<int>(std::lround(c.rpm)),static_cast<int>(std::lround(Unit(c.heat)*100.0f)),state);
        DrillLaunchHint(*drill,c);
        drill->scale=kLineScale*0.85f;
        drill->rgba=c.overheated ? kRed : c.heat>=0.7f ? HeatColour(Unit(c.heat),false) : share>=0.99f ? kGreen : share>0.0f ? kAmber : kCyan;
    }
    Line* const head[]={&warn,&title,&info};
    for(Line* l:head){l->w=l->h=0.0f;if(text)MeasureAll(*text,l,1);}
    for(int i=0;i<arms;++i){arm[i].w=arm[i].h=0.0f;if(text)MeasureAll(*text,&arm[i],1);}
    if(drill){drill->w=drill->h=0.0f;if(text)MeasureAll(*text,drill,1);}
    float emcBar=0.0f;
    const float* emcColour=kHud;
    if(emcLine) {
        EmcLine(*emcLine,*emc,&emcBar,&emcColour);
        emcLine->w=emcLine->h=0.0f;
        if(text)MeasureAll(*text,emcLine,1);
    }
    for(int i=0;i<prots;++i){pl[i].line->w=pl[i].line->h=0.0f;if(text)MeasureAll(*text,pl[i].line,1);}
    if(armor)DurabilityLine(*armor,Tx::armorRow,x.armor->armor,text);
    const float lineH=18.0f*s,gap=3.0f*s,barW=kDurabilityBarW*s,barH=kDurabilityBarH*s;
    float h=(title.h>0.0f ? title.h : lineH)+gap+(info.h>0.0f ? info.h : lineH)+gap+barH+gap*2.0f;
    if(armor)h+=(armor->h>0.0f ? armor->h : lineH)+gap+barH+gap*2.0f;
    for(int i=0;i<arms;++i)h+=(arm[i].h>0.0f ? arm[i].h : lineH)+gap;
    if(drill)h+=(drill->h>0.0f ? drill->h : lineH)+gap;
    if(emcLine)h+=(emcLine->h>0.0f ? emcLine->h : lineH)+gap+barH+gap;
    for(int i=0;i<prots;++i)h+=(pl[i].line->h>0.0f ? pl[i].line->h : lineH)+gap+(pl[i].bar ? barH+gap : 0.0f);
    const float cx=compact ? (kIndicatorR+24.0f)*s : width*0.5f-460.0f*s,tx=cx+kIndicatorR*s+18.0f*s;
    float y=(bottom>=0.0f ? bottom : height*0.80f)-h;
    float hull=sight::HeadingOf(r.hull),gun=r.aimOk ? sight::HeadingOf(r.aim) : -1.0f;
    const float look=r.lookOk ? sight::HeadingOf(r.look) : -1.0f;
    float stops[2]={-1.0f,-1.0f};
    if(nix) {
        hull=HeadingOfYaw(x.nix->legsYaw);gun=sight::HeadingOf(x.nix->dir);
        stops[0]=HeadingOfYaw(x.nix->legsYaw+x.nix->twistMin);stops[1]=HeadingOfYaw(x.nix->legsYaw+x.nix->twistMax);
    }
    const float up=look>=0.0f ? look : gun>=0.0f ? gun : hull;
    HullTurret(drawer,ctx,cx,y+kIndicatorR*s+4.0f*s,s,up<0.0f ? 0.0f : up,hull,gun,nix ? stops : nullptr);
    if(prot && prot->shieldUp) {   // the standing shield round the hull's nose, on the ring
        const float half=prot->shieldHalfArc,a=HdgDiff(hull,up<0.0f ? 0.0f : up)*kDeg-1.5707963f;
        Arc(drawer,ctx,cx,y+kIndicatorR*s+4.0f*s,(kIndicatorR+6.0f)*s,a-half,2.0f*half,3.0f*s,12,prot->overheated ? kRed : kCyan);
    }
    const float x0=tx;
    warn.x=x0;warn.y=y-(warn.h>0.0f ? warn.h : 24.0f*s)-4.0f*s;
    title.x=x0;title.y=y;y+=(title.h>0.0f ? title.h : lineH)+gap;
    info.x=x0;info.y=y;y+=(info.h>0.0f ? info.h : lineH)+gap;
    Bar(drawer,ctx,x0,y,barW,barH,hp,TrailOf(&kStockHpKey,hp,GetTickCount64()),HpColour(hp),s);
    y+=barH+gap*2.0f;
    if(armor) {
        armor->x=x0;armor->y=y;y+=(armor->h>0.0f ? armor->h : lineH)+gap;
        Bar(drawer,ctx,x0,y,barW,barH,x.armor->armor.share,x.armor->armor.share,DurabilityFill(x.armor->armor,true),s);
        y+=barH+gap*2.0f;
    }
    for(int i=0;i<arms;++i){arm[i].x=x0;arm[i].y=y;y+=(arm[i].h>0.0f ? arm[i].h : lineH)+gap;}
    if(drill){drill->x=x0;drill->y=y;y+=(drill->h>0.0f ? drill->h : lineH)+gap;}
    if(emcLine) {
        emcLine->x=x0;emcLine->y=y;y+=(emcLine->h>0.0f ? emcLine->h : lineH)+gap;
        Bar(drawer,ctx,x0,y,barW,barH,emcBar,emcBar,emcColour,s);
        y+=barH+gap;   // the Proteus's lines (a vehicle has one or the other) under it
    }
    for(int i=0;i<prots;++i) {
        pl[i].line->x=x0;pl[i].line->y=y;
        y+=(pl[i].line->h>0.0f ? pl[i].line->h : lineH)+gap;
        if(!pl[i].bar)continue;
        const float trail=pl[i].trailKey ? TrailOf(pl[i].trailKey,pl[i].share,GetTickCount64()) : pl[i].share;
        Bar(drawer,ctx,x0,y,barW,barH,pl[i].share,trail,pl[i].fill,s);
        y+=barH+gap;
    }
}

// The stock vehicle HUD, part by part (see above). A stock heli's are elsewhere (HeliHud, HeliGunSight, StockCells).
void StockVehicleHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,
                     const StockExtras& x,Line* lines,int* at,const LoadoutDock* dock=nullptr) noexcept {
    StockMarks(drawer,ctx,text,vp,width,height,s,r,x,lines,at);
    if(x.proteus && vec::Dist(x.proteus->pos,r.pos)<2.0f)ProteusMarks(drawer,ctx,text,vp,width,height,s,*x.proteus,lines,at);
    const bool nix=x.nix && vec::Dist(x.nix->at,r.pos)<2.0f;
    const float hull=nix ? HeadingOfYaw(x.nix->legsYaw) : sight::HeadingOf(r.hull);
    const float gun=nix ? sight::HeadingOf(x.nix->dir) : r.aimOk ? sight::HeadingOf(r.aim) : -1.0f;
    if(gun>=0.0f)StockTape(drawer,ctx,text,width,height,s,gun,hull,lines,at);
    else if(hull>=0.0f)StockTape(drawer,ctx,text,width,height,s,hull,-1.0f,lines,at);
    if(dock) {
        HudRegionText region(text,s,dock->scale,lines,at);
        StockBlock(drawer,ctx,region.text,width,height,dock->scale,r,x,lines,at,dock->top-12.0f*dock->scale,true);
    } else StockBlock(drawer,ctx,text,width,height,s,r,x,lines,at);
    if(Cfg().playerJetThreatHud && r.threats>0) {
        PlayerJetSymbols y{};
        std::memcpy(y.pos,r.pos,12);
        y.threats=r.threats<kMostThreats ? r.threats : kMostThreats;
        for(int i=0;i<y.threats && i<kStockThreats;++i){std::memcpy(y.threatAt[i],r.threatAt[i],12);y.threatKind[i]=r.threatKind[i];}
        std::memcpy(y.nose,r.lookOk ? r.look : r.hull,12);   // the scope's up: where the player looks (else the hull)
        // The warnings' RWR scope and the marks (no launch cue: ours); the scope right of the centre, clear of the block.
        const float fitted=std::fmin(dock ? dock->scale : s,width/(2.0f*(kBoxOff+kBoxW*0.5f+60.0f+2.0f*kRwrR+24.0f)));
        HudRegionText region(text,s,fitted,lines,at);
        RwrScope(drawer,ctx,region.text,width,height,fitted,y,0,GetTickCount64(),1.0f,lines,at,
                 dock ? dock->top-12.0f*fitted : -1.0f);
        ThreatMarks(drawer,ctx,vp,width,height,s,y);
    }
}

// A stock vehicle's (a heli's too) weapons in the seat's order and with its pick (hud_cue.h StockSeatArms: the
// fire-control's selected arm, else the sight's, else the first), as the cockpit's weapons screen lists them.
hudcue::SeatArms StockSeatArmsOf(const StockHudReadout& r) noexcept {
    const int arms=r.arms<kStockArms ? r.arms : kStockArms;
    float side[kStockArms];
    for(int i=0;i<arms;++i)side[i]=r.arm[i].side;
    return hudcue::StockSeatArms(side,arms,r.selected,r.sight);
}

// A stock vehicle's stores for the loadout strips (HeliStrip, StockDockHud): each weapon's picture by its round
// (hud_cue.h ArmIconOf), its rounds and its reload (ArmLine's text and colour); the picked one cyan. In StockSeatArmsOf's
// order and pick; `picked` (if given): the picked one's cell, or -1.
int StockCells(const StockHudReadout& r,LoadCell* cells,int* picked) noexcept {
    const hudcue::SeatArms a=StockSeatArmsOf(r);
    if(picked)*picked=a.picked;
    int n=0;
    for(int k=0;k<a.n;++k) {
        const int i=a.rows[k].index;
        Line l{};
        ArmLine(l,r.arm[i],false);
        LoadCell& c=cells[n++];
        const reticle::Style sight=r.arm[i].reticle;
        c.icon=hudcue::ArmIconOf(static_cast<int>(r.arm[i].kind),r.arm[i].lobbed,static_cast<int>(r.arm[i].style),
                                 sight==reticle::Style::cannon || sight==reticle::Style::precision);
        c.picked=k==a.picked || r.arm[i].ownPick;   // the primary's round and the own secondary's missile, both lit
        c.load=hudcue::CellLoad(r.arm[i].ammo,r.arm[i].reload,r.arm[i].cycleSec,r.arm[i].cycle);
        if(r.arm[i].name[0]) {
            if(r.arm[i].ammo<=0 && r.arm[i].canReload)_snwprintf_s(c.text,_countof(c.text),_TRUNCATE,L"%ls  %ls",r.arm[i].name,l.text);
            else if(r.arm[i].cycleSec>=0.0f)   // loading the next round (vhud.cpp Cycle): its seconds, amber
                _snwprintf_s(c.text,_countof(c.text),_TRUNCATE,L"%ls %d/%d  %ls %.1fs",r.arm[i].name,r.arm[i].ammo,r.arm[i].ammoMax,
                             Tr(Tx::armReloadWord),r.arm[i].cycleSec);
            else _snwprintf_s(c.text,_countof(c.text),_TRUNCATE,L"%ls %d/%d",r.arm[i].name,r.arm[i].ammo,r.arm[i].ammoMax);
        }
        else wcsncpy_s(c.text,_countof(c.text),l.text,_TRUNCATE);
        c.rgba=c.picked && l.rgba==kHud ? kCyan : l.rgba;
    }
    return n;
}

// The weapons of the player's seat as every read-out lists them (hud_cue.h SeatArms): a plugin aircraft's, else a stock
// vehicle's (a heli's too); none.
hudcue::SeatArms SeatArmList(const Snapshot& snap) noexcept {
    if(snap.cockpit)return hudcue::JetSeatArms(snap.jet.stores<kMostStores ? snap.jet.stores : kMostStores,snap.jet.store,snap.jet.guns>0);
    if(snap.stock)return StockSeatArmsOf(snap.stockHud);
    return hudcue::SeatArms{};
}
// The picked weapon as one number (a switch's banner watches it): its arm or store, the gun kGunPick, none -1.
constexpr int kGunPick=1000;
int SeatArmPick(const hudcue::SeatArms& a) noexcept {
    if(a.picked<0 || a.picked>=a.n)return -1;
    return a.rows[a.picked].source==hudcue::ArmSource::gun ? kGunPick : a.rows[a.picked].index;
}

void StockDockHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                  const StockHudReadout& r,const StockExtras& x,const PayloadReadout* payload,bool switched,Line* lines,int* at) noexcept {
    LoadCell cells[kStockArms];
    int pickedCell=-1;
    const int n=StockCells(r,cells,&pickedCell);
    Line controls{};
    if(payload && PayloadControlsShown(*payload))PayloadControls(controls,*payload);
    else if(payload)AircraftControls(controls,payload->keys,payload->choices,payload->switchButton,0,false,false);
    const LoadoutDock dock=LoadoutDockOf(text,width,height,s,cells,n,controls.text[0] ? 1 : 0);
    StockVehicleHud(drawer,ctx,text,vp,width,height,s,r,x,lines,at,&dock);
    HudRegionText region(text,s,dock.scale,lines,at);
    LoadoutStrip(drawer,ctx,region.text,width,dock.y,dock.scale,cells,n,lines,at);
    if(controls.text[0] && LineSink{lines,at}.Room(1,__func__)) {
        Line& keys=lines[(*at)++];keys=controls;
        ControlRow(region.text,width,dock.footer,dock.scale,keys);
    }
    if(switched && pickedCell>=0 && pickedCell<n)
        LoadoutBanner(drawer,ctx,region.text,width,dock.bannerBottom,dock.scale,cells[pickedCell],lines,at);
}
}  // namespace hudi
}  // namespace crew
