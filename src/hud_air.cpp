// The vehicle HUD's aircraft parts (split from hud.cpp; the HUD's overview is hud.cpp's head): the jet cockpit's
// panel and stores, the gunship gunner's frame, the fighter HUD (pitch ladder, heading tape, boxes, sights, the RWR,
// the warnings), the reticles' rendering, and the helicopter's HUD and panel.
#include "hud_internal.h"

namespace crew {
namespace hudi {
void AircraftControls(Line& line,bool keys,int choices,int storeButton,int targetButton,bool target,bool flares) noexcept {
    Format(line,L"");
    auto add=[&](Tx text,const wchar_t* key){if(line.text[0])Append(line,L"   ");Append(line,Tr(text),key);};
    wchar_t key[32];
    if(choices>1) {
        if(keys)KeyName(Cfg().playerJetSwitchKey,key,32);else SeatButtonName(storeButton,key,32);
        add(Tx::controlStores,key);
    }
    if(target) {
        if(keys)KeyName(Cfg().playerJetTargetKey,key,32);else SeatButtonName(targetButton,key,32);
        add(Tx::controlTarget,key);
    }
    if(flares) {
        KeyName(Cfg().playerJetFlareKey,key,32);
        add(keys ? Tx::controlFlares : Tx::controlFlaresKeyboard,key); // the flight path currently has no pad flare binding
    }
}
void JetControls(Line& line,const PlayerJetReadout& j) noexcept {
    bool guided=false;
    for(int i=0;i<j.stores && i<kMostStores;++i)guided=guided || j.storeRole[i]==static_cast<int>(StoreRole::air) ||
                                                        j.storeRole[i]==static_cast<int>(StoreRole::ground);
    AircraftControls(line,j.keys,j.stores,j.storeButton,j.targetButton,guided,j.stores>0 && Cfg().playerJetFlares>0);
}

// The gunship's gun with the player at it (playerjet_crew.inc, README 炮舰机): the cross where a round of the picked gun
// fired now lands (where the screen's centre meets the ground), red out of its reach; under it the gun (SHELLS, CANNON
// or GATLING in brackets, the others there named after it, in the switch's order), the range and READY or the gun's
// wait; a cyan square
// on the pylon turn's centre (the point last fired at). No ground under the centre: the line alone.
// The gunship gunner's sight (the user, 2026-10-08: "the gunship too"), as an AC-130's sensor view frames its guns:
// round the screen's centre (the gunner's line of sight: the camera aims the guns) four corner brackets and a
// crosshair broken at the middle (its lower arm under the gun line), and round the middle a mark for the gun picked, each its own shape so a glance tells
// which is up: the shells' (105 mm) a square, the cannon's (40 mm) a diamond, the gatling's (25 mm) a ring. Drawn dim
// under GunnerMarks' impact cross (the round's point, which the frame is not).
alignas(16) const float kFrameTint[4]={0.85f,0.95f,0.85f,0.55f};   // a sensor view's symbols: pale, under the HUD's marks
constexpr float kFrameW=200.0f,kFrameH=130.0f,kFrameLeg=44.0f,kHairGap=18.0f,kHairOut=110.0f;   // px at 1080 lines
// px under the centre the lower hair starts at: GunnerMarks' gun line is under the centre (22 px), clear of it
constexpr float kHairUnder=44.0f,kGunMark=15.0f;
void GunshipFrame(void* drawer,void* ctx,float width,float height,float s,GunnerGun gun) noexcept {
    const float cx=width*0.5f,cy=height*0.5f,t=2.0f*s,w=kFrameW*s,h=kFrameH*s,leg=kFrameLeg*s;
    for(int sxi=-1;sxi<=1;sxi+=2)
        for(int syi=-1;syi<=1;syi+=2) {
            const float x=cx+sxi*w,y=cy+syi*h;
            Seg(drawer,ctx,x,y,x-sxi*leg,y,t,kFrameTint);Seg(drawer,ctx,x,y,x,y-syi*leg,t,kFrameTint);
        }
    const float gap=kHairGap*s,out=kHairOut*s,thin=1.5f*s;
    Seg(drawer,ctx,cx-out,cy,cx-gap,cy,thin,kFrameTint);Seg(drawer,ctx,cx+gap,cy,cx+out,cy,thin,kFrameTint);
    Seg(drawer,ctx,cx,cy-out,cx,cy-gap,thin,kFrameTint);Seg(drawer,ctx,cx,cy+kHairUnder*s,cx,cy+out,thin,kFrameTint);
    const float m=kGunMark*s;   // round the impact cross (12 px): the cross stays the round's point, the mark the gun's
    switch(gun) {
        case GunnerGun::shells:
            Seg(drawer,ctx,cx-m,cy-m,cx+m,cy-m,t,kFrameTint);Seg(drawer,ctx,cx+m,cy-m,cx+m,cy+m,t,kFrameTint);
            Seg(drawer,ctx,cx+m,cy+m,cx-m,cy+m,t,kFrameTint);Seg(drawer,ctx,cx-m,cy+m,cx-m,cy-m,t,kFrameTint);
            break;
        case GunnerGun::cannon:
            Seg(drawer,ctx,cx,cy-m,cx+m,cy,t,kFrameTint);Seg(drawer,ctx,cx+m,cy,cx,cy+m,t,kFrameTint);
            Seg(drawer,ctx,cx,cy+m,cx-m,cy,t,kFrameTint);Seg(drawer,ctx,cx-m,cy,cx,cy-m,t,kFrameTint);
            break;
        default: Arc(drawer,ctx,cx,cy,m,0.0f,6.2831853f,t,16,kFrameTint);break;
    }
}

void GunnerMarks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const GunnerReadout& g,
                 Line* lines,int* at) noexcept {
    GunshipFrame(drawer,ctx,width,height,s,g.gun);
    if(g.zoom>1.0f)Label(text,lines,at,width*0.5f-kFrameW*s,height*0.5f-(kFrameH+14.0f)*s,0,kLineScale*0.85f,kFrameTint,L"%.0fx",
                         g.zoom);   // over the top left bracket
    float sx=width*0.5f,sy=height*0.5f,depth;
    if(g.centred && Project(vp,g.centre,width,height,&sx,&sy,&depth)) {
        const float r=10.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-r,sy-r,sx+r,sy-r+t,kCyan);Rect(drawer,ctx,sx-r,sy+r-t,sx+r,sy+r,kCyan);
        Rect(drawer,ctx,sx-r,sy-r,sx-r+t,sy+r,kCyan);Rect(drawer,ctx,sx+r-t,sy-r,sx+r,sy+r,kCyan);
    }
    sx=width*0.5f;sy=height*0.5f;
    if(g.ground && g.inReach)ImpactCross(drawer,ctx,vp,width,height,s,g.sight,&sx,&sy);
    else if(g.ground && Project(vp,g.sight,width,height,&sx,&sy,&depth)) {
        const float r=12.0f*s,t=2.0f*s;
        Rect(drawer,ctx,sx-r,sy-t*0.5f,sx+r,sy+t*0.5f,kRed);Rect(drawer,ctx,sx-t*0.5f,sy-r,sx+t*0.5f,sy+r,kRed);
    }
    Line* const pushed=LineSink{lines,at}.Push(__func__);
    if(!pushed)return;
    Line& line=*pushed;
    wchar_t gun[64];
    const wchar_t* const names[]={Tr(Tx::gunnerShells),Tr(Tx::gunnerCannon),Tr(Tx::gunnerGatling)};
    static_assert(sizeof(names)/sizeof(names[0])==static_cast<std::size_t>(GunnerGun::count),"GunnerGun's names");
    const int picked=static_cast<int>(g.gun),n=static_cast<int>(GunnerGun::count);
    const bool more=(g.guns&~(1u<<picked))!=0;
    int used=std::swprintf(gun,64,more ? L"[%ls]" : L"%ls",names[picked]);
    for(int k=1;k<n && used>0;++k) {   // the others there, from the one the switch goes to next
        const int i=(picked+k)%n;
        if(!(g.guns&(1u<<i)))continue;
        const int w=std::swprintf(gun+used,64-used,L" %ls",names[i]);
        if(w<0)break;   // no room: the names so far
        used+=w;
    }
    if(!g.ground)Format(line,Tr(Tx::gunnerNoGround),gun);
    else if(!g.inReach)Format(line,Tr(Tx::gunnerOutOfRange),gun,static_cast<int>(std::lround(g.range)));
    else if(g.ready)Format(line,Tr(Tx::gunnerReady),gun,static_cast<int>(std::lround(g.range)));
    else Format(line,L"%ls   %d m   %.1f s",gun,static_cast<int>(std::lround(g.range)),g.wait);
    line.scale=kLineScale;line.rgba=g.ground && g.inReach ? (g.ready ? kGreen : kYellow) : kRed;line.w=line.h=0.0f;
    if(text)MeasureAll(*text,&line,1);
    line.x=sx-line.w*0.5f;line.y=sy+22.0f*s;
    // Its controls (the user, 2026-10-14: the gatling was there behind a key the HUD never named): the switch key while
    // there is another gun to go to, the secondary fire for the gatling.
    Line* const pushedKeys=LineSink{lines,at}.Push(__func__);
    if(!pushedKeys)return;
    Line& keys=*pushedKeys;
    Format(keys,L"");
    wchar_t key[32];
    if(more) {
        if(g.keys)KeyName(Cfg().playerJetSwitchKey,key,32);else SeatButtonName(0x10,key,32);   // pad LB (playerjet_crew.inc)
        Append(keys,Tr(Tx::controlGuns),key);
    }
    if(g.guns&(1u<<static_cast<int>(GunnerGun::gatling))) {
        if(g.keys)KeyName(Cfg().gunshipGatlingKey,key,32);else SeatButtonName(0x20,key,32);   // pad RB
        if(keys.text[0])Append(keys,L"   ");
        Append(keys,Tr(Tx::controlGatlingHold),key);
    }
    if(!keys.text[0]){--*at;return;}
    keys.scale=kLineScale*0.75f;keys.rgba=kCyan;keys.w=keys.h=0.0f;
    if(text)MeasureAll(*text,&keys,1);
    keys.x=sx-keys.w*0.5f;keys.y=line.y+24.0f*s;
}
// A lock (stores.h StoreLock: `lock` 2 locked, 1 locking at `progress`, on the lock point `p`): locking, a yellow
// square closing in as it locks; locked, a red diamond on the target. False off the screen (`sx`, `sy`: its point).
bool LockAt(void* drawer,void* ctx,const float* vp,float width,float height,float s,int lock,const float* p,float progress,float* sx,
            float* sy) noexcept {
    float depth;
    if(!lock || !Project(vp,p,width,height,sx,sy,&depth))return false;
    const float t=2.0f*s;
    if(lock==1) {
        const float r=(40.0f-24.0f*progress)*s;
        const float x=*sx,y=*sy;
        Rect(drawer,ctx,x-r,y-r,x+r,y-r+t,kYellow);Rect(drawer,ctx,x-r,y+r-t,x+r,y+r,kYellow);
        Rect(drawer,ctx,x-r,y-r,x-r+t,y+r,kYellow);Rect(drawer,ctx,x+r-t,y-r,x+r,y+r,kYellow);
        return true;
    }
    const float r=14.0f*s,x=*sx,y=*sy;
    for(int k=0;k<6;++k) {   // a diamond from small squares along its four edges
        const float f=static_cast<float>(k)/6.0f,d=r*f,e=r-d;
        Rect(drawer,ctx,x+d-t,y-e-t,x+d+t,y-e+t,kRed);Rect(drawer,ctx,x-d-t,y-e-t,x-d+t,y-e+t,kRed);
        Rect(drawer,ctx,x+d-t,y+e-t,x+d+t,y+e+t,kRed);Rect(drawer,ctx,x-d-t,y+e-t,x-d+t,y+e+t,kRed);
    }
    return true;
}

