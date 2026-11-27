// 战机任务阶段（src/jet_logic.h）的转移表与旧状态等价核对，不依赖游戏。
// 1) 每个阶段 × 每个事件：接受与否、转移后的阶段与手写的期望表一致，非法转移阶段不变；
// 2) Derive：旧 Mode（withdraw / recover / 其它）× ferry 的组合逐个推导（withdraw / recover 压过 ferry）；
// 3) 随机操作序列：旧模型（照搬旧 jet*.cpp 各入口：Withdraw、回母舰、JetFerry、JetFerryDone、GunnerHold、
//    JetHangarRelease）与新转移逐步对照。旧模型里退场 / 回收时 ferry 仍可为真（被盖住、不起作用），新阶段里退场丢下运送；
//    由此产生的差别只有三处，逐一计数核对：退场中 JetFerry 被拒（旧：接受但无效），退场中 JetFerryDone 返回 false
//    （旧：true；调用方 transport.cpp 随后 JetWithdrawNow，本来就在退场），退场被取消后不再接着运送（旧：恢复运送）。
#include "../src/jet_logic.h"

#include <cstdio>
#include <cstdint>

using namespace crew::jetphase;

namespace {

int failures=0,checks=0;
void Check(bool ok,const char* what,int a=-1,int b=-1,int c=-1) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL: %s (%d %d %d)\n",what,a,b,c);}
}

constexpr Phase kPhases[]={Phase::fly,Phase::ferry,Phase::recover,Phase::withdraw};
constexpr Event kEvents[]={Event::ferry,Event::recover,Event::withdraw,Event::resume};

// 手写的期望（与 jet_logic.h 分开写）：[阶段][事件] -> 转移后的阶段，-1 为拒绝。
constexpr int kTable[4][4]={
    //            ferry                  recover                withdraw                resume
    /* fly */     {int(Phase::ferry),    int(Phase::recover),   int(Phase::withdraw),   -1},
    /* ferry */   {int(Phase::ferry),    -1,                    int(Phase::withdraw),   -1},
    /* recover */ {-1,                   -1,                    int(Phase::withdraw),   int(Phase::fly)},
    /* withdraw */{-1,                   -1,                    -1,                     int(Phase::fly)},
};

void TransitionTable() {
    for(Phase p:kPhases)for(Event e:kEvents) {
        Phase got=p;
        const bool ok=Transition(got,e);
        const int want=kTable[int(p)][int(e)];
        Check(ok==(want>=0),"accept matches the table",int(p),int(e));
        Check(int(got)==(want>=0 ? want : int(p)),"phase after matches the table (rejected: unchanged)",int(p),int(e));
    }
    Check(Leaving(Phase::withdraw) && Leaving(Phase::recover) && !Leaving(Phase::fly) && !Leaving(Phase::ferry),"Leaving");
}

void DeriveCombos() {
    for(int m=0;m<3;++m)for(int f=0;f<2;++f) {
        const bool w=m==1,r=m==2,ferry=f!=0;
        const Phase p=Derive(w,r,ferry);
        Check(p==(w ? Phase::withdraw : r ? Phase::recover : ferry ? Phase::ferry : Phase::fly),"Derive",m,f);
        // 旧读法：Guide 先按 Mode 分 withdraw / recover，再看 ferry；SoftEdgeHolds = mode!=withdraw；JetLeaving
        Check((p==Phase::withdraw)==w && (p==Phase::recover)==r && Leaving(p)==(w || r),"withdraw / recover read as before",m,f);
        Check((p==Phase::ferry)==(ferry && !w && !r),"a ferry counts only off the way out (as Guide flew it)",m,f);
    }
}

struct Old {
    int mode=0;          // 0 其它飞法, 1 Mode::withdraw, 2 Mode::recover
    bool ferry=false;
};

std::uint32_t rng=0x9E3779B9u;
std::uint32_t Next() { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }

void Equivalence() {
    int refusedFerry=0,ferryDoneOut=0,resumedNoFerry=0;
    for(int run=0;run<4000;++run) {
        Old o{};Phase p=Phase::fly;
        const bool drone=Next()&1;   // 母舰的无人机会回收，不运送；其它的会运送，不回收（jet.cpp Leave，transport.cpp）
        for(int step=0;step<40;++step) {
            const unsigned op=Next()%5;
            switch(op) {
            case 0: {   // Withdraw（jet_flight.cpp）：旧 if(mode==withdraw)return; SetMode(withdraw)
                const bool oldOk=o.mode!=1;
                if(oldOk)o.mode=1;
                Check(Transition(p,Event::withdraw)==oldOk,"withdraw accepted as before",run,step);
                break;
            }
            case 1: {   // 回母舰（Leave 的回收理由 / RecallDrones）：旧 只在 mode 非 recover / withdraw 时 SetMode(recover)
                if(!drone)break;
                const bool oldOk=o.mode==0;
                if(oldOk)o.mode=2;
                Check(Transition(p,Event::recover)==oldOk,"recover accepted as before",run,step);
                break;
            }
            case 2: {   // JetFerry：旧 置 ferry（不看 Mode）
                if(drone)break;
                const bool leaving=o.mode!=0;
                o.ferry=true;
                const bool ok=Transition(p,Event::ferry);
                Check(ok==!leaving,"ferry accepted off the way out, refused on it",run,step);
                if(leaving)++refusedFerry;
                break;
            }
            case 3: {   // JetFerryDone：旧 要 ferry；新 要 phase==ferry
                const bool oldOk=o.ferry,newOk=p==Phase::ferry;
                Check(newOk==(oldOk && o.mode==0),"ferry done as before but on the way out",run,step);
                if(oldOk && !newOk)++ferryDoneOut;
                break;
            }
            case 4: {   // GunnerHold（只取消 withdraw）/ JetHangarRelease（withdraw / recover 都取消）
                const bool hangar=Next()&1;
                const bool oldOk=hangar ? o.mode!=0 : o.mode==1;
                const bool wasFerry=o.ferry;
                if(oldOk)o.mode=0;
                if(hangar ? Leaving(p) : p==Phase::withdraw)Check(Transition(p,Event::resume),"resume",run,step);
                else Check(!oldOk,"no resume where the old code had none",run,step);
                // 旧：ferry 一直留着，取消退场后又接着运送；新：退场时已丢下运送（之后的对照从新状态接着）。
                if(oldOk && wasFerry){++resumedNoFerry;o.ferry=false;}
                break;
            }
            }
            // 旧状态推导出的阶段 == 新阶段：Derive 让 withdraw / recover 压过 ferry，退场中留下的旧 ferry 不算。
            Check(Derive(o.mode==1,o.mode==2,o.ferry)==p,"phase == Derive(old)",run,step,int(op));
        }
    }
    // 三处差别都真的走到了（不是因为对照没覆盖才没报错），且除此之外处处一致（上面的 Check）。
    Check(refusedFerry>0 && ferryDoneOut>0 && resumedNoFerry>0,"the three documented differences are each reached");
    std::printf("jet_phase_test: differences reached: ferry refused %d, ferry done refused %d, resumed without ferry %d\n",
                refusedFerry,ferryDoneOut,resumedNoFerry);
}

}  // namespace

int main() {
    TransitionTable();
    DeriveCombos();
    Equivalence();
    if(failures){std::printf("jet_phase_test: %d of %d checks failed\n",failures,checks);return 1;}
    std::printf("jet_phase_test: %d checks passed\n",checks);
    return 0;
}
