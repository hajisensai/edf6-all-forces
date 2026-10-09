// Included in proteus.cpp after the Unit table. The Proteus fights with its own three stock weapons only (two cannons,
// the missile launcher; WEAPON/V_407BIGBEGARUTA_CANNON / _MISSILE): no custom rounds. A mount whose seat stands empty is
// borrowed by the first soldier of its chain (proteus_logic.h kMounts / Operator); the stock machinery then fires it:
//  - aim: before VehicleBase slot 5 (0x630250) poses the model, the borrowed mount's aim axes copy its operator seat's
//    (clamped to the mount's own stops) and are applied to the bones (0x5FC280), so the pose, the muzzle matrices the
//    slot copies into the weapon (+0x150) and the shot all share the real barrel;
//  - activation: slot 5 calls, per seat, the weapon's occupied (0x68F8E0) or empty (0x690230) callback; the empty one's
//    LEA at 0x6302B0 is pointed at ProteusEmptyWeapon, which activates a borrowed mount the stock way and, on the
//    operator's own machine while their trigger is held, pulls its holder (0x62C000) as the stock seat input would;
//  - the shooter: the fire step asks the vehicle's interface slot 11 (0x62D950, BigBegaruta's at 0x17DEE68) who works
//    the weapon and fires nothing for nobody; UserHook answers the borrowing soldier (their +0x120, as the stock does).
// A soldier in the mount's own seat always keeps the stock behaviour. The launcher's lock-on and missiles are the
// stock weapon's own (its lock tick runs while it is active and pulled, docs/lockon-re.md §2).
constexpr unsigned kWeaponPostSlot=kVtBig+5*8,kWeaponEmptyLea=0x6302B0;
constexpr unsigned kNativeActive=0x68F8E0,kNativeEmpty=0x690230;
using WeaponPostFn=void(__fastcall*)(void*,const float*);
using WeaponEnableFn=void(__fastcall*)(void*);
void* nextWeaponPost=nullptr;
WeaponEnableFn nativeEmpty=nullptr,nativeActive=nullptr;
bool weaponPhaseReady=false;

// Seat `s`'s first weapon and its holder (nullptr: none).
unsigned char* SeatWeapon(unsigned char* v,unsigned s,unsigned char** holder) noexcept {
    *holder=nullptr;
    if(s>=SeatCount(v))return nullptr;
    unsigned char* const seat=SeatAt(v,s);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!n || n>8 || !Readable(holders,8) || !Readable(holders[0],kHolderWeapon+8))return nullptr;
    unsigned char* const w=At<unsigned char*>(holders[0],kHolderWeapon);
    if(!Readable(w,kSpread+4,true))return nullptr;
    *holder=holders[0];
    return w;
}
bool HolderAlive(const unsigned char* holder) noexcept {
    const auto ctrl=At<const unsigned char*>(holder,kHolderCtrl);
    return ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,8)>0;
}

// The mounts' weapons as the seats hold them now; a new weapon's countdown rate and cone are taken to give back.
void RefreshWeapons(Unit& u,unsigned char* v) noexcept {
    for(unsigned m=0;m<proteus::kMountCount;++m) {
        Arm& a=u.arm[m];
        unsigned char* holder=nullptr;
        unsigned char* const w=SeatWeapon(v,proteus::kMounts[m].seat,&holder);
        if(w!=a.weapon){a.weapon=w;a.rate=w ? At<float>(w,kRate) : 1.0f;a.spread=w ? At<float>(w,kSpread) : 1.0f;}
        a.holder=holder;
    }
}

// The seats holding a living soldier (bit i: seat i).
unsigned Occupied(unsigned char* v) noexcept {
    unsigned bits=0;
    for(unsigned s=0;s<proteus::kSeats && s<SeatCount(v);++s)if(edf::LivingSoldierInSeat(image,SeatAt(v,s)))bits|=1u<<s;
    return bits;
}
int OperatorOf(const Unit& u,unsigned char* v,unsigned m) noexcept {
    if(SeatCount(v)<proteus::kSeats)return -1;
    return proteus::Operator(proteus::kMounts[m],Occupied(v),u.st.mode);
}
// The soldier at seat `s` (its object; nullptr: none).
unsigned char* RiderAt(unsigned char* v,unsigned s) noexcept { return At<unsigned char*>(SeatAt(v,s),kSeatRider); }

