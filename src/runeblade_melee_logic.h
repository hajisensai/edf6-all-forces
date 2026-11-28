// The RuneBlade's melee and special arms, the pure part (docs/plan-2026-11-28-zero-worldline-gear.md §2.2 / §2.3; the design
// and its gaps: docs/newclass-re/runeblade-melee-plan.md). runeblade_melee.cpp feeds it one frame of the local RuneBlade's
// facts (trigger, air, place, view, the arm in hand, its ether) and carries out what it says: strikes (area hits at points,
// now or later, repeated), a velocity, ether to pay, the ring's place, marks to take, bolts to fire. tests/runeblade_melee_test.cpp
// runs it.
// What the stock weapon classes already do (the arms' rows, RB-WEAPONS: the swing / hammer CAS, their SlashWave / ShockWave
// rounds) is not here: only what no stock weapon does.
//   M1 Daybreak   the combo's count (the swing's own three stages); stage 3's trail lingers kTrailFrames (0.5 s), hitting
//                 each kTrailEvery; pressed in the air: a dive straight down, a slash where it lands
//   M2 Firmament  held: a charge (kLanceMin..kLanceFull); released: a dash along the view (ground or air), its path one
//                 strike of points hit in turn (a straight pierce through everything on it), a burst at its end
//   M3 Stars      held kRingHold and released: the twin blades thrown as a ring that goes out, circles once round the thrower
//                 and comes back (RingAt), hitting where it is each kRingEvery; who it cuts is marked (the runtime's), and
//                 once it is back the marks burst one after another (kMarkStagger)
//   M4 Fallstar   in the air, held then released: the ether drained (none: refused), a fall straight down; on the ground the
//                 smash: its damage and radius grow with the height fallen, its shock wave spreading in waves
//   M5 Afterglow  held: the stance, its dial lighting kDialTicks ticks; released full: one line across the front (cut), every
//                 one it crossed marked, all of them bursting together kIaidoDelay (about 1 s) later; released early: nothing
//   R2 Firefly    with a melee arm in hand and Firefly in the ranged slot: the secondary trigger fires one of its 6 rounds
//                 from the hip; the combo's count and window are left as they are (the swing goes on)
//   R3 Septenary  held: an array raised each kArrayEvery while the ether pays kArrayCost (at most 7, the Big Dipper's
//                 shape, ArrayOffset); released: one homing bolt from each at the enemies (the runtime's aim)
// Frames are the game's (60 a second). Velocities are metres a second, places metres (the soldier's +0x6B0 / +0x90).
#pragma once
#include "runeblade_ether.h"
#include <cmath>
#include <cstdint>
#include <cwchar>

