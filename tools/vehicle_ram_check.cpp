// The ram's pure part (src/vehicleram.h) checked without the game, and the damage of representative rams printed
// against the jets' formula (the same ram::Damage; the user, 2026-10-06: "和飞机一样，要看质量和速度的关系"):
//  - the formula is the jets' (playerjet.cpp's old constant 2.87e5 J a point) and goes with m v^2;
//  - each profile's parts are sane (a box, a share in (0, 1], the hull slabs at the hull's front and back, the
//    feet down to the ground), and every class in kProfiles is named once;
//  - the contact: a foot coming down onto an ant under it closes at its whole descent, one passing beside it at
//    nothing; a tank's front slab meets an ant just ahead and not one far ahead, and backing away closes at nothing;
//  - the charge pick: the nearest in ratio (8 -> 8, 12 -> 12, 13 -> 12, 20 -> 16, 35 -> 32, 3 -> 4, 1.4 -> 2), and an
//    install with the four old charges alone falls back to the nearest of those;
//  - the cases' damage at tier 1 and the ini's 1.0: a mech's stomp, a tank at 20 km/h into a giant ant, a bike at
//    100 km/h, ..., next to the jets' rams.
// Exit code 1 when one fails. Built on request only:
// cmake --build build --target vehicle_ram_check && build\vehicle_ram_check.exe
#include "../src/vehicleram.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
int failures=0;

void Expect(bool ok,const char* what,double a=0.0,double b=0.0) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%.4f, %.4f)\n",what,a,b);
}

const ram::Profile* Find(const char* name) {
    for(const auto& p:ram::kProfiles)if(std::strcmp(p.name,name)==0)return &p;
    return nullptr;
}

// The vehicle standing at the origin facing +z (veh+0x60: right, up, forward rows).
constexpr float kIdentity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

void CooldownChecks() {
    struct Hit { int target; std::uint64_t at; };
    constexpr int capacity=64;
    // Nine contacts overflowed the old eight-slot ring on every frame. Sixteen
    // is a full contact scan; neither crowd may get a second hit in this second.
    for(int enemies : {9,16}) {
        Hit hits[capacity]{};
        int counts[16]{};
        for(int frame=0;frame<60;++frame) {
            const auto now=static_cast<std::uint64_t>(frame*1000/60);
            for(int enemy=1;enemy<=enemies;++enemy) {
                const int slot=ram::CooldownSlot(hits,capacity,now,1000,[enemy](int target){return target==enemy;});
                if(slot>=0){hits[slot]=Hit{enemy,now};++counts[enemy-1];}
            }
        }
        for(int enemy=1;enemy<=enemies;++enemy) {
            Expect(counts[enemy-1]==1,"crowd keeps one hit per enemy per second",counts[enemy-1],enemies);
            const int slot=ram::CooldownSlot(hits,capacity,1000,1000,[enemy](int target){return target==enemy;});
            Expect(slot>=0,"enemy becomes due at one second",enemy);
            if(slot>=0)hits[slot]=Hit{enemy,1000};
        }
    }
    Hit full[capacity]{};
    for(int i=0;i<capacity;++i)full[i]=Hit{i+1,100};
    Expect(ram::CooldownSlot(full,capacity,200,1000,[](int target){return target==65;})<0,
           "full cooldown table defers new targets");
    for(int enemy=1;enemy<=capacity;++enemy)
        Expect(ram::CooldownSlot(full,capacity,200,1000,[enemy](int target){return target==enemy;})<0,
               "full table preserves existing cooldowns",enemy);
    Expect(ram::CooldownSlot(full,capacity,1100,1000,[](int target){return target==65;})>=0,
           "expired full table accepts a new target");
}

