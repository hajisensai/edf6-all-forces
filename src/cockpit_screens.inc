// The cockpit's screens (included by hud.cpp; the user, 2026-10-13: "关，信息全上面板": in a vehicle's first person the
// stock HUD is off and the plugin's read-outs are on the cockpit's own screens). Each screen is a rectangle of the
// vehicle model (a vc_scr_ marker, pylib/cockpits.py Screen; firstperson.h CockpitScreen) round the eye; its corners go
// through the view-projection onto the view, and what the screen shows is laid out on a canvas (kCanvasH high, as wide as
// the screen's shape) whose every vertex Emit maps through the homography from the canvas onto that quad (straight lines
// stay straight: the bars and ticks land on the screen in perspective; cockpit_canvas.h). Text cannot be warped: each line's centre goes
// through the homography and its size scales by the local stretch there (snapped to kScreenTextStep: the glyph cache).
// The aim's marks (reticles, lock boxes, impact points) and an aircraft's head-up symbols stay on the view: they are
// what a sight or a head-up glass shows.

constexpr float kCanvasH=1080.0f;
constexpr float kScreenTextStep=1.0f/16.0f;
alignas(16) const float kScreenGlow[4]={0.1f,0.9f,0.45f,0.08f};
alignas(16) const float kScreenInk[4]={0.4f,1.0f,0.6f,0.95f};
alignas(16) const float kScreenDim[4]={0.4f,1.0f,0.6f,0.45f};

// What every screen may show, gathered once a draw from the snapshot (whichever vehicle the player is in).
struct ScreenFeed {
    float hp=-1.0f,hpMax=0.0f;
    float vehicleHp=-1.0f,vehicleHpMax=0.0f; // current vehicle provider, separate from the rider armor gauge
    bool hull=false;float hullHp=0.0f,hullMax=0.0f;   // a vehicle whose hull is counted apart (stock_armor_hud.h)
    FuelReading fuel{};
    float speed=-1.0f,clear=-1.0f,climb=0.0f;         // m/s, m over the ground (<0 unknown), m/s
    bool flies=false;
    float heading=-1.0f,pitch=0.0f;                   // degrees (<0 unknown), radians up
    float nose[3]{};bool noseOk=false;                // the nose (unit, world) the heading and pitch are read from
    float path[3]{};bool moving=false;                // an aircraft's flight path (unit, world) once it moves
    float power=-1.0f;
    int cells=0,picked=-1;
    LoadCell cell[kStockArms>kMostStores ? kStockArms : kMostStores];
    unsigned warn=0;
    float pos[3]{};
    int threats=0;
    float threatAt[kMostThreats][3]{};
};

void NoseOf(const float* nose,ScreenFeed& f) noexcept {
    f.heading=sight::HeadingOf(nose);
    const float len=std::sqrt(nose[0]*nose[0]+nose[1]*nose[1]+nose[2]*nose[2]);
    f.pitch=len>1e-4f ? std::asin(vec::Clamp(nose[1]/len,-1.0f,1.0f)) : 0.0f;
    f.noseOk=len>1e-4f && std::isfinite(len);
    for(int i=0;i<3;++i)f.nose[i]=f.noseOk ? nose[i]/len : 0.0f;
}
void PathOf(const PlayerJetSymbols& y,ScreenFeed& f) noexcept {
    f.moving=y.moving;
    std::memcpy(f.path,y.dir,12);
}

ScreenFeed FeedOf(const Snapshot& snap) noexcept {
    ScreenFeed f{};
    if(snap.cockpit) {   // a plugin aircraft
        const PlayerJetReadout& j=snap.jet;
        f.hp=j.hp;f.hpMax=j.hpMax;f.fuel=j.fuel;f.speed=j.speed;f.clear=j.ground ? j.clear : -1.0f;f.climb=j.climb;f.flies=true;
        f.power=j.rotor ? j.heli.power : j.throttle;
        NoseOf(j.sym.nose,f);PathOf(j.sym,f);
        f.cells=JetCells(j,f.cell);f.picked=j.store;
        std::memcpy(f.pos,j.sym.pos,12);
        f.threats=j.sym.threats<kMostThreats ? j.sym.threats : kMostThreats;
        for(int i=0;i<f.threats;++i)std::memcpy(f.threatAt[i],j.sym.threatAt[i],12);
    } else if(snap.heliFly) {   // a stock heli
        const HeliFlight& h=snap.heliHud.f;
        f.hp=h.hp;f.hpMax=h.hpMax;f.fuel=snap.heliHud.fuel;f.speed=h.speed;f.clear=h.ground ? h.clear : -1.0f;f.climb=h.climb;
        f.flies=true;
        f.power=h.power;
        NoseOf(snap.heliHud.sym.nose,f);PathOf(snap.heliHud.sym,f);
        std::memcpy(f.pos,snap.heliHud.sym.pos,12);
    }
    if(snap.stock) {   // a stock vehicle's (a heli's stores too)
        const StockHudReadout& r=snap.stockHud;
        if(!snap.cockpit) {
            f.cells=StockCells(r,f.cell,&f.picked);
            f.threats=r.threats<kStockThreats ? r.threats : kStockThreats;
            for(int i=0;i<f.threats;++i)std::memcpy(f.threatAt[i],r.threatAt[i],12);
        }
        if(!f.flies) {
            f.hp=r.hp;f.hpMax=r.hpMax;f.fuel=r.fuel;f.speed=r.speed;
            NoseOf(r.hull,f);
            std::memcpy(f.pos,r.pos,12);
        }
    }
    f.vehicleHp=f.hp;f.vehicleHpMax=f.hpMax;
    if(snap.armor) {
        f.hp=snap.armorRo.armor.hp;f.hpMax=snap.armorRo.armor.most;
        f.hull=snap.armorRo.hasHull;f.hullHp=snap.armorRo.hull.hp;f.hullMax=snap.armorRo.hull.most;
    }
    if(snap.warned)f.warn=snap.warn.on;
    return f;
}