// The picked store's lock (LockAt). The missile flies at what this marks (stores.cpp StoreLock reads the same entry the
// round will hold).
void LockMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetReadout& j) noexcept {
    float sx,sy;
    LockAt(drawer,ctx,vp,width,height,s,j.lock,j.lockAt,j.lockProgress,&sx,&sy);
}

// The stores: the guns' rounds (the fewest in one), each store's name and rounds (`names`; the picked one in brackets),
// and the flares in the air. Without `names` the stores are the loadout strip's (LoadoutStrip) and the line keeps the
// guns and the flares.
void StoresText(wchar_t* text,std::size_t size,const PlayerJetReadout& j,bool names) noexcept {
    text[0]=L'\0';
    std::size_t at=0;
    if(j.guns>0) {
        const int n=_snwprintf_s(text,size,_TRUNCATE,Tr(Tx::storesGun),j.gunRounds>0 ? j.gunRounds : 0);
        if(n>0)at=static_cast<std::size_t>(n);
    }
    for(int i=0;names && i<j.stores && i<kMostStores;++i) {
        const int n=_snwprintf_s(text+at,size-at,_TRUNCATE,i==j.store ? L"[%hs %d]  " : L"%hs %d  ",
                                 j.storeName[i] ? j.storeName[i] : "?",j.storeRounds[i]);
        if(n<0)break;
        at+=static_cast<std::size_t>(n);
    }
    if(j.air)_snwprintf_s(text+at,size-at,_TRUNCATE,Tr(Tx::storesFlare),j.flares);
}
void StoresLine(Line& l,const PlayerJetReadout& j) noexcept {
    wchar_t text[128];
    StoresText(text,_countof(text),j);
    Format(l,L"%ls",text);
}

// The fuel tank's readout (empty with none): FUEL and the share left, and the time left at its burn under 100 minutes (a
// jet's 506 body idles its rotor: a full tank lasts some 21 hours, no time worth showing). It replaces the stock gauge's
// FUEL panel (stockgauge.cpp).
void FuelText(wchar_t* out,std::size_t size,const FuelReading& f) noexcept {
    out[0]=L'\0';
    if(!f.ok)return;
    const int share=static_cast<int>(std::lround(f.share*100.0f));
    if(f.sec>=0.0f && f.sec<5999.5f) {
        const int sec=static_cast<int>(std::lround(f.sec));
        _snwprintf_s(out,size,_TRUNCATE,Tr(Tx::fuelTime),share,sec/60,sec%60);
    } else _snwprintf_s(out,size,_TRUNCATE,Tr(Tx::fuelShare),share);
}

// The takeoff roll's cue (empty: none; `rotate`: the cue to pull up is showing). On the takeoff roll (not in the air:
// j.ground only says there is ground under it, so the cue stayed on after takeoff, 2026-10-05): the speed it may lift
// off from coming up, then the cue to pull up (the user, 2026-10-05).
void TakeoffCue(const PlayerJetReadout& j,wchar_t* cue,std::size_t size,bool* rotate) noexcept {
    const int rotateKmh=static_cast<int>(std::lround(j.rotate*3.6f));
    const bool rolling=!j.air && j.rotate>0.0f && j.speed>1.0f;
    *rotate=rolling && j.speed>=j.rotate;
    cue[0]=L'\0';
    if(*rotate)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::rotateNow));
    else if(rolling && j.speed>=j.rotate*0.7f)_snwprintf_s(cue,size,_TRUNCATE,Tr(Tx::rotateAt),rotateKmh);
}
// The old panel's cue (empty: none): the ground-proximity warning, a missile, a lock, else the takeoff's (TakeoffCue).
void CockpitCue(const PlayerJetReadout& j,wchar_t* cue,std::size_t size,bool* rotate) noexcept {
    TakeoffCue(j,cue,size,rotate);
    if(j.pullUp)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::pullUpTerrain));
    else if(j.threat==2)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::missileBang));
    else if(j.threat==1)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::locked));
    else if(j.area==2)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::areaTurning));
    else if(j.area==1)_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::areaAhead));
    else if(!cue[0] && FuelLow(j.fuel))_snwprintf_s(cue,size,_TRUNCATE,L"%ls",Tr(Tx::lowFuel));
}
// The cue's colour (over `calm` without one): the ground and a missile blink red and white (8 Hz), a lock is yellow,
// a stall red, the area's edge amber (blinking while a wall turns it back), the pull-up cue blinks green (4 Hz).
const float* CueColour(const PlayerJetReadout& j,bool rotate,const float* calm) noexcept {
    const bool blink=(GetTickCount64()/125)%2==0;
    return j.pullUp || j.threat==2 ? (blink ? kRed : kWhite) : j.threat==1 ? kYellow : j.stall ? kRed :
           j.area==2 ? (blink ? kAmber : kWhite) : j.area==1 ? kAmber : rotate && blink ? kGreen : rotate ? kYellow : calm;
}

// The cockpit readout of the jet the player flies (drawn whatever VehicleHud says): at the bottom centre, its
// speed, height over the floor (over the world's zero, ALT*, with no ground under it) and climb, the throttle lever
// as a bar with the g it pulls (and STALL, red, when its wing cannot hold its path), its HP, and the controls (the
// pad's or the keyboard and mouse's, as the seat says it is flown).
void Cockpit(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,Line* lines,
             int* at) noexcept {
    if(!LineSink{lines,at}.Room(5,__func__))return;
    Line& info=lines[(*at)++];
    Line& thr=lines[(*at)++];
    Line& arms=lines[(*at)++];
    Line& keys=lines[(*at)++];
    Line& keys2=lines[(*at)++];
    const float alt=std::fmax(-9999.0f,std::fmin(j.clear,99999.0f));
    Format(info,Tr(Tx::jetInfo),static_cast<int>(std::lround(j.speed*3.6f)),
           j.ground ? L"" : L"*",static_cast<int>(std::lround(alt)),Tr(j.climb>=0.0f ? Tx::climbUp : Tx::climbDown),
           static_cast<int>(std::lround(std::fabs(j.climb))),static_cast<int>(std::lround(j.hpMax>0.0f ? 100.0f*j.hp/j.hpMax : 0.0f)));
    const int rotateKmh=static_cast<int>(std::lround(j.rotate*3.6f));
    wchar_t cue[64];
    bool rotate=false;
    CockpitCue(j,cue,_countof(cue),&rotate);
    wchar_t fuel[32];
    FuelText(fuel,_countof(fuel),j.fuel);
    wchar_t stall[24]=L"";
    if(j.stall)std::swprintf(stall,24,L"    %ls",Tr(Tx::stall));
    Format(thr,Tr(Tx::jetThrottle),static_cast<int>(std::lround(j.throttle*100.0f)),fuel[0] ? L"    " : L"",fuel,j.load,
           stall,cue[0] ? L"    " : L"",cue);
    StoresLine(arms,j);
    if(j.keys) {
        // War Thunder's keys (playerjet.cpp ReadStick): W / S the throttle lever, SPACE / the brake key the pitch.
        wchar_t brake[32],swap[32];
        KeyName(Cfg().playerJetBrakeKey,brake,32);KeyName(Cfg().playerJetSwitchKey,swap,32);
        if(j.air) {
            Format(keys,Tr(Cfg().playerJetMouseFlight ? Tx::jetKeysMouseHand : Tx::jetKeysHand),brake);
            wchar_t target[32];KeyName(Cfg().playerJetTargetKey,target,32);
            wchar_t flare[32];KeyName(Cfg().playerJetFlareKey,flare,32);
            Format(keys2,Tr(Tx::jetKeysAir),swap,target,flare);
        } else {
            Format(keys,L"%ls",Tr(Tx::jetKeysGround));
            Format(keys2,Tr(Tx::jetKeysTakeoff),rotateKmh);
        }
    } else if(j.air) {
        Format(keys,L"%ls",Tr(Tx::jetPadAir));
        wchar_t swap[32],target[32],flare[32];
        SeatButtonName(j.storeButton,swap,32);SeatButtonName(j.targetButton,target,32);KeyName(Cfg().playerJetFlareKey,flare,32);
        Format(keys2,Tr(Tx::jetPadAir2),swap,target,flare);
    } else {
        Format(keys,L"%ls",Tr(Tx::jetPadGround));
        Format(keys2,Tr(Tx::jetPadTakeoff),rotateKmh);
    }
    info.scale=kTitleScale;info.rgba=kWhite;
    thr.scale=kLineScale;
    thr.rgba=CueColour(j,rotate,kCyan);
    arms.scale=kLineScale;arms.rgba=j.bomb ? kYellow : kWhite;
    keys.scale=keys2.scale=kLineScale*0.85f;keys.rgba=keys2.rgba=kWhite;
    info.w=info.h=thr.w=thr.h=arms.w=arms.h=keys.w=keys.h=keys2.w=keys2.h=0.0f;
    if(text){MeasureAll(*text,&info,1);MeasureAll(*text,&thr,1);MeasureAll(*text,&arms,1);MeasureAll(*text,&keys,1);MeasureAll(*text,&keys2,1);}
    const float pad=8.0f*s,gap=5.0f*s,barW=320.0f*s,barH=10.0f*s;
    float w=barW;
    const Line* const parts[]={&info,&thr,&arms,&keys,&keys2};
    for(const Line* l:parts)w=l->w>w ? l->w : w;
    w+=2.0f*pad;
    const float lineH=info.h>0.0f ? info.h : 24.0f*s,smallH=thr.h>0.0f ? thr.h : 18.0f*s,keysH=keys.h>0.0f ? keys.h : 16.0f*s;
    const float h=pad+lineH+gap+smallH+gap*0.5f+barH+gap+smallH+gap+keysH+gap*0.5f+keysH+pad;
    const float x0=(width-w)*0.5f,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kCyan);
    float y=y0+pad;
    info.x=(width-info.w)*0.5f;info.y=y;y+=lineH+gap;
    thr.x=(width-barW)*0.5f;thr.y=y;y+=smallH+gap*0.5f;
    Bar(drawer,ctx,(width-barW)*0.5f,y,barW,barH,j.throttle,j.throttle,kCyan,s);
    y+=barH+gap;
    arms.x=(width-arms.w)*0.5f;arms.y=y;y+=smallH+gap;
    keys.x=(width-keys.w)*0.5f;keys.y=y;y+=keysH+gap*0.5f;
    keys2.x=(width-keys2.w)*0.5f;keys2.y=y;
}
// The world directions `a` and `b` joined on the screen (both in front of the eye); `dashes` > 0: that many dashes.
void DirSeg(void* drawer,void* ctx,const float* vp,float width,float height,const float* a,const float* b,float t,int dashes,
            const float* rgba) noexcept {
    float x0,y0,x1,y1;
    if(!sight::ToScreen(vp,a,0.0f,width,height,&x0,&y0) || !sight::ToScreen(vp,b,0.0f,width,height,&x1,&y1))return;
    // Pitch geometry owns the central flight window. Clip entire segments, not
    // just their degree labels, clear of the instruments and lower dock.
    const float scale=t*0.5f,half=(kBoxOff-kBoxW*0.5f-18.0f)*scale;
    const float dx=x1-x0,dy=y1-y0;
    float enter=0.0f,leave=1.0f;
    auto clip=[&](float p,float q) {
        if(std::fabs(p)<1e-6f)return q>=0.0f;
        const float r=q/p;
        if(p<0.0f){if(r>leave)return false;enter=std::fmax(enter,r);}
        else{if(r<enter)return false;leave=std::fmin(leave,r);}
        return true;
    };
    if(!clip(-dx,x0-(width*0.5f-half)) || !clip(dx,width*0.5f+half-x0) ||
       !clip(-dy,y0) || !clip(dy,height*kFlightRegionBottom-y0))return;
    x1=x0+dx*leave;y1=y0+dy*leave;x0+=dx*enter;y0+=dy*enter;
    if(dashes<=0){Seg(drawer,ctx,x0,y0,x1,y1,t,rgba);return;}
    const int pieces=2*dashes-1;
    for(int i=0;i<pieces;i+=2) {
        const float f0=static_cast<float>(i)/static_cast<float>(pieces),f1=static_cast<float>(i+1)/static_cast<float>(pieces);
        Seg(drawer,ctx,x0+(x1-x0)*f0,y0+(y1-y0)*f0,x0+(x1-x0)*f1,y0+(y1-y0)*f1,t,rgba);
    }
}

