// 直升机任务阶段（src/heli_logic.h）的转移表与旧 bool 等价核对，不依赖游戏。
// 1) 每个合法状态 × 每个事件：接受与否、转移后的状态与手写的期望表逐项一致，非法转移状态不变；
// 2) Derive：旧 heli.cpp 能产生的六个 bool 组合逐个推导，读法（Guard / Ferrying / …）与旧 bool 一致，
//    唯一不一致的是「退场中仍在运送」（旧 StartLeave 没清 ferry），新状态归为退场；
// 3) 随机操作序列：旧 bool 模型（照搬旧 heli.cpp 各入口的写法）与新转移逐步对照，返回值和 Derive(旧) 都相同。
#include "../src/heli_logic.h"

#include <cstdio>
#include <cstdint>
#include <initializer_list>

using namespace crew::heliphase;

namespace {

int failures=0,checks=0;
void Check(bool ok,const char* what,int a=-1,int b=-1,int c=-1) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL: %s (%d %d %d)\n",what,a,b,c);}
}
bool Same(const State& a,const State& b) { return a.phase==b.phase && a.duty==b.duty && a.own==b.own; }

constexpr Phase kPhases[]={Phase::follow,Phase::guard,Phase::ferry,Phase::ferryLand,Phase::leaving,Phase::gone};
constexpr Phase kDuties[]={Phase::follow,Phase::guard};
constexpr Event kEvents[]={Event::follow,Event::guard,Event::release,Event::ferry,Event::ferryLand,Event::ferryEnd,
                           Event::leave,Event::reap};

// 手写的期望（与 heli_logic.h 的实现分开写）：返回是否接受，并写出转移后的状态。
bool Expect(const State& s,Event e,State& out) {
    out=s;
    const bool out_=s.phase==Phase::leaving || s.phase==Phase::gone;
    const bool duty=s.phase==Phase::follow || s.phase==Phase::guard;
    switch(e) {
    case Event::follow: if(out_)return false; out.duty=Phase::follow; if(duty)out.phase=Phase::follow; return true;
    case Event::guard: if(out_)return false; out.duty=Phase::guard; if(duty)out.phase=Phase::guard; return true;
    case Event::release: if(out_)return false; out.duty=s.own; if(duty)out.phase=s.own; return true;
    case Event::ferry: if(out_)return false; out.phase=Phase::ferry; return true;
    case Event::ferryLand: if(out_)return false; out.phase=Phase::ferryLand; return true;
    case Event::ferryEnd: if(out_)return false; out.phase=s.duty; return true;
    case Event::leave: if(out_)return false; out.phase=Phase::leaving; return true;
    case Event::reap: if(s.phase!=Phase::leaving)return false; out.phase=Phase::gone; return true;
    }
    return false;
}

void TransitionTable() {
    int valid=0;
    for(Phase p:kPhases)for(Phase d:kDuties)for(Phase o:kDuties) {
        const State s{p,d,o};
        if(!Valid(s))continue;
        ++valid;
        for(Event e:kEvents) {
            State got=s,want{};
            const bool ok=Transition(got,e),expect=Expect(s,e,want);
            Check(ok==expect,"accept matches the table",int(p),int(d),int(e));
            Check(Same(got,want),"state after matches the table",int(p),int(d),int(e));
            if(!ok)Check(Same(got,s),"a rejected transition leaves the state alone",int(p),int(d),int(e));
            Check(Valid(got),"stays valid",int(p),int(d),int(e));
        }
    }
    Check(valid==2*2+4*2*2,"follow/guard (own any) + ferry/ferryLand/leaving/gone (duty any, own any)");
}

void Rejections() {
    State s{};
    Check(Transition(s,Event::leave) && s.phase==Phase::leaving,"leave");
    for(Event e:{Event::follow,Event::guard,Event::release,Event::ferry,Event::ferryLand,Event::ferryEnd,Event::leave})
        Check(!Transition(s,e) && s.phase==Phase::leaving,"nothing but reap out of leaving",int(e));
    Check(Transition(s,Event::reap) && Gone(s),"reap");
    for(Event e:kEvents)Check(!Transition(s,e) && Gone(s),"gone is terminal",int(e));
    State f{};
    Check(!Transition(f,Event::reap) && f.phase==Phase::follow,"reap only from leaving");
    Check(Transition(f,Event::ferryLand) && Transition(f,Event::guard) && f.phase==Phase::ferryLand && Guard(f),
          "a duty order on a ferry changes only the duty");
    Check(Transition(f,Event::ferryEnd) && f.phase==Phase::guard,"the ferry over: back to the new duty");
    Check(Transition(f,Event::ferryEnd) && f.phase==Phase::guard,"ferryEnd with no ferry: accepted, nothing changes");
}

// 旧 heli.cpp 的六个 bool（与各入口的写法逐句对照）。
struct Old {
    bool guard=false,ferry=false,ferryLand=false,leaving=false,reap=false,ownGuard=false;
    bool commanded=false;   // h->cmd.order!=Order::none
};

