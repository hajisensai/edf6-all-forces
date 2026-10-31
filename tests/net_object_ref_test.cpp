// net_object_ref.h and wire.h's Ref: an object's reference on the wire (a player by its mission slot, anything else by its
// native NetId). Pure, no game.
#include "net_object_ref.h"
#include "wire.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
using namespace crew;
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}

void Kinds() {
    unsigned char ref[32]{};
    int slot=7;
    Check(netref::Classify(ref,&slot)==netref::Kind::none && slot==-1,"all zero: none");
    Check(netref::Classify(nullptr)==netref::Kind::none,"null: none");
    Check(netref::Valid(ref,true) && !netref::Valid(ref,false),"none only when allowed");

    unsigned char native[32]{};
    for(int i=0;i<20;++i)native[i]=static_cast<unsigned char>(i+1);
    native[24]=0x77;native[31]=0x10;
    Check(netref::MakeNative(native,ref) && !std::memcmp(ref,native,32),"a NetId copies as it is");
    Check(netref::Classify(ref,&slot)==netref::Kind::native && slot==-1,"a NetId is native");
    Check(netref::Valid(ref,false) && !netref::Valid(ref,false,true),"a NetId is no player");
    Check(netref::ValidNative(ref,false),"a NetId is a unit's");
    // A NetId's first word may be anything: it never reads as a player (the tag is where a NetId is zero).
    unsigned char looksOld[32]{};
    looksOld[0]=0x41;looksOld[1]=0x46;looksOld[2]=0x50;looksOld[3]=0x52;looksOld[4]=1;   // the old "AFPR" + slot 1 layout
    Check(netref::Classify(looksOld)==netref::Kind::native,"the old requester layout is a NetId's now, never a player");
    unsigned char zero[32]{};
    Check(!netref::MakeNative(zero,ref) && netref::IsNone(ref),"an all-zero NetId is none");
    native[21]=1;
    Check(!netref::MakeNative(native,ref) && netref::IsNone(ref),"bytes 20..23 set: not a NetId");
}

void Players() {
    unsigned char ref[32]{};
    int slot=-1;
    for(int s:{0,1,3,1023}) {
        Check(netref::MakePlayer(s,ref),"a slot in range makes a player");
        Check(netref::Classify(ref,&slot)==netref::Kind::player && slot==s,"its slot comes back");
        Check(netref::Valid(ref,false,true) && !netref::ValidNative(ref,true),"a player is a player, never a unit");
        Check(ref[20]==0x41 && ref[21]==0x46 && ref[22]==0x50 && ref[23]==0x52,"the tag at bytes 20..23");
    }
    Check(!netref::MakePlayer(1024,ref) && netref::IsNone(ref),"slot 1024 rejected");
    Check(!netref::MakePlayer(-1,ref) && netref::IsNone(ref),"a negative slot rejected");
    netref::MakePlayer(5,ref);
    unsigned char bad[32];
    std::memcpy(bad,ref,32);bad[0]=0;bad[1]=4;   // slot 1024
    Check(netref::Classify(bad)==netref::Kind::invalid,"a tagged slot past 1024: invalid");
    std::memcpy(bad,ref,32);bad[8]=1;
    Check(netref::Classify(bad)==netref::Kind::invalid && !netref::Valid(bad,true),"a tagged ref with other bytes: invalid");
    std::memcpy(bad,ref,32);bad[30]=1;
    Check(netref::Classify(bad)==netref::Kind::invalid,"a tagged ref with a byte after the tag: invalid");
    std::memcpy(bad,ref,32);bad[22]=0x51;
    Check(netref::Classify(bad)==netref::Kind::invalid && !netref::Valid(bad,true),"bytes 20..23 neither zero nor the tag: invalid");
    unsigned char other[32]{};netref::MakePlayer(6,other);
    Check(netref::Same(ref,ref) && !netref::Same(ref,other),"same compares every byte");
}

void Wire() {
    unsigned char player[32]{},native[32]{},bad[32]{};
    netref::MakePlayer(2,player);
    for(int i=0;i<20;++i)native[i]=static_cast<unsigned char>(0xA0+i);
    std::memcpy(bad,native,32);bad[20]=9;
    unsigned char bytes[100];
    wire::Writer w(bytes,sizeof bytes);
    w.Ref(player,false,true).Ref(native,false).Ref(nullptr);
    Check(w.Ok() && w.Used()==96,"a player, a NetId and none written");
    wire::Reader r(bytes,sizeof bytes);
    unsigned char a[32],b[32],c[32];
    r.Ref(a,false,true);r.Ref(b,false);r.Ref(c);
    Check(r.Ok() && !std::memcmp(a,player,32) && !std::memcmp(b,native,32) && netref::IsNone(c),"read back as written");
    wire::Writer w2(bytes,sizeof bytes);
    Check(!w2.Ref(native,false,true).Ok(),"a NetId where a player must be: the message fails");
    wire::Writer w3(bytes,sizeof bytes);
    Check(!w3.Ref(bad).Ok(),"an invalid reference fails the message");
    wire::Writer w4(bytes,sizeof bytes);
    Check(!w4.Ref(nullptr,false).Ok(),"none where one is needed fails the message");
    std::memcpy(bytes,bad,32);
    wire::Reader r2(bytes,sizeof bytes);
    r2.Ref(a);
    Check(!r2.Ok() && netref::IsNone(a),"an invalid reference read: the message fails, zeros out");
    std::memcpy(bytes,native,32);
    wire::Reader r3(bytes,sizeof bytes);
    r3.Ref(a,false,true);
    Check(!r3.Ok(),"a NetId read where a player must be: the message fails");
    // Id (native IDs alone) keeps refusing a player's reference: its tag is where a NetId is zero.
    std::memcpy(bytes,player,32);
    wire::Reader r4(bytes,sizeof bytes);
    r4.Id(a);
    Check(!r4.Ok(),"a player's reference is no native ID");
}
}  // namespace

int main() {
    Kinds();Players();Wire();
    std::printf("net_object_ref_test: %d checks OK\n",checks);
    return 0;
}