void FormulaChecks() {
    // The jets' formula as playerjet.cpp had it: 1/2 m v^2 / 2.87e5.
    const float jet=0.5f*16000.0f*100.0f*100.0f/2.87e5f;
    Expect(std::fabs(ram::Damage(16000.0f,100.0f,1.0f,1.0f)-jet)<1e-3f,"the jets' formula",ram::Damage(16000.0f,100.0f,1.0f,1.0f),jet);
    Expect(std::fabs(ram::Damage(16000.0f,200.0f,1.0f,1.0f)/ram::Damage(16000.0f,100.0f,1.0f,1.0f)-4.0f)<1e-4f,"twice the speed, four times");
    Expect(std::fabs(ram::Damage(32000.0f,100.0f,2.0f,0.5f)/ram::Damage(16000.0f,100.0f,1.0f,1.0f)-2.0f)<1e-4f,"mass x tier x scale");
    Expect(ram::Damage(0.0f,100.0f,1.0f,1.0f)==0.0f && ram::Damage(1000.0f,0.0f,1.0f,1.0f)==0.0f &&
           ram::Damage(1000.0f,10.0f,1.0f,0.0f)==0.0f,"no mass, no speed or a 0 scale: nothing");
}

void ProfileChecks() {
    for(int i=0;i<ram::kProfileCount;++i) {
        const ram::Profile& p=ram::kProfiles[i];
        for(int k=i+1;k<ram::kProfileCount;++k)
            Expect(p.vtable!=ram::kProfiles[k].vtable && std::strcmp(p.name,ram::kProfiles[k].name)!=0,"a class named once");
        Expect(p.count>0 && p.count<=ram::kMostParts && p.kg>0.0f && p.durability>0.0f,"a profile's mass, durability, parts",p.kg,p.count);
        for(int k=0;k<p.count;++k) {
            const ram::Part& part=p.parts[k];
            Expect(part.share>0.0f && part.share<=1.0f && part.box.half[0]>0.0f && part.box.half[1]>0.0f && part.box.half[2]>0.0f,
                   "a part's share and box",part.share,part.box.half[0]);
            if(!part.bone) {
                // A hull's slab: from the ground to the top, kSlab into the hull and kSlabOut out past it.
                Expect(std::fabs(part.box.centre[1]-part.box.half[1])<1e-4f,"a slab stands on the ground",part.box.centre[1],part.box.half[1]);
                Expect(std::fabs(part.box.half[2]*2.0f-(ram::kSlab+ram::kSlabOut))<1e-4f,"a slab's depth",part.box.half[2]);
            }
        }
    }
    const ram::Profile* tank=Find("505_Tank");
    Expect(tank && tank->parts[0].box.centre[2]+tank->parts[0].box.half[2]>3.4f && tank->parts[1].box.centre[2]-tank->parts[1].box.half[2]<-3.6f,
           "the Blacker's slabs reach past its front and back");
    // The feet's boxes reach the ground under the bone (a mech's sole is 0.6-0.85 m under ashi_* / foot_*, the Proteus'
    // 2.1 m under heel_*, the Barga's 3.8 m under foot_*).
    const struct { const char* name; float boneUp; } feet[]={{"612_nix",0.62f},{"504_begaruta",0.85f},{"BigBegaruta",2.09f},{"501_FortressRobo",3.81f}};
    for(const auto& f:feet) {
        const ram::Profile* p=Find(f.name);
        Expect(p!=nullptr,"a walker's profile");
        if(!p)continue;
        const ram::Box& b=p->parts[0].box;
        Expect(b.centre[1]-b.half[1]<=-f.boneUp+0.05f && b.centre[1]-b.half[1]>=-f.boneUp-0.6f,"a foot's box reaches its sole",
               b.centre[1]-b.half[1],-f.boneUp);
    }
}