// Game thread, once the complete vehicle snapshot is gathered. The model draw consumes these readings; the HUD
// renderer never reads native objects or paints over the physical instruments.
namespace {
void PublishInstruments(const Snapshot& snap) noexcept {
    using namespace instruments;
    const ScreenFeed f=FeedOf(snap);
    Readings r{};
    r.language=static_cast<int>(hudtext::Resolve(Cfg().hudLanguage,GameTextLanguage()));
    for(float& v:r.value)v=-1.0f;
    r.value[hp]=f.vehicleHpMax>0 ? f.vehicleHp : -1.0f;r.share[hp]=f.vehicleHpMax>0 ? f.vehicleHp/f.vehicleHpMax : 0.0f;
    r.value[hull]=f.hull ? f.hullHp : -1.0f;r.share[hull]=f.hullMax>0 ? f.hullHp/f.hullMax : 0.0f;
    r.value[fuel]=f.fuel.ok ? f.fuel.share*100.0f : -1.0f;r.share[fuel]=f.fuel.ok ? f.fuel.share : 0.0f;
    r.value[speed]=f.speed>=0 ? f.speed*3.6f : -1.0f;r.value[altitude]=f.clear;r.value[heading]=f.heading;
    r.flight=f.flies;r.value[climb]=f.climb;r.value[pitch]=f.pitch/kDeg;
    r.value[power]=f.power>=0 ? f.power*100.0f : -1.0f;
    r.value[warn]=static_cast<float>(f.warn);r.share[warn]=f.warn ? 1.0f : 0.0f;
    const float hd=f.heading>=0 ? f.heading*kDeg : 0.0f;
    for(int i=0;i<f.threats && i<6;++i) {
        const float dx=f.threatAt[i][0]-f.pos[0],dz=f.threatAt[i][2]-f.pos[2];
        const float distance=std::sqrt(dx*dx+dz*dz),bearing=std::atan2(-dx,dz)-hd;
        const float range=vec::Clamp(distance/600.0f,0.0f,1.0f)*.35f;
        r.contact[i]=true;r.at[i][0]=range*std::sin(bearing);r.at[i][1]=range*std::cos(bearing);
    }
    // The seat's weapons in the order and with the pick the loadout strips and the switch banner use (hud.cpp SeatArmList).
    const hudcue::SeatArms list=SeatArmList(snap);
    ArmRow rows[hudcue::kMostSeatArms];
    const int n=list.n;
    for(int k=0;k<n;++k) {
        const hudcue::SeatArmRow& at=list.rows[k];
        ArmRow& w=rows[k];
        if(at.source==hudcue::ArmSource::store) {   // a plugin aircraft's store
            const auto& j=snap.jet;
            w.round=static_cast<int>(hudcue::CockpitRoundOf(hudcue::StoreIconOf(j.storeName[at.index],j.storeRole[at.index])));
            w.rounds=j.storeRounds[at.index];w.spent=j.storeRounds[at.index]<=0;
        } else if(at.source==hudcue::ArmSource::gun) {   // its gun
            w.round=static_cast<int>(hudcue::CockpitRound::mg);w.rounds=snap.jet.gunRounds;w.spent=snap.jet.gunRounds<=0;
        } else {   // a stock vehicle's (a heli's too)
            const StockArm& m=snap.stockHud.arm[at.index];
            const reticle::Style sight=m.reticle;
            w.round=static_cast<int>(hudcue::CockpitRoundOfArm(static_cast<int>(m.kind),m.lobbed,static_cast<int>(m.style),
                                                                sight==reticle::Style::cannon || sight==reticle::Style::precision,
                                                                m.shell));
            w.rounds=m.ammo;
            w.magazine=m.ammoMax>0 ? static_cast<float>(m.ammo)/static_cast<float>(m.ammoMax) : -1.0f;
            w.loading=hudcue::CellLoad(m.ammo,m.reload,m.cycleSec,m.cycle);
            w.spent=m.ammo<=0 && !m.canReload;
        }
    }
    const int picked=list.picked;
    ArmReadings(r,rows,n,picked);
    FirstPersonInstrumentReadings(r,PlayerHuman());
}
} // namespace