// The plugin aircraft's stores as cells: each one's picture (hud_cue.h StoreIconOf), its name and rounds; out of rounds
// dim; the picked one cyan. In the seat's order and with its pick (hud_cue.h JetSeatArms); the gun has its own line
// (StoresText), not a cell, so a cell's index is its store's.
int JetCells(const PlayerJetReadout& j,LoadCell* cells) noexcept {
    const hudcue::SeatArms a=hudcue::JetSeatArms(j.stores<kMostStores ? j.stores : kMostStores,j.store,j.guns>0);
    int n=0;
    for(int k=0;k<a.n;++k) {
        if(a.rows[k].source!=hudcue::ArmSource::store)continue;
        const int i=a.rows[k].index;
        LoadCell& c=cells[n++];
        c.icon=hudcue::StoreIconOf(j.storeName[i],j.storeRole[i]);
        c.picked=k==a.picked;
        _snwprintf_s(c.text,_countof(c.text),_TRUNCATE,L"%hs %d",j.storeName[i] ? j.storeName[i] : "?",j.storeRounds[i]);
        c.rgba=j.storeRounds[i]<=0 ? kHudDim : c.picked ? kCyan : kHud;
    }
    return n;
}

// The level heading the ladder and the tape go by: the flight path's, else (hovering, straight up or down) the nose's.
bool LevelHeading(const PlayerJetSymbols& y,float* h) noexcept {
    const float* ref=y.moving ? y.dir : y.nose;
    h[0]=ref[0];h[1]=0.0f;h[2]=ref[2];
    if(vec::Normalize(h))return true;
    h[0]=y.nose[0];h[2]=y.nose[2];
    return vec::Normalize(h);
}

// The horizon and the pitch ladder: a rung every kLadderStep degrees within kLadderShown of the path's climb, its two
// halves from kLadderGap to kLadderHalf off the path's heading (tangents), a tab at each outer end pointing to the
// horizon, the dives' rungs dashed, the climb's degrees at their ends; the horizon out to kHorizonHalf (60 degrees).
constexpr int kLadderStep=10;
constexpr float kLadderShown=25.0f,kLadderGap=0.035f,kLadderHalf=0.13f,kHorizonHalf=1.7f,kLadderTab=0.012f;
void Ladder(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,
            Line* lines,int* at) noexcept {
    float h[3];
    if(!LevelHeading(y,h))return;
    const float r[3]={-h[2],0.0f,h[0]};   // its level right (playerjet.cpp RightOf)
    const float* ref=y.moving ? y.dir : y.nose;
    const float climb=std::asin(vec::Clamp(ref[1],-1.0f,1.0f))/kDeg,t=2.0f*s;
    for(int e=-90+kLadderStep;e<90;e+=kLadderStep) {
        if(std::fabs(static_cast<float>(e)-climb)>kLadderShown)continue;
        const float co=std::cos(static_cast<float>(e)*kDeg),si=std::sin(static_cast<float>(e)*kDeg);
        const float c[3]={h[0]*co,si,h[2]*co};
        const float inner=e==0 ? 2.0f*kLadderGap : kLadderGap,outer=e==0 ? kHorizonHalf : kLadderHalf;
        for(int side=-1;side<=1;side+=2) {
            const float k=static_cast<float>(side);
            const float a[3]={c[0]+r[0]*inner*k,c[1],c[2]+r[2]*inner*k},b[3]={c[0]+r[0]*outer*k,c[1],c[2]+r[2]*outer*k};
            DirSeg(drawer,ctx,vp,width,height,a,b,t,e<0 ? 3 : 0,kHud);
            if(e==0)continue;
            const float tab[3]={b[0],b[1]-(e>0 ? kLadderTab : -kLadderTab),b[2]};
            DirSeg(drawer,ctx,vp,width,height,b,tab,t,0,kHud);
            float lx,ly;
            // The lower fixed readouts and control rows own this space; projected pitch labels must not cover them.
            if(sight::ToScreen(vp,b,0.0f,width,height,&lx,&ly) && lx>0.0f && lx<width && ly>0.0f && ly<height*kFlightRegionBottom)
                Label(text,lines,at,lx+k*8.0f*s,ly,side>0 ? 0 : 2,kLineScale*0.85f,kHud,L"%d",e);
        }
    }
}

