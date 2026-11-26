// The heap tables share their allocated prefix with native readers. Check partial allocation, reuse,
// exhaustion, failed growth and iterator stability without starting the game.
#include "../src/lazy_rows.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace crew {
int memoryLines=0;
void Log(const char* format,...) noexcept {if(format && !std::strncmp(format,"MEMORY ",7))++memoryLines;}
}

namespace {
int checks=0;
void Check(bool condition,const char* why) {++checks;if(!condition){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}

struct Row {
    int held;
    unsigned next=1;
    unsigned char payload[64];
    static inline int made=0;
    static inline bool refuse=false;
    static void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
        if(refuse)return nullptr;
        ++made;
        void* p=std::malloc(size);
        if(p)std::memset(p,0xCD,size);
        return p;
    }
    static void operator delete(void* p) noexcept {std::free(p);}
    static void operator delete(void* p,const std::nothrow_t&) noexcept {operator delete(p);}
};

constexpr crew::LazyRows<Row,4> kConstantInit{};
constexpr auto kFree=[](const Row& row){return row.held==0;};

void GrowthAndReuse() {
    crew::LazyRows<Row,2> table;
    Check(kConstantInit.Size()==0 && !(table.begin()!=table.end()) && Row::made==0,"an unused table has no rows");
    const auto before=table.begin();
    auto* first=table.FindOrAppend("rows",kFree);
    Check(first && table.Size()==1 && Row::made==1,"first use constructs one row");
    Check(!(before!=table.end()),"a reader begun before allocation sees no rows");
    Check(first->held==0 && first->next==1,"the new row is value-initialised");
    for(unsigned char byte:first->payload)Check(byte==0,"new payload is zeroed");
    auto at=table.begin();
    first->held=42;
    auto* second=table.FindOrAppend("rows",kFree);
    Check(second && second!=first && table.Size()==2,"one additional occupied row grows the table");
    Check(&*at==first && !(++at!=table.end()),"an older iterator stops at its original prefix");
    second->held=7;
    Check(!table.FindOrAppend("rows",kFree),"a full table refuses growth");
    first->held=0;
    Check(table.FindOrAppend("rows",kFree)==first && Row::made==2 && second->held==7,
          "a released row is reused without moving or clearing other rows");
    delete first;delete second;
}

void FailureKeepsExistingRows() {
    crew::LazyRows<Row,8> table;
    auto* first=table.Append("refused rows");
    Check(first!=nullptr,"allocation succeeds before growth failure");
    first->held=1;
    Row::refuse=true;
    Check(!table.FindOrAppend("refused rows",kFree),"heap failure refuses just the new row");
    Row::refuse=false;
    Check(!table.Append("refused rows") && crew::memoryLines==1,"growth failure logs once");
    first->held=0;
    Check(table.FindOrAppend("refused rows",kFree)==first && table.Size()==1,"existing rows can be reused after a failed grow");
    delete first;
}

void IndexedGrowth() {
    crew::LazyRows<Row,8> table;
    const int made=Row::made;
    Check(!table.Get(1) && !table.Ensure(8,"indexed rows") && table.Size()==0,"invalid indexed access allocates nothing");
    auto* const third=table.Ensure(2,"indexed rows");
    Check(third && third==table.Get(2) && table.Size()==3 && Row::made==made+3,"the selected index only grows its prefix");
    third->held=27;
    Row::refuse=true;
    Check(!table.Ensure(4,"indexed rows") && table.Size()==3 && table.Ensure(2,"indexed rows")==third && third->held==27,
          "a failed indexed grow leaves the existing row and its address intact");
    Row::refuse=false;
    for(auto& row:table)delete &row;
}

void ConcurrentPublish() {
    crew::LazyRows<Row,16> table;
    std::vector<Row*> seen(16,nullptr);
    std::vector<std::thread> threads;
    for(std::size_t i=0;i<seen.size();++i)threads.emplace_back([&,i]{seen[i]=table.Append("concurrent rows");});
    for(auto& thread:threads)thread.join();
    Check(table.Size()==seen.size(),"concurrent append publishes every row");
    for(auto& row:table) {
        int matches=0;
        for(const Row* allocated:seen)if(allocated==&row)++matches;
        Check(matches==1 && row.next==1,"each published slot has one fully initialised row");
        delete &row;
    }
}
}

int main() {
    GrowthAndReuse();
    FailureKeepsExistingRows();
    IndexedGrowth();
    ConcurrentPublish();
    std::printf("lazy_rows_test: %d checks passed\n",checks);
    return 0;
}