// The canvas's lines [from, *at) onto the view: centre through the map, size by the stretch, as a scale of the HUD's `s`.
void CanvasText(const canvas::Map& k,Line* lines,int from,int at,float s) noexcept {
    for(int i=from;i<at;++i) {
        Line& l=lines[i];
        const float cx=l.x+l.w*0.5f,cy=l.y+l.h*0.5f,stretch=canvas::Stretch(k,cx,cy);
        float px,py;
        canvas::Point(k,cx,cy,&px,&py);
        const float scale=std::round(l.scale*stretch/s/kScreenTextStep)*kScreenTextStep;
        const float r=scale>0.0f ? scale*s/(l.scale>1e-4f ? l.scale : 1.0f) : 0.0f;
        l.scale=scale>kScreenTextStep ? scale : kScreenTextStep;
        l.w*=r;l.h*=r;l.x=px-l.w*0.5f;l.y=py-l.h*0.5f;
    }
}

// --- What each kind shows (canvas units: kCanvasH high, cw wide; text scales in canvas pixels at HUD scale 1) ---
void ScreenTitle(Text* t,Line* lines,int* at,float cw,const wchar_t* title) noexcept {
    Label(t,lines,at,cw*0.5f,70.0f,1,2.4f,kScreenDim,L"%ls",title);
}

void StatusScreen(void* drawer,void* ctx,Text* t,float cw,const ScreenFeed& f,Line* lines,int* at) noexcept {
    ScreenTitle(t,lines,at,cw,Tr(Tx::scrStatus));
    float y=200.0f;
    const float x=70.0f,w=cw-140.0f;
    if(f.hpMax>0.0f) {
        const float share=f.hp/f.hpMax;
        Label(t,lines,at,x,y,0,3.2f,share<0.3f ? kRed : kScreenInk,Tr(Tx::scrArmor),static_cast<int>(f.hp+0.5f),static_cast<int>(f.hpMax+0.5f));
        Bar(drawer,ctx,x,y+70.0f,w,70.0f,share,share,share<0.3f ? kRed : kScreenInk,1.0f);
        y+=230.0f;
    }
    if(f.hull && f.hullMax>0.0f) {
        const float share=f.hullHp/f.hullMax;
        Label(t,lines,at,x,y,0,3.2f,share<0.3f ? kRed : kScreenInk,Tr(Tx::scrHull),static_cast<int>(f.hullHp+0.5f),static_cast<int>(f.hullMax+0.5f));
        Bar(drawer,ctx,x,y+70.0f,w,70.0f,share,share,share<0.3f ? kRed : kScreenInk,1.0f);
        y+=230.0f;
    }
    if(f.fuel.ok) {
        const int sec=f.fuel.sec>=0.0f ? static_cast<int>(f.fuel.sec) : -1;
        if(sec>=0)Label(t,lines,at,x,y,0,3.2f,f.fuel.share<0.2f ? kAmber : kScreenInk,Tr(Tx::scrFuel),sec/60,sec%60);
        else Label(t,lines,at,x,y,0,3.2f,f.fuel.share<0.2f ? kAmber : kScreenInk,Tr(Tx::scrFuelShare),static_cast<int>(f.fuel.share*100.0f+0.5f));
        Bar(drawer,ctx,x,y+70.0f,w,50.0f,f.fuel.share,f.fuel.share,f.fuel.share<0.2f ? kAmber : kScreenInk,1.0f);
        y+=200.0f;
    }
    int shown=0;
    for(int k=0;k<kWarnCount && shown<3 && y<kCanvasH-80.0f;++k) {
        if(!(f.warn>>k&1u))continue;
        Label(t,lines,at,cw*0.5f,y+40.0f,1,3.4f,IsWarning(k) ? kRed : kAmber,L"%ls",Tr(kWarnText[k]));
        y+=110.0f;++shown;
    }
    if(!f.warn && y<kCanvasH-80.0f)Label(t,lines,at,cw*0.5f,y+40.0f,1,3.0f,kScreenDim,L"%ls",Tr(Tx::scrNoWarn));
}

void WeaponsScreen(void* drawer,void* ctx,Text* t,float cw,const ScreenFeed& f,Line* lines,int* at) noexcept {
    ScreenTitle(t,lines,at,cw,Tr(Tx::scrWeapons));
    if(!f.cells){Label(t,lines,at,cw*0.5f,kCanvasH*0.5f,1,3.0f,kScreenDim,L"%ls",Tr(Tx::scrNoArms));return;}
    const int rows=f.cells<6 ? f.cells : 6;
    const float top=170.0f,row=(kCanvasH-top-40.0f)/static_cast<float>(rows>3 ? rows : 3);
    for(int i=0;i<rows;++i) {
        const LoadCell& c=f.cell[i];
        const float y=top+row*(static_cast<float>(i)+0.5f);
        if(c.picked)Rect(drawer,ctx,40.0f,y-row*0.42f,cw-40.0f,y+row*0.42f,kScreenGlow);
        if(c.picked)Tri(drawer,ctx,60.0f,y,100.0f,y,18.0f,kCyan);
        Label(t,lines,at,120.0f,y,0,row>200.0f ? 3.4f : 2.8f,c.rgba ? c.rgba : kScreenInk,L"%ls",c.text);
    }
}