// The flight path marker (velocity vector): a circle with wings and a fin where the aircraft goes; none hovering.
void FlightPath(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept {
    float x,yy;
    if(!y.moving || !sight::ToScreen(vp,y.dir,0.0f,width,height,&x,&yy))return;
    const float r=8.0f*s,t=2.0f*s;
    Arc(drawer,ctx,x,yy,r,0.0f,kTurn,t,12,kHud);
    Seg(drawer,ctx,x-r-12.0f*s,yy,x-r,yy,t,kHud);Seg(drawer,ctx,x+r,yy,x+r+12.0f*s,yy,t,kHud);
    Seg(drawer,ctx,x,yy-r,x,yy-r-8.0f*s,t,kHud);
}
void HeadingTape(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetSymbols& y,Line* lines,
                 int* at) noexcept {
    float h[3];
    if(!LevelHeading(y,h))return;
    const float hdg=sight::HeadingOf(h);
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
}

// The boxes left and right of the middle: the speed (km/h) with `under` under it (the jets' g, the helis' speed set), the
// height over the floor (ALT*: over the world's zero, nothing under it) with the climb under it. The landing gear's
// indicator (branch feat/jet-gear) has the row under the left one: (width / 2 - kBoxOff * s, height / 2 + 2 * kBoxRow * s).
constexpr float kBarHalf=100.0f;   // px: the half length of the bars beside the boxes (a heli's height, a jet's lift)
void Boxes(void* drawer,void* ctx,Text* text,float width,float height,float s,float speed,const wchar_t* under,const float* underRgba,
           float clear,bool ground,float climb,Line* lines,int* at,bool heli=false) noexcept {
    const float cy=height*0.5f,w=kBoxW*s*0.5f,hh=kBoxH*s*0.5f,t=2.0f*s;
    const float left=width*0.5f-kBoxOff*s,right=width*0.5f+kBoxOff*s;
    const float boxes[2]={left,right};
    for(const float x:boxes) {
        Seg(drawer,ctx,x-w,cy-hh,x+w,cy-hh,t,kHud);Seg(drawer,ctx,x-w,cy+hh,x+w,cy+hh,t,kHud);
        Seg(drawer,ctx,x-w,cy-hh,x-w,cy+hh,t,kHud);Seg(drawer,ctx,x+w,cy-hh,x+w,cy+hh,t,kHud);
    }
    const float alt=std::fmax(-9999.0f,std::fmin(clear,99999.0f));
    Label(text,lines,at,left,cy,1,kTitleScale,kHud,L"%d",static_cast<int>(std::lround(speed*3.6f)));
    Label(text,lines,at,left,cy-hh-12.0f*s,1,kLineScale*0.8f,kHud,L"%ls",Tr(heli ? Tx::heliSpeedLabel : Tx::speedLabel));
    if(under && under[0])Label(text,lines,at,left,cy+kBoxRow*s,1,kLineScale,underRgba,L"%ls",under);
    Label(text,lines,at,right,cy,1,kTitleScale,kHud,L"%d",static_cast<int>(std::lround(alt)));
    Label(text,lines,at,right,cy-hh-12.0f*s,1,kLineScale*0.8f,kHud,L"%ls",Tr(ground ? (heli ? Tx::heliHeightLabel : Tx::altLabel) : Tx::altLabelNoGround));
    Label(text,lines,at,right,cy+kBoxRow*s,1,kLineScale,kHud,Tr(Tx::climbRate),static_cast<int>(std::lround(climb)));
}
void SpeedAltBoxes(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,Line* lines,
                   int* at) noexcept {
    wchar_t g[16];
    std::swprintf(g,16,L"G %.1f",j.load);
    Boxes(drawer,ctx,text,width,height,s,j.speed,g,j.load>5.0f ? kWarn : kHud,j.clear,j.ground,j.climb,lines,at);
}

// The boresight: a cross with a gap at its centre on the direction `dir` (where it vanishes: the guns' line, far).
void Boresight(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* dir) noexcept {
    const float t=2.0f*s,r=10.0f*s,g=3.0f*s;
    float x,y;
    if(!sight::ToScreen(vp,dir,0.0f,width,height,&x,&y))return;
    Seg(drawer,ctx,x-r,y,x-g,y,t,kHud);Seg(drawer,ctx,x+g,y,x+r,y,t,kHud);
    Seg(drawer,ctx,x,y-r,x,y-g,t,kHud);Seg(drawer,ctx,x,y+g,x,y+r,t,kHud);
}
// The pipper: a circle of radius kPipper with a dot in it at the point `at`, its screen point in (x, y); false off
// the screen.
constexpr float kPipper=18.0f;
bool Pipper(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at,const float* rgba,float* x,
            float* y) noexcept {
    if(!sight::ToScreen(vp,at,1.0f,width,height,x,y))return false;
    Arc(drawer,ctx,*x,*y,kPipper*s,0.0f,kTurn,2.0f*s,16,rgba);
    Rect(drawer,ctx,*x-1.5f*s,*y-1.5f*s,*x+1.5f*s,*y+1.5f*s,rgba);
    return true;
}

// The target's lead mark: a cross in a circle at `at` (where the rounds meet it, hud.cpp GunSight / StockMark).
void LeadMark(void* drawer,void* ctx,const float* vp,float width,float height,float s,const float* at,const float* rgba) noexcept {
    float x,y;
    if(!sight::ToScreen(vp,at,1.0f,width,height,&x,&y))return;
    const float r=7.0f*s,t=2.0f*s;
    Arc(drawer,ctx,x,y,r,0.0f,kTurn,t,10,rgba);
    Seg(drawer,ctx,x-r,y-r,x+r,y+r,t,rgba);Seg(drawer,ctx,x-r,y+r,x+r,y-r,t,rgba);
}
// A vehicle gun's lead as its fire control shows it (firecontrol.h): the aim point (vhud.cpp GunMarkOf: the solved way to
// fire, lead and drop both: the gun's line on it, the round meets the target); a radar's dashed line to it from the gun's
// line (the way to swing the gun); a computer's solution under it (the round's flight, the target's speed).
void FcsLead(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockArm& a,const float* rgba,
             Line* lines,int* at) noexcept {
    const fcs::Kit kit=fcs::KitOf(a.fcs);
    if(!kit.lead)return;
    LeadMark(drawer,ctx,vp,width,height,s,a.lead,rgba);
    float lx,ly,bx,by;
    if(!sight::ToScreen(vp,a.lead,1.0f,width,height,&lx,&ly))return;
    if(kit.leadLine && sight::ToScreen(vp,a.bore,0.0f,width,height,&bx,&by)) {
        const float dx=lx-bx,dy=ly-by,len=std::sqrt(dx*dx+dy*dy),dash=8.0f*s,gap=7.0f*s,clear=10.0f*s;
        for(float d=clear;len>0.0f && d+dash<=len-clear;d+=dash+gap)
            Seg(drawer,ctx,bx+dx*d/len,by+dy*d/len,bx+dx*(d+dash)/len,by+dy*(d+dash)/len,1.5f*s,rgba);
    }
    if(kit.solution && a.leadFlight>0.0f)
        Label(text,lines,at,lx,ly+20.0f*s,1,kLineScale*0.7f,rgba,Tr(Tx::fcsSolution),a.leadFlight,
              static_cast<int>(std::lround(a.targetSpeed*3.6f)));
}

// The guns' sight: a cross on the boresight (where the nose points: the guns' line), the pipper (a circle, a dot in it)
// where the rounds fired now will be at the target's range (else at the sight's own), the target's lead mark (a cross
// in a circle, dim out of the rounds' reach) and the range as an arc round the pipper (from its top, the share of the
// reach). Pipper on the lead mark: the rounds meet the target.
void GunSight(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept {
    if(!y.gun)return;
    float x,yy;
    Boresight(drawer,ctx,vp,width,height,s,y.nose);
    if(Pipper(drawer,ctx,vp,width,height,s,y.pipper,kHud,&x,&yy) && y.lead && y.gunRange>0.0f) {
        const float share=vec::Clamp(y.leadRange/y.gunRange,0.0f,1.0f);
        const int sides=static_cast<int>(std::ceil(share*24.0f));
        if(sides>0)Arc(drawer,ctx,x,yy,(kPipper+5.0f)*s,-0.25f*kTurn,share*kTurn,3.0f*s,sides,y.leadInRange ? kHud : kHudDim);
    }
    if(y.lead)LeadMark(drawer,ctx,vp,width,height,s,y.leadAt,y.leadInRange ? kHud : kHudDim);
}

// A stock helicopter's weapons' sight (helisight.cpp; ini PlayerHeliGunSight). The gun (the primary trigger's): the
// jet sight's boresight and pipper, the pipper where a round fired now first hits the ground (dim: none in its life,
// it is where the round ends), its distance in metres to its right. The other weapon (the secondary button's), each
// in its own shape so neither is taken for the gun's pipper: the missile's lock as the jets' (LockAt: a yellow square
// closing in, a red diamond locked) with MSL and its distance, or with no lock the seeker gate (reticle.h Gate) round its
// boresight with MSL and the range it locks within; the rockets' mark a hollow diamond where their path (flown as the
// game flies them: vhud.h RoundLands) meets the map with its label (RKT, GREN...) and distance (none met: a dim one on
// their boresight).
// A reticle's sketch (reticle.h) in the HUD's quads and text: green, dim green, amber.
// A magnified optic's inks (scopereticle.h): the etched glass black (it reads against the sky and the ground the way a
// real reticle does), the illuminated aiming point red, the fire-control display's LED digits orange.
alignas(16) const float kEtch[4]={0.02f,0.02f,0.02f,0.92f};
alignas(16) const float kEtchDim[4]={0.02f,0.02f,0.02f,0.5f};
const float* InkOf(reticle::Ink ink) noexcept {
    switch(ink) {
        case reticle::Ink::dim: return kHudDim;
        case reticle::Ink::hot: return kAmber;
        case reticle::Ink::etch: return kEtch;
        case reticle::Ink::etchDim: return kEtchDim;
        case reticle::Ink::lit: return kLit;
        case reticle::Ink::led: return kLed;
        case reticle::Ink::ledDim: return kLedDim;
        default: return kHud;
    }
}
void Render(void* drawer,void* ctx,Text* text,float s,const reticle::Sketch& k,Line* lines,int* at) noexcept {
    for(int i=0;i<k.strokes;++i){const auto& l=k.stroke[i];Seg(drawer,ctx,l.x0,l.y0,l.x1,l.y1,l.w,InkOf(l.ink));}
    for(int i=0;i<k.circles;++i) {
        const auto& c=k.circle[i];
        const int sides=static_cast<int>(vec::Clamp(c.r/(4.0f*s),16.0f,64.0f));
        Arc(drawer,ctx,c.x,c.y,c.r,0.0f,kTurn,c.w,sides,InkOf(c.ink));
    }
    for(int i=0;i<k.dots;++i){const auto& d=k.dot[i];Rect(drawer,ctx,d.x-d.r,d.y-d.r,d.x+d.r,d.y+d.r,InkOf(d.ink));}
    for(int i=0;i<k.notes;++i){const auto& n=k.note[i];if(n.text[0])Label(text,lines,at,n.x,n.y,n.align,n.scale,InkOf(n.ink),L"%ls",n.text);}
}
// The pixels a unit of tan off `bore` spans on the screen from its point (cx, cy): the bore turned a little to its right,
// projected (the view's own projection: the sight's magnification is in it). 0: not known.
float FocalAt(const float* vp,const float* bore,float width,float height,float cx,float cy) noexcept {
    constexpr float kProbe=0.01f;   // rad
    float turned[3],mx,my;
    if(!reticle::Turned(bore,kProbe,turned) || !sight::ToScreen(vp,turned,0.0f,width,height,&mx,&my))return 0.0f;
    return std::sqrt((mx-cx)*(mx-cx)+(my-cy)*(my-cy))/std::tan(kProbe);
}
// The px a unit of tan spans at the screen's centre on view-projection `vp`: the view's forward direction unprojected
// from the centre (near and far), then FocalAt along it. 0: not known.
float CentreFocal(const float* vp,float width,float height) noexcept {
    float inv[16];
    if(!mapcmd::Invert4(vp,inv))return 0.0f;
    const float nearH[4]={0.0f,0.0f,0.0f,1.0f},farH[4]={0.0f,0.0f,1.0f,1.0f};
    float n[4],f[4];
    for(int k=0;k<4;++k) {
        n[k]=nearH[0]*inv[k]+nearH[1]*inv[4+k]+nearH[2]*inv[8+k]+nearH[3]*inv[12+k];
        f[k]=farH[0]*inv[k]+farH[1]*inv[4+k]+farH[2]*inv[8+k]+farH[3]*inv[12+k];
    }
    if(!(std::fabs(n[3])>1e-9f) || !(std::fabs(f[3])>1e-9f))return 0.0f;
    float dir[3]={f[0]/f[3]-n[0]/n[3],f[1]/f[3]-n[1]/n[3],f[2]/f[3]-n[2]/n[3]};
    const float l=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    if(!(l>1e-6f))return 0.0f;
    for(float& d:dir)d/=l;
    return FocalAt(vp,dir,width,height,width*0.5f,height*0.5f);
}

// A gun's range ladder (gunsight.h) projected onto the screen as reticle.h's ticks.
void LadderMarks(const float* vp,float width,float height,const gunsight::Ladder& l,reticle::Aim* a) noexcept {
    a->step=l.step;a->ticks=0;
    for(int k=0;k<l.ticks && k<gunsight::kMostTicks;++k) {
        float x,y;
        if(!sight::ToScreen(vp,l.at[k],1.0f,width,height,&x,&y))continue;
        a->tick[a->ticks++]=reticle::Mark{x,y,l.range[k]};
    }
}
// A gun's range ladder under its aim point (cx, top): reticle.h Ladder.
void LadderTicks(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const gunsight::Ladder& l,
                 float cx,float top,Line* lines,int* at) noexcept {
    reticle::Aim a{};
    LadderMarks(vp,width,height,l,&a);
    reticle::Sketch k{};
    reticle::Ladder(k,reticle::View{cx,top,0.0f,s,kLineScale},a,cx,top,reticle::kLadderTick);
    Render(drawer,ctx,text,s,k,lines,at);
}
// A guided round's seeker gate at its boresight (reticle.h Gate), `line` under it (its name, the range it locks within).
void SeekerGate(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const float* bore,const wchar_t* line,
                Line* lines,int* at) noexcept {
    float x,y;
    if(!sight::ToScreen(vp,bore,0.0f,width,height,&x,&y))return;
    reticle::Sketch k{};
    reticle::Gate(k,reticle::View{x,y,0.0f,s,kLineScale});
    Render(drawer,ctx,text,s,k,lines,at);
    Label(text,lines,at,x,y+(reticle::kGate+14.0f)*s,1,kLineScale*0.85f,kHudDim,L"%ls",line);
}
void RocketDiamond(void* drawer,void* ctx,float x,float y,float s,const float* rgba) noexcept {
    const float r=kRocketMark*s,t=2.0f*s;
    Seg(drawer,ctx,x,y-r,x+r,y,t,rgba);Seg(drawer,ctx,x+r,y,x,y+r,t,rgba);
    Seg(drawer,ctx,x,y+r,x-r,y,t,rgba);Seg(drawer,ctx,x-r,y,x,y-r,t,rgba);
}
// Each marker is one actual barrel's predicted terrain intersection or finite path end.
// An open dash is never presented as an enemy hit confirmation.
void PhysicalPaths(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,
                   const roundaim::Impact* paths,int count,bool label,Line* lines,int* at) noexcept {
    for(int i=0;i<count && i<roundaim::kSightPaths;++i) {
        const auto& p=paths[i];float x,y;
        if(!sight::ToScreen(vp,p.at,1.0f,width,height,&x,&y))continue;
        if(p.hit)ImpactCross(drawer,ctx,vp,width,height,s,p.at,&x,&y);
        else {
            Seg(drawer,ctx,x-12*s,y,x-5*s,y,2*s,kHudDim);
            Seg(drawer,ctx,x+5*s,y,x+12*s,y,2*s,kHudDim);
        }
        if(label && i==0)Label(text,lines,at,x,y+26*s,1,kLineScale*0.75f,p.hit ? kYellow : kHudDim,
                              Tr(p.hit ? Tx::predictedGround : Tx::noGroundHit),p.range,p.seconds);
    }
}
void HeliGunSight(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const HeliSightReadout& h,
                  Line* lines,int* at) noexcept {
    float x,y;
    const float note=kLineScale*0.85f;
    if(h.gun) {
        Boresight(drawer,ctx,vp,width,height,s,h.bore);
        float bx,by;
        if(!h.physicalOnly && h.ladder.ticks>0 && sight::ToScreen(vp,h.bore,0.0f,width,height,&bx,&by))
            LadderTicks(drawer,ctx,text,vp,width,height,s,h.ladder,bx,by+12.0f*s,lines,at);
        PhysicalPaths(drawer,ctx,text,vp,width,height,s,h.path,h.paths,true,lines,at);
    }
    if(h.arm==HeliArm::missile) {
        if(LockAt(drawer,ctx,vp,width,height,s,h.lock,h.armAt,h.lockProgress,&x,&y))
            Label(text,lines,at,x,y+44.0f*s,1,note,h.lock==2 ? kRed : kYellow,Tr(Tx::missileRange),static_cast<int>(std::lround(h.armRange)));
        else if(!h.lock) {
            wchar_t line[48];
            _snwprintf_s(line,_countof(line),_TRUNCATE,Tr(Tx::missileRange),static_cast<int>(std::lround(h.lockRange)));
            SeekerGate(drawer,ctx,text,vp,width,height,s,h.armBore,line,lines,at);
        }
    } else if(h.arm==HeliArm::rockets) {
        wchar_t arm[24];
        hudtext::WordTo(h.armLabel ? h.armLabel : "RKT",arm,_countof(arm));
        if(h.armHit && sight::ToScreen(vp,h.armAt,1.0f,width,height,&x,&y)) {
            RocketDiamond(drawer,ctx,x,y,s,kHud);
            Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,kHud,L"%ls %d m",arm,static_cast<int>(std::lround(h.armRange)));
        } else if(sight::ToScreen(vp,h.armBore,0.0f,width,height,&x,&y)) {
            RocketDiamond(drawer,ctx,x,y,s,kHudDim);
            Label(text,lines,at,x,y+(kRocketMark+12.0f)*s,1,note,kHudDim,L"%ls",arm);
        }
    }
}

// The mouse-aim flight's aim of a heli or rotor craft (heliaim.h): the jets' cyan square, FLY beside it, so it is not
// taken for a weapon's mark (the user, 2026-10-05: the flight's aim and the gun's pipper were confused). The jets keep
// the bare square: theirs is where both the flight and the nose's guns go.
void FlightAim(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const float* p,Line* lines,
               int* at) noexcept {
    float x,y,depth;
    AimSquare(drawer,ctx,vp,width,height,s,p);
    if(Project(vp,p,width,height,&x,&y,&depth))Label(text,lines,at,x+22.0f*s,y,0,kLineScale*0.75f,kCyan,L"%ls",Tr(Tx::fly));
}

// --- The warnings (the user, 2026-10-06: "告警换成更现实一点的如何。比如圆圈告警这种。还有拉起、失速提示音和拉起提示"):
// what warn.cpp decides, drawn as a real cockpit shows it instead of a line of text: the threats on a radar warning
// receiver's scope (RwrScope) and marked in view (ThreatMarks), the ground's chevrons, break X and pull-up arrow
// (GroundCue), the stall's lift tape and box (StallCue), and every warning lit on a fixed annunciator (Annunciator). ---
alignas(16) const float kInk[4]={0.03f,0.03f,0.03f,1.0f};
void RwrCentre(float width,float height,float s,float side,float* cx,float* cy) noexcept {
    *cx=width*0.5f+side*((kBoxOff+kBoxW*0.5f+60.0f+kRwrR)*s);
    *cy=height*0.80f-(kRwrR+10.0f)*s;
}
void RwrScope(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetSymbols& y,ULONGLONG launchAt,
              ULONGLONG now,float side,Line* lines,int* at,float bottom) noexcept {
    const float r=kRwrR*s,t=2.0f*s;
    float cx,cy;
    RwrCentre(width,height,s,side,&cx,&cy);
    if(bottom>=0.0f) {
        cx=vec::Clamp(cx,(kRwrR+24.0f)*s,width-(kRwrR+24.0f)*s);
        cy=bottom-(kRwrR+10.0f)*s;
    }
    const bool launch=launchAt && now-launchAt<kLaunchMs,blink=(now/125)%2==0;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,launch ? 3.0f*s : t,48,launch && blink ? kRed : y.threats>0 ? kHud : kHudDim);
    Arc(drawer,ctx,cx,cy,r*0.5f,0.0f,kTurn,t,32,kHudDim);
    for(int i=0;i<12;++i) {
        const float a=static_cast<float>(i)*kTurn/12.0f,dx=std::sin(a),dy=-std::cos(a),tick=(i==0 ? 10.0f : 5.0f)*s;
        Seg(drawer,ctx,cx+dx*r,cy+dy*r,cx+dx*(r+tick),cy+dy*(r+tick),t,y.threats>0 ? kHud : kHudDim);
    }
    Seg(drawer,ctx,cx-9.0f*s,cy+1.0f*s,cx+9.0f*s,cy+1.0f*s,t,kHud);   // the aircraft: wings, fuselage, tail
    Seg(drawer,ctx,cx,cy-7.0f*s,cx,cy+8.0f*s,t,kHud);
    Seg(drawer,ctx,cx-4.0f*s,cy+7.0f*s,cx+4.0f*s,cy+7.0f*s,t,kHud);
    if(launch)Label(text,lines,at,cx,cy-r-22.0f*s,1,kLineScale,blink ? kRed : kWhite,L"%ls",Tr(Tx::launch));
    float fwd[3]={y.nose[0],0.0f,y.nose[2]};
    if(y.threats<=0 || !vec::Normalize(fwd))return;
    const float right[3]={-fwd[2],0.0f,fwd[0]};   // its level right (playerjet.cpp RightOf)
    int nearest=-1;
    for(int i=0;i<y.threats && i<kMostThreats;++i)
        if(y.threatKind[i]==2 && (nearest<0 || vec::Dist(y.threatAt[i],y.pos)<vec::Dist(y.threatAt[nearest],y.pos)))nearest=i;
    for(int i=0;i<y.threats && i<kMostThreats;++i) {
        const float* p=y.threatAt[i];
        const float d[3]={p[0]-y.pos[0],p[1]-y.pos[1],p[2]-y.pos[2]},dist=vec::Dist(p,y.pos);
        const float across=d[0]*right[0]+d[2]*right[2],along=d[0]*fwd[0]+d[2]*fwd[2];
        const float b=across*across+along*along>1e-6f ? std::atan2(across,along) : 0.0f;
        const float rr=r*(0.15f+0.85f*std::sqrt(Unit(dist/kRwrRange)));
        const float x=cx+std::sin(b)*rr,yy=cy-std::cos(b)*rr;
        if(y.threatKind[i]!=2) {
            Label(text,lines,at,x,yy+2.0f*s,1,kLineScale*0.8f,kAmber,L"J");
            Seg(drawer,ctx,x-8.0f*s,yy-8.0f*s,x,yy-14.0f*s,t,kAmber);Seg(drawer,ctx,x,yy-14.0f*s,x+8.0f*s,yy-8.0f*s,t,kAmber);
            continue;
        }
        const float* c=launch && !blink ? kWhite : kRed;
        Label(text,lines,at,x,yy,1,kLineScale*0.8f,c,L"M");
        Label(text,lines,at,x+12.0f*s,yy+12.0f*s,0,kLineScale*0.6f,kRed,L"%.1f",dist*0.001f);
        if(i!=nearest)continue;
        const float q=13.0f*s;
        Seg(drawer,ctx,x,yy-q,x+q,yy,t,c);Seg(drawer,ctx,x+q,yy,x,yy+q,t,c);
        Seg(drawer,ctx,x,yy+q,x-q,yy,t,c);Seg(drawer,ctx,x-q,yy,x,yy-q,t,c);
    }
}