void ContactChecks() {
    float at[3];
    // A Nix foot (ashi_l 0.62 m up) coming down onto an ant whose root is under it and lock point 1.2 m up.
    const ram::Profile* nix=Find("612_nix");
    const ram::Box& foot=nix->parts[0].box;
    const float bone[3]={0.0f,1.8f,0.0f};
    const float centre[3]={bone[0]+foot.centre[0],bone[1]+foot.centre[1],bone[2]+foot.centre[2]};
    const float root[3]={0.0f,0.0f,0.5f},lock[3]={0.0f,1.2f,0.5f};
    const float down[3]={0.0f,-4.0f,0.0f};
    Expect(ram::Touches(kIdentity,centre,foot,2.0f,root,lock,at),"a foot over an ant touches it");
    const float closing=ram::Closing(centre,down,at);
    Expect(closing>3.0f,"a foot coming down closes at nearly its descent",closing,4.0);
    // The same foot 5 m to the side: no contact; one passing beside it horizontally closes at nothing.
    const float rootSide[3]={5.5f,0.0f,0.5f},lockSide[3]={5.5f,1.2f,0.5f};
    Expect(!ram::Touches(kIdentity,centre,foot,2.0f,rootSide,lockSide,at),"an ant 5 m aside is not touched");
    const float side[3]={2.5f,0.0f,0.0f},past[3]={0.0f,0.0f,4.0f};
    Expect(ram::Closing(side,past,side)>0.0f && ram::Closing(centre,past,side)<0.05f,"passing beside it closes at nothing",
           ram::Closing(centre,past,side));
    // The Blacker's front slab against an ant 1.5 m past the front (lock point 1.5 m up), then one 6 m past it.
    const ram::Profile* tank=Find("505_Tank");
    const ram::Box& slab=tank->parts[0].box;
    const float near0[3]={0.0f,0.0f,3.4f+1.5f},near1[3]={0.0f,1.5f,3.4f+1.5f};
    const float far0[3]={0.0f,0.0f,3.4f+6.0f},far1[3]={0.0f,1.5f,3.4f+6.0f};
    Expect(ram::Touches(kIdentity,slab.centre,slab,2.0f,near0,near1,at),"the front slab meets an ant 1.5 m ahead");
    Expect(!ram::Touches(kIdentity,slab.centre,slab,2.0f,far0,far1,at),"...not one 6 m ahead");
    ram::Touches(kIdentity,slab.centre,slab,2.0f,near0,near1,at);
    const float fwd[3]={0.0f,0.0f,20.0f/3.6f},back[3]={0.0f,0.0f,-20.0f/3.6f};
    Expect(std::fabs(ram::Closing(slab.centre,fwd,at)-20.0f/3.6f)<0.6f,"driving into it closes at the tank's speed",
           ram::Closing(slab.centre,fwd,at),20.0/3.6);
    Expect(ram::Closing(slab.centre,back,at)==0.0f,"backing away closes at nothing");
}

void ChargeChecks() {
    const float radii[]={8.0f,16.0f,32.0f,64.0f,2.0f,4.0f,12.0f};   // jet_bay.cpp kCharges' order
    const bool all[]={true,true,true,true,true,true,true},old[]={true,true,true,true,false,false,false};
    const struct { float asked,want,old; } cases[]={
        {8.0f,8.0f,8.0f},{12.0f,12.0f,16.0f},{12.5f,12.0f,16.0f},{13.0f,12.0f,16.0f},{20.0f,16.0f,16.0f},{35.0f,32.0f,32.0f},
        {3.0f,4.0f,8.0f},{1.4f,2.0f,8.0f},{2.6f,2.0f,8.0f},{4.2f,4.0f,8.0f},{5.7f,8.0f,8.0f},{100.0f,64.0f,64.0f},
    };
    std::printf("charge pick (m): asked -> all charges / an install with 8/16/32/64 alone\n");
    for(const auto& c:cases) {
        const int a=ram::NearestCharge(radii,all,7,c.asked),o=ram::NearestCharge(radii,old,7,c.asked);
        std::printf("  %5.1f -> %4.0f / %4.0f\n",c.asked,a>=0 ? radii[a] : -1.0f,o>=0 ? radii[o] : -1.0f);
        Expect(a>=0 && radii[a]==c.want,"the nearest charge",c.asked,a>=0 ? radii[a] : -1.0f);
        Expect(o>=0 && radii[o]==c.old,"the nearest installed one",c.asked,o>=0 ? radii[o] : -1.0f);
    }
    const bool none[]={false,false,false,false,false,false,false};
    Expect(ram::NearestCharge(radii,none,7,8.0f)==-1,"none installed: none");
}

