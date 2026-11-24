// EDF6AutoTurret's game frame and its world scan, on the real plugin code (autoturret/src/plugin.cpp, designate.cpp) over
// a fake EDF.dll image: the System with its scene step (common/edf/game_frame.h), the lock-target registry and the team
// table (common/edf/world_tables.h).
//  (b) The frame is the scene step: inputs run twice for one vehicle in a step (A, B, A; the exports asking in between)
//      stay one frame and take the world snapshot once; the next step is the next frame; a flak gun's stamp holds through
//      the step after it and no later (GunFuse: "this frame or the last"). Paused steps (the step count goes on, the scene
//      time +0xCE4 does not) are no frames: the player's lock and a stamp outlast the pause menu; a call running two
//      sub-steps (the time added once, for both) is two frames.
//  (a) ScanEnemies through the snapshot's grid gives the linear scan's own list (the same rows, in the same order) and so
//      the same pick, over random worlds (clusters, strays far off, dead and non-finite points), random vehicle matrices
//      (turned, scaled, sheared, singular), teams, pivot heights and ranges (0, boundary, huge, infinite, NaN).
#include "../autoturret/src/plugin.cpp"
#include "../autoturret/src/designate.cpp"
#include <array>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

namespace autoturret {
int HookGunners() noexcept { return 0; }
void ResetGunnerLog() noexcept {}
}

namespace {
using namespace autoturret;

constexpr std::size_t kObjectSize=0x400,kTargetSize=0x40;
struct Node { Node* next; Node* prev; unsigned char* target; };

// The lock registry and its objects: one lock point per object, in registry order.
struct Registry {
    std::vector<Node> nodes;
    std::vector<std::vector<unsigned char>> targets,objects;
    unsigned char registry[0x20]{};
    void Make(int n) {
        nodes.assign(static_cast<std::size_t>(n)+1,Node{});
        targets.assign(static_cast<std::size_t>(n),std::vector<unsigned char>(kTargetSize));
        objects.assign(static_cast<std::size_t>(n),std::vector<unsigned char>(kObjectSize));
        Node* head=&nodes[static_cast<std::size_t>(n)];
        head->next=n ? &nodes[0] : head;
        for(int i=0;i<n;++i) {
            Node& node=nodes[static_cast<std::size_t>(i)];
            node.next=i+1<n ? &nodes[static_cast<std::size_t>(i)+1] : head;
            node.target=targets[static_cast<std::size_t>(i)].data();
            unsigned char* t=node.target;
            Put<void*>(t,kTargetObject,objects[static_cast<std::size_t>(i)].data());
            t[kTargetValid]=t[kTargetLockable]=1;
        }
        Put<void*>(registry,edf::kRegList,head);Put<std::uint64_t>(registry,edf::kRegSize,static_cast<std::uint64_t>(n));
        Put<void*>(image,edf::kLockRegistry,registry);
    }
    unsigned char* Object(int i) { return objects[static_cast<std::size_t>(i)].data(); }
    unsigned char* Target(int i) { return targets[static_cast<std::size_t>(i)].data(); }
    void Set(int i,std::int32_t team,const float* aim,const float* origin) {
        Put<std::int32_t>(Object(i),kTeam,team);
        for(int k=0;k<3;++k){Put<float>(Target(i),kTargetAim+4*k,aim[k]);Put<float>(Object(i),kPosition+4*k,origin[k]);}
    }
};

void Identity(unsigned char* at) {
    float* m=reinterpret_cast<float*>(at);
    for(int i=0;i<16;++i)m[i]=i%5==0 ? 1.0f : 0.0f;
}

// The scan before the grid (plugin.cpp ScanEnemies until 2026-11-24), over the same snapshot.
void LinearScan(const unsigned char* vehicle,float range,std::vector<const Enemy*>& out) {
    out.clear();
    const auto relation=edf::Relations(image,At<std::int32_t>(vehicle,kTeam));
    if(!relation)return;
    for(int i=0;i<worldCount;++i) {
        const Enemy& e=worldEnemies[static_cast<std::size_t>(i)];
        if(e.object==vehicle || relation[e.team]!=kEnemyRelation)continue;
        float local[3];ToLocal(vehicle,e.pos,local);
        if(Dot(local,local)<=range*range)out.push_back(&e);
    }
}
// The nearest in the vehicle's frame, the first of equals (as the gun's pick ranks a list).
const Enemy* Nearest(const unsigned char* vehicle,const Enemy* const* list,int n) {
    const Enemy* best=nullptr;float bestD=0.0f;
    for(int i=0;i<n;++i) {
        float l[3];ToLocal(vehicle,list[i]->pos,l);
        const float d=Dot(l,l);
        if(!best || d<bestD){best=list[i];bestD=d;}
    }
    return best;
}
}  // namespace

