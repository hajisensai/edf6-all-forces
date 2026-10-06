// The Sazabi's NPC pilot (sazabi.cpp includes this inside its anonymous namespace, after sazabi_arms.inc; docs/gundam-plan.md
// 阶段 4). Once the player has driven it and got out, crew.cpp's auto-crew seats an NPC in it (the stock RideAi, after
// CrewDelayMs, within CrewRange of the player) and the plugin pilots it: what a player's stick and buttons would be is
// worked out here each frame (Controls, ArmsInput) and goes through the same walk, flight and arms as the player's.
//  - it keeps by the player: past kFollowFar it walks (runs, further) to within kFollowNear of them, on its feet;
//    with the player far above it (on a roof, in the air) it climbs on its thrusters;
//  - it faces the nearest enemy within kEngage (else where it walks, else the player's way) and pitches its aim at it;
//  - the beam rifle while that enemy is within kRifleRange and the aim on it; the shield missiles once the game's lock
//    holds one; the funnels whenever they are home and kFunnelEnemies or more enemies are close; the tomahawk at an enemy
//    within kMeleeRange; never the cannon (its charge would stall the walk and sweep friends).
// Its sounds are heard where it is (one-shots); the loops and the HUD stay the player's.

constexpr float kFollowNear=35.0f,kFollowFar=60.0f,kFollowRun=150.0f;   // m from the player
constexpr float kClimbOver=20.0f;          // m: the player this far above it: it climbs
constexpr float kEngage=300.0f,kRifleRange=400.0f,kMeleeRange=22.0f;
constexpr float kOnTarget=8.0f*sazabi::kDeg;   // the aim this close to the enemy: it fires
constexpr int kFunnelEnemies=3;
constexpr float kTurnGain=3.0f;            // 1/s: the turn toward its heading, at most SazabiTurn

float Wrap(float a) noexcept {
    while(a>sazabi::kPi)a-=2.0f*sazabi::kPi;
    while(a<-sazabi::kPi)a+=2.0f*sazabi::kPi;
    return a;
}

// This frame's controls and buttons for the NPC piloting `m`.
void Pilot(Mech& m,unsigned char* v,float dt,Controls* c,ArmsInput* in) noexcept {
    *c=Controls{};*in=ArmsInput{};
    if(!m.rootOk)return;
    const float* r=m.root+12;
    Enemies e{};
    SeeEnemies(v,r,&e);
    const bool enemy=e.n>0 && e.d2[0]<=kEngage*kEngage;
    // the walk: toward the player while they are far
    float want[3]{};
    float away=0.0f;
    const bool seen=player.at && GameMs()-player.at<10000;
    if(seen) {
        const float d[3]={player.pos[0]-r[0],player.pos[1]-r[1],player.pos[2]-r[2]};
        away=std::sqrt(d[0]*d[0]+d[2]*d[2]);
        if(away>kFollowNear){want[0]=d[0]/away;want[2]=d[2]/away;}
        c->ascend=d[1]>kClimbOver && m.thruster>0.2f ? 1.0f : 0.0f;
    }
    const float walkTo=away>kFollowFar ? Clamp((away-kFollowNear)/(kFollowRun-kFollowNear),kWalkShare,1.0f) :
                       away>kFollowNear ? kWalkShare*0.6f : 0.0f;
    // the facing: the enemy, else the way it walks
    float face=m.heading;
    if(enemy)face=std::atan2(e.at[0][0]-r[0],e.at[0][2]-r[2]);
    else if(walkTo>0.0f)face=std::atan2(want[0],want[2]);
    const float off=Wrap(face-m.heading);
    const float most=Cfg().sazabiTurn*sazabi::kDeg;
    c->turn=Clamp(off*kTurnGain,-most,most);
    // the stick relative to the facing (Move: forward along the heading, right across it)
    const float s=std::sin(m.heading),co=std::cos(m.heading);
    c->forward=(want[0]*s+want[2]*co)*walkTo;
    c->right=-(want[0]*co-want[2]*s)*walkTo;
    if(!enemy)return;
    // the aim's pitch onto the enemy, from the muzzle
    float muzzle[3];
    BoneAt(m,sazabi::kMuzzle,nullptr,muzzle);
    const float d[3]={e.at[0][0]-muzzle[0],e.at[0][1]-muzzle[1],e.at[0][2]-muzzle[2]};
    const float flat=std::sqrt(d[0]*d[0]+d[2]*d[2]);
    const float pitch=std::atan2(d[1],flat);
    c->pitch=Clamp(pitch-m.aimPitch,-kPitchRate*dt*2.0f,kPitchRate*dt*2.0f);
    const float dist=std::sqrt(e.d2[0]);
    const bool onIt=std::fabs(off)<kOnTarget && std::fabs(pitch-m.aimPitch)<kOnTarget;
    Arms& a=m.arms;
    // the special: the funnels when they are home and the enemies many, else the missiles
    int around=0;
    for(int i=0;i<e.n;++i)around+=e.d2[i]<=kFunnelRange*kFunnelRange;
    bool docked=true;
    for(const auto& f:a.funnels)docked=docked && f.phase==FunnelPhase::docked;
    const Special pick=docked && a.funnelCool<=0.0f && around>=kFunnelEnemies ? Special::funnels : Special::missiles;
    in->cycle=a.special!=pick && !a.switchHeld;   // one press a frame until it is picked
    if(a.special==Special::funnels)in->secondary=pick==Special::funnels && !a.secondaryHeld;   // a press: launch
    else if(a.special==Special::missiles)in->secondary=a.hasLock;
    in->melee=dist<=kMeleeRange && a.swing<0.0f && !a.meleeHeld;
    in->primary=onIt && dist<=kRifleRange && !in->melee;
}
