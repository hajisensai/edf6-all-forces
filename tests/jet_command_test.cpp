// Map command state and production patrol guidance, against in-memory aircraft only.
#include "../src/jet_internal.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

// Flight code is linked as-is. Only Patrol/Level are exercised; other game services fail closed.
namespace crew {
void MissingDependency() noexcept { void(*volatile stop)()=std::abort;stop(); }
const Config& Cfg() noexcept { MissingDependency();static Config unused{};return unused; }
void Log(const char*,...) noexcept { MissingDependency(); }
void CarrierFlames(const unsigned char*,unsigned char* const*,float,ULONGLONG) noexcept { MissingDependency(); }
void JetFlames(const unsigned char*,float,bool,ULONGLONG) noexcept { MissingDependency(); }
void JetSmoke(const unsigned char*,bool,ULONGLONG) noexcept { MissingDependency(); }
float MapRay(const float*,const float*,float*) noexcept { MissingDependency();return 0; }
PlayArea MapPlayArea() noexcept { MissingDependency();return PlayArea{}; }
float GroundClearance(const float*) noexcept { MissingDependency();return 0; }
float CeilingY() noexcept { MissingDependency();return 0; }
void BodyAttitude(const unsigned char*,const float*,const float*,float,float,float*) noexcept { MissingDependency(); }
unsigned char* BoneRecord506(const unsigned char*,const wchar_t*) noexcept { MissingDependency();return nullptr; }
void NpcGear(unsigned char*,float,float) noexcept { MissingDependency(); }
PlayerFix player{{-5000.0f,0.0f,-5000.0f},1,1};
namespace jet {
Jet jets[kMaxJets]{};
bool Alive(const ObjRef& ref) noexcept {
    // These fixtures own native-shaped buffers; preserve the production weak identity/liveness contract.
    if(!ref.obj || !ref.ctrl || !Readable(ref.ctrl,0x10) || At<long>(ref.ctrl,8)<=0)return false;
    const auto* object=static_cast<const unsigned char*>(ref.obj);
    return Readable(object,kSeats+8) && At<const void*>(object,kSelfCtrl)==ref.ctrl && !(object[kObjFlags]&kObjDeleted);
}
}

}

