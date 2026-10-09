// Included in proteus.cpp after ActiveOf. The support piles: slot 45 (0x6437D0) first runs all stock pose work, then
// the piles of the stock model (MK2 pile_l / pile_r, the 407's four Pile_*) are driven down to the ground deployed and
// back up walking, composed through the same native SetWorld routine (0x1100B90). No resource of our own: the stock
// MRAB/CAS are what the game loads. See tests/proteus_visual_native_profile.py for the actual EDF.dll contract.
constexpr unsigned kProteusPoseSlot=0x17DEC40+45*8,kProteusPoseFn=0x6437D0,kSetModelWorld=0x1100B90;
using ProteusPoseFn=void(__fastcall*)(void*);
using SetModelWorldFn=void(__fastcall*)(void*,const void*);
void* nextProteusPose=nullptr;

struct PileSpec { const wchar_t* name; float tip[3]; float retract; };
// Lowest mesh vertices' centroid in the bone's own coordinates, measured from the real Root.cpk MDBs. Retraction is
// the Mk2 pile's measured vertical extent.
const PileSpec kPiles[]={
    {L"pile_l",{.850656107f,-.000388831f,-.000428850f},1.365234375f},
    {L"pile_r",{-.850656107f,.000385731f,.000432903f},1.365234375f},
    {L"Pile_in_l",{.011108620f,-.160031199f,-.002471046f},0.0f},
    {L"Pile_out_l",{-.012075119f,-.160031199f,-.002471046f},0.0f},
    {L"Pile_in_r",{-.011108620f,-.160031199f,-.002471046f},0.0f},
    {L"Pile_out_r",{.012075119f,-.160031199f,-.002471046f},0.0f},
};
constexpr int kPileCount=static_cast<int>(sizeof(kPiles)/sizeof(kPiles[0]));

bool ProteusBind(const unsigned char* inst,const unsigned char* rec,float* bind) noexcept {
    const auto skeleton=At<const unsigned char*>(inst,0);
    const auto bones=At<const unsigned char*>(inst,kInstBones506);
    if(!Readable(skeleton,0x6C) || !bones)return false;
    const auto index=(rec-bones)/static_cast<std::ptrdiff_t>(kBoneStride);
    if(index<0 || index>At<std::int32_t>(skeleton,0x68))return false;
    const auto records=At<const unsigned char*>(skeleton,0x58);
    const auto source=records+static_cast<std::size_t>(index)*0xD0+0x20;
    if(!Readable(source,64))return false;
    std::memcpy(bind,source,64);
    for(int i=0;i<16;++i)if(!std::isfinite(bind[i]))return false;
    return true;
}

// Only a reworked Proteus is posed; the frame the rework gives one back its piles return to the bind pose once (the
// stock pose leaves bones it does not animate where they were), and from then on the stock pose is untouched.
void ProteusModelPose(unsigned char* v) noexcept {
    Unit* const rec=UnitOf(v,false);
    const Unit* u=ActiveOf(v);
    const bool active=u && Cfg().enabled && Cfg().proteus && !v[kDead];
    if(!rec || (!active && !rec->posed))return;
    rec->posed=active;
    auto inst=v+kModelInst506;
    const float deployed=active ? proteus::pose::Deployed(u->st,RulesOf(*u)) : 0.0f;
    unsigned char* piles[kPileCount]{};
    float bind[kPileCount][16]{};
    bool any=false;
    for(int i=0;i<kPileCount;++i) {
        piles[i]=BoneRecord506(inst,kPiles[i].name);
        if(piles[i] && ProteusBind(inst,piles[i],bind[i])){std::memcpy(piles[i]+kBoneLocal506,bind[i],64);any=true;}
        else piles[i]=nullptr;
    }
    if(!any)return;
    auto compose=reinterpret_cast<SetModelWorldFn>(image+kSetModelWorld);
    compose(inst,v+kMatrix); // establish the bind pile frames under this frame's animated heels
    for(int i=0;i<kPileCount;++i) {
        if(!piles[i] || !active)continue;
        const float* world=reinterpret_cast<const float*>(piles[i]+kBoneWorld506);
        float tip[3];proteus::pose::Point(kPiles[i].tip,world,tip);
        const float top[3]={tip[0],tip[1]+2.0f,tip[2]},bottom[3]={tip[0],tip[1]-4.0f,tip[2]};
        float hit[3];
        const float drop=MapRay(top,bottom,hit)>=0.0f ? hit[1]-tip[1] : 0.0f;
        const float delta[3]={0.0f,kPiles[i].retract*(1.0f-deployed)+drop*deployed,0.0f};
        float local[16];
        if(proteus::pose::MoveWorld(bind[i],world,delta,local))std::memcpy(piles[i]+kBoneLocal506,local,64);
    }
    compose(inst,v+kMatrix); // the render palette and native muzzle transforms share this final pose
}

void __fastcall ProteusPoseHook(void* object) {
    reinterpret_cast<ProteusPoseFn>(nextProteusPose)(object);
    __try { ProteusModelPose(static_cast<unsigned char*>(object)); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

bool InstallProteusPose() noexcept {
    const unsigned char code[]={0xC6,0x81,0xB0,0,0,0,1,0x0F,0x28,0x02};
    if(!Matches(kSetModelWorld,code,sizeof(code)) ||
       At<const void*>(image,kProteusPoseSlot)!=image+kProteusPoseFn)return false;
    return edf::ChainVtableSlot(reinterpret_cast<void**>(image+kProteusPoseSlot),reinterpret_cast<void*>(&ProteusPoseHook),&nextProteusPose);
}
