// Included in proteus.cpp after the weapons. The shield is the stock Air Raider's electromagnetic barrier, the wall of
// the 電磁トーチカ (EWEAPON196 and kin): bullet class BarrierBullet01 (vtable 0x17A45E8, ctor 0x28FB00, update slot 5
// 0x2917B0). Static RE, docs/proteus-re.md §10 (H unless said):
//  - It is raised as any round of a DemoIndirectFire (jet_bay.cpp EmcFire with EmcRound::proteusShield, the SGO
//    tools/make_proteus.py writes: BarrierBullet01, speed 0, no gravity, one round, a life that never runs out). Its
//    Ammo_CustomParameter is [arc (rad), radius, height, [sx, sy, sz], [offset]]: the ctor builds a wall of
//    int(arc / 0.0872) segments centred on its local +Z, a 2 m skirt under it, and a collision mesh on layer 14 keyed
//    with its BarrierInfo (team: the IFC's, its owner's +0x314 when fired). The bullets' candidate collector
//    (0x232AA0 -> 0x22E800) stops a round on it only when the round's side is hostile to the barrier's: the
//    Proteus's own and its allies' rounds pass, the enemy's stop and are taken off its HP.
//  - Its HP is +0x14D8 (float, from the round's damage; the hit message handler 0x2913B0 is the only other writer).
//    Slot 5 deletes it the update HP <= 0 or the dead bit (+0xC34 & 1) is seen (0x29189E / 0x2918AB, -> 0x291B73),
//    online first sending a delete unless +0x154C is set (0x291B99); no break effect.
//  - Slot 5 copies the round's world matrix (+0xC90 right, +0xCA0 up, +0xCB0 forward, +0xCC0 position: the bullet core's,
//    nothing in the bullet code rewrites its rotation) to the object (+0x60..), the render matrix and the collision
//    body (interface slot 0x88). So writing +0xC90 before the stock update moves the whole wall with the hull.
//  - Only while it is not stuck: a round that touches something sets +0xD40 and is anchored to it from then on
//    (0x236AC6 / 0x236B4C overwrite the position). One that does is no longer the hull's: it is dropped (a new one is
//    raised while the shield stays on). Its owner is the weak pair at +0x880 / +0x888 (core 0x231FF2).
constexpr unsigned kBarrierVt=0x17A45E8,kBarrierUpdateSlot=kBarrierVt+5*8,kBarrierUpdate=0x2917B0;
constexpr std::size_t kBarrierFlags=0xC34,kBarrierMatrix=0xC90,kBarrierStuck=0xD40,kBarrierHp=0x14D8,kBarrierSegmentsAt=0x14DC,
                      kBarrierNoEcho=0x154C,kBarrierOwner=0x880,kBarrierOwnerCtrl=0x888;
// The wall: tools/make_proteus.py SHIELD_* (arc, radius, height), checked against it by tools/test_proteus_assets.py.
constexpr float kBarrierArcDeg=120.0f,kBarrierRadius=12.0f,kBarrierHeight=17.0f;
constexpr float kBarrierHalfArc=kBarrierArcDeg*0.5f*kPi/180.0f;
constexpr int kBarrierSegmentCount=static_cast<int>(kBarrierArcDeg*kPi/180.0f/0.0872f);   // the ctor's own count (0x2900A7: arc / 0.0872, truncated: 24)
constexpr ULONGLONG kBarrierRaiseFrames=30;   // a raise not seen standing by then failed (logged; no shield this ride)
using BarrierUpdateFn=void(__fastcall*)(void*,const void*);
void* nextBarrierUpdate=nullptr;
bool barrierOk=false;
// Barriers this file raised and still owns, with their owners (a pointer the game reuses for another round is not ours).
struct Raised { unsigned char* obj; const void* owner; const void* ownerCtrl; };
Raised raised[kMaxUnits*2]{};

bool BarrierReady() noexcept { return barrierOk && EmcRoundReady(EmcRound::proteusShield); }

Raised* RaisedOf(const unsigned char* b) noexcept {
    for(auto& r:raised)if(r.obj==b && r.owner==At<const void*>(b,kBarrierOwner) && r.ownerCtrl==At<const void*>(b,kBarrierOwnerCtrl))return &r;
    return nullptr;
}
// Gone the stock way this update: no echo to other machines (a local presentation of the shield, never broadcast).
void KillBarrier(unsigned char* b) noexcept {
    Put<unsigned char>(b,kBarrierNoEcho,1);
    Put<float>(b,kBarrierHp,0.0f);
}