// Whether operator seat `op` is pulling borrowed mount `m` now. A seat with its own weapon pulls with that weapon's
// stock trigger latch (+0x139 this frame / +0x13A held: the native input already read its rider, player or NPC);
// the driver, who has none, with the trigger of its seat the mount names (the second one, a pad's LT or the
// ProteusSalvoKey mouse button, for the launcher).
bool OperatorPulls(const Unit& u,unsigned char* v,unsigned m,unsigned op) noexcept {
    if(op!=proteus::kDriver) {
        for(unsigned k=0;k<proteus::kMountCount;++k)
            if(proteus::kMounts[k].seat==op && u.arm[k].weapon)return u.arm[k].weapon[kPull] || u.arm[k].weapon[kHeld];
        return false;
    }
    const unsigned char* const seat=SeatAt(v,proteus::kDriver);
    if(proteus::kMounts[m].driverTrigger==proteus::Trigger::primary)return At<float>(seat,kSeatFire)>=kTriggerOn;
    if(At<unsigned char>(seat,kSeatPad))return At<float>(seat,kSeatFire2)>=kTriggerOn;
    return SeatRider(seat)==Rider::player && KeyHeld(Cfg().proteusSalvoKey);
}
// The operator's own machine pulls (online: where that soldier is run; a copied remote rider fires on its machine).
bool OperatorLocal(unsigned char* v,unsigned op) noexcept {
    const auto rider=RiderAt(v,op);
    return rider && (!InSession() || IsOnlineAuthority(rider));
}

// The mount `weapon` is on a reworked Proteus with its own seat empty: its unit, vehicle, index and operator seat (-1:
// nobody works it now). A mount whose seat holds anyone is the stock's.
struct Borrow { Unit* u; unsigned char* v; unsigned m; int op; };
bool EmptyMount(const void* weapon,Borrow* found) noexcept {
    if(!weapon || !ok || !userOk || !Cfg().enabled || !Cfg().proteus)return false;
    for(auto& u:units) {
        auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(u.ref.obj));
        if(!u.ref || !u.ref.Is(v) || !ActiveOf(v) || v[kDead] || SeatCount(v)<proteus::kSeats)continue;
        for(unsigned m=0;m<proteus::kMountCount;++m) {
            if(u.arm[m].weapon!=weapon)continue;
            if(SeatRider(SeatAt(v,proteus::kMounts[m].seat))!=Rider::none)return false;
            *found=Borrow{&u,v,m,OperatorOf(u,v,m)};return true;
        }
    }
    return false;
}
// ...and borrowed (operated from another seat).
bool Borrowed(const void* weapon,Borrow* found) noexcept {
    return EmptyMount(weapon,found) && proteus::Borrowed(proteus::kMounts[found->m],found->op);
}

// Before the stock slot 5 poses the model: every borrowed mount turns as its operator's seat aims.
void AimBorrowed(Unit& u,unsigned char* v) noexcept {
    for(unsigned m=0;m<proteus::kMountCount;++m) {
        const unsigned seat=proteus::kMounts[m].seat;
        if(SeatRider(SeatAt(v,seat))!=Rider::none)continue;
        const int op=OperatorOf(u,v,m);
        if(!proteus::Borrowed(proteus::kMounts[m],op))continue;
        seataim::Follow(seataim::Object(SeatAt(v,static_cast<unsigned>(op))),seataim::Object(SeatAt(v,seat)),[](unsigned char* axis) noexcept {
            reinterpret_cast<AxisApplyFn>(image+kAxisApply)(axis,true);
        });
    }
}

// The stance's numbers on the cannons (the launcher keeps its own).
void Guns(Unit& u,const Config& c) noexcept {
    const bool deployed=u.st.mode==proteus::Mode::deployed;
    const float rate=deployed ? c.proteusDeployGunRate : c.proteusWalkGunRate,spread=deployed ? c.proteusDeployGunSpread : c.proteusWalkGunSpread;
    for(unsigned m=0;m<proteus::kMountCount;++m) {
        if(proteus::kMounts[m].seat==proteus::kLauncherSeat || !u.arm[m].weapon)continue;
        Put<float>(u.arm[m].weapon,kRate,rate);Put<float>(u.arm[m].weapon,kSpread,spread);
    }
}
void GunsBack(Unit& u) noexcept {
    for(auto& a:u.arm)if(a.weapon && Readable(a.weapon,kSpread+4,true)){Put<float>(a.weapon,kRate,a.rate);Put<float>(a.weapon,kSpread,a.spread);}
}