int main() {
    int checks=0,failed=0;
    auto check=[&](bool pass,const char* what){++checks;if(!pass){++failed;std::printf("FAIL %s\n",what);}};
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image)return 2;

    // The System: its vtable, its scene and its step count.
    static unsigned char system[gameframe::kSystemStep+0x20]{};
    static unsigned char sceneA[0x40]{},sceneB[0x40]{};
    Put<void*>(image,gameframe::kSystem,system);Put<void*>(system,0,image+gameframe::kSystemVtable);
    Put<void*>(system,gameframe::kSystemScene,sceneA);
    constexpr float kDt=1.0f/60.0f;
    Put<float>(image,gameframe::kStepSeconds,kDt);
    std::uint32_t step=500;
    float run=0.0f;
    // As the game: a running step adds its time (once per call, before the sub-steps), a scene start zeroes both.
    const auto setRun=[&](float r){run=r;Put<float>(system,gameframe::kSystemRunTime,r);};
    const auto setCount=[&](std::uint32_t s){step=s;Put<std::uint32_t>(system,gameframe::kSystemStep,s);};
    const auto setStep=[&](std::uint32_t s) {
        if(s>step)setRun(run+static_cast<float>(s-step)*kDt);
        else if(s<step)setRun(0.0f);
        setCount(s);
    };
    setRun(static_cast<float>(step)*kDt);setCount(step);

    // The team table: 8 teams, a random relation row each (team 0 and 1 always enemies of each other).
    constexpr int kTeamsUsed=8;
    static std::int32_t relation[kMaxTeam][kMaxTeam]{};
    std::vector<unsigned char> rows(edf::kTeamStride*kMaxTeam);
    static unsigned char manager[0x40]{};
    std::mt19937 rng(20261124u);
    for(int t=0;t<kMaxTeam;++t) {
        for(int o=0;o<kMaxTeam;++o)relation[t][o]=t!=o && rng()%3!=0 ? kEnemyRelation : 0;
        Put<const std::int32_t*>(rows.data()+t*edf::kTeamStride,edf::kTeamRelation,relation[t]);
    }
    relation[0][1]=relation[1][0]=kEnemyRelation;
    Put<void*>(image,edf::kTeamManager,manager);Put<void*>(manager,edf::kTeamRows,rows.data());

    // ---- (b) the frame is the scene step ----
    {
        Registry w;w.Make(6);
        for(int i=0;i<6;++i){const float p[3]={10.0f*i,0,50};w.Set(i,i<3 ? 0 : 1,p,p);}
        unsigned char* a=w.Object(0);unsigned char* b=w.Object(3);
        Identity(a+kMatrix);Identity(b+kMatrix);
        Nearby na{},nb{};
        SeeFrame();
        const ULONGLONG f0=Frame();
        const unsigned before=diag.registry;
        // One step: A's input, B's, A's again (its input slot run twice), the exports asking in between.
        SeeFrame();ScanEnemies(a,1000.0f,na);
        check(EDF6AutoTurret_SteersV2(a,0)==false && Frame()==f0,"an export asking in the step is no new frame");
        SeeFrame();ScanEnemies(b,1000.0f,nb);
        SeeFrame();ScanEnemies(a,1000.0f,na);
        SeeFrame();ScanEnemies(a,1000.0f,na);
        check(Frame()==f0,"one vehicle's input twice in one step (A, B, A, A) is one frame, not two");
        check(diag.registry-before==6u,"the world snapshot is taken once in the step, however many scans");
        check(na.count==3 && nb.count==3,"each side sees the other's three");
        setStep(step+1);SeeFrame();
        check(Frame()==f0+1,"the next scene step is the next frame");
        ScanEnemies(a,1000.0f,na);ScanEnemies(b,1000.0f,nb);
        check(diag.registry-before==12u,"the next step takes the snapshot again, once");
        setStep(step+3);SeeFrame();
        check(Frame()==f0+4,"steps no hook of ours ran in still count (a stamp from before them is stale)");
        setStep(step);SeeFrame();
        check(Frame()==f0+4,"the same step seen again is the same frame");

        // GunFuse: a stamp of step N holds in N and N+1 (SpawnHook's \"this frame or the last\"), not in N+2.
        static unsigned char gun[0x10]{},other[0x10]{};
        const std::uint32_t n=step;
        StampGun(gun,7,1);
        check(GunStamp(gun).fuse==7 && GunStamp(gun).team==1,"a gun's stamp holds in its own step");
        SeeFrame();SeeFrame();   // more inputs in the same step
        check(GunStamp(gun).fuse==7,"more inputs in the step do not age the stamp");
        setStep(n+1);SeeFrame();
        check(GunStamp(gun).fuse==7,"a stamp holds in the next step (a round spawned before that step's input)");
        setStep(n+1);SeeFrame();
        check(GunStamp(gun).fuse==7,"the next step's inputs run twice: still the next step");
        setStep(n+2);SeeFrame();
        check(GunStamp(gun).fuse==kNoFuse && GunStamp(gun).team==kNoTeam,"two steps on, the stamp is stale");
        StampGun(gun,9,0);
        check(GunStamp(gun).fuse==9,"a fresh stamp replaces it");
        check(GunStamp(other).fuse==kNoFuse,"a gun never stamped has none");

        // Paused: the step count goes on (0x1198FD0 before the pause test 0x11990C7), the scene time does not.
        {
            setStep(step+1);SeeFrame();
            const ULONGLONG fp=Frame();
            pilot=Pilot{};pilot.vehicle=a;pilot.seenFrame=fp;
            StampGun(gun,5,1);
            for(int i=0;i<300;++i) {   // the pause menu, five seconds; an export asks now and then
                setCount(step+1);
                if(i%7==0){SeeFrame();EDF6AutoTurret_SteersV2(a,0);}
            }
            check(Frame()==fp,"paused steps are no frames");
            check(Fresh(),"the player's lock outlasts the pause menu");
            check(GunStamp(gun).fuse==5,"a stamp outlasts the pause menu");
            setStep(step+1);SeeFrame();
            check(Frame()==fp+1 && Fresh(),"back from the pause: one frame on, the lock still the player's");
            check(GunStamp(gun).fuse==5,"the stamp holds in the step after it, the pause between not counted");
            setStep(step+1);SeeFrame();
            check(GunStamp(gun).fuse==kNoFuse,"two run steps on, the stamp is stale");
            // Paused with no hook or export of ours seeing it: the count jumps, the time only by the steps that ran.
            const ULONGLONG fq=Frame();
            setRun(run+5.0f*kDt);setCount(step+5);   // five steps run
            setCount(step+200);                     // then paused
            setStep(step+1);SeeFrame();             // and back
            check(Frame()==fq+6,"unseen: the steps that ran count, the paused ones do not");
            // One call running two sub-steps: the time of both is added before the first.
            const ULONGLONG fr=Frame();
            setRun(run+2.0f*kDt);
            setCount(step+1);SeeFrame();SeeFrame();
            check(Frame()==fr+1,"the first of two sub-steps: one frame on");
            setCount(step+1);SeeFrame();
            check(Frame()==fr+2,"the second sub-step of the call: one more");
            StampGun(gun,6,1);
            setStep(step+1);SeeFrame();
            check(Frame()==fr+3 && GunStamp(gun).fuse==6,"the next call's step: a stamp of the second sub-step still holds");
            // A long mission: the float time's steps still come out one by one.
            setRun(36000.0f);setCount(step+1);SeeFrame();
            const ULONGLONG fs=Frame();
            for(int i=0;i<600;++i){setStep(step+1);SeeFrame();}
            check(Frame()==fs+600,"ten hours in: 600 steps are 600 frames");
            const float good=run;
            setRun(std::numeric_limits<float>::quiet_NaN());setCount(step+1);SeeFrame();
            check(Frame()==fs+600,"a scene time that is no number: the frame stays");
            setRun(good);
            pilot=Pilot{};
        }

        // A new scene (and its step back at 0): a frame on, never backwards; no System: the frame stays.
        const ULONGLONG f1=Frame();
        Put<void*>(system,gameframe::kSystemScene,sceneB);setStep(0);SeeFrame();
        check(Frame()==f1+1,"a new scene is one frame on, its step reset is not a jump back");
        Put<void*>(image,gameframe::kSystem,nullptr);SeeFrame();SeeFrame();
        check(Frame()==f1+1,"no System: the frame stays");
        Put<void*>(image,gameframe::kSystem,system);
        Put<void*>(system,0,nullptr);SeeFrame();
        check(Frame()==f1+1,"not the System (another vtable): the frame stays");
        Put<void*>(system,0,image+gameframe::kSystemVtable);
        setStep(step+1);SeeFrame();
        check(Frame()==f1+2,"the System back: on from its step");
    }

    // ---- (a) the grid's scan is the linear scan ----
    std::uniform_real_distribution<float> unit(0.0f,1.0f);
    const auto uni=[&](float lo,float hi){return lo+(hi-lo)*unit(rng);};
    int worlds=0,queries=0,gridQueries=0,coverQueries=0,nonEmpty=0;
    long long listed=0;
    for(int round=0;round<12;++round) {
        const int n=round==0 ? 0 : round==1 ? 1 : 200+static_cast<int>(rng()%3000);
        Registry w;w.Make(n);
        std::vector<std::array<float,3>> centers(8);
        for(auto& c:centers)c={uni(-3000,3000),uni(-50,300),uni(-3000,3000)};
        for(int i=0;i<n;++i) {
            float p[3];
            const unsigned kind=rng()%100;
            if(kind<60){const auto& c=centers[rng()%centers.size()];p[0]=c[0]+uni(-60,60);p[1]=c[1]+uni(-30,30);p[2]=c[2]+uni(-60,60);}
            else if(kind<90){p[0]=uni(-4000,4000);p[1]=uni(-100,600);p[2]=uni(-4000,4000);}
            else if(kind<94){p[0]=uni(-2e5f,2e5f);p[1]=uni(-100,100);p[2]=uni(-2e5f,2e5f);}   // strays far off
            else if(kind<96){p[0]=uni(-3e6f,3e6f);p[1]=0;p[2]=uni(-3e6f,3e6f);}             // past kFar
            else if(kind<98 && i>0){std::memcpy(p,w.Target(i-1)+kTargetAim,12);}           // exactly another's point
            else {p[0]=static_cast<float>(64*(static_cast<int>(rng()%40)-20));p[1]=0;p[2]=static_cast<float>(64*(static_cast<int>(rng()%40)-20));}   // on cell edges
            const float origin[3]={p[0],p[1]-1.0f,p[2]};
            w.Set(i,static_cast<std::int32_t>(rng()%kTeamsUsed),p,origin);
            const unsigned odd=rng()%200;
            if(odd==0)w.Object(i)[kDead]=1;
            else if(odd==1)Put<float>(w.Target(i),kTargetAim,std::numeric_limits<float>::quiet_NaN());
            else if(odd==2)Put<float>(w.Object(i),kPosition+4,std::numeric_limits<float>::infinity());
            else if(odd==3)Put<std::int32_t>(w.Object(i),kTeam,kMaxTeam+3);
            else if(odd==4)w.Target(i)[kTargetLockable]=0;
        }
        setStep(step+1);SeeFrame();
        int count=0;World(&count);
        check(!worldGridBuilt,"the snapshot alone builds no grid (the first scan that needs it does)");
        ++worlds;
        for(int q=0;q<400 && n>0;++q) {
            // The scanning vehicle: one of the registry's objects (the scan leaves itself out), its matrix random.
            unsigned char* v=w.Object(static_cast<int>(rng()%static_cast<unsigned>(n)));
            float* m=reinterpret_cast<float*>(v+kMatrix);
            const float qa=uni(-1,1),qb=uni(-1,1),qc=uni(-1,1),qd=uni(-1,1);
            const float qn=std::sqrt(qa*qa+qb*qb+qc*qc+qd*qd)+1e-6f;
            const float x=qa/qn,y=qb/qn,z=qc/qn,s=qd/qn;
            const float r[9]={1-2*(y*y+z*z),2*(x*y+z*s),2*(x*z-y*s),2*(x*y-z*s),1-2*(x*x+z*z),2*(y*z+x*s),2*(x*z+y*s),2*(y*z-x*s),1-2*(x*x+y*y)};
            const unsigned shape=rng()%10;
            for(int row=0;row<3;++row) {
                const float scale=shape<6 ? 1.0f : uni(0.3f,3.0f);
                for(int k=0;k<3;++k)m[row*4+k]=r[row*3+k]*scale;
                m[row*4+3]=0;
            }
            if(shape==8)for(int k=0;k<3;++k)m[8+k]+=0.7f*m[k];   // sheared
            if(shape==9 && rng()%2)for(int k=0;k<3;++k)m[4+k]=0;  // singular: every row tested
            const unsigned where=rng()%10;
            if(where<7){const auto& c=centers[rng()%centers.size()];m[12]=c[0]+uni(-300,300);m[13]=c[1];m[14]=c[2]+uni(-300,300);}
            else if(where<9){m[12]=uni(-5000,5000);m[13]=uni(-100,500);m[14]=uni(-5000,5000);}
            else {m[12]=uni(-3e5f,3e5f);m[13]=0;m[14]=uni(-3e5f,3e5f);}
            m[15]=1;
            Put<std::int32_t>(v,kTeam,static_cast<std::int32_t>(rng()%kTeamsUsed));
            cfg.pivotHeight=uni(-6.0f,6.0f);
            float range;
            const unsigned pick=rng()%20;
            if(pick==0)range=0.0f;
            else if(pick==1)range=1.0e6f;
            else if(pick==2)range=std::numeric_limits<float>::infinity();
            else if(pick==3)range=std::numeric_limits<float>::quiet_NaN();
            else if(pick<8 && count>0) {   // exactly a point's own distance: the <= at the boundary
                float l[3];ToLocal(v,worldEnemies[rng()%static_cast<unsigned>(count)].pos,l);
                range=std::sqrt(Dot(l,l));
            }
            else range=uni(1.0f,2500.0f);
            Nearby got{};
            ScanEnemies(v,range,got);
            std::vector<const Enemy*> want;
            LinearScan(v,range,want);
            bool same=got.count==static_cast<int>(want.size());
            for(int i=0;same && i<got.count;++i)same=got.e[static_cast<std::size_t>(i)]==want[static_cast<std::size_t>(i)];
            if(!same)std::printf("  world %d query %d: grid %d, scan %zu (range %g, pivot %g %g %g)\n",round,q,got.count,want.size(),
                                 static_cast<double>(range),static_cast<double>(m[12]),static_cast<double>(m[13]),static_cast<double>(m[14]));
            check(same,"the grid's scan lists the linear scan's rows, in its order");
            check(Nearest(v,got.e.data(),got.count)==Nearest(v,want.data(),static_cast<int>(want.size())),"the grid's scan picks the linear scan's target");
            ++queries;listed+=got.count;nonEmpty+=got.count>0;
            const float rr=ScanRadius(m,range);
            const bool covers=!(m[12]-rr>worldLo[0] || m[12]+rr<worldHi[0] || m[14]-rr>worldLo[1] || m[14]+rr<worldHi[1]);
            gridQueries+=std::isfinite(rr) && !covers;coverQueries+=std::isfinite(rr) && covers;
        }
    }
    check(gridQueries>queries/2,"most queries went through the grid (not the every-row fallback)");
    check(coverQueries>0,"some bounded queries held every point and went linear (and still matched)");
    check(nonEmpty>queries/3,"most queries found enemies (the comparison is not of empty lists)");
    std::printf("%d checks, %d failed; %d worlds, %d scans (%d through the grid, %d bounded but holding all, %d found any, %lld listed)\n",checks,failed,worlds,
                queries,gridQueries,coverQueries,nonEmpty,listed);
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    return failed ? 1 : 0;
}
