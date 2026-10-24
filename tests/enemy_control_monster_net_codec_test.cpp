#include "../src/enemy_control_monster_net_codec.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace {
namespace wire=crew::enemy_native::monster_wire;
unsigned checks=0;bool allowSample=true;
void Check(bool ok,const char* why) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
wire::Event Example() {
    wire::Event event{};event.serial=0x1020304050607080ull;event.digest=0x8877665544332211ull;event.family=2;
    auto& sample=event.sample;
    for(unsigned i=0;i<16;++i){sample.matrix[i]=static_cast<float>(i)-7.25f;sample.muzzleMatrix[i]=static_cast<float>(i)*2.5f+0.125f;}
    sample.matrix[3]=-0.0f;
    for(unsigned i=0;i<crew::enemy_native::kProjectileWords;++i)sample.scalar[i]=0xA1020304u+i*0x01010101u;
    sample.random=0xFEDCBA98u;sample.shot=0x76543210u;sample.flags=0x80000001u;
    for(unsigned i=0;i<crew::enemy_native::kProjectileBytes;++i)sample.bytes[i]=static_cast<unsigned char>(0xF0u+i);
    sample.scalar178=0xFEDCu;
    for(unsigned i=0;i<32;++i){sample.target[i]=static_cast<unsigned char>(i+1);sample.extra[i]=static_cast<unsigned char>(0xF0u-i);}
    sample.lock=0xF1020304u;sample.hasMuzzle=true;sample.extraObject=true;sample.resourceMask=3;return event;
}
bool Same(const wire::Event& a,const wire::Event& b) {
    const auto& x=a.sample;const auto& y=b.sample;
    return a.serial==b.serial && a.digest==b.digest && a.family==b.family &&
        !std::memcmp(x.matrix,y.matrix,sizeof(x.matrix)) && !std::memcmp(x.muzzleMatrix,y.muzzleMatrix,sizeof(x.muzzleMatrix)) &&
        !std::memcmp(x.scalar,y.scalar,sizeof(x.scalar)) && x.random==y.random && x.shot==y.shot && x.flags==y.flags &&
        !std::memcmp(x.bytes,y.bytes,sizeof(x.bytes)) && x.scalar178==y.scalar178 &&
        !std::memcmp(x.target,y.target,sizeof(x.target)) && !std::memcmp(x.extra,y.extra,sizeof(x.extra)) &&
        x.lock==y.lock && x.hasMuzzle==y.hasMuzzle && x.extraObject==y.extraObject && x.resourceMask==y.resourceMask;
}
}
namespace crew::enemy_native {
// The codec delegates sample semantics to the production validator. This stub
// verifies that delegation and tests lossless transport of opaque numeric bits.
bool ValidNativeProjectileSample(const NativeProjectileSample& sample) noexcept {
    if(!allowSample)return false;
    for(float value:sample.matrix)if(!std::isfinite(value))return false;
    for(float value:sample.muzzleMatrix)if(!std::isfinite(value))return false;
    return true;
}
}
int main() {
    static_assert(wire::kTag==0x31504E4Du && wire::kVersion==1 && wire::kMaxBatch==1 && wire::kMaxBytes==512,"fixed Monster envelope");
    static_assert(wire::kEventBytes==361 && wire::kHeaderBytes==8,"complete explicit field layout");
    auto input=Example();wire::Event output[wire::kMaxBatch]{};
    unsigned char bytes[wire::kMaxBytes+1]{},original[wire::kMaxBytes+1]{};std::size_t used=999;unsigned count=999;
    Check(wire::EventBytes(input)==361 && wire::Bytes(&input,1)==369,"explicit full sample size");
    Check(wire::Encode(&input,1,bytes,sizeof(bytes),&used) && used==369,"full outcome fits one reliable packet");
    Check(bytes[0]=='M' && bytes[1]=='N' && bytes[2]=='P' && bytes[3]=='1' && bytes[4]==1 && bytes[5]==1 && !bytes[6] && !bytes[7],"canonical eight byte header");
    Check(bytes[8]==0x80 && bytes[15]==0x10 && bytes[16]==0x11 && bytes[23]==0x88 && bytes[24]==2,"metadata is explicitly little endian");
    Check(bytes[296]==0xDC && bytes[297]==0xFE && bytes[366]==1 && bytes[367]==1 && bytes[368]==3,"unaligned uint16, booleans and resource mask have exact packed offsets");
    Check(wire::Decode(bytes,used,output,wire::kMaxBatch,&count) && count==1 && Same(input,output[0]),"every typed field and negative zero roundtrips exactly");
    Check(wire::Hash(input)!=0 && wire::Hash(input)==wire::Hash(output[0]),"receipt hash survives roundtrip");
    std::memcpy(original,bytes,sizeof(bytes));
    auto Reject=[&](const void* packet,std::size_t size,unsigned capacity=wire::kMaxBatch) {
        const auto before=output[0];count=999;
        Check(!wire::Decode(packet,size,output,capacity,&count) && !count,"malformed packet rejected with zero count");
        Check(Same(output[0],before),"failed decode publishes no partial state");
    };
    for(std::size_t size=0;size<used;++size)Reject(bytes,size);
    Reject(bytes,used+1);Reject(bytes,wire::kMaxBytes+1);Reject(nullptr,used);Reject(bytes,used,0);
    Check(!wire::Decode(bytes,used,nullptr,1,&count) && !count,"null event output rejected");
    Check(!wire::Decode(bytes,used,output,1,nullptr),"null count rejected");
    for(unsigned offset:{0u,1u,2u,3u,4u,6u,7u}) {std::memcpy(bytes,original,sizeof(bytes));bytes[offset]^=0x80;Reject(bytes,used);}
    for(unsigned value:{0u,2u,255u}) {std::memcpy(bytes,original,sizeof(bytes));bytes[5]=static_cast<unsigned char>(value);Reject(bytes,used);}
    for(unsigned value:{0u,3u,255u}) {std::memcpy(bytes,original,sizeof(bytes));bytes[24]=static_cast<unsigned char>(value);Reject(bytes,used);}
    for(unsigned offset:{8u,16u}) {std::memcpy(bytes,original,sizeof(bytes));std::memset(bytes+offset,0,8);Reject(bytes,used);}
    for(unsigned offset:{366u,367u})for(unsigned value:{2u,255u}) {
        std::memcpy(bytes,original,sizeof(bytes));bytes[offset]=static_cast<unsigned char>(value);Reject(bytes,used);
    }
    for(unsigned value:{4u,128u,255u}) {
        std::memcpy(bytes,original,sizeof(bytes));bytes[368]=static_cast<unsigned char>(value);Reject(bytes,used);
    }
    for(unsigned offset:{25u,89u}) {
        std::memcpy(bytes,original,sizeof(bytes));const std::uint32_t nan=0x7FC00000u;
        for(unsigned i=0;i<4;++i)bytes[offset+i]=static_cast<unsigned char>(nan>>(8*i));Reject(bytes,used);
    }
    std::memcpy(bytes,original,sizeof(bytes));allowSample=false;Reject(bytes,used);
    Check(!wire::EventBytes(input) && !wire::Hash(input),"semantic sample refusal is shared by size and hash");allowSample=true;
    for(unsigned flags=0;flags<4;++flags)for(unsigned resources=0;resources<4;++resources) {
        auto value=input;value.family=1;value.sample.hasMuzzle=(flags&1)!=0;value.sample.extraObject=(flags&2)!=0;
        value.sample.resourceMask=static_cast<unsigned char>(resources);
        Check(wire::Encode(&value,1,bytes,sizeof(bytes),&used) && wire::Decode(bytes,used,output,1,&count) && Same(value,output[0]),"both families and all canonical boolean and resource combinations retain the full sample");
    }
    auto RejectEncode=[&](const wire::Event* events,unsigned total,void* packet,std::size_t capacity,std::size_t* written) {
        unsigned char before[sizeof(bytes)]{};std::memcpy(before,bytes,sizeof(bytes));if(written)*written=999;
        Check(!wire::Encode(events,total,packet,capacity,written) && (!written || !*written),"invalid encode rejected with zero used");
        Check(!std::memcmp(bytes,before,sizeof(bytes)),"failed encode does not modify destination");
    };
    RejectEncode(&input,1,bytes,368,&used);RejectEncode(nullptr,1,bytes,sizeof(bytes),&used);
    RejectEncode(&input,0,bytes,sizeof(bytes),&used);RejectEncode(&input,2,bytes,sizeof(bytes),&used);
    RejectEncode(&input,1,nullptr,sizeof(bytes),&used);RejectEncode(&input,1,bytes,sizeof(bytes),nullptr);
    auto invalid=input;invalid.serial=0;RejectEncode(&invalid,1,bytes,sizeof(bytes),&used);
    invalid=input;invalid.digest=0;RejectEncode(&invalid,1,bytes,sizeof(bytes),&used);
    invalid=input;invalid.family=0;RejectEncode(&invalid,1,bytes,sizeof(bytes),&used);
    invalid=input;invalid.sample.matrix[12]=std::numeric_limits<float>::infinity();RejectEncode(&invalid,1,bytes,sizeof(bytes),&used);
    for(unsigned value:{4u,128u,255u}) {
        invalid=input;invalid.sample.resourceMask=static_cast<unsigned char>(value);RejectEncode(&invalid,1,bytes,sizeof(bytes),&used);
    }
    // A change to every meaningful field must affect the receipt fingerprint.
    const auto fingerprint=wire::Hash(input);
    auto Different=[&](const wire::Event& value){Check(wire::Hash(value)!=fingerprint,"receipt fingerprint includes each typed field");};
    auto changed=input;++changed.serial;Different(changed);changed=input;++changed.digest;Different(changed);changed=input;changed.family=1;Different(changed);
    for(unsigned i=0;i<16;++i){changed=input;changed.sample.matrix[i]+=1;Different(changed);changed=input;changed.sample.muzzleMatrix[i]+=1;Different(changed);}
    for(unsigned i=0;i<crew::enemy_native::kProjectileWords;++i){changed=input;changed.sample.scalar[i]^=1;Different(changed);}
    changed=input;changed.sample.random^=1;Different(changed);changed=input;changed.sample.shot^=1;Different(changed);changed=input;changed.sample.flags^=1;Different(changed);
    for(unsigned i=0;i<crew::enemy_native::kProjectileBytes;++i){changed=input;changed.sample.bytes[i]^=1;Different(changed);}
    changed=input;changed.sample.scalar178^=1;Different(changed);
    for(unsigned i=0;i<32;++i){changed=input;changed.sample.target[i]^=1;Different(changed);changed=input;changed.sample.extra[i]^=1;Different(changed);}
    changed=input;changed.sample.lock^=1;Different(changed);changed=input;changed.sample.hasMuzzle=false;Different(changed);changed=input;changed.sample.extraObject=false;Different(changed);
    for(unsigned resources=0;resources<3;++resources){changed=input;changed.sample.resourceMask=static_cast<unsigned char>(resources);Different(changed);}
    std::printf("Monster projectile codec: %u checks passed\n",checks);return 0;
}
