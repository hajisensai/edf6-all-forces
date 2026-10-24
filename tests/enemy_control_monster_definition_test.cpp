#include "../common/edf/memory.h"
#include "../src/enemy_control_monster_definition.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <vector>

namespace {
namespace definition=crew::enemy_native::monster_definition;
struct Range {const unsigned char* pointer;std::size_t bytes;bool writable;};
std::vector<Range> ranges;
unsigned checks=0;
constexpr std::uint64_t kSeed=0xA9B8C7D6E5F40312ull;
void Check(bool value,const char* description) {
    ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}
// Only fixture allocations are readable. This catches one-byte overreads and
// invalid relative addresses without dereferencing deliberately invalid data.
struct Memory {
    std::unique_ptr<unsigned char[]> data;std::size_t size;
    explicit Memory(std::size_t bytes,bool writable=true):data(new unsigned char[bytes]{}),size(bytes){ranges.push_back({data.get(),size,writable});}
    Memory(const Memory&)=delete;Memory& operator=(const Memory&)=delete;
    ~Memory(){for(auto i=ranges.begin();i!=ranges.end();++i)if(i->pointer==data.get()){ranges.erase(i);break;}}
    unsigned char* At(std::size_t offset=0){Check(offset<=size,"fixture offset within allocation");return data.get()+offset;}
    template<class T> void Put(std::size_t offset,T value){Check(offset<=size && sizeof(value)<=size-offset,"fixture write within allocation");std::memcpy(data.get()+offset,&value,sizeof(value));}
    template<class T> T Get(std::size_t offset) const {T value{};Check(offset<=size && sizeof(value)<=size-offset,"fixture read within allocation");std::memcpy(&value,data.get()+offset,sizeof(value));return value;}
};
void Variant(Memory& value,std::uint16_t type,const void* payload,std::uint32_t index=0) {
    value.Put<const void*>(0,payload);value.Put<std::uint32_t>(8,index);value.Put<std::uint16_t>(16,type);
}
std::uint64_t Hash(const void* value,std::uint64_t seed=kSeed) {
    auto result=seed;Check(definition::HashVariant(value,&result),"valid semantic definition accepted");
    Check(result!=seed,"successful hash extends incoming state");return result;
}
void Reject(const void* value,const char* why) {
    auto hash=kSeed;Check(!definition::HashVariant(value,&hash),why);Check(hash==kSeed,"failed hash leaves incoming state unchanged");
}
void Same(const void* a,const void* b,const char* why) {Check(Hash(a)==Hash(b),why);}
void Different(const void* a,const void* b,const char* why) {Check(Hash(a)!=Hash(b),why);}

struct OldFixture {
    Memory nodes{512},variant{18};
    OldFixture() {
        Variant(variant,1,nodes.At());
        Node(0,0,5,64);Node(64,1,4,0xFFFFFF85u);Node(76,2,4,0x80000000u);
        Node(88,3,3,200-88);Node(100,4,4,220-100);Node(112,0,1,256-112);Node(256,1,4,7);
        nodes.Put<std::uint16_t>(200,0x706B);nodes.Put<std::uint16_t>(202,0x30D0);nodes.Put<std::uint16_t>(204,0xD83D);
        nodes.Put<std::uint32_t>(220,0xFF020100u);
    }
    void Node(std::size_t offset,std::uint32_t type,std::int32_t length,std::uint32_t bits) {
        nodes.Put(offset,type);nodes.Put(offset+4,length);nodes.Put(offset+8,bits);
    }
    void* Value(){return variant.At();}
};
struct DsFixture {
    Memory raw{2048},tree{0x58},variant{18};
    DsFixture() {
        raw.Put<std::uint32_t>(0,0x4F475344u);raw.Put<std::uint32_t>(4,16);raw.Put<std::uint32_t>(8,7);raw.Put<std::uint32_t>(12,16);
        Node(0,3,256-16);Node(1,0,0x3FF8000000000000ull);Node(2,1,800-48);Node(3,2,400-64);Node(4,4,480-80);Node(5,3,560-96);Node(6,0,0xC004000000000000ull);
        raw.Put<std::uint32_t>(256,40);raw.Put<std::uint32_t>(260,2);raw.Put<std::uint32_t>(264,16);raw.Put<std::uint32_t>(268,5);
        for(unsigned i=0;i<5;++i)raw.Put<std::uint32_t>(272+4*i,i+1);
        raw.Put<std::int32_t>(296,1024-296);raw.Put<std::uint32_t>(300,0);raw.Put<std::int32_t>(304,1088-304);raw.Put<std::uint32_t>(308,3);
        raw.Put<std::uint32_t>(400,4);raw.Put<std::uint32_t>(404,16);raw.Put<std::uint32_t>(416,0x00FE0201u);
        raw.Put<std::uint32_t>(480,5);raw.Put<std::uint32_t>(484,16);raw.Put<std::uint32_t>(496,0x12345678u);raw.Put<unsigned char>(500,0xFF);
        raw.Put<std::uint32_t>(560,0);raw.Put<std::uint32_t>(564,0);raw.Put<std::uint32_t>(568,16);raw.Put<std::uint32_t>(572,1);raw.Put<std::uint32_t>(576,6);
        Text(800,{0x706B,0x30D0,0xD83D,0xDD25});Text(1024,{'a','t','t','a','c','k'});Text(1088,{'m','o','d','e','l'});
        tree.Put<void*>(0x50,raw.At());tree.Put<void*>(8,nullptr);tree.Put<std::size_t>(0x18,0);Variant(variant,2,tree.At());
    }
    void Node(unsigned index,std::uint32_t type,std::uint64_t value){const auto offset=16+16*index;raw.Put(offset,value);raw.Put(offset+8,type);}
    void Text(std::size_t offset,std::initializer_list<std::uint16_t> value){for(auto unit:value){raw.Put(offset,unit);offset+=2;}raw.Put<std::uint16_t>(offset,0);}
    void* Value(){return variant.At();}
};

void TestVariantEnvelope() {
    Memory emptyA(18),emptyB(18);Variant(emptyA,0,reinterpret_cast<const void*>(1));Variant(emptyB,0,reinterpret_cast<const void*>((std::numeric_limits<std::uintptr_t>::max)()));
    Same(emptyA.At(),emptyB.At(),"empty variant ignores all payload pointer bits");
    emptyB.Put<std::uint16_t>(16,0xFFFF);Different(emptyA.At(),emptyB.At(),"empty and absent variant tags remain distinct");
    emptyA.Put<std::uint16_t>(16,0xFFFF);Same(emptyA.At(),emptyB.At(),"absent variant ignores payload without dereferencing it");
    Check(Hash(emptyA.At(),kSeed)!=Hash(emptyA.At(),kSeed^0x8000),"hash extends caller's state instead of resetting it");
    Memory truncated(17);Reject(truncated.At(),"truncated eighteen-byte variant envelope rejected");Reject(nullptr,"null variant rejected");
    Check(!definition::HashVariant(emptyA.At(),nullptr),"null hash output rejected");
    Memory readOnlyHash(8,false);readOnlyHash.Put<std::uint64_t>(0,kSeed);Check(!definition::HashVariant(emptyA.At(),reinterpret_cast<std::uint64_t*>(readOnlyHash.At())) && readOnlyHash.Get<std::uint64_t>(0)==kSeed,"read-only digest output rejected before writing");
    for(unsigned tag:{3u,255u,65534u}){emptyA.Put<std::uint16_t>(16,static_cast<std::uint16_t>(tag));Reject(emptyA.At(),"unknown native variant tag rejected");}
    Variant(emptyA,1,nullptr);Reject(emptyA.At(),"null old SGO node rejected");Variant(emptyA,2,nullptr);Reject(emptyA.At(),"null DSGO tree rejected");
}
void TestOldSemanticIdentity() {
    OldFixture a,b;Check(a.nodes.At()!=b.nodes.At() && a.variant.At()!=b.variant.At(),"equivalent old SGO fixtures use distinct addresses");
    Same(a.Value(),b.Value(),"old SGO content hash is independent of allocation addresses");b.variant.Put<std::uint64_t>(8,0xDEADBEEFC001CAFEull);
    Same(a.Value(),b.Value(),"unused old-variant payload bytes do not enter definition hash");
    for(const auto offset:{72u,84u,200u,220u,264u}){const auto old=b.nodes.Get<unsigned char>(offset);b.nodes.Put<unsigned char>(offset,static_cast<unsigned char>(old^1));Different(a.Value(),b.Value(),"old SGO hash includes integer, float bits, UTF16, raw bytes and nested leaves");b.nodes.Put(offset,old);}
    b.Node(400,0,5,static_cast<std::uint32_t>(64-400));Variant(b.variant,1,b.nodes.At(400));Same(a.Value(),b.Value(),"old SGO supports legal negative relative child offsets");Variant(b.variant,1,b.nodes.At());
    b.nodes.Put<std::uint32_t>(76,1);Different(a.Value(),b.Value(),"old SGO scalar type is part of definition semantics");
    OldFixture emptyA,emptyB;emptyA.Node(0,0,0,0);emptyB.Node(0,0,0,0xFFFFFFFCu);Same(emptyA.Value(),emptyB.Value(),"zero-length old array does not dereference unused relative pointer");
    emptyA.Node(0,3,0,0);emptyB.Node(0,3,0,0x7FFFFFFFu);Same(emptyA.Value(),emptyB.Value(),"zero-length old text has no pointer identity");
    emptyA.Node(0,4,0,0);emptyB.Node(0,4,0,0x7FFFFFFFu);Same(emptyA.Value(),emptyB.Value(),"zero-length old raw block has no pointer identity");
}
void TestOldFailures() {
    OldFixture f;Variant(f.variant,1,reinterpret_cast<const void*>(1));Reject(f.Value(),"invalid old node pointer rejected");Variant(f.variant,1,f.nodes.At());
    f.Node(64,9,4,5);Reject(f.Value(),"unknown old leaf tag rejects entire definition after preceding fields");f.Node(64,1,4,0xFFFFFF85u);
    f.Node(0,0,-1,64);Reject(f.Value(),"negative old array length rejected");f.Node(0,0,4097,64);Reject(f.Value(),"old array above total node bound rejected");
    f.Node(0,0,1,0);Reject(f.Value(),"old relative self-cycle rejected");f.Node(0,0,1,501);Reject(f.Value(),"truncated old child node rejected");
    f.Node(0,3,-1,64);Reject(f.Value(),"negative old UTF16 length rejected");f.Node(0,3,3,510);Reject(f.Value(),"truncated old UTF16 data rejected");
    f.Node(0,4,-1,64);Reject(f.Value(),"negative old raw block length rejected");f.Node(0,4,8,508);Reject(f.Value(),"truncated old raw block rejected");
    Memory chain(40*12),variant(18);Variant(variant,1,chain.At());
    for(unsigned i=0;i<40;++i){chain.Put<std::uint32_t>(12*i,i==39?1u:0u);chain.Put<std::int32_t>(12*i+4,1);chain.Put<std::uint32_t>(12*i+8,i==39?7u:12u);}
    Reject(variant.At(),"old SGO nesting beyond depth bound rejected transactionally");
    constexpr unsigned groups=5,leaves=1024;Memory wide((1+groups+groups*leaves)*12);Variant(variant,1,wide.At());
    wide.Put<std::uint32_t>(0,0);wide.Put<std::int32_t>(4,groups);wide.Put<std::uint32_t>(8,12);
    for(unsigned g=0;g<groups;++g){const auto p=(1+g)*12,child=(1+groups+g*leaves)*12;wide.Put<std::uint32_t>(p,0);wide.Put<std::int32_t>(p+4,leaves);wide.Put<std::uint32_t>(p+8,child-p);for(unsigned i=0;i<leaves;++i){wide.Put<std::uint32_t>(child+i*12,1);wide.Put<std::uint32_t>(child+i*12+8,i);}}
    Reject(variant.At(),"old SGO total node budget rejects broad finite graph");
    constexpr unsigned block=1024*1024;Memory bulky(12+5*12+5*block);Variant(variant,1,bulky.At());bulky.Put<std::uint32_t>(0,0);bulky.Put<std::int32_t>(4,5);bulky.Put<std::uint32_t>(8,12);
    for(unsigned i=0;i<5;++i){const auto p=12+i*12,data=72+i*block;bulky.Put<std::uint32_t>(p,4);bulky.Put<std::int32_t>(p+4,block);bulky.Put<std::uint32_t>(p+8,data-p);}
    Reject(variant.At(),"old SGO cumulative hashed-byte budget rejected");
}
void TestDsSemanticIdentity() {
    DsFixture a,b;Check(a.raw.At()!=b.raw.At() && a.tree.At()!=b.tree.At(),"equivalent DSGO fixtures use distinct addresses");
    Same(a.Value(),b.Value(),"DSGO tree and raw-buffer addresses do not enter definition hash");b.tree.Put<std::uint64_t>(0x28,0xDEADBEEF88776655ull);b.variant.Put<std::uint32_t>(12,0xC001CAFEu);
    Same(a.Value(),b.Value(),"unused tree and variant padding is excluded from hash");
    for(const auto offset:{32u,800u,416u,496u,112u,1024u}){const auto old=b.raw.Get<unsigned char>(offset);b.raw.Put<unsigned char>(offset,static_cast<unsigned char>(old^1));Different(a.Value(),b.Value(),"DSGO hash includes numeric bits, UTF16, raw, VM, nested leaves and member names");b.raw.Put(offset,old);}
    b.raw.Put<std::uint32_t>(384,b.raw.Get<std::uint32_t>(416));b.raw.Put<std::int32_t>(404,-16);Same(a.Value(),b.Value(),"DSGO blob data supports a legal negative relative offset");b.raw.Put<std::int32_t>(404,16);
    b.raw.Put<std::int32_t>(296,1088-296);b.raw.Put<std::uint32_t>(300,3);b.raw.Put<std::int32_t>(304,1024-304);b.raw.Put<std::uint32_t>(308,0);
    Same(a.Value(),b.Value(),"DSGO member-name table storage order does not alter semantics");b.raw.Put<std::int32_t>(296,1024-296);b.raw.Put<std::uint32_t>(300,0);b.raw.Put<std::int32_t>(304,1088-304);b.raw.Put<std::uint32_t>(308,3);
    b.raw.Put<std::uint32_t>(300,1);Different(a.Value(),b.Value(),"DSGO name-to-member ordinal is included");b.raw.Put<std::uint32_t>(300,0);
    b.raw.Put<std::uint32_t>(272,2);b.raw.Put<std::uint32_t>(276,1);Different(a.Value(),b.Value(),"DSGO child order is semantic");
    Variant(a.variant,2,a.tree.At(),2);Variant(b.variant,2,b.tree.At(),2);Same(a.Value(),b.Value(),"non-root DSGO subtree has address-independent semantics");
    b.raw.Put<std::uint64_t>(32,0x4009000000000000ull);Same(a.Value(),b.Value(),"unselected DSGO sibling does not enter selected subtree hash");
}
void TestDsFailures() {
    DsFixture f;f.variant.Put<const void*>(0,reinterpret_cast<const void*>(1));Reject(f.Value(),"invalid DSGO tree rejected");f.variant.Put<void*>(0,f.tree.At());
    f.tree.Put<const void*>(0x50,reinterpret_cast<const void*>(1));Reject(f.Value(),"invalid DSGO raw-buffer pointer rejected");f.tree.Put<void*>(0x50,f.raw.At());
    Memory overrides(32);for(unsigned i=0;i<8;++i)overrides.Put<std::uint32_t>(i*4,UINT32_MAX);f.tree.Put<void*>(8,overrides.At());f.tree.Put<std::size_t>(0x18,8);const auto unoverridden=Hash(f.Value());
    DsFixture same;Check(Hash(same.Value())==unoverridden,"populated all-sentinel DSGO override map preserves raw semantics");
    overrides.Put<std::uint32_t>(0,0);Reject(f.Value(),"overridden selected DSGO root rejected instead of hashing stale raw defaults");overrides.Put<std::uint32_t>(0,UINT32_MAX);
    overrides.Put<std::uint32_t>(6*4,17);Reject(f.Value(),"overridden nested DSGO leaf rejects whole definition transactionally");overrides.Put<std::uint32_t>(6*4,UINT32_MAX);
    overrides.Put<std::uint32_t>(7*4,17);Check(Hash(f.Value())==unoverridden,"unvisited DSGO override does not alter selected subtree semantics");f.tree.Put<std::size_t>(0x18,0);f.tree.Put<void*>(8,nullptr);
    for(const auto offset:{0u,4u,12u}){const auto before=f.raw.Get<std::uint32_t>(offset);f.raw.Put<std::uint32_t>(offset,before^1);Reject(f.Value(),"malformed DSGO raw header rejected");f.raw.Put(offset,before);}
    f.variant.Put<std::uint32_t>(8,7);Reject(f.Value(),"DSGO selected index outside raw node table rejected");f.variant.Put<std::uint32_t>(8,0);
    f.raw.Put<std::uint32_t>(40,99);Reject(f.Value(),"unknown DSGO node tag rejects entire table transactionally");f.raw.Put<std::uint32_t>(40,0);
    f.raw.Put<std::uint32_t>(272,0);Reject(f.Value(),"DSGO container self-cycle rejected");f.raw.Put<std::uint32_t>(272,1);
    f.raw.Put<std::uint32_t>(272,7);Reject(f.Value(),"DSGO child index outside node table rejected");f.raw.Put<std::uint32_t>(272,1);
    f.raw.Put<std::uint32_t>(268,1025);Reject(f.Value(),"DSGO child count above per-container bound rejected");f.raw.Put<std::uint32_t>(268,5);
    f.raw.Put<std::uint32_t>(260,1025);Reject(f.Value(),"DSGO member-name count above bound rejected");f.raw.Put<std::uint32_t>(260,2);
    f.raw.Put<std::uint32_t>(300,5);Reject(f.Value(),"DSGO member-name ordinal outside table rejected");f.raw.Put<std::uint32_t>(300,0);
    f.raw.Put<std::int32_t>(296,(std::numeric_limits<std::int32_t>::max)());Reject(f.Value(),"DSGO member-name relative pointer above bounded window rejected");f.raw.Put<std::int32_t>(296,1024-296);
    f.raw.Put<std::uint64_t>(48,16ull*1024*1024+1);Reject(f.Value(),"DSGO node relative pointer above bounded window rejected");f.raw.Put<std::uint64_t>(48,800-48);
    f.raw.Put<std::uint32_t>(400,1024*1024+1);Reject(f.Value(),"DSGO raw block above single-block byte bound rejected");f.raw.Put<std::uint32_t>(400,4);
    f.raw.Put<std::uint32_t>(404,1645);Reject(f.Value(),"DSGO raw data crossing readable allocation end rejected");f.raw.Put<std::uint32_t>(404,16);
    Memory unterminated(16+16+1025*2),tree(0x58),variant(18);unterminated.Put<std::uint32_t>(0,0x4F475344u);unterminated.Put<std::uint32_t>(4,16);unterminated.Put<std::uint32_t>(8,1);unterminated.Put<std::uint32_t>(12,16);unterminated.Put<std::uint64_t>(16,16);unterminated.Put<std::uint32_t>(24,1);
    for(unsigned i=0;i<1025;++i)unterminated.Put<std::uint16_t>(32+2*i,'x');tree.Put<void*>(0x50,unterminated.At());Variant(variant,2,tree.At());Reject(variant.At(),"DSGO UTF16 text above bounded scan length rejected");
    Memory chain(16+40*16+40*20);chain.Put<std::uint32_t>(0,0x4F475344u);chain.Put<std::uint32_t>(4,16);chain.Put<std::uint32_t>(8,40);chain.Put<std::uint32_t>(12,16);tree.Put<void*>(0x50,chain.At());
    for(unsigned i=0;i<40;++i){const auto node=16+16*i,block=16+40*16+20*i;chain.Put<std::uint64_t>(node,i==39?0u:block-node);chain.Put<std::uint32_t>(node+8,i==39?0u:3u);if(i!=39){chain.Put<std::uint32_t>(block+8,16);chain.Put<std::uint32_t>(block+12,1);chain.Put<std::uint32_t>(block+16,i+1);}}
    Reject(variant.At(),"DSGO nesting beyond depth bound rejected transactionally");
}
} // namespace

namespace edf {
bool Readable(const void* pointer,std::size_t bytes,bool writable) noexcept {
    if(!pointer || !bytes)return false;const auto value=reinterpret_cast<std::uintptr_t>(pointer);
    for(const auto& range:ranges){const auto first=reinterpret_cast<std::uintptr_t>(range.pointer);if(value>=first && value-first<=range.bytes && bytes<=range.bytes-(value-first))return !writable || range.writable;}
    ULONG_PTR stackLow=0,stackHigh=0;GetCurrentThreadStackLimits(&stackLow,&stackHigh);
    return value>=stackLow && value<=stackHigh && bytes<=stackHigh-value;
}
}
int main() {
    TestVariantEnvelope();TestOldSemanticIdentity();TestOldFailures();TestDsSemanticIdentity();TestDsFailures();
    Check(ranges.empty(),"all registered readable fixture regions released");
    std::printf("Monster semantic definition: %u checks passed\n",checks);return 0;
}
