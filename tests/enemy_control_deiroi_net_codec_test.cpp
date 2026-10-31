#include "../src/enemy_control_deiroi_net_codec.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <initializer_list>

namespace {
namespace wire=crew::enemy_native::deiroi_wire;
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
wire::Event Control(std::uint64_t serial) {
    wire::Event value{};
    value.kind=wire::Kind::control;value.serial=serial;value.key=(31u<<16)|127u;
    value.commandSequence=0x8877665544332211ull;value.seed=0xFEDCBA9876543210ull;
    value.point[0]=1.25f;value.point[1]=-2.5f;value.point[2]=10000.0f;
    return value;
}
std::uint32_t FloatBits(float value) {
    std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));return bits;
}
wire::Event Shot(std::uint64_t serial) {
    auto value=Control(serial);value.kind=wire::Kind::shot;
    value.shotIndex=0x01234567u;value.rng90=0x12345678u;value.rng94=0xABCDEF90u;
    value.actual[0]=0xFEDCBA98u;value.actual[1]=FloatBits(2.5f);value.actual[2]=0xAABBCCDDu;
    value.actual[3]=FloatBits(-3.25f);value.actual[4]=0x10203040u;value.actual[5]=0xFFFFFFFFu;
    value.matrix[0]=2.0f;value.matrix[5]=3.0f;value.matrix[10]=-4.0f;
    value.matrix[12]=10.0f;value.matrix[13]=-20.0f;value.matrix[14]=30.0f;value.matrix[15]=1.0f;
    return value;
}
wire::Event Entity(std::uint64_t serial) {
    auto value=Shot(serial);value.kind=wire::Kind::entity;value.entityTeam=0xFFFFFFFFu;value.lockOrdinal=0x10203040u;
    for(unsigned i=0;i<32;++i)if(i<20 || i>=24)value.targetActorId[i]=static_cast<unsigned char>(0x40u+i);
    return value;
}
void Set32(unsigned char* at,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)at[i]=static_cast<unsigned char>(value>>(i*8));
}
void SetFloat(unsigned char* at,float value) {
    std::uint32_t bits=0;std::memcpy(&bits,&value,4);Set32(at,bits);
}
}

