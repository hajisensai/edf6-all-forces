// crew::LazyRows (src/lazy_rows.h, mem-4): the heap tables that replaced the enemy-control and
// support static arrays. Checks the lifetime the callers rely on, without the game:
// - nothing is allocated until the first Allocate(); before it the table iterates as empty;
// - the first Allocate() makes all N rows once, value-initialised like the former `T rows[N]{}`;
// - later Allocate() calls (every bind / spawn, every mission and room reset) neither allocate
//   again nor move the rows, and the rows are never freed, so pointers into them never dangle;
// - a failed allocation logs exactly one MEMORY line, keeps the table empty, and never retries
//   (the feature stays off; no half-made table);
// - threads racing the first Allocate() all see the same rows and the losers' blocks are freed;
// - a loop's begin() and end() use one read of the rows pointer: a null begin has a null end.
#include "../src/lazy_rows.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace crew {
int logLines=0;int memoryLines=0;
void Log(const char* format,...) noexcept {++logLines;if(format && !std::strncmp(format,"MEMORY ",7))++memoryLines;}
}

namespace {
int checks=0;
void Check(bool condition,const char* why) {++checks;if(!condition){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}

// Counts the array allocations the table makes; `refuse` makes the nothrow new fail.
struct Row {
    int held;
    unsigned next=1;   // a default member initialiser, like the streams' sequence counters
    unsigned char payload[64];
    static inline int made=0;
    static inline int freed=0;
    static inline bool refuse=false;
    static void* operator new[](std::size_t size,const std::nothrow_t&) noexcept {
        if(refuse)return nullptr;
        ++made;
        void* p=std::malloc(size);
        if(p)std::memset(p,0xCD,size);   // garbage, so value-initialisation is really checked
        return p;
    }
    static void operator delete[](void* p) noexcept {if(p){++freed;std::free(p);}}
    static void operator delete[](void* p,const std::nothrow_t&) noexcept {operator delete[](p);}
};

// Constant-initialised (so a namespace-scope table lands in .bss with no dynamic initialiser):
// this only compiles if the constructor runs at compile time.
constexpr crew::LazyRows<Row,4> kConstantInit{};

crew::LazyRows<Row,16> table;
crew::LazyRows<Row,8> refused;

std::size_t Count(crew::LazyRows<Row,16>& rows) {std::size_t n=0;for(auto& row:rows){(void)row;++n;}return n;}

void FirstUseAndReuse() {
    Check(!kConstantInit.Allocated() && kConstantInit.begin()==nullptr,"a constant-initialised table holds no rows");
    Check(!table.Allocated() && table.begin()==nullptr && table.begin().Last()==nullptr && Count(table)==0,
          "before first use: no rows, iteration is empty");
    Check(Row::made==0,"before first use: nothing allocated");
    Check(table.Allocate("test rows") && table.Allocated(),"first use allocates");
    Check(Row::made==1 && Count(table)==16 && table.begin().Last()-table.begin()==16,"all N rows at once, in one block");
    bool valueInit=true;
    for(const auto& row:table) {
        if(row.held!=0 || row.next!=1)valueInit=false;
        for(unsigned char b:row.payload)if(b)valueInit=false;
    }
    Check(valueInit,"rows are value-initialised: zero, with the default member initialisers applied");

    Row* const first=table.begin();
    table[3].held=42;table[15].next=9;
    // Every later bind / spawn / mission reset calls Allocate() again.
    for(int session=0;session<1000;++session) {
        Check(table.Allocate("test rows"),"re-entry: still allocated");
        for(auto& row:table)if(&row!=&table[3] && &row!=&table[15])row.held=session;   // the callers reuse rows in place
    }
    Check(Row::made==1 && Row::freed==0,"1000 re-entries: no second allocation, nothing freed");
    Check(table.begin()==first && &table[3]==first+3,"re-entry: the rows never move, pointers into them stay valid");
    Check(table[3].held==42 && table[15].next==9,"re-entry: rows keep what the callers wrote");
    Check(crew::memoryLines==0,"a successful allocation logs nothing");
}

void FailureTurnsTheFeatureOff() {
    const int madeBefore=Row::made;
    Row::refuse=true;
    Check(!refused.Allocate("refused rows"),"the heap refuses: Allocate fails");
    Check(!refused.Allocated() && refused.begin()==nullptr && refused.begin().Last()==nullptr,"after a failure the table stays empty");
    Check(crew::memoryLines==1,"the failure logs one MEMORY line");
    Row::refuse=false;
    for(int i=0;i<100;++i)Check(!refused.Allocate("refused rows"),"after a failure the feature stays off");
    Check(crew::memoryLines==1 && crew::logLines==1,"no second log line");
    Check(Row::made==madeBefore,"no retry, no half-made table");
}

// A loop whose begin() ran before another thread's first Allocate() and whose end() after it: begin is null, so its
// end must be too (the loop runs no rows), not rows + N (a loop from null through N rows). Deterministic: the
// "other thread" allocates between the two calls.
void LoopStraddlingFirstAllocate() {
    crew::LazyRows<Row,16> fresh;
    auto first=fresh.begin();
    Check(fresh.Allocate("straddled rows"),"the other thread's first use allocates");
    const auto last=fresh.end();
    Check(first==nullptr && first.Last()==nullptr,"begin is null, so its end is null");
    std::size_t n=0;
    for(;first!=last && n<=fresh.size();++first)++n;
    Check(n==0,"a loop begun before the first Allocate() runs no rows");
    std::size_t after=0;
    for(auto& row:fresh){(void)row;++after;}
    Check(after==16,"the next loop sees all N rows");
    delete[] static_cast<Row*>(fresh.begin());   // test only
}

void RacingFirstUse() {
    for(int round=0;round<50;++round) {
        auto* rows=new crew::LazyRows<Row,32>();
        const int madeBefore=Row::made,freedBefore=Row::freed;
        std::vector<Row*> seen(8,nullptr);
        std::vector<std::thread> threads;
        for(std::size_t t=0;t<seen.size();++t)threads.emplace_back([rows,&seen,t] {
            if(rows->Allocate("race rows"))seen[t]=rows->begin();
        });
        for(auto& thread:threads)thread.join();
        bool same=true;for(Row* p:seen)if(!p || p!=rows->begin())same=false;
        Check(same,"racing first use: every thread gets the same rows");
        Check((Row::made-madeBefore)-(Row::freed-freedBefore)==1,"racing first use: exactly one block survives, the losers are freed");
        delete[] rows->begin();   // test only, so 50 rounds do not pile up: the plugin never frees its tables
        delete rows;
    }
}
}

int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    FirstUseAndReuse();
    FailureTurnsTheFeatureOff();
    LoopStraddlingFirstAllocate();
    RacingFirstUse();
    std::printf("lazy_rows_test: %d checks passed\n",checks);
    return 0;
}