void FlightScreen(void* drawer,void* ctx,Text* t,float cw,const ScreenFeed& f,Line* lines,int* at) noexcept {
    // The heading tape across the top: a tick each 10 degrees over +-40, the cardinal points named.
    if(f.heading>=0.0f) {
        const float cx=cw*0.5f,y=130.0f,span=cw*0.8f;
        for(int d=-40;d<=40;++d) {
            const float hd=std::floor(f.heading)+static_cast<float>(d),x=cx+(hd-f.heading)/80.0f*span;
            const int deg=(static_cast<int>(hd)%360+360)%360;
            if(deg%10)continue;
            Seg(drawer,ctx,x,y,x,y+(deg%30 ? 30.0f : 55.0f),5.0f,kScreenInk);
            if(deg%90==0 && std::fabs(x-cx)>60.0f)   // the cardinal points beside the tape (the readout is under its middle)
                Label(t,lines,at,x,y-50.0f,1,2.4f,kScreenInk,L"%ls",deg==0 ? L"N" : deg==90 ? L"E" : deg==180 ? L"S" : L"W");
        }
        Tri(drawer,ctx,cx,y+95.0f,cx,y+60.0f,18.0f,kCyan);
        Label(t,lines,at,cx,y+150.0f,1,2.6f,kScreenInk,Tr(Tx::scrHeading),static_cast<int>(f.heading+0.5f)%360);
    }
    // The pitch: the horizon line moved by the pitch (`unit` canvas pixels a degree), the boresight fixed in the middle.
    const float midY=kCanvasH*0.56f,unit=7.0f;
    if(f.flies) {
        const float hy=midY+f.pitch/kDeg*unit;
        for(int p=-30;p<=30;p+=10) {
            const float y=hy-static_cast<float>(p)*unit,half=p ? cw*0.12f : cw*0.3f;
            if(y>300.0f && y<kCanvasH*0.8f)Seg(drawer,ctx,cw*0.5f-half,y,cw*0.5f+half,y,p ? 3.0f : 6.0f,p ? kScreenDim : kScreenInk);
        }
        Seg(drawer,ctx,cw*0.5f-80.0f,midY,cw*0.5f-25.0f,midY,7.0f,kCyan);
        Seg(drawer,ctx,cw*0.5f+25.0f,midY,cw*0.5f+80.0f,midY,7.0f,kCyan);
    }
    // Speed bottom left, height and climb bottom right, under the pitch.
    const float rowY=kCanvasH*0.9f;
    if(f.speed>=0.0f)Label(t,lines,at,60.0f,rowY,0,3.2f,kScreenInk,Tr(Tx::scrSpeed),static_cast<int>(f.speed*3.6f+0.5f));
    if(f.clear>=0.0f)Label(t,lines,at,cw-60.0f,rowY,2,3.2f,kScreenInk,Tr(Tx::scrHeight),static_cast<int>(f.clear+0.5f));
    if(f.flies)Label(t,lines,at,cw-60.0f,rowY-170.0f,2,2.6f,kScreenDim,Tr(Tx::scrClimb),f.climb);
}

void MapScreenOf(void* drawer,void* ctx,Text* t,float cw,const ScreenFeed& f,Line* lines,int* at) noexcept {
    ScreenTitle(t,lines,at,cw,Tr(Tx::scrMap));
    const float cx=cw*0.5f,cy=kCanvasH*0.56f,r=kCanvasH*0.38f,range=600.0f;
    for(int ring=1;ring<=3;++ring)Arc(drawer,ctx,cx,cy,r*static_cast<float>(ring)/3.0f,0.0f,kTurn,ring==3 ? 5.0f : 3.0f,40,kScreenDim);
    Seg(drawer,ctx,cx,cy-r,cx,cy+r,2.0f,kScreenDim);Seg(drawer,ctx,cx-r,cy,cx+r,cy,2.0f,kScreenDim);
    Tri(drawer,ctx,cx,cy+20.0f,cx,cy-30.0f,20.0f,kCyan);   // the vehicle, heading up
    const float hd=(f.heading>=0.0f ? f.heading : 0.0f)*kDeg;
    for(int i=0;i<f.threats;++i) {   // each threat by its bearing from the heading and its distance (game +x is west)
        const float dx=f.threatAt[i][0]-f.pos[0],dz=f.threatAt[i][2]-f.pos[2];
        const float east=-dx,north=dz,dist=std::sqrt(east*east+north*north);
        const float bearing=std::atan2(east,north)-hd,rr=r*vec::Clamp(dist/range,0.0f,1.0f);
        const float x=cx+rr*std::sin(bearing),y=cy-rr*std::cos(bearing);
        Arc(drawer,ctx,x,y,22.0f,0.0f,kTurn,9.0f,10,kRed);
    }
    if(f.heading>=0.0f) {   // north on the ring
        const float x=cx+(r-60.0f)*std::sin(-hd),y=cy-(r-60.0f)*std::cos(-hd);   // inside the ring: clear of the title
        Label(t,lines,at,x,y,1,2.4f,kScreenInk,L"N");
    }
}