// A threat in view, marked where it is: a missile a red circle with a cross, an enemy jet amber brackets.
void ThreatMarks(void* drawer,void* ctx,const float* vp,float width,float height,float s,const PlayerJetSymbols& y) noexcept {
    const float t=2.0f*s;
    for(int i=0;i<y.threats && i<kMostThreats;++i) {
        const float* p=y.threatAt[i];
        const bool m=y.threatKind[i]==2;
        float x,yy;
        if(!sight::ToScreen(vp,p,1.0f,width,height,&x,&yy) || x<0.0f || x>width || yy<0.0f || yy>height)continue;
        const float q=14.0f*s;
        if(m) {
            Arc(drawer,ctx,x,yy,q,0.0f,kTurn,t,12,kRed);
            Seg(drawer,ctx,x-q*0.6f,yy,x+q*0.6f,yy,t,kRed);Seg(drawer,ctx,x,yy-q*0.6f,x,yy+q*0.6f,t,kRed);
            continue;
        }
        for(int side=-1;side<=1;side+=2) {   // brackets either side
            const float k=static_cast<float>(side);
            Seg(drawer,ctx,x+k*q,yy-q,x+k*q*0.4f,yy-q,t,kAmber);Seg(drawer,ctx,x+k*q,yy-q,x+k*q,yy+q,t,kAmber);
            Seg(drawer,ctx,x+k*q,yy+q,x+k*q*0.4f,yy+q,t,kAmber);
        }
    }
}
void Threats(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,
             ULONGLONG launchAt,Line* lines,int* at,float side=-1.0f) noexcept {
    RwrScope(drawer,ctx,text,width,height,s,y,launchAt,GetTickCount64(),side,lines,at);
    ThreatMarks(drawer,ctx,vp,width,height,s,y);
}

