// VehicleBase::slot5 (630250) poses the model, applies occupied/empty weapon activation, then copies the
// current bones into each weapon matrix. Custom fire must consume the latter, never the slot-4 cache.
constexpr unsigned kWeaponPostSlot=kVtBig+5*8,kWeaponEmptyLea=0x6302B0;
using WeaponPostFn=void(__fastcall*)(void*,const float*);
using WeaponEnableFn=void(__fastcall*)(void*);
void* nextWeaponPost=nullptr;
WeaponEnableFn nativeEmpty=nullptr,nativeActive=nullptr;
void FollowCannon(void* aim) noexcept;

void RefreshWeapons(Unit& u,unsigned char* v) noexcept {
    for(unsigned s=1;s<kProteusSeats;++s) {
        unsigned char* holder=nullptr;
        auto w=SeatWeapon(v,s,&holder);
        if(w!=u.weapon[s]) {
            u.weapon[s]=w;
            u.rate[s]=w ? At<float>(w,kRate) : 1.0f;
            u.spread[s]=w ? At<float>(w,kSpread) : 1.0f;
        }
        u.holder[s]=holder;
    }
}
void CancelWeapons(Unit& u) noexcept {
    u.queuedDriver=ObjRef{};u.queuedFireFrame=0;u.driverFireRequested=false;u.salvoLeft=0;
}
void QueueWeapons(Unit& u,unsigned char* v,bool driver,const unsigned char* seat) noexcept {
    if(!driver || u.net.remote || u.st.mode!=proteus::Mode::deployed || !u.closed ||
       !edf::LivingSoldierInSeat(image,seat) || (RegisteredProteus(v) && !IsOnlineAuthority(v))) {
        CancelWeapons(u);return;
    }
    const auto actor=At<const void*>(seat,kSeatRider);
    if(u.queuedDriver && !u.queuedDriver.Is(actor))u.salvoLeft=0;
    u.queuedDriver=ObjRef::Of(actor);u.queuedFireFrame=GameFrame();
    u.driverFireRequested=At<float>(seat,kSeatFire)>=kTriggerOn;
}

// Replace only the native empty-seat callback, not its whole loop and not seat occupancy. A real rider in
// the right seat therefore keeps the native occupied callback and operator. Replicas never pull a trigger.
void __fastcall ProteusEmptyWeapon(void* weapon) {
    bool pair=false;
    __try {
        if(ok && Cfg().enabled && Cfg().proteus)for(auto& u:units) {
            auto v=const_cast<unsigned char*>(static_cast<const unsigned char*>(u.ref.obj));
            if(!u.ref.Is(v) || !ActiveOf(v) || v[kDead] || !u.closed || SeatCount(v)<kProteusSeats)continue;
            RefreshWeapons(u,v);
            if(weapon!=u.weapon[kRightSeat] || SeatRider(SeatAt(v,kRightSeat))!=Rider::none)continue;
            pair=edf::LivingSoldierInSeat(image,SeatAt(v,kLeftSeat));
            if(pair) {
                nativeActive(weapon);
                if(LocalGunner(v) && u.weapon[kLeftSeat] && HolderAlive(u.holder[kRightSeat]) &&
                   (u.weapon[kLeftSeat][kPull] || u.weapon[kLeftSeat][kHeld]))
                    reinterpret_cast<PullFn>(image+kPullFn)(u.holder[kRightSeat]);
            }
            break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){pair=false;}
    if(!pair && nativeEmpty)nativeEmpty(weapon);
}
void FlushWeapons(unsigned char* v) noexcept {
    auto u=UnitOf(v,false);
    if(!u)return;
    const auto& c=Cfg();
    const auto seat=SeatCount(v) ? SeatAt(v,0) : nullptr;
    if(!ok || !c.enabled || !c.proteus || v[kDead] || !ActiveOf(v) || u->net.remote || !u->closed ||
       u->st.mode!=proteus::Mode::deployed || !seat || SeatRider(seat)!=Rider::player ||
       !edf::LivingSoldierInSeat(image,seat) || !u->queuedDriver.Is(At<const void*>(seat,kSeatRider)) ||
       (RegisteredProteus(v) && !IsOnlineAuthority(v)) || u->queuedFireFrame!=GameFrame()) {
        CancelWeapons(*u);return;
    }
    if(u->flushedFireFrame==GameFrame())return;
    u->flushedFireFrame=GameFrame();
    RefreshWeapons(*u,v);
    if(u->driverFireRequested)DriverGun(*u,v,seat,GameMs(),c);
    Salvo(*u,v,GameMs(),c);
    u->driverFireRequested=false;
}
void __fastcall ProteusWeaponPost(void* object,const float* step) {
    auto v=static_cast<unsigned char*>(object);
    __try {
        if(ok && ActiveOf(v) && SeatCount(v)>=kProteusSeats) {
            FollowCannon(seataim::Object(SeatAt(v,kRightSeat)));
            FollowCannon(seataim::Object(SeatAt(v,kLauncherSeat)));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    reinterpret_cast<WeaponPostFn>(nextWeaponPost)(object,step);
    __try {FlushWeapons(v);} __except(EXCEPTION_EXECUTE_HANDLER){}
}
bool InstallProteusWeapons() noexcept {
    if(weaponPhaseReady)return true;
    constexpr unsigned char lea[]={0x48,0x8D,0x05,0x79,0xFF,0x05,0x00};
    constexpr unsigned char muzzle[]={0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0xA0,0,0,0,0x0F,0x10,0x41,0x10,0x4C,0x8B,0xC2};
    constexpr unsigned char post[]={0x48,0x8B,0xC4,0x53,0x57,0x48,0x81,0xEC,0xD8,0,0,0};
    if(!Matches(kWeaponEmptyLea,lea,sizeof(lea)) || !Matches(kNativeMuzzle,muzzle,sizeof(muzzle)) ||
       !Matches(0x630250,post,sizeof(post)))return false;
    auto thunk=static_cast<unsigned char*>(AllocateNearThunk(image+kWeaponEmptyLea,reinterpret_cast<void*>(&ProteusEmptyWeapon)));
    if(!thunk)return false;
    unsigned char patched[sizeof(lea)];std::memcpy(patched,lea,sizeof(lea));
    const auto delta=thunk-(image+kWeaponEmptyLea+sizeof(lea));
    if(delta<INT32_MIN || delta>INT32_MAX){VirtualFree(thunk,0,MEM_RELEASE);return false;}
    const auto rel=static_cast<std::int32_t>(delta);std::memcpy(patched+3,&rel,4);
    nativeEmpty=reinterpret_cast<WeaponEnableFn>(image+0x690230);
    nativeActive=reinterpret_cast<WeaponEnableFn>(image+0x68F8E0);
    buildMuzzle=reinterpret_cast<BuildMuzzleFn>(image+kNativeMuzzle);
    if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+kWeaponPostSlot),reinterpret_cast<void*>(&ProteusWeaponPost),&nextWeaponPost)) {
        VirtualFree(thunk,0,MEM_RELEASE);return false;
    }
    if(!edf::PatchCode(image+kWeaponEmptyLea,lea,patched,sizeof(lea))) {
        edf::PatchVtableSlot(reinterpret_cast<void**>(image+kWeaponPostSlot),reinterpret_cast<void*>(&ProteusWeaponPost),nextWeaponPost);
        VirtualFree(thunk,0,MEM_RELEASE);return false;
    }
    weaponPhaseReady=true;return true;
}