// The aim's marks of a stock vehicle alone (StockVehicleHud less its tape, block and scope: those are on the screens).
void StockAimOnly(void* drawer,void* ctx,Text* text,const float* vp,float width,float height,float s,const StockHudReadout& r,
                  const StockExtras& x,Line* lines,int* at) noexcept {
    StockMarks(drawer,ctx,text,vp,width,height,s,r,x,lines,at);
    if(x.proteus && vec::Dist(x.proteus->pos,r.pos)<2.0f)ProteusMarks(drawer,ctx,text,vp,width,height,s,*x.proteus,lines,at);
    if(Cfg().playerJetThreatHud && r.threats>0) {
        PlayerJetSymbols y{};
        std::memcpy(y.pos,r.pos,12);
        y.threats=r.threats<kMostThreats ? r.threats : kMostThreats;
        for(int i=0;i<y.threats && i<kStockThreats;++i){std::memcpy(y.threatAt[i],r.threatAt[i],12);y.threatKind[i]=r.threatKind[i];}
        std::memcpy(y.nose,r.lookOk ? r.look : r.hull,12);
        ThreatMarks(drawer,ctx,vp,width,height,s,y);
    }
}

// Whether the cockpit has a head-up glass (a hud screen): then the HUD's head-up symbols stay on the view; without one
// they give way too (their numbers are on the screens) and only the aim's marks stay.
bool HudGlass(const CockpitScreens& c) noexcept {
    for(int i=0;i<c.n;++i)if(c.s[i].kind==ScreenKind::hud)return true;
    return false;
}

// A remote weapon station's monitor over the whole view (a sensor screen: the eye is the gun's camera, looking where the
// gun is laid: firstperson.cpp Ride): the sensor's frame (ScopeShade's: the whole view, a thin bezel just inside its
// edge, corner marks, the field of view), a faint green of a monitor over it,
// the gun's bearing off the hull and its elevation bottom left, the weapon and its rounds top right. The gun's reticle
// is the aim marks' (StockAimOnly).
void RwsDisplay(void* drawer,void* ctx,Text* t,float width,float height,float s,const ScreenFeed& f,const Snapshot& snap,
                Line* lines,int* at) noexcept {
    const float zoom=snap.stock && snap.stockHud.zoom>1.0f ? snap.stockHud.zoom : 1.0f;
    ScopeShade(drawer,ctx,t,width,height,s,zoom,sightzoom::Mask::sensor,lines,at);
    float hx,hy;
    scopeview::RectHalf(width,height,&hx,&hy);
    const float cx=width*0.5f,cy=height*0.5f,pad=40.0f*s;
    Rect(drawer,ctx,0.0f,0.0f,width,height,kScreenGlow);
    Label(t,lines,at,cx+hx-pad,cy-hy+pad,2,kLineScale,kScreenInk,L"%ls",Tr(Tx::rwsTitle));
    if(f.picked>=0 && f.picked<f.cells)Label(t,lines,at,cx+hx-pad,cy-hy+pad+34.0f*s,2,kLineScale,kScreenInk,L"%ls",f.cell[f.picked].text);
    else if(f.cells>0)Label(t,lines,at,cx+hx-pad,cy-hy+pad+34.0f*s,2,kLineScale,kScreenInk,L"%ls",f.cell[0].text);
    // The station's fire control (firecontrol.h), of the gun it lays now.
    const StockHudReadout& r=snap.stockHud;
    const int sightArm=snap.stock && r.sight>=0 && r.sight<r.arms && r.sight<kStockArms ? r.sight : -1;
    if(sightArm>=0) {
        static const Tx kSystems[]={Tx::fcsOptical,Tx::fcsGyro,Tx::fcsComputer,Tx::fcsRadar};
        Label(t,lines,at,cx+hx-pad,cy-hy+pad+68.0f*s,2,kLineScale*0.8f,kScreenDim,L"%ls",
              Tr(kSystems[static_cast<int>(r.arm[sightArm].fcs)]));
    }
    if(snap.stock && snap.stockHud.aimOk) {
        const float* a=snap.stockHud.aim;
        const float gun=sight::HeadingOf(a),hull=sight::HeadingOf(snap.stockHud.hull);
        float off=gun-hull;
        while(off>180.0f)off-=360.0f;
        while(off<-180.0f)off+=360.0f;
        const float len=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
        const float el=len>1e-4f ? std::asin(vec::Clamp(a[1]/len,-1.0f,1.0f))/kDeg : 0.0f;
        Label(t,lines,at,cx-hx+pad,cy+hy-pad-34.0f*s,0,kLineScale,kScreenInk,Tr(Tx::rwsAzimuth),static_cast<int>(std::round(off)));
        Label(t,lines,at,cx-hx+pad,cy+hy-pad,0,kLineScale,kScreenInk,Tr(Tx::rwsElevation),static_cast<int>(std::round(el)));
    }
    if(f.hpMax>0.0f)Label(t,lines,at,cx+hx-pad,cy+hy-pad,2,kLineScale,f.hp<0.3f*f.hpMax ? kRed : kScreenDim,Tr(Tx::scrArmor),
                          static_cast<int>(f.hp+0.5f),static_cast<int>(f.hpMax+0.5f));
}