// The ground (warn.cpp's GPWS; the F-16's and its Auto-GCAS's symbols): two chevrons either side of the flight path marker
// (the nose's point hovering) closing in on it as the impact nears, amber from kTerrainSeconds out, red at
// kPullUpSeconds, the seconds to the impact under it; with PULL UP a large blinking break X over the HUD's centre, PULL
// UP under it, and an arrow off the marker the way the path must go to climb away (toward the sky: rolled over, that
// is down the screen).
void GroundCue(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,Gpws g,
               float impactIn,Line* lines,int* at) noexcept {
    if(g==Gpws::none)return;
    const ULONGLONG now=GetTickCount64();
    const bool pull=g==Gpws::pullUp,blink=(now/125)%2==0;
    const float* c=pull ? (blink ? kRed : kWhite) : kAmber;
    const float* base=y.moving ? y.dir : y.nose;
    float cx=width*0.5f,cy=height*0.5f,x,yy;
    if(sight::ToScreen(vp,base,0.0f,width,height,&x,&yy)){cx=x;cy=yy;}
    const float t=3.0f*s,k=12.0f*s;
    const float off=(30.0f+110.0f*Unit((impactIn-kPullUpSeconds)/(kTerrainSeconds-kPullUpSeconds)))*s;
    for(int side=-1;side<=1;side+=2) {   // pointing in at the marker
        const float e=static_cast<float>(side),bx=cx+e*off;
        Seg(drawer,ctx,bx,cy-k,bx-e*k,cy,t,c);Seg(drawer,ctx,bx-e*k,cy,bx,cy+k,t,c);
    }
    if(impactIn>=0.0f)Label(text,lines,at,cx,cy+28.0f*s,1,kLineScale*0.8f,c,L"%.1f s",impactIn);
    // The way the path's point moves on the screen as the path turns toward the world's up across it: the projection's
    // derivative there ((u w_b - b w_u) / w_b^2 of the clip coordinates, the square dropped: only the way counts), so it
    // holds with the marker off the screen too (a steep sink: the escape's point on and the path's off it pointed the
    // arrow down, found offline with tools/hud_view.cpp). Behind the eye or straight down: up the screen.
    float up[3]={-base[0]*base[1],1.0f-base[1]*base[1],-base[2]*base[1]};   // the world's up across the path
    float dx=0.0f,dy=-1.0f;
    if(vec::Normalize(up)) {
        float b[4],u[4];
        for(int i=0;i<4;++i){b[i]=base[0]*vp[i]+base[1]*vp[4+i]+base[2]*vp[8+i];u[i]=up[0]*vp[i]+up[1]*vp[4+i]+up[2]*vp[8+i];}
        const float mx=(u[0]*b[3]-b[0]*u[3])*width*0.5f,my=-(u[1]*b[3]-b[1]*u[3])*height*0.5f,len=std::sqrt(mx*mx+my*my);
        if(b[3]>1e-6f && std::isfinite(len) && len>1e-6f){dx=mx/len;dy=my/len;}
    }
    const float from=22.0f*s,to=(pull ? 95.0f : 70.0f)*s,head=14.0f*s;
    Seg(drawer,ctx,cx+dx*from,cy+dy*from,cx+dx*(to-head),cy+dy*(to-head),4.0f*s,c);
    Tri(drawer,ctx,cx+dx*(to-head),cy+dy*(to-head),cx+dx*to,cy+dy*to,9.0f*s,c);
    if(!pull)return;
    const float mx=width*0.5f,my=height*0.5f,r=110.0f*s;
    Seg(drawer,ctx,mx-r,my-r,mx+r,my+r,5.0f*s,c);Seg(drawer,ctx,mx-r,my+r,mx+r,my-r,5.0f*s,c);
    Label(text,lines,at,mx,my+r+22.0f*s,1,kTitleScale,c,L"%ls",Tr(Tx::pullUp));
}

// The stall (the user: "失速提示"): a lift tape left of the speed box, the share of all the wing gives that its path needs
// (an AoA indexer's job: this flight model has the wing's lift, not an angle of attack, playerjet.cpp Air) from 0 at
// the bottom to the stall at the top, red over kStallNear, a caret at the share; stalled, a blinking STALL box beside
// the flight path marker. `share` 0: on the ground or a rotor craft, none.
constexpr float kStallNear=0.85f;
void StallCue(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetSymbols& y,float share,
              bool stall,Line* lines,int* at) noexcept {
    if(!(share>0.0f))return;
    const bool blink=(GetTickCount64()/125)%2==0;
    const float x=width*0.5f-(kBoxOff+kBoxW*0.5f+18.0f)*s,cy=height*0.5f,half=kBarHalf*s,t=2.0f*s;
    const float bottom=cy+half,per=2.0f*half;
    Seg(drawer,ctx,x,cy-half,x,bottom,t,kHudDim);
    Seg(drawer,ctx,x,cy-half,x,bottom-per*kStallNear,4.0f*s,kRed);
    for(int q=0;q<=4;++q)Seg(drawer,ctx,x,bottom-per*0.25f*static_cast<float>(q),x+(q%2==0 ? 10.0f : 6.0f)*s,bottom-per*0.25f*static_cast<float>(q),t,kHud);
    const float yy=bottom-per*Unit(share);
    const float* c=stall ? kRed : share>kStallNear ? kAmber : kHud;
    Tri(drawer,ctx,x-14.0f*s,yy,x-2.0f*s,yy,6.0f*s,c);
    Label(text,lines,at,x,cy-half-14.0f*s,1,kLineScale*0.7f,kHudDim,L"%ls",Tr(Tx::lift));
    if(!stall)return;
    float fx=width*0.5f,fy=height*0.5f,px,py;
    if(y.moving && sight::ToScreen(vp,y.dir,0.0f,width,height,&px,&py)){fx=px;fy=py;}
    const float* box=blink ? kRed : kWhite;
    const int first=*at;
    Label(text,lines,at,fx+46.0f*s,fy,0,kTitleScale,box,L"%ls",Tr(Tx::stall));
    if(*at==first)return;
    const Line& l=lines[first];
    const float w=(l.w>0.0f ? l.w : 70.0f*s)+12.0f*s,h=(l.h>0.0f ? l.h : 24.0f*s)+8.0f*s,x0=l.x-6.0f*s,y0=fy-h*0.5f;
    Seg(drawer,ctx,x0,y0,x0+w,y0,t,box);Seg(drawer,ctx,x0,y0+h,x0+w,y0+h,t,box);
    Seg(drawer,ctx,x0,y0,x0,y0+h,t,box);Seg(drawer,ctx,x0+w,y0,x0+w,y0+h,t,box);
}

// The annunciator (a real cockpit's master caution and its lights: one fixed place to look): a row of lit tiles under
// the heading tape, MASTER WARN (red) or MASTER CAUTION (amber) filled first, then each warning lit in warn.h's order,
// the warnings red and the cautions amber in a frame; the warnings blink, and so does a caution its first kNewMs.
constexpr ULONGLONG kNewMs=3000;
void Annunciator(void* drawer,void* ctx,Text* text,float width,float height,float s,const Warnings& w,Line* lines,int* at) noexcept {
    if(!w.on)return;
    const ULONGLONG now=GetTickCount64();
    const bool blink=(now/125)%2==0;
    int lit[kWarnCount],n=0;
    bool warning=false;
    for(int k=0;k<kWarnCount;++k)if(w.on>>k&1u){lit[n++]=k;warning=warning || IsWarning(k);}
    if(!LineSink{lines,at}.Room(n+1,__func__))return;
    Line* const tile=&lines[*at];
    Format(tile[0],L"%ls",Tr(warning ? Tx::masterWarn : Tx::masterCaution));
    tile[0].rgba=kInk;
    for(int i=0;i<n;++i) {
        const int k=lit[i];
        const bool launch=k==kWarnMissile && w.launchAt && now-w.launchAt<kLaunchMs;
        Format(tile[1+i],L"%ls",Tr(launch ? Tx::missileLaunch : kWarnText[k]));
        const bool flash=IsWarning(k) || now-w.litAt[k]<kNewMs;
        tile[1+i].rgba=flash && !blink ? kWhite : IsWarning(k) ? kRed : kAmber;
    }
    for(int i=0;i<=n;++i){tile[i].scale=kLineScale*0.85f;tile[i].w=tile[i].h=0.0f;}
    if(text)MeasureAll(*text,tile,n+1);
    const float pad=8.0f*s,gap=6.0f*s,t=2.0f*s;
    float textH=0.0f,total=-gap;
    for(int i=0;i<=n;++i){textH=tile[i].h>textH ? tile[i].h : textH;total+=(tile[i].w>0.0f ? tile[i].w : 80.0f*s)+2.0f*pad+gap;}
    const float h=(textH>0.0f ? textH : 18.0f*s)+2.0f*4.0f*s,y0=height*0.11f+44.0f*s;
    float x=(width-total)*0.5f;
    for(int i=0;i<=n;++i) {
        Line& l=tile[i];
        const float tw=(l.w>0.0f ? l.w : 80.0f*s)+2.0f*pad;
        const float* c=i==0 ? (warning ? kRed : kAmber) : l.rgba;
        if(i==0)Rect(drawer,ctx,x,y0,x+tw,y0+h,c);
        else {
            Rect(drawer,ctx,x,y0,x+tw,y0+h,kBarBack);
            Rect(drawer,ctx,x,y0,x+tw,y0+t,c);Rect(drawer,ctx,x,y0+h-t,x+tw,y0+h,c);
            Rect(drawer,ctx,x,y0,x+t,y0+h,c);Rect(drawer,ctx,x+tw-t,y0,x+tw,y0+h,c);
        }
        l.x=x+pad;l.y=y0+(h-l.h)*0.5f;
        x+=tw+gap;
    }
    *at+=n+1;
}