namespace crew::runeblade {

// ---- the arms and their rows (the names RB-WEAPONS gives them in tools/zw_weapons.py; the merge aligns them here) ----
enum class Arm : std::uint8_t { none=0,daybreak=1,firmament=2,stars=3,fallstar=4,afterglow=5,firefly=6,septenary=7 };
constexpr std::uint8_t kArmsMost=8;
constexpr int kMeleeCategory=500,kRangedCategory=501;
struct ArmRow { Arm arm; const wchar_t* id; int category; };
constexpr ArmRow kArmRows[]={
    {Arm::daybreak,L"ZW_RuneSaber01",kMeleeCategory},   {Arm::firmament,L"ZW_RuneLance01",kMeleeCategory},
    {Arm::stars,L"ZW_RuneRing01",kMeleeCategory},         {Arm::fallstar,L"ZW_RuneHammer01",kMeleeCategory},
    {Arm::afterglow,L"ZW_RuneIai01",kMeleeCategory}, {Arm::firefly,L"ZW_RuneRevolver01",kRangedCategory},
    {Arm::septenary,L"ZW_RuneStars01",kRangedCategory},
};
inline Arm ArmOf(const wchar_t* id) noexcept {
    if(!id)return Arm::none;
    for(const ArmRow& r:kArmRows)if(!std::wcscmp(r.id,id))return r.arm;
    return Arm::none;
}
constexpr bool IsMelee(Arm a) noexcept { return a>=Arm::daybreak && a<=Arm::afterglow; }

// ---- tuning (frames; multiples of the arm's AmmoDamage, the row's star-levelled number) ----
constexpr int kComboWindow=40,kComboStages=3;
constexpr int kTrailDelay=8,kTrailFrames=30,kTrailEvery=6;               // M1 stage 3: 0.5 s, 5 waves
constexpr float kTrailReach=2.5f,kTrailWidth=5.0f,kTrailRadius=2.5f,kTrailDamage=0.25f;
constexpr float kDiveSpeed=45.0f,kDiveRadius=5.0f,kDiveDamage=1.5f;
constexpr int kDiveMost=150;
constexpr int kLanceMin=20,kLanceFull=60,kLanceFrames=16,kLancePoints=8,kLanceStagger=2;
constexpr float kLanceSlow=40.0f,kLanceFast=75.0f,kLanceRadius=2.5f,kLanceDamage=0.6f,kLanceBurst=4.0f,kLanceBurstDamage=1.0f;
constexpr int kRingHold=20,kRingFrames=90,kRingEvery=3,kMarkStagger=4,kMarksMost=32;
constexpr float kRingReach=12.0f,kRingRadius=2.5f,kRingDamage=0.3f,kMarkRadius=3.0f,kMarkDamage=0.8f;
constexpr int kSmashMin=15,kSmashMost=240,kSmashWaves=3,kSmashEvery=6;
constexpr float kSmashSpeed=80.0f,kSmashPerMetre=0.1f,kSmashMostScale=5.0f,kSmashRadius=6.0f,kSmashRadiusPerMetre=0.2f,
                kSmashMostRadius=14.0f,kSmashGrow=4.0f;
constexpr int kDialTicks=8,kStanceTick=12,kIaidoDelay=60;
constexpr float kIaidoLength=40.0f,kIaidoRadius=3.0f,kIaidoDamage=1.0f,kIaidoBurst=3.5f,kIaidoBurstDamage=2.0f;
constexpr int kFireflyRounds=6,kFireflyCool=6,kFireflyReload=90;
constexpr int kArraysMost=7,kArrayEvery=10;
constexpr float kArrayCost=8.0f;

// ---- vectors ----
struct V3 { float x=0.0f,y=0.0f,z=0.0f; };
inline V3 Add(V3 a,V3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline V3 Mul(V3 a,float k) noexcept { return {a.x*k,a.y*k,a.z*k}; }
inline float Dot(V3 a,V3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float Len(V3 a) noexcept { return std::sqrt(Dot(a,a)); }
inline V3 Unit(V3 a,V3 fallback={0.0f,0.0f,1.0f}) noexcept { const float l=Len(a); return l>1e-5f ? Mul(a,1.0f/l) : fallback; }
// The view (the soldier's smoothed look +0x1240 pitch, negative up, +0x1244 yaw; forward (sin yaw, 0, cos yaw) as
// enemy_control_camera.h CameraForward).
inline V3 LookDir(float pitch,float yaw) noexcept {
    const float c=std::cos(pitch);
    return {std::sin(yaw)*c,-std::sin(pitch),std::cos(yaw)*c};
}
inline V3 Flat(float yaw) noexcept { return {std::sin(yaw),0.0f,std::cos(yaw)}; }
inline V3 Right(float yaw) noexcept { return {std::cos(yaw),0.0f,-std::sin(yaw)}; }
// The nearest point of segment [a, a + dir*length] to p, its distance.
inline float SegmentDistance(V3 p,V3 a,V3 dir,float length) noexcept {
    float t=Dot(Add(p,Mul(a,-1.0f)),dir);
    t=t<0.0f ? 0.0f : t>length ? length : t;
    return Len(Add(p,Mul(Add(a,Mul(dir,t)),-1.0f)));
}

// ---- strikes ----
enum class Effect : std::uint8_t { slash=1,trail=2,dive=3,thrust=4,ring=5,mark=6,smash=7,iaido=8,burst=9,firefly=10,bolt=11 };
constexpr std::uint8_t kEffectsMost=12;
// `points` hits spread evenly along [from, from + dir*length] (1: at from), point i `stagger`*i frames after the wave;
// `waves` waves, `every` frames apart, the first `delay` frames from now, each wave's radius `grow` more than the last.
struct Strike {
    Arm arm=Arm::none;
    Effect effect=Effect::slash;
    V3 from{},dir{0.0f,0.0f,1.0f};
    float length=0.0f,radius=1.0f,grow=0.0f,damage=0.0f;
    std::uint16_t points=1,waves=1,every=0,delay=0,stagger=0;
};
constexpr std::uint16_t kMostPoints=16,kMostWaves=16,kMostEvery=120,kMostDelay=240,kMostStagger=30;
constexpr float kMostLength=200.0f,kMostRadius=60.0f;
inline bool Valid(const Strike& s) noexcept {
    const float dl=Len(s.dir);
    return s.arm!=Arm::none && static_cast<std::uint8_t>(s.arm)<kArmsMost && static_cast<std::uint8_t>(s.effect)>=1 &&
           static_cast<std::uint8_t>(s.effect)<kEffectsMost && std::isfinite(dl) && dl>0.9f && dl<1.1f &&
           s.length>=0.0f && s.length<=kMostLength && s.radius>0.0f && s.radius<=kMostRadius && s.grow>=0.0f &&
           s.grow<=kMostRadius && s.damage>=0.0f && std::isfinite(s.damage) && s.points>=1 && s.points<=kMostPoints &&
           s.waves>=1 && s.waves<=kMostWaves && s.every<=kMostEvery && s.delay<=kMostDelay && s.stagger<=kMostStagger &&
           (s.waves==1 || s.every>0);
}
inline V3 PointOf(const Strike& s,int i) noexcept {
    if(s.points<=1)return s.from;
    return Add(s.from,Mul(s.dir,s.length*static_cast<float>(i)/static_cast<float>(s.points-1)));
}
inline int StrikeFrames(const Strike& s) noexcept {   // frames from now to its last hit
    return s.delay+(s.waves-1)*s.every+(s.points-1)*s.stagger;
}

// The strikes waiting for their frames (each machine's own: the local ones dealt, the peers' shown). Step(): each hit due.
struct Hit { V3 at; float radius,damage; Arm arm; Effect effect; std::uint32_t owner; };
template<int N> struct Queue {
    struct Entry { Strike s; std::uint32_t owner=0; int age=0; bool used=false; };
    Entry e[N];
    bool Push(const Strike& s,std::uint32_t owner) noexcept {
        if(!Valid(s))return false;
        for(Entry& x:e)if(!x.used){x.s=s;x.owner=owner;x.age=0;x.used=true;return true;}
        return false;
    }
    // Calls hit(const Hit&) for each hit of this frame, then ages every entry a frame.
    template<class F> void Step(F&& hit) noexcept {
        for(Entry& x:e) {
            if(!x.used)continue;
            const Strike& s=x.s;
            for(int w=0;w<s.waves;++w)for(int p=0;p<s.points;++p)
                if(s.delay+w*s.every+p*s.stagger==x.age)
                    hit(Hit{PointOf(s,p),s.radius+s.grow*static_cast<float>(w),s.damage,s.arm,s.effect,x.owner});
            if(++x.age>StrikeFrames(s))x.used=false;
        }
    }
    int Pending() const noexcept { int n=0; for(const Entry& x:e)n+=x.used ? 1 : 0; return n; }
    void Clear() noexcept { for(Entry& x:e)x.used=false; }
};

// ---- the ring (M3): out kRingFrames/4, once round `origin` at kRingReach, back to `home` ----
inline V3 RingAt(V3 origin,V3 home,float yaw,int frame) noexcept {
    const float u=static_cast<float>(frame<0 ? 0 : frame>kRingFrames ? kRingFrames : frame)/static_cast<float>(kRingFrames);
    constexpr float kTwoPi=6.28318531f;
    const V3 up{0.0f,1.2f,0.0f};
    if(u<0.25f)return Add(Add(origin,up),Mul(Flat(yaw),kRingReach*u/0.25f));
    if(u<0.75f) {
        const float a=yaw+kTwoPi*(u-0.25f)/0.5f;
        return Add(Add(origin,up),Mul(Flat(a),kRingReach));
    }
    const V3 far=Add(Add(origin,up),Mul(Flat(yaw),kRingReach));
    const float k=(u-0.75f)/0.25f;
    return Add(Mul(far,1.0f-k),Mul(Add(home,up),k));
}

// ---- the arrays (R3): the Big Dipper behind the shoulders, turned with the yaw ----
inline V3 ArrayOffset(int i,float yaw) noexcept {
    constexpr float kDipper[kArraysMost][2]={{-2.4f,0.6f},{-1.6f,0.9f},{-0.8f,0.8f},{0.0f,0.6f},{0.6f,0.2f},{1.6f,0.4f},{1.8f,1.4f}};
    const int k=i<0 ? 0 : i>=kArraysMost ? kArraysMost-1 : i;
    return Add(Add(Mul(Right(yaw),kDipper[k][0]),V3{0.0f,1.8f+kDipper[k][1],0.0f}),Mul(Flat(yaw),-0.8f));
}

// ---- marks (M3, M5): who is to burst, in the order marked, each once ----
struct Marks {
    std::uint64_t id[kMarksMost]{};
    int count=0;
    bool Add(std::uint64_t who) noexcept {
        if(!who || count>=kMarksMost)return false;
        for(int i=0;i<count;++i)if(id[i]==who)return false;
        id[count++]=who;
        return true;
    }
    void Clear() noexcept { count=0; }
};
// The burst of mark `i` at `at`: M3's one after another, M5's all at once.
inline Strike MarkBurst(Arm arm,int i,V3 at,float base) noexcept {
    Strike s;s.arm=arm;s.effect=Effect::burst;s.from=at;
    if(arm==Arm::afterglow){s.radius=kIaidoBurst;s.damage=base*kIaidoBurstDamage;s.delay=kIaidoDelay;}
    else {s.radius=kMarkRadius;s.damage=base*kMarkDamage;s.delay=static_cast<std::uint16_t>(i*kMarkStagger<kMostDelay ? i*kMarkStagger : kMostDelay);}
    return s;
}

// ---- one frame ----
struct Frame {
    Arm inHand=Arm::none;    // the arm of the weapon in hand (none: none of these)
    Arm ranged=Arm::none;    // the ranged slot's arm (R2's interleave)
    bool held=false;         // the primary trigger
    bool second=false;       // the secondary trigger
    bool air=false;
    V3 pos{};
    float pitch=0.0f,yaw=0.0f;
    float base=0.0f;         // the arm in hand's AmmoDamage
    float rangedBase=0.0f;   // the ranged slot weapon's (R2)
    ether::Gauge gauge{};
};
constexpr int kOutStrikes=4;
struct Out {
    Strike strike[kOutStrikes];
    int strikes=0;
    bool velocity=false; V3 vel{};
    float spend=0.0f;        // ether to pay now (already checked against the gauge)
    bool drain=false;        // the whole gauge to take (M4)
    bool refused=false;      // M4 released with no ether
    bool ringThrown=false;   // the ring left this frame (from `ringFrom`, `ringYaw`)
    V3 ringFrom{}; float ringYaw=0.0f;
    bool ringOut=false; V3 ringAt{};   // the ring's place this frame (mark who it cuts)
    bool ringBack=false;     // the ring is back: burst the marks
    bool markLine=false;     // M5's cut: mark every one within strike[0]'s line
    int dial=0;              // M5's lit ticks
    int arrays=0;            // R3's raised arrays
    int salvo=0;             // R3: fire this many bolts (arrays 0..salvo-1)
    bool firefly=false;      // R2: one round
    bool Push(const Strike& s) noexcept { if(strikes>=kOutStrikes || !Valid(s))return false; strike[strikes++]=s; return true; }
};
enum class Phase : std::uint8_t { idle,dive,lanceCharge,lanceDash,ringHold,smashCharge,smashFall,stance,arrays };
struct Machine {
    Phase phase=Phase::idle;
    int frames=0;
    int comboStep=0,comboLeft=0;
    bool held=false,second=false;   // last frame's
    V3 dashDir{}; float dashSpeed=0.0f;
    float startY=0.0f,drained=0.0f;
    int ringFrame=-1; V3 ringFrom{}; float ringYaw=0.0f;
    int rounds=kFireflyRounds,cool=0,reload=0;
    int arrays=0;
    Arm arm=Arm::none;
};

inline void Reset(Machine& m) noexcept { m=Machine{}; }

namespace detail {
inline void Idle(Machine& m) noexcept { m.phase=Phase::idle; m.frames=0; }
inline Strike At(Arm arm,Effect fx,V3 at,float radius,float damage) noexcept {
    Strike s;s.arm=arm;s.effect=fx;s.from=at;s.radius=radius;s.damage=damage;return s;
}
}  // namespace detail

inline Out Step(Machine& m,const Frame& f) noexcept {
    Out o;
    const bool pressed=f.held && !m.held,released=!f.held && m.held,secondPressed=f.second && !m.second;
    m.held=f.held;m.second=f.second;
    if(m.comboLeft>0)--m.comboLeft;
    if(m.comboLeft==0)m.comboStep=0;
    if(m.cool>0)--m.cool;
    if(m.reload>0 && --m.reload==0)m.rounds=kFireflyRounds;
    const V3 look=LookDir(f.pitch,f.yaw);

    // The ring flies on whatever is in hand now.
    if(m.ringFrame>=0) {
        ++m.ringFrame;
        o.ringOut=true;o.ringAt=RingAt(m.ringFrom,f.pos,m.ringYaw,m.ringFrame);
        if(m.ringFrame%kRingEvery==0)o.Push(detail::At(Arm::stars,Effect::ring,o.ringAt,kRingRadius,f.base*kRingDamage));
        if(m.ringFrame>=kRingFrames){o.ringBack=true;m.ringFrame=-1;}
    }
    // A switch of arm ends what the last one was doing (a dash, a fall keep their velocity: the game's own from here).
    if(f.inHand!=m.arm) {
        m.arm=f.inHand;
        if(m.phase!=Phase::smashFall && m.phase!=Phase::dive)detail::Idle(m);
        m.arrays=0;
    }
    // R2 between the swings: the combo untouched.
    if(IsMelee(f.inHand) && f.ranged==Arm::firefly && secondPressed && m.rounds>0 && m.cool==0 && !m.reload) {
        o.firefly=true;--m.rounds;m.cool=kFireflyCool;
        if(!m.rounds)m.reload=kFireflyReload;
    }

    switch(m.phase) {
    case Phase::dive:
        if(!f.air) {
            o.Push(detail::At(Arm::daybreak,Effect::dive,f.pos,kDiveRadius,f.base*kDiveDamage));
            detail::Idle(m);
        } else if(++m.frames>kDiveMost)detail::Idle(m);
        else {o.velocity=true;o.vel={0.0f,-kDiveSpeed,0.0f};}
        return o;
    case Phase::lanceDash:
        if(++m.frames>kLanceFrames) {
            o.Push(detail::At(Arm::firmament,Effect::burst,Add(f.pos,Mul(m.dashDir,1.5f)),kLanceBurst,f.base*kLanceBurstDamage));
            detail::Idle(m);
        } else {o.velocity=true;o.vel=Mul(m.dashDir,m.dashSpeed);}
        return o;
    case Phase::smashFall:
        if(!f.air) {
            const float h=m.startY-f.pos.y>0.0f ? m.startY-f.pos.y : 0.0f;
            float scale=1.0f+h*kSmashPerMetre;if(scale>kSmashMostScale)scale=kSmashMostScale;
            float r=kSmashRadius+h*kSmashRadiusPerMetre;if(r>kSmashMostRadius)r=kSmashMostRadius;
            Strike s=detail::At(Arm::fallstar,Effect::smash,f.pos,r,f.base*scale);
            s.waves=kSmashWaves;s.every=kSmashEvery;s.grow=kSmashGrow;
            o.Push(s);
            detail::Idle(m);
        } else if(++m.frames>kSmashMost)detail::Idle(m);
        else {o.velocity=true;o.vel={0.0f,-kSmashSpeed,0.0f};}
        return o;
    default: break;
    }

    switch(f.inHand) {
    case Arm::daybreak:
        if(!pressed)break;
        if(f.air){m.phase=Phase::dive;m.frames=0;o.velocity=true;o.vel={0.0f,-kDiveSpeed,0.0f};m.comboStep=0;m.comboLeft=0;break;}
        m.comboStep=m.comboStep%kComboStages+1;m.comboLeft=kComboWindow;
        if(m.comboStep==kComboStages) {   // stage 3: its trail stays 0.5 s across the front
            const V3 right=Right(f.yaw),centre=Add(Add(f.pos,V3{0.0f,1.0f,0.0f}),Mul(Flat(f.yaw),kTrailReach));
            Strike s=detail::At(Arm::daybreak,Effect::trail,Add(centre,Mul(right,-kTrailWidth*0.5f)),kTrailRadius,f.base*kTrailDamage);
            s.dir=right;s.length=kTrailWidth;s.points=3;s.waves=kTrailFrames/kTrailEvery;s.every=kTrailEvery;s.delay=kTrailDelay;
            o.Push(s);
        }
        break;
    case Arm::firmament:
        if(m.phase==Phase::idle && pressed){m.phase=Phase::lanceCharge;m.frames=0;}
        if(m.phase!=Phase::lanceCharge)break;
        if(f.held){if(m.frames<kLanceFull)++m.frames;break;}
        if(released && m.frames>=kLanceMin) {
            const float k=static_cast<float>(m.frames-kLanceMin)/static_cast<float>(kLanceFull-kLanceMin);
            m.dashDir=look;m.dashSpeed=kLanceSlow+(kLanceFast-kLanceSlow)*k;
            Strike s=detail::At(Arm::firmament,Effect::thrust,Add(f.pos,V3{0.0f,1.0f,0.0f}),kLanceRadius,f.base*kLanceDamage);
            s.dir=look;s.length=m.dashSpeed*static_cast<float>(kLanceFrames)/60.0f;
            s.points=kLancePoints;s.stagger=kLanceStagger;
            o.Push(s);
            o.velocity=true;o.vel=Mul(look,m.dashSpeed);
            m.phase=Phase::lanceDash;m.frames=0;
        } else detail::Idle(m);
        break;
    case Arm::stars:
        if(m.phase==Phase::idle && pressed){m.phase=Phase::ringHold;m.frames=0;}
        if(m.phase!=Phase::ringHold)break;
        if(f.held){++m.frames;break;}
        if(m.frames>=kRingHold && m.ringFrame<0) {
            m.ringFrame=0;m.ringFrom=f.pos;m.ringYaw=f.yaw;
            o.ringThrown=true;o.ringFrom=f.pos;o.ringYaw=f.yaw;
        }
        detail::Idle(m);
        break;
    case Arm::fallstar:
        if(m.phase==Phase::idle && pressed && f.air){m.phase=Phase::smashCharge;m.frames=0;}
        if(m.phase!=Phase::smashCharge)break;
        if(f.held){++m.frames;break;}
        if(m.frames>=kSmashMin) {
            ether::Gauge g=f.gauge;
            const float took=ether::Drain(g);
            if(took<=0.0f){o.refused=true;detail::Idle(m);break;}
            o.drain=true;m.drained=took;m.startY=f.pos.y;
            m.phase=Phase::smashFall;m.frames=0;
            o.velocity=true;o.vel={0.0f,-kSmashSpeed,0.0f};
        } else detail::Idle(m);
        break;
    case Arm::afterglow:
        if(m.phase==Phase::idle && pressed){m.phase=Phase::stance;m.frames=0;}
        if(m.phase!=Phase::stance)break;
        if(f.held){if(m.frames<kDialTicks*kStanceTick)++m.frames;o.dial=m.frames/kStanceTick;break;}
        if(m.frames/kStanceTick>=kDialTicks) {
            const V3 dir=Flat(f.yaw);
            Strike s=detail::At(Arm::afterglow,Effect::iaido,Add(f.pos,V3{0.0f,1.0f,0.0f}),kIaidoRadius,f.base*kIaidoDamage);
            s.dir=dir;s.length=kIaidoLength;s.points=10;
            o.Push(s);o.markLine=true;
        }
        detail::Idle(m);
        break;
    case Arm::septenary: {
        if(m.phase==Phase::idle && pressed){m.phase=Phase::arrays;m.frames=0;m.arrays=0;}
        if(m.phase!=Phase::arrays)break;
        if(f.held) {
            if(m.arrays<kArraysMost && m.frames%kArrayEvery==0 && ether::CanSpend(f.gauge,kArrayCost)){o.spend=kArrayCost;++m.arrays;}
            ++m.frames;o.arrays=m.arrays;
            break;
        }
        o.salvo=m.arrays;m.arrays=0;
        detail::Idle(m);
        break;
    }
    default: break;
    }
    return o;
}

}  // namespace crew::runeblade
