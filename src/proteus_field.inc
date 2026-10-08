// Multiple support fields select the strongest value for each effect, once per target-owner frame. Pending native
// multipliers are remembered with their baseline and restored when withdrawn; native-consumed/reset values are left
// alone. Energy/credits/countdown already awarded for elapsed time are not awarded again by another field.
struct FieldParams { float radius,defense,attack,fireRate,energy,power;std::int32_t team; };
FieldParams FieldParameters(const Unit& u) noexcept {
    if(u.net.remote && u.net.haveControl) {
        const auto& s=u.net.control;return {s.fieldRadius,s.fieldDefense,s.fieldAttack,s.fieldFireRate,s.fieldEnergy,s.fieldPower,s.fieldTeam};
    }
    const auto& c=Cfg();return {c.proteusFieldRadius,c.proteusFieldDefense,c.proteusFieldAttack,c.proteusFieldFireRate,c.proteusFieldEnergy,c.proteusFieldPower,player.team};
}
bool FieldOwnedHere(const unsigned char* o) noexcept {
    if(!InSession())return true;
    const auto flags=At<std::uint16_t>(o,0x128);
    return online::LocalCopy(flags) ? IsOnlineAuthority(o) : (flags&1)==0;
}
struct FieldContribution { bool present=false;float dt=0;int points=0;std::int32_t team=0; };
struct FieldTarget {
    ObjRef ref;
    ULONGLONG frame=0;
    FieldContribution sources[kMaxUnits];
    bool human=false,takenWritten=false,dealtWritten=false;
    unsigned soldier=0;
    float takenBase=1,dealtBase=1,takenLast=1,dealtLast=1,extraGiven=0,energyGiven=0;
    int pointsGiven=0;
};
constexpr int kFieldTargets=1024;
FieldTarget fieldTargets[kFieldTargets]{};
bool FieldTargetLive(const FieldTarget& t) noexcept {
    return t.ref && Readable(t.ref.obj,kHpMax+4) && Readable(t.ref.ctrl,12) && At<int>(t.ref.ctrl,8)>0 && t.ref.Is(t.ref.obj);
}
void RestoreFieldMultiplier(unsigned char* o,std::size_t offset,bool& written,float base,float last) noexcept {
    if(written && At<float>(o,offset)==last)Put<float>(o,offset,base);
    written=false;
}
void SetFieldMultiplier(unsigned char* o,std::size_t offset,float factor,bool& written,float& base,float& last) noexcept {
    RestoreFieldMultiplier(o,offset,written,base,last);
    if(factor==1)return;
    base=At<float>(o,offset);last=base*factor;Put<float>(o,offset,last);written=true;
}
bool FieldSourceActive(int index,const unsigned char* o,const FieldContribution& contribution) noexcept {
    if(!Cfg().enabled || !Cfg().proteus)return false;
    const auto& u=units[index];const auto v=static_cast<const unsigned char*>(u.ref.obj);
    if(!u.ref || !Readable(v,kHpMax+4) || !u.ref.Is(v) || v[kDead] || ActiveOf(v)!=&u || u.st.mode!=proteus::Mode::deployed)return false;
    const auto p=FieldParameters(u);
    return contribution.team==p.team && p.radius>0 && vec::Dist(Pos(v),Pos(o))<=p.radius;
}
void RecomputeField(FieldTarget& t) noexcept {
    if(!FieldTargetLive(t))return;
    auto o=const_cast<unsigned char*>(static_cast<const unsigned char*>(t.ref.obj));
    float taken=1,dealt=1,extra=0,energy=0;int points=0;
    if(!o[kDead] && FieldOwnedHere(o))for(int i=0;i<kMaxUnits;++i) {
        const auto& contribution=t.sources[i];
        if(!contribution.present || !FieldSourceActive(i,o,contribution))continue;
        const auto p=FieldParameters(units[i]);
        taken=std::fmin(taken,1-p.defense);dealt=std::fmax(dealt,1+p.attack);
        extra=std::fmax(extra,(p.fireRate-1)*contribution.dt*60);energy=std::fmax(energy,p.energy*contribution.dt);
        points=points>contribution.points ? points : contribution.points;
    }
    SetFieldMultiplier(o,kTakenMul,taken,t.takenWritten,t.takenBase,t.takenLast);
    if(t.human)SetFieldMultiplier(o,kDealtMul,dealt,t.dealtWritten,t.dealtBase,t.dealtLast);
    const float giveExtra=std::fmax(0.0f,extra-t.extraGiven),giveEnergy=std::fmax(0.0f,energy-t.energyGiven);
    const int givePoints=points>t.pointsGiven ? points-t.pointsGiven : 0;
    t.extraGiven=std::fmax(t.extraGiven,extra);t.energyGiven=std::fmax(t.energyGiven,energy);t.pointsGiven=points>t.pointsGiven ? points : t.pointsGiven;
    if(t.human) {
        const auto list=At<unsigned char* const*>(o,kHumanWeapons);const auto n=At<std::uint64_t>(o,kHumanWeaponCount);
        if(n && n<=16 && Readable(list,n*8))for(std::uint64_t i=0;i<n;++i) {
            unsigned char* wp=list[i];if(!Readable(wp,kReloadLeft+4,true))continue;
            if(giveExtra>0)Hurry(wp,giveExtra);
            if(t.soldier==kVtAirRaider && givePoints>0 && At<int>(wp,kReloadType)==kReloadByPoints && At<int>(wp,kReloadLeft)>0)
                Put<int>(wp,kReloadLeft,At<int>(wp,kReloadLeft)-givePoints);
        }
        if(t.soldier==kVtWingDiver && giveEnergy>0) {
            const float most=At<float>(o,kEnergyMax),now=At<float>(o,kEnergy);
            if(std::isfinite(most) && most>0 && std::isfinite(now) && now<most)Put<float>(o,kEnergy,std::fmin(most,now+most*giveEnergy));
        }
    } else if(giveExtra>0) {
        const auto n=At<std::uint64_t>(o,kHolderCount);const auto base=At<unsigned char*>(o,kHolders);
        if(n && n<=16 && Readable(base,n*kHolderStride))for(std::uint64_t i=0;i<n;++i) {
            const auto h=base+i*kHolderStride;if(HolderAlive(h))Hurry(At<unsigned char*>(h,kHolderWeapon),giveExtra);
        }
    }
}
void RefreshFieldWrites() noexcept {
    const auto frame=GameFrame();
    for(auto& t:fieldTargets) {
        if(!t.ref)continue;
        if(!FieldTargetLive(t)){t=FieldTarget{};continue;}
        if(t.frame!=frame) {
            auto o=const_cast<unsigned char*>(static_cast<const unsigned char*>(t.ref.obj));
            RestoreFieldMultiplier(o,kTakenMul,t.takenWritten,t.takenBase,t.takenLast);
            RestoreFieldMultiplier(o,kDealtMul,t.dealtWritten,t.dealtBase,t.dealtLast);
            t=FieldTarget{};
        } else RecomputeField(t);
    }
}
void RecordField(const Field& f,unsigned char* o,bool human,unsigned soldier) noexcept {
    if(!f.source)return;
    const auto source=f.source-units;if(source<0 || source>=kMaxUnits)return;
    FieldTarget* found=nullptr,*free=nullptr;
    for(auto& t:fieldTargets){if(t.ref.Is(o)){found=&t;break;}if(!free && !t.ref)free=&t;}
    if(!found){found=free;if(!found)return;*found=FieldTarget{};found->ref=ObjRef::Of(o);found->frame=GameFrame();found->human=human;found->soldier=soldier;}
    found->sources[source]={true,f.dt,f.points,FieldParameters(*f.source).team};RecomputeField(*found);
}