// The fighter HUD of the aircraft the player flies, part by part as the ini switches them (`launchAt`: warn.cpp's).
void FighterHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const PlayerJetReadout& j,
                ULONGLONG launchAt,Line* lines,int* at) noexcept {
    const PlayerJetSymbols& y=j.sym;
    if(Cfg().playerJetFlightHud) {
        if(j.air){Ladder(drawer,ctx,text,vp,width,height,s,y,lines,at);FlightPath(drawer,ctx,vp,width,height,s,y);}
        HeadingTape(drawer,ctx,text,width,height,s,y,lines,at);
        SpeedAltBoxes(drawer,ctx,text,width,height,s,j,lines,at);
        GroundCue(drawer,ctx,text,vp,width,height,s,y,j.gpws,j.impactIn,lines,at);
        StallCue(drawer,ctx,text,vp,width,height,s,y,j.liftShare,j.stall,lines,at);
    }
    if(Cfg().playerJetGunSight)GunSight(drawer,ctx,vp,width,height,s,y);
    if(Cfg().playerJetThreatHud)Threats(drawer,ctx,text,vp,width,height,s,y,launchAt,lines,at);
}

// With the flight HUD on (ini PlayerJetFlightHud; the user, 2026-10-05: "飞机底部的旧hud可以删了吧") the old cockpit
// panel goes: its speed, height, climb and g are the HUD's boxes (SpeedAltBoxes), its HP the game's own vehicle bar,
// its key list the README's, its warnings the annunciator's and the HUD's symbols (2026-10-06: "只留挂载和告警").
// What only it showed stays, as HUD text without a panel under the HUD's centre: one line of the throttle, the fuel (the
// stock gauge's FUEL panel gone, stockgauge.cpp), the stores (the picked one in brackets) and the flares, and over it on
// the takeoff roll its cue (TakeoffCue). The stores are the loadout strip under it (their pictures, names and rounds;
// `switched`: the picked one large over the line too, LoadoutBanner).
void CockpitStrip(void* drawer,void* ctx,Text* text,float width,float height,float s,const PlayerJetReadout& j,bool switched,Line* lines,
                  int* at) noexcept {
    if(!LineSink{lines,at}.Room(2,__func__))return;
    LoadCell cells[kMostStores];
    const int n=JetCells(j,cells);
    const LoadoutDock dock=LoadoutDockOf(text,width,height,s,cells,n,1);
    HudRegionText region(text,s,dock.scale,lines,at);text=region.text;s=dock.scale;
    Line& warn=lines[(*at)++];
    Line& arms=lines[(*at)++];
    wchar_t cue[64],stores[128],fuel[32];
    bool rotate=false;
    TakeoffCue(j,cue,_countof(cue),&rotate);
    StoresText(stores,_countof(stores),j,false);
    FuelText(fuel,_countof(fuel),j.fuel);
    Format(warn,L"%ls",cue);
    Format(arms,Tr(Tx::strip),static_cast<int>(std::lround(j.throttle*100.0f)),fuel[0] ? L"  " : L"",fuel,stores);
    warn.scale=kTitleScale;warn.rgba=rotate ? ((GetTickCount64()/125)%2==0 ? kGreen : kYellow) : kHud;
    arms.scale=kLineScale;arms.rgba=j.bomb ? kYellow : kHud;
    warn.w=warn.h=arms.w=arms.h=0.0f;
    if(text){MeasureAll(*text,&warn,1);MeasureAll(*text,&arms,1);}
    const float armsH=arms.h>0.0f ? arms.h : 18.0f*s,warnH=warn.h>0.0f ? warn.h : 24.0f*s;
    arms.x=(width-arms.w)*0.5f;arms.y=dock.top-armsH-8.0f*s;
    warn.x=(width-warn.w)*0.5f;warn.y=arms.y-warnH-6.0f*s;
    LoadoutStrip(drawer,ctx,text,width,dock.y,s,cells,n,lines,at);
    if(LineSink{lines,at}.Room(1,__func__)) {
        Line& keys=lines[(*at)++];JetControls(keys,j);
        ControlRow(text,width,dock.footer,s,keys);
    }
    if(switched && j.store>=0 && j.store<n)LoadoutBanner(drawer,ctx,text,width,dock.bannerBottom,s,cells[j.store],lines,at);
}

// --- The helicopter HUD (the user, 2026-10-05: "a helicopter HUD": the flight instruments, the weapons' aim, the hover's
// aids, the old panel gone; ini HeliFlightHud): for a stock helicopter the player flies (heli.cpp PlayerHeliHud) and a
// rotor craft of the plugin (PlayerJetReadout::rotor), from their HeliFlight and the fighter HUD's symbols. The fighter
// HUD's horizon, ladder, heading tape and boxes (the left one's under-line the speed W / S set), the flight path marker
// once it flies (kMovingSpeed), else and low over the ground the hover's: a drift circle (its level velocity on the
// nose's frame, kDriftFull at the ring) and a bar of the height over the ground up to kLowHover. The mouse's aim is the
// jets' cyan square. The weapons' aim is the stock heli's gun sight (HeliGunSight, drawn apart) or the rotor craft's the
// jets' (GunSight, LockMark, ImpactMark); the threat ring the fighter's. One line under the HUD's centre in place of the
// old panel (HeliStrip): the warning or the takeoff cue over the speed set, the height held and the stores. ---
constexpr float kDriftShown=8.0f;    // m/s: slower than this (level) the drift circle shows
constexpr float kDriftFull=5.0f;     // m/s: the drift at the circle's ring
constexpr float kDriftR=40.0f,kDriftDown=150.0f;   // px (at 1080 lines): its radius, its centre under the screen's
constexpr float kLowHover=30.0f;     // m over the ground: the height bar's top (shown under it)

// The hover's drift: a ring round a centre cross under the screen's middle, an arrow from the centre the way it drifts
// (up: along the nose, right: to its right), kDriftFull m/s at the ring; faster, the arrow stops at the ring, amber.
void DriftMark(void* drawer,void* ctx,float width,float height,float s,const HeliFlight& f,const PlayerJetSymbols& y) noexcept {
    float nose[3]={y.nose[0],0.0f,y.nose[2]};
    if(!vec::Normalize(nose))return;
    const float right[3]={-nose[2],0.0f,nose[0]};   // its level right (playerjet.cpp RightOf)
    const float fore=f.vel[0]*nose[0]+f.vel[2]*nose[2],side=f.vel[0]*right[0]+f.vel[2]*right[2];
    const float cx=width*0.5f,cy=height*0.5f+kDriftDown*s,r=kDriftR*s,t=2.0f*s;
    Arc(drawer,ctx,cx,cy,r,0.0f,kTurn,t,24,kHudDim);
    Seg(drawer,ctx,cx-6.0f*s,cy,cx+6.0f*s,cy,t,kHud);Seg(drawer,ctx,cx,cy-6.0f*s,cx,cy+6.0f*s,t,kHud);
    const float drift=std::sqrt(fore*fore+side*side);
    if(drift<0.2f)return;
    const float len=r*vec::Clamp(drift/kDriftFull,0.0f,1.0f),dx=side/drift,dy=-fore/drift;
    const float* c=drift>kDriftFull ? kAmber : kHud;
    const float tipX=cx+dx*len,tipY=cy+dy*len,head=8.0f*s;
    if(len>head)Seg(drawer,ctx,cx,cy,tipX-dx*head,tipY-dy*head,3.0f*s,c);
    Tri(drawer,ctx,tipX-dx*head,tipY-dy*head,tipX,tipY,5.0f*s,c);
}

// The height over the ground for the landing and the low hover: a bar right of the height box from 0 to kLowHover m, a
// tick every 5 m, a caret at the height, amber sinking faster than kSinkWarn under kSinkLow m.
void HeightBar(void* drawer,void* ctx,float width,float height,float s,const HeliFlight& f) noexcept {
    if(!f.ground || !(f.clear<kLowHover))return;
    const float x=width*0.5f+(kBoxOff+kBoxW*0.5f+18.0f)*s,cy=height*0.5f,half=kBarHalf*s,t=2.0f*s;
    const float bottom=cy+half,per=2.0f*half/kLowHover;
    Seg(drawer,ctx,x,cy-half,x,bottom,t,kHudDim);
    for(int m=0;m<=static_cast<int>(kLowHover);m+=5)Seg(drawer,ctx,x,bottom-per*static_cast<float>(m),x+(m%10==0 ? 10.0f : 6.0f)*s,
                                                        bottom-per*static_cast<float>(m),t,kHud);
    const float at=bottom-per*vec::Clamp(f.clear,0.0f,kLowHover);
    const bool sink=f.clear<kSinkLow && f.climb<-kSinkWarn;
    Tri(drawer,ctx,x-14.0f*s,at,x-2.0f*s,at,6.0f*s,sink ? kAmber : kHud);
}

// The helicopter HUD's instruments (see above); the weapons' aim and the mouse's aim (drawn with the HUD off too) are the
// caller's.
void HeliHud(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const HeliFlight& f,
             const PlayerJetSymbols& y,ULONGLONG launchAt,Line* lines,int* at) noexcept {
    const float fitted=std::fmin(s,width/(2.0f*(kBoxOff+kBoxW*0.5f+60.0f+2.0f*kRwrR+24.0f)));
    HudRegionText region(text,s,fitted,lines,at);text=region.text;s=fitted;
    if(!f.landed)Ladder(drawer,ctx,text,vp,width,height,s,y,lines,at);
    if(y.moving && !f.landed)FlightPath(drawer,ctx,vp,width,height,s,y);
    if(!f.landed && (f.speed<kDriftShown || (f.ground && f.clear<kLowHover)))DriftMark(drawer,ctx,width,height,s,f,y);
    HeadingTape(drawer,ctx,text,width,height,s,y,lines,at);
    wchar_t set[32]=L"";
    if(f.aiming)std::swprintf(set,32,Tr(f.setSpeed==0.0f ? Tx::setHover : Tx::setSpeed),static_cast<int>(std::lround(f.setSpeed*3.6f)));
    Boxes(drawer,ctx,text,width,height,s,f.speed,set,kCyan,f.clear,f.ground,f.climb,lines,at,true);
    HeightBar(drawer,ctx,width,height,s,f);
    GroundCue(drawer,ctx,text,vp,width,height,s,y,f.gpws,f.impactIn,lines,at);
    if(Cfg().playerJetThreatHud) {
        const float bottom=height*kFlightRegionBottom;
        float cx,cy;RwrCentre(width,height,s,-1.0f,&cx,&cy);
        cx=vec::Clamp(cx,(kRwrR+24.0f)*s,width-(kRwrR+24.0f)*s);cy=bottom-(kRwrR+10.0f)*s;
        Label(text,lines,at,cx,cy+kRwrR*s+18.0f*s,1,kLineScale*0.7f,kHudDim,L"%ls",Tr(Tx::heliThreatScope));
        RwrScope(drawer,ctx,text,width,height,s,y,launchAt,GetTickCount64(),-1.0f,lines,at,bottom);
        ThreatMarks(drawer,ctx,vp,width,height,s,y);
    }
}