// --- The head-up glass of a physical cockpit (a 'hud' screen: the F-16's combiner, a helmet's display, the C-130J's HUD;
// docs/feedback-2026-10-21-attitude-indicator.md). The user, 2026-10-15: no HUD over the dials, "要么是模型上的hud":
// the symbols are drawn on the glass, cut to its edges. The horizon, the pitch ladder, the nose's waterline and the
// flight path are conformal (canvas::Through: where the line of sight along a world direction crosses the glass, so
// through the glass the horizon lies on the world's); the heading tape, the speed and height boxes and the bank scale
// are fixed to the glass. The readings are the screens' own (FeedOf: NoseOf's heading and pitch) and the attitude
// cards' bank (instruments::BankOf of the seat's eye frame). Only this machine's view: nothing is sent or stored.
constexpr float kGlassMargin=0.035f;      // of the canvas's height: kept clear round the glass's edge
constexpr float kGlassStep=5.0f;          // degrees between the ladder's rungs
constexpr float kGlassShown=30.0f;        // degrees either side of the climb the rungs go to
constexpr float kGlassGap=1.5f,kGlassRung=5.0f,kGlassTab=0.6f,kGlassHorizon=60.0f;   // degrees: a rung's halves, its tab
constexpr float kGlassInk=7.0f;           // canvas pixels: the lines' thickness

void GlassSeg(void* drawer,void* ctx,const canvas::Glass& g,float ax,float ay,float bx,float by,float t,int dashes,
              const float* rgba) noexcept {
    const float m=g.ch*kGlassMargin;
    if(!canvas::Clip(ax,ay,bx,by,m,m,g.cw-m,g.ch-m))return;
    if(dashes<=0){Seg(drawer,ctx,ax,ay,bx,by,t,rgba);return;}
    const int pieces=2*dashes-1;
    for(int i=0;i<pieces;i+=2) {
        const float f0=static_cast<float>(i)/static_cast<float>(pieces),f1=static_cast<float>(i+1)/static_cast<float>(pieces);
        Seg(drawer,ctx,ax+(bx-ax)*f0,ay+(by-ay)*f0,ax+(bx-ax)*f1,ay+(by-ay)*f1,t,rgba);
    }
}
// The world directions `a` and `b` joined on the glass.
void GlassDirSeg(void* drawer,void* ctx,const canvas::Glass& g,const float* a,const float* b,float t,int dashes,
                 const float* rgba) noexcept {
    float ax,ay,bx,by;
    if(canvas::Through(g,a,&ax,&ay) && canvas::Through(g,b,&bx,&by))GlassSeg(drawer,ctx,g,ax,ay,bx,by,t,dashes,rgba);
}
bool OnGlass(const canvas::Glass& g,float x,float y,float pad) noexcept {
    const float m=g.ch*kGlassMargin+pad;
    return x>m && x<g.cw-m && y>m && y<g.ch-m;
}