// The native empty-seat callback (slot 5's LEA at 0x6302B0). Only a borrowed mount is activated / pulled; every other
// weapon of every vehicle gets the stock callback.
void __fastcall ProteusEmptyWeapon(void* weapon) {
    Borrow b{};
    bool borrowed=false;
    __try {
        borrowed=Borrowed(weapon,&b);
        if(borrowed) {
            nativeActive(weapon);
            const auto op=static_cast<unsigned>(b.op);
            const Arm& a=b.u->arm[b.m];
            if(OperatorLocal(b.v,op) && HolderAlive(a.holder) && OperatorPulls(*b.u,b.v,b.m,op))
                reinterpret_cast<PullFn>(image+kPullFn)(a.holder);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){borrowed=false;}
    if(!borrowed && nativeEmpty)nativeEmpty(weapon);
}

void __fastcall ProteusWeaponPost(void* object,const float* step) {
    auto v=static_cast<unsigned char*>(object);
    __try {
        if(ok && SeatCount(v)>=proteus::kSeats)if(Unit* u=ActiveOf(v)){RefreshWeapons(*u,v);AimBorrowed(*u,v);}
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<WeaponPostFn>(nextWeaponPost)(object,step);
}

// The fire step's "who works this weapon" (0x62D950): for an empty mount of a reworked Proteus its borrowing soldier, or
// nobody. Never the stock answer there: for an empty seat it names the seat's LAST rider (+0x300), who may be gone or on
// another machine, and the round would be theirs.
const void* __fastcall UserHook(void* iface,const void* weapon) noexcept {
    if(!nextUser)return nullptr;
    __try {
        Borrow b{};
        if(EmptyMount(weapon,&b)) {
            if(!proteus::Borrowed(proteus::kMounts[b.m],b.op))return nullptr;
            const auto rider=RiderAt(b.v,static_cast<unsigned>(b.op));
            return rider ? rider+kUserIface : nullptr;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return nextUser(iface,weapon);
}

// The fire step refuses a weapon nobody works: without this hook no mount is borrowed (the empty callback stays stock).
bool InstallProteusUser() noexcept {
    userOk=false;
    if(!Matches(kUserSig.rva,kUserSig.bytes,kUserSig.size))return false;
    void* next=nullptr;
    if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+kUserSlotRva),reinterpret_cast<void*>(&UserHook),&next))return false;
    nextUser=reinterpret_cast<UserFn>(next);
    userOk=true;return true;
}

bool InstallProteusWeapons() noexcept {
    if(weaponPhaseReady)return true;
    constexpr unsigned char lea[]={0x48,0x8D,0x05,0x79,0xFF,0x05,0x00};   // lea rax,[0x690230]
    constexpr unsigned char post[]={0x48,0x8B,0xC4,0x53,0x57,0x48,0x81,0xEC,0xD8,0,0,0};
    if(!Matches(kWeaponEmptyLea,lea,sizeof(lea)) || !Matches(0x630250,post,sizeof(post)))return false;
    auto thunk=static_cast<unsigned char*>(AllocateNearThunk(image+kWeaponEmptyLea,reinterpret_cast<void*>(&ProteusEmptyWeapon)));
    if(!thunk)return false;
    unsigned char patched[sizeof(lea)];std::memcpy(patched,lea,sizeof(lea));
    const auto delta=thunk-(image+kWeaponEmptyLea+sizeof(lea));
    if(delta<INT32_MIN || delta>INT32_MAX){VirtualFree(thunk,0,MEM_RELEASE);return false;}
    const auto rel=static_cast<std::int32_t>(delta);std::memcpy(patched+3,&rel,4);
    nativeEmpty=reinterpret_cast<WeaponEnableFn>(image+kNativeEmpty);
    nativeActive=reinterpret_cast<WeaponEnableFn>(image+kNativeActive);
    if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+kWeaponPostSlot),reinterpret_cast<void*>(&ProteusWeaponPost),&nextWeaponPost)) {
        VirtualFree(thunk,0,MEM_RELEASE);return false;
    }
    if(!edf::PatchCode(image+kWeaponEmptyLea,lea,patched,sizeof(lea))) {
        edf::PatchVtableSlot(reinterpret_cast<void**>(image+kWeaponPostSlot),reinterpret_cast<void*>(&ProteusWeaponPost),nextWeaponPost);
        VirtualFree(thunk,0,MEM_RELEASE);return false;
    }
    weaponPhaseReady=true;return true;
}