// The lines in place of the old panel (see above; its warnings are the annunciator's and the HUD's symbols): on top on
// the ground the takeoff cue (the rotor's share of the lift-off speed; LIFT OK blinking from there); under it the speed
// set and the height held (the mouse-aim flight); at the bottom `stores` (a rotor craft's; nullptr: none) on a line of
// their own (with the speed and ALT HOLD on one line, 5-6 stores ran past a line's 128 characters: the last cut off).
// The fuel (`fuel`, the stock gauge's FUEL panel gone: stockgauge.cpp) goes on the speed set's line. The stores' cells
// (`cells`, `n`) are the loadout strip under it (`picked` the picked one's, -1 none; `switched`: it large over the
// lines too, LoadoutBanner).
void HeliStrip(void* drawer,void* ctx,Text* text,float width,float height,float s,const HeliFlight& f,const FuelReading& fuel,
               const wchar_t* stores,const LoadCell* cells,int n,int picked,bool switched,Line* lines,int* at,const Line* controls) noexcept {
    if(!LineSink{lines,at}.Room(4,__func__))return;
    const int footers=(f.keys && f.aiming ? 1 : 0)+(controls && controls->text[0] ? 1 : 0);
    const LoadoutDock dock=LoadoutDockOf(text,width,height,s,cells,n,footers);
    HudRegionText region(text,s,dock.scale,lines,at);text=region.text;s=dock.scale;
    Line& warn=lines[(*at)++];
    Line& info=lines[(*at)++];
    Line& arms=lines[(*at)++];
    Line& help=lines[(*at)++];
    // The keys (the user, 2026-10-06: "I do not know how to climb or descend, except Space"): the mouse-aim flight on the
    // keyboard and mouse spells its controls under the strip; the descent is the brake key (ini PlayerJetBrakeKey).
    Format(help,L"");
    if(f.keys && f.aiming) {
        wchar_t down[32];
        KeyName(Cfg().playerJetBrakeKey,down,32);
        if(f.landed)Format(help,L"%ls",Tr(f.collective ? Tx::heliKeysInstructorLanded : Tx::heliKeysLanded));
        else Format(help,Tr(f.collective ? Tx::heliKeysInstructor : Tx::heliKeysAir),down);
    }
    const bool blink=(GetTickCount64()/125)%2==0,lift=f.landed && f.hover>0.0f && f.rotor>0.01f,liftOk=lift && f.rotor>=f.hover;
    if(liftOk){Format(warn,L"%ls",Tr(Tx::liftOk));warn.rgba=blink ? kGreen : kYellow;}
    else if(lift){Format(warn,Tr(Tx::rotorShare),static_cast<int>(std::lround(100.0f*f.rotor/f.hover)));warn.rgba=kCyan;}
    else{Format(warn,L"");warn.rgba=kHud;}
    Format(info,L"");
    if(f.aiming)Append(info,Tr(f.setSpeed==0.0f ? Tx::speedSetHover : Tx::speedSet),static_cast<int>(std::lround(f.setSpeed*3.6f)));
    if(f.power>=0.0f && !f.landed)Append(info,Tr(Tx::heliPower),info.text[0] ? L"    " : L"",static_cast<int>(std::lround(f.power*100.0f)));
    if(f.hoverMode)Append(info,L"%ls%ls",info.text[0] ? L"    " : L"",Tr(Tx::heliHoverActive));
    if(f.holding)Append(info,Tr(Tx::altHold),info.text[0] ? L"    " : L"");
    wchar_t tank[32];
    FuelText(tank,_countof(tank),fuel);
    if(tank[0])Append(info,L"%ls%ls",info.text[0] ? L"    " : L"",tank);
    Format(arms,L"%ls",stores ? stores : L"");
    warn.scale=kTitleScale;info.scale=arms.scale=kLineScale;info.rgba=arms.rgba=kHud;
    help.scale=kLineScale*0.85f;help.rgba=kCyan;
    warn.w=warn.h=info.w=info.h=arms.w=arms.h=help.w=help.h=0.0f;
    if(text){MeasureAll(*text,&warn,1);MeasureAll(*text,&info,1);MeasureAll(*text,&arms,1);MeasureAll(*text,&help,1);}
    ControlRow(text,width,0.0f,s,help);
    const float lineH=18.0f*s,armsH=arms.h>0.0f ? arms.h : (arms.text[0] ? lineH : 0.0f);
    const float infoH=info.h>0.0f ? info.h : lineH,warnH=warn.h>0.0f ? warn.h : 24.0f*s;
    arms.x=(width-arms.w)*0.5f;arms.y=dock.top-armsH-8.0f*s;
    info.x=(width-info.w)*0.5f;info.y=arms.y-infoH-(arms.text[0] ? 4.0f*s : 0.0f);
    warn.x=(width-warn.w)*0.5f;warn.y=info.y-warnH-6.0f*s;
    LoadoutStrip(drawer,ctx,text,width,dock.y,s,cells,n,lines,at);
    help.y=dock.footer;   // the keys under the loadout strip, not over it
    if(controls && controls->text[0] && LineSink{lines,at}.Room(1,__func__)) {
        Line& keys=lines[(*at)++];keys=*controls;
        ControlRow(text,width,help.y+(help.text[0] ? (help.h>0 ? help.h : 18.0f*s)+6.0f*s : 0.0f),s,keys);
    }
    if(switched && picked>=0 && picked<n)LoadoutBanner(drawer,ctx,text,width,dock.bannerBottom,s,cells[picked],lines,at);
}

// The landing gear (gear.cpp GearHudLatest; the jets with gear only), at the screen's right over the cockpit's line: its
// three lights as a real gear panel lays them out (the nose's over the two mains'): green down and locked, amber in
// transit, out (an empty frame) up and locked; under them DOWN / TRANSIT / UP and the key. Warnings over it: GEAR! (red,
// blinking) low and slow with it not down, GEAR SPEED (amber) over the gear's limit with it not up, WEIGHT ON WHEELS
// (amber) an up command refused on the ground; with the annunciator up (`annunciated`) those are its tiles instead.
void GearPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,bool annunciated,Line* lines,int* at,ULONGLONG now) noexcept {
    GearHud g{};
    if(!GearHudLatest(&g) || !stepfresh::Fresh(g.step) || !LineSink{lines,at}.Room(2,__func__))return;
    bool down=true,up=true;
    for(int i=0;i<kGearLegs;++i){down=down && g.at[i]<=0.0f;up=up && g.at[i]>=1.0f;}
    // The lines first: the panel is as wide as they need (WEIGHT ON WHEELS, or a language's longer words, ran past it).
    Line& state=lines[(*at)++];
    wchar_t key[32]{};
    if(g.keys)KeyName(Cfg().playerJetGearKey,key,32);
    else if(Cfg().playerJetGearPad==0x0002)wcscpy_s(key,Tr(Tx::gearPadHold));   // the d-pad down, held (playerjet.cpp PilotGear)
    else std::swprintf(key,32,Tr(Tx::padButton),Cfg().playerJetGearPad);
    Format(state,Tr(Tx::gearState),Tr(down ? Tx::gearDown : up ? Tx::gearUp : Tx::gearTransit),key);
    state.scale=kLineScale*0.85f;state.rgba=down ? kGreen : up ? kWhite : kAmber;
    state.w=state.h=0.0f;
    if(text)MeasureAll(*text,&state,1);
    const wchar_t* const what=annunciated ? nullptr : g.warn ? Tr(Tx::gearBang) : g.overspeed ? Tr(Tx::warnGearSpeed) :
                              g.blocked ? Tr(Tx::warnWeightOnWheels) : nullptr;
    Line* warn=nullptr;
    if(what) {
        warn=&lines[(*at)++];
        Format(*warn,L"%ls",what);
        warn->scale=kLineScale;warn->rgba=g.warn ? ((now/125)%2==0 ? kRed : kWhite) : kAmber;
        warn->w=warn->h=0.0f;
        if(text)MeasureAll(*text,warn,1);
    }
    const float box=24.0f*s,gap=10.0f*s,pad=10.0f*s,h=2.0f*box+gap+2.0f*pad+60.0f*s;
    const float w=std::fmax(3.0f*box+4.0f*gap+2.0f*pad,std::fmax(state.w,warn ? warn->w : 0.0f)+2.0f*pad);
    const float x0=width-w-40.0f*s,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,kCyan);
    const float cx=x0+w*0.5f,top=y0+pad;
    const float lx[kGearLegs]={cx-box*0.5f,cx-box*1.5f-gap,cx+box*0.5f+gap};   // nose; left / right main (x>0 is left)
    const float ly[kGearLegs]={top,top+box+gap,top+box+gap};
    for(int i=0;i<kGearLegs;++i) {
        const float t=2.0f*s;
        Rect(drawer,ctx,lx[i],ly[i],lx[i]+box,ly[i]+box,kBarEdge);
        if(g.at[i]<=0.0f)Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kGreen);
        else if(g.at[i]<1.0f)Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kAmber);
        else Rect(drawer,ctx,lx[i]+t,ly[i]+t,lx[i]+box-t,ly[i]+box-t,kBarBack);
    }
    state.x=cx-state.w*0.5f;state.y=top+2.0f*box+gap+8.0f*s;
    if(warn){warn->x=cx-warn->w*0.5f;warn->y=state.y+(state.h>0.0f ? state.h : 18.0f*s)+4.0f*s;}
}

// The player's helicopter on the ground (HeliCue, the user 2026-10-05): its rotor spinning up to the speed whose lift
// holds it, then 'LIFT OK: TAKE OFF' blinking: from there the collective lifts it off.
void HeliPanel(void* drawer,void* ctx,Text* text,float width,float height,float s,const HeliCue& c,Line* lines,int* at) noexcept {
    if(!LineSink{lines,at}.Room(1,__func__) || !(c.rotor>0.01f) || !(c.hover>0.0f))return;
    Line& l=lines[(*at)++];
    const bool ok=c.rotor>=c.hover;
    const int pct=static_cast<int>(std::lround(100.0f*c.rotor/c.hover));
    if(ok)Format(l,Tr(Tx::rotorLiftOk),pct);
    else Format(l,Tr(Tx::rotorShare),pct);
    l.scale=kTitleScale;
    l.rgba=ok ? ((GetTickCount64()/125)%2==0 ? kGreen : kYellow) : kCyan;
    l.w=l.h=0.0f;
    if(text)MeasureAll(*text,&l,1);
    const float pad=8.0f*s,h=(l.h>0.0f ? l.h : 24.0f*s)+2.0f*pad,w=(l.w>0.0f ? l.w : 360.0f*s)+2.0f*pad;
    const float x0=(width-w)*0.5f,y0=height*0.80f-h;
    Rect(drawer,ctx,x0,y0,x0+w,y0+h,kPanel);
    Rect(drawer,ctx,x0,y0,x0+w,y0+2.0f*s,ok ? kGreen : kCyan);
    l.x=(width-l.w)*0.5f;l.y=y0+pad;
}
}  // namespace hudi
}  // namespace crew