void HeadUpGlass(void* drawer,void* ctx,Text* t,const canvas::Glass& g,const ScreenFeed& f,float bank,Line* lines,int* at) noexcept {
    const float cw=g.cw,ch=g.ch,ink=kGlassInk;
    // The horizon and the ladder round the flight path's (else the nose's) level heading, the dives' rungs dashed.
    const float* ref=f.moving ? f.path : f.nose;
    float h[3],r[3];
    if(f.noseOk && canvas::Level(ref,h,r)) {
        const float climb=std::asin(vec::Clamp(ref[1],-1.0f,1.0f))/kDeg;
        const float first=std::ceil((climb-kGlassShown)/kGlassStep)*kGlassStep;
        for(float e=first;e<=climb+kGlassShown && e<90.0f;e+=kGlassStep) {
            if(e<=-90.0f)continue;
            const bool horizon=std::fabs(e)<0.5f;
            const float inner=(horizon ? 2.0f*kGlassGap : kGlassGap)*kDeg,outer=(horizon ? kGlassHorizon : kGlassRung)*kDeg;
            for(int side=-1;side<=1;side+=2) {
                const float k=static_cast<float>(side);
                float a[3],b[3];
                canvas::Toward(h,r,e,inner*k,a);canvas::Toward(h,r,e,outer*k,b);
                GlassDirSeg(drawer,ctx,g,a,b,ink,e<-0.5f ? 3 : 0,kHud);
                if(horizon)continue;
                float tab[3];canvas::Toward(h,r,e+(e>0.0f ? -kGlassTab : kGlassTab),outer*k,tab);
                GlassDirSeg(drawer,ctx,g,b,tab,ink,0,kHud);
                float lx,ly;
                if(canvas::Through(g,b,&lx,&ly) && OnGlass(g,lx+k*70.0f,ly,30.0f))
                    Label(t,lines,at,lx+k*18.0f,ly,side>0 ? 0 : 2,2.4f,kHud,L"%d",static_cast<int>(std::lround(e)));
            }
        }
    }
    // The waterline: where the nose points (the guns' boresight), a W.
    float nx,ny;
    if(f.noseOk && canvas::Through(g,f.nose,&nx,&ny)) {
        const float u=ch*0.03f;
        GlassSeg(drawer,ctx,g,nx-3.0f*u,ny,nx-1.5f*u,ny,ink,0,kHud);GlassSeg(drawer,ctx,g,nx-1.5f*u,ny,nx-0.75f*u,ny+u,ink,0,kHud);
        GlassSeg(drawer,ctx,g,nx-0.75f*u,ny+u,nx,ny,ink,0,kHud);GlassSeg(drawer,ctx,g,nx,ny,nx+0.75f*u,ny+u,ink,0,kHud);
        GlassSeg(drawer,ctx,g,nx+0.75f*u,ny+u,nx+1.5f*u,ny,ink,0,kHud);GlassSeg(drawer,ctx,g,nx+1.5f*u,ny,nx+3.0f*u,ny,ink,0,kHud);
    }
    // The flight path marker: a circle with wings and a fin where the aircraft goes.
    float px,py;
    if(f.moving && canvas::Through(g,f.path,&px,&py) && OnGlass(g,px,py,ch*0.05f)) {
        const float rr=ch*0.022f;
        Arc(drawer,ctx,px,py,rr,0.0f,kTurn,ink,16,kHud);
        GlassSeg(drawer,ctx,g,px-rr*2.4f,py,px-rr,py,ink,0,kHud);GlassSeg(drawer,ctx,g,px+rr,py,px+rr*2.4f,py,ink,0,kHud);
        GlassSeg(drawer,ctx,g,px,py-rr,px,py-rr*1.9f,ink,0,kHud);
    }
    // The heading tape along the top: a tick every 5 degrees over +-15, the tens numbered, a caret and the heading.
    if(f.heading>=0.0f) {
        const float cx=cw*0.5f,y=ch*0.13f,per=cw*0.42f/15.0f;
        GlassSeg(drawer,ctx,g,cx-15.0f*per,y,cx+15.0f*per,y,ink*0.6f,0,kHudDim);
        for(int d=static_cast<int>(std::ceil((f.heading-15.0f)/5.0f))*5;static_cast<float>(d)<=f.heading+15.0f;d+=5) {
            const float x=cx+(static_cast<float>(d)-f.heading)*per;
            const bool ten=d%10==0;
            GlassSeg(drawer,ctx,g,x,y,x,y-(ten ? ch*0.03f : ch*0.018f),ink,0,kHud);
            if(ten && OnGlass(g,x,y-ch*0.06f,40.0f))Label(t,lines,at,x,y-ch*0.06f,1,2.2f,kHud,L"%02d",((d%360+360)%360)/10);
        }
        Tri(drawer,ctx,cx,y+ch*0.035f,cx,y+ch*0.008f,ch*0.014f,kHud);
        Label(t,lines,at,cx,y+ch*0.07f,1,2.4f,kHud,L"%03d",static_cast<int>(std::lround(f.heading))%360);
    }
    // The speed (km/h) left and the height over the ground (m) right, boxed, the climb under the height.
    const float boxW=cw*0.2f,boxH=ch*0.075f,boxY=ch*0.5f;
    auto box=[&](float cx,float value) {
        const float x0=cx-boxW*0.5f,x1=cx+boxW*0.5f,y0=boxY-boxH*0.5f,y1=boxY+boxH*0.5f;
        GlassSeg(drawer,ctx,g,x0,y0,x1,y0,ink*0.7f,0,kHud);GlassSeg(drawer,ctx,g,x1,y0,x1,y1,ink*0.7f,0,kHud);
        GlassSeg(drawer,ctx,g,x1,y1,x0,y1,ink*0.7f,0,kHud);GlassSeg(drawer,ctx,g,x0,y1,x0,y0,ink*0.7f,0,kHud);
        Label(t,lines,at,cx,boxY,1,2.6f,kHud,L"%d",static_cast<int>(std::lround(value)));
    };
    if(f.speed>=0.0f)box(cw*0.15f,f.speed*3.6f);
    if(f.clear>=0.0f) {
        box(cw*0.85f,f.clear);
        Label(t,lines,at,cw*0.85f,boxY+boxH*1.5f,1,2.0f,kHudDim,L"%+.0f",f.climb);
    }
    // The bank scale under the middle, fixed to the glass: ticks at 0, 10, 20, 30, 45 and 60 degrees either way, the
    // pointer where the world's down is (turned with the horizon: a right bank turns it toward the right).
    if(bank>-180.0f && bank<=180.0f) {
        const float cx=cw*0.5f,cy=ch*0.5f,rr=ch*0.38f;
        static const float ticks[]={0.0f,10.0f,20.0f,30.0f,45.0f,60.0f};
        for(float d:ticks)for(int side=-1;side<=1;side+=2) {
            if(d==0.0f && side>0)continue;
            const float a=kTurn*0.25f+static_cast<float>(side)*d*kDeg,len=(d==30.0f || d==60.0f || d==0.0f) ? ch*0.04f : ch*0.025f;
            GlassSeg(drawer,ctx,g,cx+rr*std::cos(a),cy+rr*std::sin(a),cx+(rr+len)*std::cos(a),cy+(rr+len)*std::sin(a),ink*0.7f,0,kHud);
        }
        const float a=canvas::DownOnGlass(vec::Clamp(bank,-70.0f,70.0f));
        const float tipX=cx+rr*std::cos(a),tipY=cy+rr*std::sin(a),baseX=cx+(rr-ch*0.04f)*std::cos(a),baseY=cy+(rr-ch*0.04f)*std::sin(a);
        if(OnGlass(g,tipX,tipY,0.0f))Tri(drawer,ctx,baseX,baseY,tipX,tipY,ch*0.016f,kHud);
    }
}