// Where the shield faces: the hull's nose walking; deployed, the driver's horizontal view (this machine's driver;
// a replica takes the driver's from the control state).
void FaceShield(Unit& u,const unsigned char* v,bool driver) noexcept {
    float nose[3]={At<float>(v,kMatrix+32),0.0f,At<float>(v,kMatrix+40)};
    if(driver && proteus::ShieldFollowsView(u.st)) {
        float eye[3],view[3];
        if(CameraRay(eye,view)){view[1]=0.0f;if(vec::Normalize(view))std::memcpy(nose,view,12);}
    }
    if(vec::Normalize(nose))std::memcpy(u.shieldNose,nose,12);
}

// The game thread's per-frame decision (Frame / NetworkFrame): raise a barrier when the shield stands and none does.
// The barrier object itself is touched only from its own update (BarrierHook), where it is certainly alive.
void BarrierFrame(Unit& u,unsigned char* v) noexcept {
    const ULONGLONG frame=GameFrame();
    u.barrier.full=BarrierShare(u)*At<float>(v,kHpMax);
    if(u.barrier.obj && frame-u.barrier.seenFrame>2) {   // deleted without its update telling us (the mission's end, ...)
        for(auto& r:raised)if(r.obj==u.barrier.obj)r=Raised{};
        u.barrier.obj=nullptr;u.barrier.raisedFrame=0;
    }
    const bool want=proteus::ShieldUp(u.st) && BarrierReady() && u.barrier.full>0.0f && !u.barrier.failed;
    if(!want || u.barrier.obj)return;
    if(u.barrier.raisedFrame) {
        if(frame-u.barrier.raisedFrame<=kBarrierRaiseFrames)return;   // fired, its first update not seen yet
        Log("PROTEUS v=%p: the shield's barrier did not appear %llu frames after it was raised: no shield this ride",v,kBarrierRaiseFrames);
        u.st.shieldOn=false;u.barrier.raisedFrame=0;u.barrier.failed=true;
        return;
    }
    const float* p=Pos(v);
    const float at[3]={p[0]+u.shieldNose[0],p[1],p[2]+u.shieldNose[2]};
    if(EmcFire(EmcRound::proteusShield,v,p,at,1.0f).obj)u.barrier.raisedFrame=frame;
    else{Log("PROTEUS v=%p: the shield's barrier could not be raised (EDF6VC_PROTEUS_SHIELD.SGO not preloaded)",v);u.st.shieldOn=false;u.barrier.failed=true;}
}
// The shield is no longer wanted: its barrier (if any) goes on its next update.
void DropBarrier(Unit& u,const char* why) noexcept {
    if(u.barrier.obj || u.barrier.raisedFrame)Log("PROTEUS: the shield's barrier dropped (%s)",why);
    u.barrier.obj=nullptr;u.barrier.raisedFrame=0;
}

// The barrier's matrix: upright, its +Z on the shield's facing, at the hull's feet.
void BarrierMatrix(const Unit& u,const unsigned char* v,float* m) noexcept {
    const float fx=u.shieldNose[0],fz=u.shieldNose[2];
    const float* p=Pos(v);
    const float rows[16]={fz,0.0f,-fx,0.0f, 0.0f,1.0f,0.0f,0.0f, fx,0.0f,fz,0.0f, p[0],p[1],p[2],1.0f};
    std::memcpy(m,rows,sizeof(rows));
}

// Which unit's raise barrier `b` is: a fresh round of ours (its segment count, a pending raise, owned by the
// Proteus or one of its riders: ShellMake's attacker).
Unit* Claimant(const unsigned char* b) noexcept {
    if(At<std::int32_t>(b,kBarrierSegmentsAt)!=kBarrierSegmentCount)return nullptr;
    const void* const owner=At<const void*>(b,kBarrierOwner);
    for(auto& u:units) {
        if(!u.barrier.raisedFrame || u.barrier.obj)continue;
        auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(u.ref.obj));
        if(!u.ref.Is(v))continue;
        bool mine=owner==At<const void*>(v,kSelf);
        for(unsigned s=0;!mine && s<SeatCount(v);++s){const auto r=RiderAt(v,s);mine=r && owner==At<const void*>(r,kSelf);}
        if(mine)return &u;
    }
    return nullptr;
}