// 旧代码能产生的组合（Derive 的定义域）：ferryLand 只随 ferry；reap 只在 leaving 之后。
void DeriveCombos() {
    int n=0;
    for(int m=0;m<64;++m) {
        const bool g=m&1,f=m&2,fl=m&4,l=m&8,r=m&16,og=m&32;
        if(fl && !f)continue;
        if(r && !l)continue;
        ++n;
        const State s=Derive(g,f,fl,l,r,og);
        Check(Valid(s),"derived state is valid",m);
        Check(Guard(s)==g,"Guard reads old guard",m);
        Check(Leaving(s)==l,"Leaving reads old leaving",m);
        Check(Gone(s)==r,"Gone reads old reap",m);
        Check((s.own==Phase::guard)==og,"own reads old ownGuard",m);
        // 旧组合 leaving+ferry（运送中受损退场）是唯一被改判的：新状态里运送随退场丢下。
        Check(Ferrying(s)==(f && !l),"Ferrying reads old ferry (but leaving drops it)",m);
        Check(FerryLands(s)==(fl && !l),"FerryLands reads old ferryLand (but leaving drops it)",m);
    }
    Check(n==3*3*2*2,"ferry x land: 3, leaving x reap: 3, guard: 2, ownGuard: 2");
}

std::uint32_t rng=0x1234567u;
std::uint32_t Next() { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }

void Equivalence() {
    for(int run=0;run<2000;++run) {
        Old o{};State s{};
        // 入口：HeliCalled（新实体，guard 或 follow）
        const bool calledGuard=Next()&1;
        o.guard=calledGuard;
        Check(Transition(s,calledGuard ? Event::guard : Event::follow),"called");
        for(int step=0;step<40;++step) {
            const unsigned op=Next()%8;
            bool oldOk=false,newOk=false;
            switch(op) {
            case 0: case 1: case 2: {   // HeliCommand guard / follow / none（Commandable：!leaving && !reap）
                const int order=int(op);   // 0 guard, 1 follow, 2 none
                if(o.leaving || o.reap){oldOk=false;newOk=false;break;}
                oldOk=newOk=true;
                if(!o.commanded)o.ownGuard=o.guard;
                if(!o.commanded)KeepOwn(s);
                if(order==0){o.guard=true;Check(Transition(s,Event::guard),"command guard");}
                else if(order==1){o.guard=false;Check(Transition(s,Event::follow),"command follow");}
                else if(o.commanded){o.guard=o.ownGuard;Check(Transition(s,Event::release),"command release");}
                o.commanded=order!=2;
                break;
            }
            case 3: case 4: {   // HeliFerry(at,land)
                const bool land=op==4;
                if(o.leaving || o.reap)oldOk=false;
                else {o.ferry=true;o.ferryLand=land;oldOk=true;}
                newOk=Transition(s,land ? Event::ferryLand : Event::ferry);
                break;
            }
            case 5:   // HeliFerry(nullptr)
                if(o.leaving || o.reap)oldOk=false;
                else {o.ferry=o.ferryLand=false;oldOk=true;}
                newOk=Transition(s,Event::ferryEnd);
                break;
            case 6:   // HeliStartLeaving（地图撤退指令：清 ferry 后 StartLeave）或 LeaveReason → StartLeave（不清 ferry）
                if(Next()&1) {
                    if(o.reap)oldOk=false;
                    else if(o.leaving)oldOk=true;
                    else {o.ferry=o.ferryLand=false;o.leaving=true;oldOk=true;}
                    newOk=Gone(s) ? false : Leaving(s) ? true : Transition(s,Event::leave);
                } else if(!o.leaving) {   // LeaveReason 只在 !leaving 时
                    o.leaving=true;oldOk=true;
                    newOk=Transition(s,Event::leave);
                } else oldOk=newOk=true;
                break;
            case 7:   // SenseFrame：退场够远 → reap
                if(o.leaving && !o.reap){o.reap=true;oldOk=true;}
                newOk=s.phase==Phase::leaving ? Transition(s,Event::reap) : false;
                if(!oldOk)Check(!newOk,"reap only where the old reap happened",run,step);
                oldOk=newOk;
                break;
            }
            Check(oldOk==newOk,"accepted where the old entry accepted",run,step,int(op));
            Check(Valid(s),"valid after every step",run,step);
            const State d=Derive(o.guard,o.ferry,o.ferryLand,o.leaving,o.reap,o.ownGuard);
            // own 只在第一次被指挥时才有意义；被指挥过就逐项对照，没被指挥过只看 phase / duty。
            Check(s.phase==d.phase && s.duty==d.duty,"new state == Derive(old)",run,step,int(op));
            if(o.commanded)Check(s.own==d.own,"own == Derive(old).own while commanded",run,step);
        }
    }
}

}  // namespace

int main() {
    TransitionTable();
    Rejections();
    DeriveCombos();
    Equivalence();
    if(failures){std::printf("heli_phase_test: %d of %d checks failed\n",failures,checks);return 1;}
    std::printf("heli_phase_test: %d checks passed\n",checks);
    return 0;
}