// Every screen of the snapshot's cockpit drawn onto the view (`vp`, width x height) round its eye.
void DrawCockpitScreens(void* drawer,void* ctx,Text* t,const float* vp,float width,float height,float s,const Snapshot& snap,
                    Line* lines,int* at) noexcept {
    float eye[3],dir[3];
    if(!RayOf(vp,eye,dir))return;
    const ScreenFeed feed=FeedOf(snap);
    const float ts=t ? t->s : s;
    for(int i=0;i<snap.scr.n;++i)if(snap.scr.s[i].kind==ScreenKind::sensor){RwsDisplay(drawer,ctx,t,width,height,s,feed,snap,lines,at);break;}
    // The screen's canvas mapped onto its quad on the view (false: not in front of the eye).
    auto onto=[&](const CockpitScreen& c,float cw) {
        float px[4],py[4];
        bool ok=true;
        const float hw=c.w*0.5f,hh=c.h*0.5f,corner[4][2]={{-hw,hh},{hw,hh},{hw,-hh},{-hw,-hh}};   // TL TR BR BL
        for(int k=0;k<4 && ok;++k) {
            float p[3],q[4];
            for(int a=0;a<3;++a)p[a]=eye[a]+c.off[a]+c.right[a]*corner[k][0]+c.up[a]*corner[k][1];
            for(int a=0;a<4;++a)q[a]=p[0]*vp[a]+p[1]*vp[4+a]+p[2]*vp[8+a]+vp[12+a];
            ok=q[3]>0.05f;
            px[k]=width*0.5f*(1.0f+q[0]/q[3]);py[k]=height*0.5f*(1.0f-q[1]/q[3]);
        }
        return ok && canvas::Onto(screenCanvas,px,py,cw,kCanvasH);
    };
    if(snap.scr.physical) {   // its screens are model geometry (cockpit_instruments.h): only the head-up glass is drawn
        if(!feed.flies)return;
        const float bank=instruments::BankOf(snap.scr.frame);
        for(int i=0;i<snap.scr.n;++i) {
            const CockpitScreen& c=snap.scr.s[i];
            const float cw=kCanvasH*c.w/c.h;
            if(c.kind!=ScreenKind::hud || !onto(c,cw))continue;
            canvas::Glass g{};
            std::memcpy(g.off,c.off,12);std::memcpy(g.right,c.right,12);std::memcpy(g.up,c.up,12);
            g.w=c.w;g.h=c.h;g.cw=cw;g.ch=kCanvasH;
            const int first=*at;
            if(t)t->s=1.0f;
            screenCanvas.on=true;
            HeadUpGlass(drawer,ctx,t,g,feed,bank,lines,at);
            screenCanvas.on=false;
            if(t)t->s=ts;
            CanvasText(screenCanvas,lines,first,*at,ts);
        }
        return;
    }
    for(int i=0;i<snap.scr.n;++i) {
        const CockpitScreen& c=snap.scr.s[i];
        if(c.kind==ScreenKind::hud || c.kind==ScreenKind::sensor)continue;   // the head-up glass / the station's monitor: on the view
        const float cw=kCanvasH*c.w/c.h;
        if(!onto(c,cw))continue;
        const int first=*at;
        if(t)t->s=1.0f;   // canvas pixels while it is laid out
        screenCanvas.on=true;
        Rect(drawer,ctx,0.0f,0.0f,cw,kCanvasH,kScreenGlow);
        switch(c.kind) {
            case ScreenKind::status: StatusScreen(drawer,ctx,t,cw,feed,lines,at);break;
            case ScreenKind::weapons: WeaponsScreen(drawer,ctx,t,cw,feed,lines,at);break;
            case ScreenKind::flight: FlightScreen(drawer,ctx,t,cw,feed,lines,at);break;
            case ScreenKind::map: MapScreenOf(drawer,ctx,t,cw,feed,lines,at);break;
            default: break;
        }
        screenCanvas.on=false;
        if(t)t->s=ts;
        CanvasText(screenCanvas,lines,first,*at,ts);
    }
}
