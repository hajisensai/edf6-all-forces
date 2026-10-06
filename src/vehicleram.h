// The ram, the pure part (no game, no Windows: tools/vehicle_ram_check.cpp includes it alone):
//  - the one kinetic-energy damage every ram of the plugin deals: the player's aircraft (playerjet.cpp RamDamage) and the
//    ground vehicles (vehicleram.cpp; the user, 2026-10-06: "载具增加碰撞伤害，和飞机一样，要看质量和速度的关系。这里的速度指的是
//    子部件，比如脚踩");
//  - the impact charge a ram's blast is picked from (jet_bay.cpp ImpactDamage): the one nearest the hitting part's size;
//  - the ground vehicles' parts that hit (a hull's front and back, a mech's feet, the Barga's fists), per vehicle class,
//    and the contact math: a part's box against an enemy's body, the closing speed along the line between them.
// Sizes are the stock models' (Root.cpk, read offline: the bones' bind boxes, docs/vehicle-ram-re.md §2); masses the stock
// SGOs' car_base_all_rigid_body[0] where the class has one, else estimates (L, docs/vehicle-ram-re.md §3).
#pragma once
#include <cmath>
#include <cstdint>

namespace ram {
// Pick only an unused/expired hit slot. Evicting a live cooldown lets a crowded
// contact list hit the same enemies every frame. A full table defers new targets.
template<class Hit,class Matches>
int CooldownSlot(const Hit* hits,int count,std::uint64_t now,std::uint64_t gap,Matches matches) noexcept {
    int free=-1;
    for(int i=0;i<count;++i) {
        const Hit& h=hits[i];
        if(h.target && now-h.at<gap) {
            if(matches(h.target))return -1;
        } else if(free<0)free=i;
    }
    return free;
}

// E = 1/2 m v^2 in the game's damage at kJoulesPerDamage J a point: the mod's Mk 82 (1500 damage, pylib/vcobjects.py
// STORES) carries some 430 MJ of explosive (87 kg of tritonal, ~103 kg of TNT at 4.184 MJ/kg): 2.87e5 J a point. Times
// `tier` (the rammer's max HP over its SGO durability: the factor the game scales a vehicle's weapons by, so the ram
// keeps pace with the mission's difficulty and the request's level) and `scale` (the ini's). 0 without a mass, a
// closing speed, a tier or a scale.
constexpr float kJoulesPerDamage=2.87e5f;
inline float Damage(float kg,float closing,float tier,float scale) noexcept {
    if(!(kg>0.0f) || !(closing>0.0f) || !(tier>0.0f) || !(scale>0.0f))return 0.0f;
    return scale*tier*0.5f*kg*closing*closing/kJoulesPerDamage;
}

// The blast for a part `radius` m in size: of the `ready` charges (radii m), the one nearest in ratio (|ln(charge /
// radius)|, a tie to the smaller), so the blast is the part's size within the ladder's step. The blast follows the
// part's size alone, never the damage (the damage already carries the mass and the speed). -1: none ready.
inline int NearestCharge(const float* radii,const bool* ready,int n,float radius) noexcept {
    const float r=radius>0.01f ? radius : 0.01f;
    int best=-1;
    float bestGap=0.0f;
    for(int i=0;i<n;++i) {
        if(!ready[i] || !(radii[i]>0.0f))continue;
        const float gap=std::fabs(std::log(radii[i]/r));
        const bool tie=best>=0 && std::fabs(gap-bestGap)<1e-5f;
        if(best<0 || (!tie && gap<bestGap) || (tie && radii[i]<radii[best])){best=i;bestGap=gap;}
    }
    return best;
}

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

// A part's box: in the vehicle's axes (x aside, y up, z forward: the rows of veh+0x60), its centre `centre` m from its
// anchor (a bone's origin, or the vehicle's own), `half` m each way.
struct Box { float centre[3],half[3]; };

// The part that hits: the bone it rides (either name: the variants of a class name it differently; none: the hull, the
// vehicle's own origin), its box, the share of the vehicle's mass behind it (a hull: all of it; a mech's foot: its leg
// and the half of the body it carries; a fist: its arm), and its name for the log.
struct Part { const wchar_t* bone; const wchar_t* alt; Box box; float share; const char* name; };
constexpr int kMostParts=4;

// A vehicle class that rams (its vtable RVA, crew.cpp kClasses' name), its mass (kg) and SGO durability as the stock
// SGO of its main model has them, whether it is a CarBase (whose total body mass is read off the vehicle instead:
// vehicleram.cpp kCarMass), and its parts.
struct Profile { unsigned vtable; const char* name; float kg,durability; bool carBase; int count; Part parts[kMostParts]; };

// A hull's front and back slabs from its box (half width `x`, top `top`, front `front` and back `back` along z, m):
// kSlab deep into the hull, kSlabOut out past it. The whole mass is behind each.
constexpr float kSlab=1.0f,kSlabOut=0.5f;
constexpr Part Front(float x,float top,float front) noexcept {
    return Part{nullptr,nullptr,{{0.0f,top*0.5f,front+(kSlabOut-kSlab)*0.5f},{x,top*0.5f,(kSlab+kSlabOut)*0.5f}},1.0f,"front"};
}
constexpr Part Back(float x,float top,float back) noexcept {
    return Part{nullptr,nullptr,{{0.0f,top*0.5f,back-(kSlabOut-kSlab)*0.5f},{x,top*0.5f,(kSlab+kSlabOut)*0.5f}},1.0f,"back"};
}
constexpr Profile Hull(unsigned vt,const char* name,float kg,float durability,bool carBase,float x,float top,float back,float front) noexcept {
    return Profile{vt,name,kg,durability,carBase,2,{Front(x,top,front),Back(x,top,back),{},{}}};
}

// The feet: half the mass behind each (the leg and the body coming down on it).
constexpr float kFootShare=0.5f,kFistShare=0.1f;
// Begaruta / Nix / the old robot: ashi_* (V504, V612) or foot_* (V608) 0.6-0.85 m up over the sole; the heel 0.85 m
// back, the toe 1.85 m forward, the foot 1.4-1.7 m wide.
constexpr Box kMechFoot{{0.0f,-0.25f,0.45f},{0.75f,0.65f,1.4f}};
// Proteus (V614, V407 BigBegaruta): heel_* 2.1 m up, its toe 2.8 m forward of it, 2.8 m wide.
constexpr Box kProteusFoot{{0.0f,-0.6f,1.15f},{1.4f,1.5f,2.65f}};
// The Barga (V515 / V605): foot_* 3.8 m up, the foot 7 m across, its front panel 4.8 m forward; hand_* 5 m across.
constexpr Box kBargaFoot{{0.0f,-1.9f,0.8f},{3.5f,1.95f,4.2f}};
constexpr Box kBargaFist{{0.0f,-0.7f,0.0f},{2.6f,2.7f,2.6f}};
constexpr Profile Walker(unsigned vt,const char* name,float kg,float durability,const wchar_t* l,const wchar_t* lAlt,
                         const wchar_t* r,const wchar_t* rAlt,Box foot) noexcept {
    return Profile{vt,name,kg,durability,false,2,{{l,lAlt,foot,kFootShare,"left foot"},{r,rAlt,foot,kFootShare,"right foot"},{},{}}};
}

// Hulls: the stock model's body without its turret, guns and antennas (x half width, top, back, front; m).
// Walkers: their feet (begaruta_foot_node / foot_name in the SGO), the Barga its fists too.
inline constexpr Profile kProfiles[]={
    Hull(0x17D8B50,"402_Rocket",27000.0f,300.0f,true,1.4f,2.8f,-3.9f,3.8f),
    Hull(0x17D8FA0,"403_Tank",24600.0f,1200.0f,true,2.0f,2.6f,-2.9f,4.7f),
    Hull(0x17D9458,"404_Tank",227500.0f,4800.0f,true,5.7f,4.2f,-13.0f,11.9f),
    Hull(0x17DA508,"503_Bike",200.0f,150.0f,true,0.4f,1.3f,-1.1f,2.5f),
    Hull(0x17DADB0,"505_Tank",17060.0f,1000.0f,true,1.6f,2.6f,-3.6f,3.4f),
    Hull(0x17DB9D8,"510_Maser",35000.0f,1000.0f,true,5.9f,4.7f,-9.7f,10.0f),
    Hull(0x17DBDF8,"511_Bike",200.0f,150.0f,true,0.4f,1.3f,-1.1f,2.5f),
    Hull(0x17DC250,"601_Tank",17060.0f,1000.0f,true,1.8f,2.7f,-3.5f,4.0f),
    Hull(0x17DC620,"603_Flak",17060.0f,350.0f,true,1.5f,3.8f,-3.3f,3.1f),
    Hull(0x17E01B0,"Car",17000.0f,650.0f,true,1.2f,2.9f,-3.0f,3.7f),
    Hull(0x17DA028,"502_GroundRobo",10000.0f,1000.0f,false,1.4f,3.0f,-3.9f,3.9f),
    Walker(0x17DA960,"504_begaruta",30000.0f,1200.0f,L"ashi_l",L"foot_l",L"ashi_r",L"foot_r",kMechFoot),
    Walker(0x17DE0A8,"Begaruta",30000.0f,1200.0f,L"ashi_l",L"foot_l",L"ashi_r",L"foot_r",kMechFoot),
    Walker(0x17DD440,"612_nix",30000.0f,1200.0f,L"ashi_l",L"foot_l",L"ashi_r",L"foot_r",kMechFoot),
    Walker(0x17DEC40,"BigBegaruta",150000.0f,7500.0f,L"heel_l",nullptr,L"heel_r",nullptr,kProteusFoot),
    Profile{0x17D98C8,"501_FortressRobo",2.0e6f,50000.0f,false,4,
            {{L"foot_l",nullptr,kBargaFoot,kFootShare,"left foot"},{L"foot_r",nullptr,kBargaFoot,kFootShare,"right foot"},
             {L"hand_l",nullptr,kBargaFist,kFistShare,"left fist"},{L"hand_r",nullptr,kBargaFist,kFistShare,"right fist"}}},
};
constexpr int kProfileCount=static_cast<int>(sizeof(kProfiles)/sizeof(kProfiles[0]));

// The blast's radius for a part: its footprint's larger half (the part's size; a hull's slab: the hull's half width).
inline float BlastRadius(const Box& b) noexcept { return b.half[0]>b.half[2] ? b.half[0] : b.half[2]; }

// An enemy's body (the segment from its root to its lock point, world) against a part's box at `centre` (world) in the
// vehicle's axes `m` (veh+0x60's rows), grown `pad` each way (a lock point sits inside the body, the body reaches past it).
// True when it is in the box; `at` gets the body's point nearest the part's centre (where the blast goes).
inline bool Touches(const float* m,const float* centre,const Box& box,float pad,const float* root,const float* lock,float* at) noexcept {
    float a[3],b[3];
    const float ra[3]={root[0]-centre[0],root[1]-centre[1],root[2]-centre[2]};
    const float rb[3]={lock[0]-centre[0],lock[1]-centre[1],lock[2]-centre[2]};
    for(int i=0;i<3;++i){a[i]=Dot(ra,m+4*i);b[i]=Dot(rb,m+4*i);}
    float t0=0.0f,t1=1.0f;
    for(int i=0;i<3;++i) {
        const float lo=-box.half[i]-pad,hi=box.half[i]+pad,d=b[i]-a[i];
        if(std::fabs(d)<1e-6f) {
            if(a[i]<lo || a[i]>hi)return false;
            continue;
        }
        const float u=(lo-a[i])/d,w=(hi-a[i])/d;
        t0=std::fmax(t0,std::fmin(u,w));t1=std::fmin(t1,std::fmax(u,w));
        if(t0>t1)return false;
    }
    // The body's point nearest the centre: the segment's parameter of the centre's projection, clamped onto the segment.
    const float d[3]={rb[0]-ra[0],rb[1]-ra[1],rb[2]-ra[2]};
    const float dd=Dot(d,d);
    const float t=dd>1e-8f ? std::fmin(1.0f,std::fmax(0.0f,-Dot(ra,d)/dd)) : 0.0f;
    for(int i=0;i<3;++i)at[i]=root[i]+d[i]*t;
    return true;
}

// How far `centre` is from the body (the segment root..lock): the miss log's measure.
inline float Gap(const float* centre,const float* root,const float* lock) noexcept {
    const float ra[3]={root[0]-centre[0],root[1]-centre[1],root[2]-centre[2]};
    const float d[3]={lock[0]-root[0],lock[1]-root[1],lock[2]-root[2]};
    const float dd=Dot(d,d);
    const float t=dd>1e-8f ? std::fmin(1.0f,std::fmax(0.0f,-Dot(ra,d)/dd)) : 0.0f;
    const float g[3]={ra[0]+d[0]*t,ra[1]+d[1]*t,ra[2]+d[2]*t};
    return std::sqrt(Dot(g,g));
}

// The closing speed of a part moving at `vel` (m/s, world) onto `at`, from its centre `centre`: its velocity along the
// unit from the centre to `at` (what it drives into it; 0 moving away). `at` at the centre (the body inside the part):
// its whole speed (any way it moves is into it).
inline float Closing(const float* centre,const float* vel,const float* at) noexcept {
    float n[3]={at[0]-centre[0],at[1]-centre[1],at[2]-centre[2]};
    const float len=std::sqrt(Dot(n,n));
    const float c=len>1e-3f ? Dot(vel,n)/len : std::sqrt(Dot(vel,vel));
    return c>0.0f ? c : 0.0f;
}
}  // namespace ram