int main() {
    using namespace crew;
    using namespace crew::jet;
    int checks=0;
    auto check=[&](bool ok,const char* what){++checks;if(!ok){std::printf("FAIL %s\n",what);std::exit(1);}};
    const float anchor[3]={10000.0f,0.0f,10000.0f};
    const Command guard{Order::guard,{10000.0f,0.0f,10000.0f}};
    for(const Role role:{Role::strike,Role::fighter,Role::interceptor,Role::multirole,Role::carrier,
                         Role::drone,Role::blast,Role::doll,Role::gunship}) {
        Jet j{};j.role=role;j.mode=Mode::dive;j.wing=kPatrolRings-1;
        j.t.target=&j;j.t.trackFrame=500;j.t.lockAt=600;j.t.lockSeen=700;j.t.missileAt=800;
        j.carrier.evadeUntil=900;
        ApplyMapCommand(j,guard,1000);
        check(j.cmdMoving && !j.t.target && !j.t.trackFrame && !j.t.lockAt && !j.t.lockSeen &&
              !j.carrier.evadeUntil && j.mode==Mode::patrol && j.modeAt==1000,"new command abandons the old attack");
        check(j.t.missileAt==800,"retargeting preserves weapon cooldowns");
        // A focus order (the map's, on the marked enemy): its order and point kept, the old attack dropped; the next
        // order lets it go.
        alignas(16) unsigned char marked[0x40]{},markedCtrl[16]{};
        Put<void*>(marked,kSelfCtrl,markedCtrl);
        j.t.target=&j;j.mode=Mode::dive;
        ApplyMapFocus(j,ObjRef::Of(marked),1100);
        check(j.focus.Is(marked) && j.cmd.order==Order::guard && j.cmdMoving && !j.t.target && j.mode==Mode::patrol,
              "focus keeps the order and its point, drops the old attack");
        ApplyMapCommand(j,guard,1000);
        check(!j.focus,"a new order lets the focus target go");
        const Kind& k=KindOf(j);
        float pos[3]={0.0f,600.0f,0.0f};
        check(MapCommandMoving(j,pos,anchor,k.range),"distant command must be reached first");
        if(k.flight==FlightModel::wing) {
            float want[3];Patrol(j,pos,anchor,k.alt,want);
            check(want[0]*(anchor[0]-pos[0])+want[2]*(anchor[2]-pos[2])>0.0f,
                  "production patrol guidance heads toward the assigned area");
        }
        const float ring=k.patrol+k.patrolStep*static_cast<float>(j.wing);
        pos[0]=anchor[0]+ring+40.0f;pos[2]=anchor[2];
        check(!MapCommandMoving(j,pos,anchor,k.range),"outer wing slot reaches the arrival boundary");
        pos[0]+=1.0f;
        check(!MapCommandMoving(j,pos,anchor,k.range),"arrival has hysteresis for ordinary combat");
        pos[0]=anchor[0]+10000.0f;
        check(MapCommandMoving(j,pos,anchor,k.range),"far pursuit returns to the commanded area");
        // 2026-11-08 「炮艇机要盘旋一会才能参战」: a gunship has arrived once its targets are in reach (`range` from the
        // anchor, TargetRange), not only on its ring; every other kind still on its ring.
        ApplyMapCommand(j,guard,1000);
        pos[0]=anchor[0]+k.range-1.0f;
        const bool shells=k.weapon==Weapon::shells;
        check(MapCommandMoving(j,pos,anchor,k.range)==(!shells && k.range-1.0f>ring+40.0f),
              "a gunship arrives inside its reach; the others only on their ring");
        if(shells) {
            check(k.range>ring+40.0f,"the gunship's reach lies past its ring (its arrival there is earlier)");
            pos[0]=anchor[0]+k.range+100.0f;
            check(!MapCommandMoving(j,pos,anchor,k.range),"its leash 100 m past its reach: no alternation at the edge");
            pos[0]+=1.0f;
            check(MapCommandMoving(j,pos,anchor,k.range),"past the leash it comes back in");
            const float cannon=1795.0f;   // TargetRange with its cannon (jet_bay.cpp): its arrival grows with it
            ApplyMapCommand(j,guard,1000);pos[0]=anchor[0]+cannon-1.0f;
            check(!MapCommandMoving(j,pos,anchor,cannon),"with the cannon's reach it arrives from further out");
            ApplyMapCommand(j,guard,1000);
            check(MapCommandMoving(j,pos,anchor,k.range),"but not past the reach it is given");
        }
        pos[0]=anchor[0]+10000.0f;
        ApplyMapCommand(j,Command{Order::none,{}},1000);
        check(!j.cmdMoving && !MapCommandMoving(j,pos,anchor,k.range),"release removes travel constraints");
        ApplyMapCommand(j,Command{Order::follow,{}},1000);
        check(MapCommandMoving(j,pos,anchor,k.range),"follow joins a distant leader first");
        check(!MapCommandMoving(j,anchor,anchor,k.range),"follow permits combat after joining");
    }
    // A called jet on its way in (support_aircraft.h SupportEntry, 2026-11-08 「部分援军无法正常跟随」): the dispatcher lets
    // its guard of the call point go on the arrival its own flight control judges, then it follows the player it escorts.
    {
        const float call[3]={10000.0f,0.0f,10000.0f},elsewhere[3]={9000.0f,0.0f,10000.0f};
        Jet j{};j.role=Role::fighter;j.mode=Mode::patrol;j.launched=true;j.escort=true;
        std::memcpy(j.anchor,player.pos,12);   // JetFrame keeps an escort's own anchor on the player
        ApplyMapCommand(j,guard,1000);
        check(SupportEntryOf(j,call)==SupportEntry::flying,"sent in: flying until its own arrival");
        check(CommandAnchor(j,true,j.anchor)==j.cmd.at,"under the call's guard it works round the call point, not the player");
        const Kind& k=KindOf(j);
        float pos[3]={call[0]+k.patrol+20.0f,600.0f,call[2]};   // on its patrol ring, far past any fixed distance like 150 m
        check(!MapCommandMoving(j,pos,call,k.range) && SupportEntryOf(j,call)==SupportEntry::arrived,
              "on its patrol ring its flight control says it is there");
        alignas(16) unsigned char marked[0x40]{},markedCtrl[16]{};
        Put<void*>(marked,kSelfCtrl,markedCtrl);
        ApplyMapFocus(j,ObjRef::Of(marked),1000);
        check(SupportEntryOf(j,call)==SupportEntry::flying,"a focus order the player gave it: at its enemy first");
        j.focus={};
        ApplyMapCommand(j,Command{Order::none,{}},1000);
        check(SupportEntryOf(j,call)==SupportEntry::superseded && CommandAnchor(j,true,j.anchor)==j.anchor &&
              j.anchor[0]==player.pos[0],"let go: it works round the player it escorts");
        ApplyMapCommand(j,Command{Order::guard,{elsewhere[0],elsewhere[1],elsewhere[2]}},1000);
        check(SupportEntryOf(j,call)==SupportEntry::superseded,"the player's own guard elsewhere is never taken back");
        ApplyMapCommand(j,guard,1000);j.cmdMoving=false;j.mode=Mode::withdraw;
        check(SupportEntryOf(j,call)==SupportEntry::gone,"withdrawing: nothing to let go");
        j.mode=Mode::patrol;j.reap=true;
        check(SupportEntryOf(j,call)==SupportEntry::gone,"being deleted: nothing to let go");
        j.reap=false;j.sortie.pass=SortiePass::gunRun;
        check(SupportEntryOf(j,call)==SupportEntry::superseded,"a sortie flies its own one pass");
    }
    for(const Mode mode:{Mode::takeoff,Mode::withdraw,Mode::recover,Mode::bomb}) {
        Jet j{};j.mode=mode;ApplyMapCommand(j,guard,1000);
        check(j.mode==mode,"orders preserve dedicated lifecycle and bombing modes");
    }
    Jet carrierDrone{};carrierDrone.drone.mother=&carrierDrone;carrierDrone.drone.carried=true;
    float farPos[3]={-10000.0f,0.0f,-10000.0f};
    check(!MapCommandMoving(carrierDrone,farPos,anchor,kOrderRange),"uncommanded carrier drone keeps its owner's flight");
    bool moving=true;Command command{Order::guard,{}};
    const float edge[3]={100.0f,99999.0f,0.0f},outer[3]={200.0f,0.0f,0.0f},zero[3]={};
    check(!AirCommandTransit(command,moving,edge,zero,100.0f,200.0f),"arrival uses horizontal distance including equality");
    check(!AirCommandTransit(command,moving,outer,zero,100.0f,200.0f),"leash equality does not trigger a return");
    std::printf("jet_command_test: %d checks passed\n",checks);
    return 0;
}