int main() {
    static_assert(wire::kMagic==0x31474445u && wire::kMaxBatch==12 && wire::kMaxBytes==512,"fixed version-one envelope");
    static_assert(wire::kHeaderBytes+wire::kControlBytes*wire::kMaxBatch==wire::kMaxBytes,"twelve controls exactly fill one packet");
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    const auto infinity=std::numeric_limits<float>::infinity();
    auto control=Control(0x0102030405060708ull);
    auto shot=Shot(control.serial+1);
    Check(wire::Valid(control) && wire::EventBytes(control)==42,"control has exact packed size");
    Check(wire::Valid(shot) && wire::EventBytes(shot)==121,"nonunit and reflected native shot frame remains valid");
    auto changed=control;changed.serial=0;Check(!wire::Valid(changed),"zero serial rejected");
    changed=control;changed.kind=static_cast<wire::Kind>(0);Check(!wire::Valid(changed) && !wire::EventBytes(changed),"unknown kind has no wire size");
    changed=control;changed.key=33u<<16;Check(!wire::Valid(changed),"group count is bounded");
    changed=control;changed.key=128;Check(!wire::Valid(changed),"part count is bounded");
    changed=control;changed.key=0xFFFF007Fu;Check(!wire::Valid(changed),"high leg bits rejected");
    changed=control;changed.key=0x001F8000u;Check(!wire::Valid(changed),"part high bits rejected");
    changed=control;changed.key=0;Check(wire::Valid(changed),"first leg and first part are legal");
    changed.phase=6;changed.key=32u<<16;Check(wire::Valid(changed),"main gun group begins with row zero");
    changed.key=(32u<<16)|63u;Check(wire::Valid(changed),"main gun group accepts its last real row");
    changed.key=(32u<<16)|64u;Check(!wire::Valid(changed),"main gun row count is bounded below foot part capacity");
    changed.key=(32u<<16)|127u;Check(!wire::Valid(changed),"main group cannot reuse the larger foot-part ordinal range");
    changed=control;changed.seed=0;Check(!wire::Valid(changed) && !wire::EventBytes(changed),"semantic start seed zero is rejected");
    changed.phase=2;Check(wire::Valid(changed),"native lifecycle seed zero remains legal");
    changed=shot;changed.seed=0;Check(wire::Valid(changed),"native shot seed zero is legal");
    changed=shot;changed.shotIndex=0;Check(wire::Valid(changed),"native first-shot counter zero remains legal");
    changed.shotIndex=0x7FFFFFFEu;Check(wire::Valid(changed),"last incrementable signed native shot counter is legal");
    for(std::uint32_t index:{0x7FFFFFFFu,0x80000000u,0xFFFFFFFFu}) {
        changed.shotIndex=index;Check(!wire::Valid(changed),"shot counter cannot overflow a signed native increment");
    }
    for(unsigned phase=0;phase<6;++phase) {
        changed=control;changed.phase=static_cast<std::uint8_t>(phase);
        Check(wire::Valid(changed),"all declared foot control phases supported");
        changed.commandSequence=0;
        Check(wire::Valid(changed)==(phase>=2),"only later lifecycle phases allow missing command sequence");
        changed.commandSequence=control.commandSequence;changed.seed=0;
        Check(wire::Valid(changed)==(phase>=2),"phase zero and one require semantic seed while native phases preserve zero");
    }
    changed=control;changed.phase=8;Check(!wire::Valid(changed),"unknown phase rejected");
    for(unsigned i=0;i<3;++i) {
        changed=control;changed.point[i]=nan;Check(!wire::Valid(changed),"control rejects each NaN point coordinate");
        changed.point[i]=infinity;Check(!wire::Valid(changed),"control rejects each infinite point coordinate");
    }
    for(unsigned i=0;i<16;++i) {
        changed=shot;changed.matrix[i]=nan;Check(!wire::Valid(changed),"shot rejects every NaN matrix entry");
        changed.matrix[i]=-infinity;Check(!wire::Valid(changed),"shot rejects every infinite matrix entry");
    }
    for(unsigned word:{1u,3u}) {
        for(float value:{nan,infinity,-infinity}) {
            changed=shot;changed.actual[word]=FloatBits(value);
            Check(!wire::Valid(changed),"actual speed and power reject nonfinite floats");
        }
        for(float value:{-5.0f,0.0f,5.0f}) {
            changed=shot;changed.actual[word]=FloatBits(value);
            Check(wire::Valid(changed),"actual speed and power preserve every finite native sign and zero");
        }
    }
    for(unsigned word:{0u,2u,4u,5u}) {
        changed=shot;changed.actual[word]=0x7FC00000u;
        Check(wire::Valid(changed),"integer InitParam words are not misinterpreted as floats");
        changed.actual[word]=0xFFFFFFFFu;
        Check(wire::Valid(changed),"integer InitParam words retain the full uint32 range");
    }
    for(unsigned row=0;row<3;++row) {
        changed=shot;
        for(unsigned col=0;col<3;++col)changed.matrix[row*4+col]=0;
        Check(!wire::Valid(changed),"zero rotation row rejected");
    }
    changed=shot;changed.matrix[4]=4;changed.matrix[5]=0;
    Check(!wire::Valid(changed),"collinear nonzero rows rejected");
    changed.matrix[5]=1.0e-12f;Check(!wire::Valid(changed),"numerically collapsed frame rejected");
    changed=shot;changed.matrix[15]=1.00005f;Check(wire::Valid(changed),"small native affine rounding accepted");
    changed.matrix[15]=1.0002f;Check(!wire::Valid(changed),"nonaffine position row rejected");
    changed=shot;changed.matrix[15]=0;Check(!wire::Valid(changed),"zero affine w rejected");
    for(float scale:{1.0e-30f,1.0e30f}) {
        changed=shot;
        changed.matrix[0]=scale;changed.matrix[5]=scale*2;changed.matrix[10]=scale*3;
        Check(wire::Valid(changed),"well-conditioned tiny or huge nonunit frame accepted without float determinant overflow");
    }

    wire::Event input[wire::kMaxBatch+1]{control,shot};
    unsigned char bytes[wire::kMaxBytes+1]{},original[wire::kMaxBytes+1]{};
    std::size_t used=999;
    Check(wire::Encode(input,2,bytes,sizeof(bytes),&used) && used==171,"mixed kinds use exact variable record sizes");
    Check(bytes[0]=='E' && bytes[1]=='D' && bytes[2]=='G' && bytes[3]=='1' && bytes[4]==2 && bytes[5]==2 && !bytes[6] && !bytes[7],"canonical magic version count and reserved bytes");
    Check(bytes[8]==1 && bytes[9]==8 && bytes[16]==1,"kind and uint64 serial use explicit little endian");
    Check(bytes[17]==127 && !bytes[18] && bytes[19]==31 && !bytes[20],"part key never depends on host struct layout");
    Check(bytes[22]==0x11 && bytes[29]==0x88 && bytes[30]==0x10 && bytes[37]==0xFE,"command and seed retain full uint64 range");
    Check(bytes[63]==0x67 && bytes[64]==0x45 && bytes[65]==0x23 && bytes[66]==0x01,"native shot index is a little-endian uint32 before seed");
    Check(bytes[50]==2 && bytes[75]==0x78 && bytes[78]==0x12 && bytes[79]==0x90 && bytes[82]==0xAB,"shot native parameter words use their declared offsets");
    Check(bytes[83]==0x98 && bytes[86]==0xFE && bytes[87]==0 && bytes[90]==0x40,
          "actual team and speed follow compatibility words in canonical little endian");
    Check(bytes[91]==0xDD && bytes[94]==0xAA && bytes[95]==0 && bytes[98]==0xC0,
          "actual pattern and signed finite power retain their exact native bits");
    Check(bytes[99]==0x40 && bytes[102]==0x10 && bytes[103]==0xFF && bytes[106]==0xFF,
          "actual random and native shot parameter counter precede the complete matrix");
    wire::Event output[wire::kMaxBatch]{};
    unsigned count=999;
    Check(wire::Decode(bytes,used,output,wire::kMaxBatch,&count) && count==2,"mixed batch roundtrips");
    Check(output[0].kind==wire::Kind::control && output[0].serial==control.serial && output[0].key==control.key
        && output[0].phase==control.phase && output[0].commandSequence==control.commandSequence && output[0].seed==control.seed
        && !std::memcmp(output[0].point,control.point,sizeof(control.point)),"all control fields roundtrip exactly");
    Check(output[1].kind==wire::Kind::shot && output[1].serial==shot.serial && output[1].key==shot.key
        && output[1].shotIndex==shot.shotIndex && output[1].seed==shot.seed && output[1].rng90==shot.rng90 && output[1].rng94==shot.rng94
        && !std::memcmp(output[1].actual,shot.actual,sizeof(shot.actual))
        && !std::memcmp(output[1].matrix,shot.matrix,sizeof(shot.matrix)),"all shot fields including native pattern frame roundtrip exactly");
    Check(!output[0].shotIndex && !output[0].rng90 && !output[0].actual[0] && !output[0].actual[5] && !output[1].commandSequence && !output[1].point[0],"nonwire fields decoded to neutral values");
    std::memcpy(original,bytes,sizeof(bytes));
    auto Reject=[&](const void* payload,std::size_t size,unsigned capacity=wire::kMaxBatch) {
        unsigned char before[sizeof(output)]{};std::memcpy(before,output,sizeof(output));
        count=999;
        Check(!wire::Decode(payload,size,output,capacity,&count) && count==0,"malformed batch is rejected");
        Check(!std::memcmp(before,output,sizeof(output)),"failed decode publishes no partial event");
    };
    for(std::size_t size=0;size<used;++size)Reject(bytes,size);
    Reject(bytes,used+1);Reject(bytes,wire::kMaxBytes+1);Reject(nullptr,used);Reject(bytes,used,1);Reject(bytes,used,0);
    Check(!wire::Decode(bytes,used,nullptr,wire::kMaxBatch,&count) && !count,"null output rejected");
    Check(!wire::Decode(bytes,used,output,wire::kMaxBatch,nullptr),"null count rejected");
    for(unsigned offset:{0u,1u,2u,3u,4u,6u,7u}) {
        std::memcpy(bytes,original,sizeof(bytes));bytes[offset]^=0x80;Reject(bytes,used);
    }
    for(unsigned value:{0u,1u,3u,13u,255u}) {
        std::memcpy(bytes,original,sizeof(bytes));bytes[5]=static_cast<unsigned char>(value);Reject(bytes,used);
    }
    for(unsigned offset:{8u,50u}) {
        for(unsigned kind:{0u,4u,255u}) {
            std::memcpy(bytes,original,sizeof(bytes));bytes[offset]=static_cast<unsigned char>(kind);Reject(bytes,used);
        }
    }
    std::memcpy(bytes,original,sizeof(bytes));std::memset(bytes+9,0,8);Reject(bytes,used);
    std::memcpy(bytes,original,sizeof(bytes));std::memcpy(bytes+51,bytes+9,8);Reject(bytes,used);
    std::memcpy(bytes,original,sizeof(bytes));bytes[51]=7;Reject(bytes,used);
    for(unsigned offset:{17u,59u}) {
        for(std::uint32_t key:{33u<<16,(32u<<16)|64u,128u,0xFFFFFFFFu}) {
            std::memcpy(bytes,original,sizeof(bytes));Set32(bytes+offset,key);Reject(bytes,used);
        }
    }
    std::memcpy(bytes,original,sizeof(bytes));bytes[21]=8;Reject(bytes,used);
    std::memcpy(bytes,original,sizeof(bytes));std::memset(bytes+22,0,8);Reject(bytes,used);
    for(unsigned offset=38;offset<50;offset+=4) {
        std::memcpy(bytes,original,sizeof(bytes));SetFloat(bytes+offset,nan);Reject(bytes,used);
    }
    for(unsigned offset=107;offset<171;offset+=4) {
        std::memcpy(bytes,original,sizeof(bytes));SetFloat(bytes+offset,infinity);Reject(bytes,used);
    }
    std::memcpy(bytes,original,sizeof(bytes));SetFloat(bytes+107,0);Reject(bytes,used);
    std::memcpy(bytes,original,sizeof(bytes));SetFloat(bytes+167,0);Reject(bytes,used);

    for(std::uint32_t index:{0x7FFFFFFFu,0x80000000u,0xFFFFFFFFu}) {
        std::memcpy(bytes,original,sizeof(bytes));Set32(bytes+63,index);Reject(bytes,used);
    }
    for(unsigned offset:{87u,95u})for(float value:{nan,infinity,-infinity}) {
        std::memcpy(bytes,original,sizeof(bytes));SetFloat(bytes+offset,value);Reject(bytes,used);
    }


    // A valid real shot before a malformed semantic start must never be
    // published to Apply: Decode validates the complete batch first.
    wire::Event shotBeforeBadControl[2]{Shot(1),Control(2)};
    unsigned char lateMalformed[171]{};
    std::size_t lateSize=0;
    for(unsigned phase:{0u,1u}) {
        shotBeforeBadControl[1].phase=static_cast<std::uint8_t>(phase);
        Check(wire::Encode(shotBeforeBadControl,2,lateMalformed,sizeof(lateMalformed),&lateSize),
              "valid shot followed by valid semantic start encodes");
        std::memset(lateMalformed+wire::kHeaderBytes+wire::kShotBytes+22,0,8);
        Reject(lateMalformed,lateSize);
    }

    wire::Event shotBeforeCrossControl[2]{Shot(1),Control(2)};
    for(unsigned group:{31u,32u}) {
        const bool mainGun=group==32;
        for(unsigned phase=mainGun ? 0u : 6u;phase<(mainGun ? 6u : 8u);++phase) {
            shotBeforeCrossControl[1]=Control(2);shotBeforeCrossControl[1].key=(group<<16)|63u;
            shotBeforeCrossControl[1].phase=static_cast<std::uint8_t>(mainGun ? 6u : 0u);
            Check(wire::Encode(shotBeforeCrossControl,2,lateMalformed,sizeof(lateMalformed),&lateSize),
                  "valid shot before correctly typed foot or main control encodes");
            lateMalformed[wire::kHeaderBytes+wire::kShotBytes+13]=static_cast<unsigned char>(phase);
            Reject(lateMalformed,lateSize);
        }
    }

    wire::Event actualAfterShot[2]{Shot(1),Shot(2)};
    unsigned char lateActual[250]{};
    for(unsigned word:{1u,3u}) {
        Check(wire::Encode(actualAfterShot,2,lateActual,sizeof(lateActual),&lateSize) && lateSize==250,
              "two valid actual-parameter shots encode before corruption");
        SetFloat(lateActual+wire::kHeaderBytes+wire::kShotBytes+33+word*4,nan);
        Reject(lateActual,lateSize);
    }

    auto BadEncode=[&](const wire::Event* events,unsigned total,std::size_t cap) {
        std::memset(bytes,0xA5,sizeof(bytes));std::memcpy(original,bytes,sizeof(bytes));used=999;
        Check(!wire::Encode(events,total,bytes,cap,&used) && !used,"bad encode rejected before publication");
        Check(!std::memcmp(bytes,original,sizeof(bytes)),"bad encode does not partially overwrite packet");
    };
    for(unsigned phase:{0u,1u}) {
        changed=control;changed.phase=static_cast<std::uint8_t>(phase);changed.seed=0;
        BadEncode(&changed,1,sizeof(bytes));
    }
    for(unsigned group:{31u,32u}) {
        const bool mainGun=group==32;
        for(unsigned phase=mainGun ? 0u : 6u;phase<(mainGun ? 6u : 8u);++phase) {
            changed=control;changed.key=(group<<16)|63u;changed.phase=static_cast<std::uint8_t>(phase);
            Check(!wire::Valid(changed),"foot and main lifecycle phases cannot be interchanged");
            BadEncode(&changed,1,sizeof(bytes));
        }
    }
    BadEncode(nullptr,2,sizeof(bytes));BadEncode(input,0,sizeof(bytes));BadEncode(input,wire::kMaxBatch+1,sizeof(bytes));
    BadEncode(input,2,170);BadEncode(input,2,0);
    Check(!wire::Encode(input,2,nullptr,171,&used) && !used,"null packet destination rejected");
    input[1].serial=input[0].serial;BadEncode(input,2,sizeof(bytes));
    input[1].serial=input[0].serial-1;BadEncode(input,2,sizeof(bytes));
    input[1]=shot;input[1].matrix[0]=nan;BadEncode(input,2,sizeof(bytes));
    for(unsigned i=0;i<wire::kMaxBatch;++i)input[i]=Control(i+1);
    Check(wire::Encode(input,wire::kMaxBatch,bytes,wire::kMaxBytes,&used) && used==512,"maximum control batch fills the packet");
    Check(wire::Decode(bytes,used,output,wire::kMaxBatch,&count) && count==wire::kMaxBatch,"maximum control batch roundtrips");
    for(unsigned i=0;i<6;++i)input[i]=Shot(i+1);
    Check(wire::Encode(input,4,bytes,wire::kMaxBytes,&used) && used==492,"four actual-parameter shot frames fit in one packet");
    BadEncode(input,5,sizeof(bytes));
    for(unsigned i=0;i<10;++i)input[i]=i<1 ? Shot(i+1) : Control(i+1);
    Check(wire::Encode(input,10,bytes,wire::kMaxBytes,&used) && used==507,"mixed capacity uses encoded bytes not padded Event size");
    Check(wire::Decode(bytes,used,output,wire::kMaxBatch,&count) && count==10,"near-full mixed batch roundtrips");
    input[0]=Control(std::numeric_limits<std::uint64_t>::max()-1);input[1]=Shot(std::numeric_limits<std::uint64_t>::max());
    input[0].phase=2;input[0].seed=0;input[1].seed=0;
    Check(wire::Encode(input,2,bytes,wire::kMaxBytes,nullptr),"optional encoded size output supported");
    Check(wire::Decode(bytes,171,output,wire::kMaxBatch,&count) && output[1].serial==std::numeric_limits<std::uint64_t>::max()
        && !output[0].seed && !output[1].seed,"maximum serial and legitimate zero native seeds preserved");
    for(unsigned phase:{6u,7u}) {
        changed=Control(1);changed.key=(32u<<16)|63u;changed.phase=static_cast<std::uint8_t>(phase);
        changed.commandSequence=changed.seed=0;
        Check(wire::Encode(&changed,1,bytes,wire::kMaxBytes,&used) && used==50,
              "native main fire and idle use the same control frame with legal zero seed and sequence");
        Check(bytes[17]==63 && bytes[19]==32,"main row key uses the canonical little-endian group and ordinal");
        Check(wire::Decode(bytes,used,output,wire::kMaxBatch,&count) && count==1
            && output[0].key==changed.key && output[0].phase==phase && !output[0].seed && !output[0].commandSequence,
              "main gun lifecycle controls retain their exact row and phase");
    }
    for(std::uint32_t index:{0u,40u,0x7FFFFFFEu}) {
        changed=Shot(1);changed.key=(32u<<16)|63u;changed.shotIndex=index;changed.seed=0;
        Check(wire::Encode(&changed,1,bytes,wire::kMaxBytes,&used) && used==129,
              "first-shot, already-active takeover and last legal index encode with zero native seed");
        Check(wire::Decode(bytes,used,output,wire::kMaxBatch,&count) && count==1
            && output[0].key==changed.key && output[0].shotIndex==index && !output[0].seed,
              "exact main-gun shot counter roundtrips without manufacturing a first shot");
    }
    static_assert(wire::kEntityBytes==wire::kShotBytes+40,"entity suffix never inflates an ordinary bullet record");
    auto entity=Entity(2);
    Check(wire::Valid(entity) && wire::EventBytes(entity)==161,"entity shot adds exactly team plus canonical target and lock ordinal");
    changed=entity;changed.entityTeam=0;Check(wire::Valid(changed),"entity team retains zero native value");
    changed.entityTeam=0x80000000u;Check(wire::Valid(changed),"entity team retains the complete uint32 domain");
    changed.lockOrdinal=0xFFFFFFFFu;Check(wire::Valid(changed),"nonempty entity target retains the full native lock ordinal");
    for(unsigned i=20;i<24;++i) {
        changed=entity;changed.targetActorId[i]=1;
        Check(!wire::Valid(changed) && !wire::EventBytes(changed),"each canonical target reserved byte must remain zero");
    }
    // A player target (the enemy player a controlled Deiroi shoots at in PvP): its reference, the slot and the tag.
    changed=entity;std::memset(changed.targetActorId,0,32);crew::netref::MakePlayer(3,changed.targetActorId);
    Check(wire::Valid(changed) && wire::EventBytes(changed)==161,"a player target by its reference (net_object_ref.h)");
    changed.targetActorId[8]=1;Check(!wire::Valid(changed),"a tagged target with other bytes set: refused");
    changed=entity;std::memset(changed.targetActorId,0,32);
    Check(!wire::Valid(changed),"an empty actor ID cannot carry a nonzero lock ordinal");
    changed.lockOrdinal=0;Check(wire::Valid(changed),"an unguided entity has an all-zero target and zero ordinal");
    changed.targetActorId[31]=1;changed.lockOrdinal=0xFFFFFFFFu;
    Check(wire::Valid(changed),"a nonzero canonical suffix is a full actor identity and must not be truncated");
    changed=entity;changed.actual[1]=FloatBits(infinity);
    Check(!wire::Valid(changed),"entity embedded actual speed uses the same finite-float contract");
    changed=entity;changed.actual[3]=FloatBits(nan);
    Check(!wire::Valid(changed),"entity embedded actual power uses the same finite-float contract");
    changed=entity;changed.matrix[15]=0;
    Check(!wire::Valid(changed),"entity shot retains the original affine matrix validation");
    changed=entity;changed.shotIndex=0x7FFFFFFFu;
    Check(!wire::Valid(changed),"entity ordinal cannot overflow the native signed attempt counter");
    unsigned char entityBytes[wire::kMaxBytes+1]{},entitySaved[wire::kMaxBytes+1]{};
    std::size_t entitySize=0;
    wire::Event controlThenEntity[2]{Control(1),entity};
    Check(wire::Encode(controlThenEntity,2,entityBytes,sizeof(entityBytes),&entitySize) && entitySize==211,
          "control followed by entity uses its exact kind-specific length");
    Check(entityBytes[50]==3 && entityBytes[171]==0xFF && entityBytes[174]==0xFF,
          "entity team follows the unchanged 121-byte shot prefix");
    Check(entityBytes[175]==0x40 && entityBytes[194]==0x53 && !entityBytes[195] && !entityBytes[198]
        && entityBytes[199]==0x58 && entityBytes[206]==0x5F,"all 32 actor identity bytes are preserved with canonical reserved word");
    Check(entityBytes[207]==0x40 && entityBytes[208]==0x30 && entityBytes[209]==0x20 && entityBytes[210]==0x10,
          "lock ordinal is the final little-endian uint32");
    Check(wire::Decode(entityBytes,entitySize,output,wire::kMaxBatch,&count) && count==2
        && output[1].kind==wire::Kind::entity && output[1].entityTeam==entity.entityTeam
        && output[1].lockOrdinal==entity.lockOrdinal && !std::memcmp(output[1].targetActorId,entity.targetActorId,32)
        && !std::memcmp(output[1].actual,entity.actual,sizeof(entity.actual))
        && !std::memcmp(output[1].matrix,entity.matrix,sizeof(entity.matrix)),
          "entity roundtrip includes its embedded actual data and complete target identity");
    Check(!output[0].entityTeam && !output[0].targetActorId[0] && !output[0].lockOrdinal,
          "control records do not inherit hidden entity metadata");
    std::memcpy(entitySaved,entityBytes,sizeof(entityBytes));
    for(std::size_t size=0;size<entitySize;++size)Reject(entityBytes,size);
    Reject(entityBytes,entitySize+1);
    for(unsigned reserved=195;reserved<199;++reserved) {
        std::memcpy(entityBytes,entitySaved,sizeof(entityBytes));entityBytes[reserved]=0xFF;
        Reject(entityBytes,entitySize);
    }
    std::memcpy(entityBytes,entitySaved,sizeof(entityBytes));std::memset(entityBytes+175,0,32);
    Reject(entityBytes,entitySize);
    Set32(entityBytes+207,0);
    Check(wire::Decode(entityBytes,entitySize,output,wire::kMaxBatch,&count) && !output[1].lockOrdinal
        && !output[1].targetActorId[0] && !output[1].targetActorId[31],"zero target plus zero lock is a valid wire entity");
    std::memcpy(entityBytes,entitySaved,sizeof(entityBytes));SetFloat(entityBytes+107,nan);
    Reject(entityBytes,entitySize);
    std::memcpy(entityBytes,entitySaved,sizeof(entityBytes));SetFloat(entityBytes+87,infinity);
    Reject(entityBytes,entitySize);
    std::memcpy(entityBytes,entitySaved,sizeof(entityBytes));SetFloat(entityBytes+95,nan);
    Reject(entityBytes,entitySize);

    // Changing the kind cannot silently consume the old bullet length or suffix.
    auto plain=Shot(1);entity=Entity(1);
    Check(wire::Encode(&plain,1,bytes,sizeof(bytes),&used) && used==129,"ordinary bullet remains exactly 121 bytes");
    Check(wire::Encode(&entity,1,entityBytes,sizeof(entityBytes),&entitySize) && entitySize==169,"entity carries its separate 161-byte record");
    Check(!std::memcmp(bytes+9,entityBytes+9,120),"bullet and entity share the entire exact prefix except their kind byte");
    bytes[8]=3;Reject(bytes,used);
    entityBytes[8]=2;Reject(entityBytes,entitySize);
    changed=entity;changed.kind=wire::Kind::shot;changed.targetActorId[20]=1;changed.lockOrdinal=0xFFFFFFFFu;
    Check(wire::Encode(&changed,1,bytes,sizeof(bytes),&used) && used==129,"nonentity kind ignores fields absent from its wire schema");
    Check(wire::Decode(bytes,used,output,wire::kMaxBatch,&count) && !output[0].entityTeam && !output[0].lockOrdinal
        && !output[0].targetActorId[20],"ordinary shot decode clears every nonwire entity field");
    changed=entity;changed.targetActorId[20]=1;BadEncode(&changed,1,sizeof(bytes));
    changed=entity;std::memset(changed.targetActorId,0,32);BadEncode(&changed,1,sizeof(bytes));
    BadEncode(&entity,1,168);

    wire::Event mixedKinds[3]{Control(1),Shot(2),Entity(3)};
    Check(wire::Encode(mixedKinds,3,entityBytes,sizeof(entityBytes),&entitySize) && entitySize==332,
          "one batch can encode all three independent record schemas");
    Check(wire::Decode(entityBytes,entitySize,output,wire::kMaxBatch,&count) && count==3
        && output[0].kind==wire::Kind::control && output[1].kind==wire::Kind::shot && output[2].kind==wire::Kind::entity,
          "mixed records preserve kind and serial order");
    entityBytes[8+42+121+125+20]=1;
    Reject(entityBytes,entitySize); // a malformed target follows an otherwise valid real shot

    wire::Event entityBatch[4]{Entity(1),Entity(2),Entity(3),Entity(4)};
    Check(wire::Encode(entityBatch,3,entityBytes,sizeof(entityBytes),&entitySize) && entitySize==491,
          "three full entity shots fit in a single 512-byte packet");
    Check(wire::Decode(entityBytes,entitySize,output,wire::kMaxBatch,&count) && count==3,
          "near-full entity batch roundtrips");
    BadEncode(entityBatch,4,sizeof(bytes));
    entityBatch[2].serial=entityBatch[1].serial;BadEncode(entityBatch,3,sizeof(bytes));
    wire::Event mixedEntity[7]{Entity(1),Shot(2),Control(3),Control(4),Control(5),Control(6),Control(7)};
    Check(wire::Encode(mixedEntity,7,entityBytes,sizeof(entityBytes),&entitySize) && entitySize==500,
          "entity, bullet and controls obey actual byte capacity rather than padded event size");
    Check(wire::Decode(entityBytes,entitySize,output,wire::kMaxBatch,&count) && count==7,
          "near-full three-kind batch roundtrips");
    std::printf("Deiroi event codec: %d checks passed\n",checks);
}
