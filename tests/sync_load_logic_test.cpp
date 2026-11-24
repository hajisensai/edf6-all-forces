// src/sync_load_logic.h: the sync load probe's call pairing, unwinds included, and the STATS interval.
#include "../src/sync_load_logic.h"

#include <cstdio>

using crew::syncload::Call;
using crew::syncload::CallStack;
using crew::syncload::Totals;

namespace {
int failures=0;
void Check(bool ok,const char* what) {
    if(!ok){std::printf("FAIL %s\n",what);++failures;}
}
// Frames: the stack grows down, a nested invocation's frame is below its caller's.
unsigned char stackBytes[4096];
const void* At(unsigned offset) { return stackBytes+offset; }
}  // namespace

int main() {
    {   // one invocation: begin, the later sites find it, it ends and the stack is empty
        CallStack<4> s;
        Check(s.Begin(At(1000),5)!=nullptr,"begin");
        Check(s.Mine(At(1000))!=nullptr && s.Mine(At(900))==nullptr,"mine by frame");
        Call c{};
        Check(s.End(At(1000),c) && c.begin==5 && s.depth==0,"end");
    }
    {   // nested: the inner ends first, then the outer
        CallStack<4> s;
        s.Begin(At(1000),1);s.Begin(At(800),2);
        Call c{};
        Check(s.End(At(800),c) && c.begin==2 && s.depth==1,"inner end");
        Check(s.End(At(1000),c) && c.begin==1 && s.depth==0,"outer end");
    }
    {   // an exception unwound the invocation at 1000 (no exit); the next one at the same depth reuses the frame. Before
        // the fix it stacked on the stale entry and the stack never reached 0 again (no more lines).
        CallStack<4> s;
        s.Begin(At(1000),1);
        Check(s.Begin(At(1000),2)!=nullptr && s.depth==1,"re-entry at the same frame drops the unwound one");
        Call c{};
        Check(s.End(At(1000),c) && c.begin==2 && s.depth==0,"the re-entered one ends at depth 0");
    }
    {   // unwound outer and inner, then a call from higher up the stack (a shallower caller): both stale entries go
        CallStack<4> s;
        s.Begin(At(1000),1);s.Begin(At(800),2);
        Check(s.Begin(At(1200),3)!=nullptr && s.depth==1,"a shallower call drops every unwound entry");
        Call c{};
        Check(s.End(At(1200),c) && c.begin==3 && s.depth==0,"and ends at depth 0");
    }
    {   // unwound inner only: the outer's exit still pairs (the inner below it is dropped)
        CallStack<4> s;
        s.Begin(At(1000),1);s.Begin(At(800),2);
        Call c{};
        Check(s.End(At(1000),c) && c.begin==1 && s.depth==0,"the outer's exit drops the unwound inner");
    }
    {   // an unwound inner, then a new inner at a lower frame while the outer runs: kept nested
        CallStack<4> s;
        s.Begin(At(1000),1);s.Begin(At(800),2);
        Check(s.Begin(At(700),3)!=nullptr && s.depth==3,"a deeper call keeps its callers");
        s.Begin(At(800),4);   // a sibling at the unwound inner's frame replaces it (and the one below it)
        Check(s.depth==2 && s.calls[1].begin==4,"a sibling at the same frame replaces the stale one");
    }
    {   // full: not timed, and an exit of an untimed invocation finds nothing
        CallStack<2> s;
        s.Begin(At(1000),1);s.Begin(At(900),2);
        Check(s.Begin(At(800),3)==nullptr && s.depth==2,"past N not timed");
        Call c{};
        Check(!s.End(At(800),c) && s.depth==2,"its exit pairs with nothing");
    }
    {   // the STATS interval
        const Totals was{10,2,1,0,0,30.0,1.0};
        const Totals now{25,5,3,1,0,80.0,4.5};
        const Totals d=now.Since(was);
        Check(d.calls==15 && d.loads==3 && d.waits==2 && d.missing==1 && d.loadMs==50.0 && d.waitMs==3.5,"interval");
    }
    if(failures)return 1;
    std::printf("sync_load_logic ok\n");
    return 0;
}