// Damage at tier 1 and the ini's 1.0 of a part of the profile `name` (its share of `kg`, or the class's own mass when 0).
void Case(const char* what,const char* name,int part,float kg,float speedKmh) {
    const ram::Profile* p=Find(name);
    if(!p){Expect(false,"a case's profile");return;}
    const ram::Part& pt=p->parts[part];
    const float mass=(kg>0.0f ? kg : p->kg)*pt.share,v=speedKmh/3.6f;
    const float d=ram::Damage(mass,v,1.0f,1.0f);
    std::printf("  %-44s %8.1f t x %.2f at %6.1f km/h (%5.1f m/s): %9.1f damage, blast %.1f m\n",what,(kg>0.0f ? kg : p->kg)*0.001f,
                pt.share,speedKmh,v,d,ram::BlastRadius(pt.box));
}

void Cases() {
    std::printf("\nrams at tier 1, VehicleRamDamage / PlayerJetRamDamage 1.0 (damage = 1/2 m v^2 / 287 kJ):\n");
    std::printf(" the jets (playerjet.cpp, for comparison):\n");
    const struct { const char* what; float kg,mps,radius; } jets[]={
        {"fighter, clean, 100 m/s",16000.0f,100.0f,8.0f},{"fighter, clean, 200 m/s",16000.0f,200.0f,8.0f},
        {"strike jet, full load (25.4 t), 150 m/s",25400.0f,150.0f,12.0f},{"drone, 150 m/s",2200.0f,150.0f,3.0f},
    };
    for(const auto& j:jets)
        std::printf("  %-44s %8.1f t        at %6.0f km/h (%5.1f m/s): %9.1f damage, blast %.1f m\n",j.what,j.kg*0.001f,j.mps*3.6f,j.mps,
                    ram::Damage(j.kg,j.mps,1.0f,1.0f),j.radius);
    std::printf(" the ground vehicles (the part's own speed):\n");
    Case("Blacker into a giant ant, 20 km/h","505_Tank",0,0.0f,20.0f);
    Case("Blacker, 60 km/h","505_Tank",0,0.0f,60.0f);
    Case("E551 (403), 40 km/h","403_Tank",0,0.0f,40.0f);
    Case("Titan (404), 30 km/h","404_Tank",0,0.0f,30.0f);
    Case("Grape / Striker (Car), 60 km/h","Car",0,0.0f,60.0f);
    Case("Kei truck (Car, 2.4 t), 60 km/h","Car",0,2400.0f,60.0f);
    Case("Freed bike, 100 km/h","503_Bike",0,0.0f,100.0f);
    Case("Maser (510), 20 km/h","510_Maser",0,0.0f,20.0f);
    Case("Begaruta / Nix foot stomp, 4 m/s down","612_nix",0,0.0f,4.0f*3.6f);
    Case("Begaruta / Nix boost dash, foot at 20 m/s","612_nix",0,0.0f,20.0f*3.6f);
    Case("Proteus foot stomp, 5 m/s down","BigBegaruta",0,0.0f,5.0f*3.6f);
    Case("Barga foot stomp, 6 m/s down","501_FortressRobo",0,0.0f,6.0f*3.6f);
    Case("Barga foot stomp, 12 m/s down","501_FortressRobo",0,0.0f,12.0f*3.6f);
    Case("Barga fist, 15 m/s","501_FortressRobo",2,0.0f,15.0f*3.6f);
    Case("Depth Crawler (502), 40 km/h","502_GroundRobo",0,0.0f,40.0f);
}
}  // namespace

int main() {
    CooldownChecks();
    FormulaChecks();
    ProfileChecks();
    ContactChecks();
    ChargeChecks();
    Cases();
    std::printf("\n%s (%d failed)\n",failures ? "FAILED" : "all passed",failures);
    return failures ? 1 : 0;
}