// Before the stock update of every BarrierBullet01: ours follows the hull, reports its HP or takes the owner's; one no
// longer wanted (or stuck) goes; an Air Raider's barrier is never touched.
void BarrierStep(unsigned char* b) noexcept {
    Raised* r=RaisedOf(b);
    Unit* u=nullptr;
    if(r) {
        for(auto& x:units)if(x.barrier.obj==b){u=&x;break;}
    } else if((u=Claimant(b))!=nullptr) {
        for(auto& slot:raised)if(!slot.obj){slot={b,At<const void*>(b,kBarrierOwner),At<const void*>(b,kBarrierOwnerCtrl)};r=&slot;break;}
        if(!r){KillBarrier(b);u->barrier.raisedFrame=0;return;}
        u->barrier.obj=b;u->barrier.seenFrame=GameFrame();
        u->barrier.raised=u->st.shield*u->barrier.full;
        Put<float>(b,kBarrierHp,u->barrier.raised);
    } else return;
    auto v=u ? const_cast<unsigned char*>(static_cast<const unsigned char*>(u->ref.obj)) : nullptr;
    const bool keep=u && u->ref.Is(v) && ActiveOf(v)==u && proteus::ShieldUp(u->st) && !At<unsigned char>(b,kBarrierStuck) &&
                    !(At<unsigned char>(b,kBarrierFlags)&1) && u->barrier.full>0.0f;
    if(keep) {
        float hp=At<float>(b,kBarrierHp);
        if(DefenseOwner(v)) {   // the hits settle here: its HP is the shield's
            const float before=u->st.shield;
            proteus::Sense(u->st,hp/u->barrier.full);
            if(u->st.shield!=before)u->net.defenseDirty=true;
        } else {                // a copy: the owner's count, whatever this copy's wall caught locally
            hp=u->st.shield*u->barrier.full;
            if(std::fabs(At<float>(b,kBarrierHp)-hp)>0.5f)Put<float>(b,kBarrierHp,hp);
        }
        if(hp>0.0f && proteus::ShieldUp(u->st)) {
            u->barrier.seenFrame=GameFrame();
            alignas(16) float m[16];BarrierMatrix(*u,v,m);
            std::memcpy(b+kBarrierMatrix,m,sizeof(m));
            return;
        }
    } else if(u && DefenseOwner(v) && At<float>(b,kBarrierHp)<=0.0f)proteus::Sense(u->st,0.0f);   // broken by the last hits
    // Not ours to keep: it goes this update; forgotten.
    KillBarrier(b);
    *r=Raised{};
    if(u && u->barrier.obj==b){u->barrier.obj=nullptr;u->barrier.raisedFrame=0;}
}

void __fastcall BarrierHook(void* bullet,const void* step) {
    __try { BarrierStep(static_cast<unsigned char*>(bullet)); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    reinterpret_cast<BarrierUpdateFn>(nextBarrierUpdate)(bullet,step);
}

bool InstallProteusBarrier() noexcept {
    barrierOk=false;
    const unsigned char prologue[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20};
    const unsigned char hp[]={0x0F,0x2F,0x87,0xD8,0x14,0x00,0x00};               // 0x29189E comiss xmm0,[rdi+0x14D8]
    const unsigned char dead[]={0xF6,0x87,0x34,0x0C,0x00,0x00,0x01};             // 0x2918AB test byte [rdi+0xC34],1
    const unsigned char copy[]={0x0F,0x10,0x87,0x90,0x0C,0x00,0x00};             // 0x2918BE movups xmm0,[rdi+0xC90]
    const unsigned char stuck[]={0x80,0xBF,0x40,0x0D,0x00,0x00,0x00};            // 0x2918F0 cmp byte [rdi+0xD40],0
    const unsigned char echo[]={0x80,0xBF,0x4C,0x15,0x00,0x00,0x00};             // 0x291B99 cmp byte [rdi+0x154C],0
    const unsigned char seed[]={0x41,0x8B,0x87,0x4C,0x0B,0x00,0x00,0x41,0x89,0x87,0xD8,0x14,0x00,0x00};   // 0x28FD62 HP = damage
    if(!Matches(kBarrierUpdate,prologue,sizeof(prologue)) || !Matches(0x29189E,hp,sizeof(hp)) || !Matches(0x2918AB,dead,sizeof(dead)) ||
       !Matches(0x2918BE,copy,sizeof(copy)) || !Matches(0x2918F0,stuck,sizeof(stuck)) || !Matches(0x291B99,echo,sizeof(echo)) ||
       !Matches(0x28FD62,seed,sizeof(seed)) || At<const void*>(image,kBarrierUpdateSlot)!=image+kBarrierUpdate) {
        Log("PROTEUS shield: the barrier's code is not as expected: no shield");
        return false;
    }
    barrierOk=edf::ChainVtableSlot(reinterpret_cast<void**>(image+kBarrierUpdateSlot),reinterpret_cast<void*>(&BarrierHook),&nextBarrierUpdate);
    return barrierOk;
}
