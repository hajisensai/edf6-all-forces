// The first person's muzzle flash edit (src/fp_flash.h), offline on a document laid out as the game's (0x52DE0 /
// 0x52F50 read it; pylib/dsgo.py's nodes):
//  - the beams' sizes ([1] [2] [5] [6]) and the colour's r, g, b scaled, its alpha and every other element untouched;
//  - put back byte for byte;
//  - the sprite layout ([1] the size, [2] the colour);
//  - not the layout (a short list, an overridden node, a size that is not a double, a colour that is not four, another
//    handle kind): false, nothing written.
// Exit code 1 when one fails. cmake --build build --target fp_flash_check && build\fp_flash_check.exe
#include "../src/fp_flash.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace crew::fpflash;
int failures=0;

void Expect(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

// A document: node 0 the list of `n` elements (doubles 1.5 + k, a string at [0]), element `colourAt` a list of four.
struct Doc {
    std::vector<unsigned char> file;
    alignas(8) unsigned char doc[0x60]{};
    alignas(8) unsigned char variant[0x18]{};
    std::vector<std::uint32_t> map;
    unsigned char slot[40]{};
};

void Node(std::vector<unsigned char>& f,std::size_t at,std::uint64_t value,std::uint32_t type) {
    std::memcpy(f.data()+at,&value,8);std::memcpy(f.data()+at+8,&type,4);
}

void Build(Doc& d,unsigned n,unsigned colourAt,unsigned channels=4) {
    const unsigned nodes=1+n+channels;
    const std::size_t table=16,blocks=table+nodes*16;
    d.file.assign(blocks+2*(16+4*16),0);
    std::memcpy(d.file.data(),"DSGO",4);
    const std::uint32_t off=16,count=nodes,size=16;
    std::memcpy(d.file.data()+4,&off,4);std::memcpy(d.file.data()+8,&count,4);std::memcpy(d.file.data()+12,&size,4);
    auto block=[&](std::size_t at,unsigned kids,unsigned first) {
        const std::uint32_t idx=16;
        std::memcpy(d.file.data()+at+8,&idx,4);std::memcpy(d.file.data()+at+12,&kids,4);
        for(unsigned k=0;k<kids;++k){const std::uint32_t c=first+k;std::memcpy(d.file.data()+at+16+4*k,&c,4);}
    };
    Node(d.file,table,blocks-table,kContainer);block(blocks,n,1);
    for(unsigned k=0;k<n;++k) {
        const std::size_t at=table+(1+k)*16;
        if(k==0){Node(d.file,at,0,1);continue;}                     // the texture's name
        if(k==colourAt){Node(d.file,at,blocks+16+4*16-at,kContainer);block(blocks+16+4*16,channels,1+n);continue;}
        double v=1.5+k;std::uint64_t raw;std::memcpy(&raw,&v,8);Node(d.file,at,raw,kDouble);
    }
    for(unsigned c=0;c<channels;++c){double v=2.0+c;std::uint64_t raw;std::memcpy(&raw,&v,8);Node(d.file,table+(1+n+c)*16,raw,kDouble);}
    d.map.assign(nodes,0xFFFFFFFFu);
    const unsigned char* file=d.file.data();const std::uint32_t* map=d.map.data();const unsigned char* slots=d.slot;
    const std::uint64_t mapped=d.map.size();
    std::memcpy(d.doc+kDocMap,&map,8);std::memcpy(d.doc+kDocMapCount,&mapped,8);std::memcpy(d.doc+kDocSlots,&slots,8);
    std::memcpy(d.doc+kDocFile,&file,8);
    const unsigned char* doc=d.doc;const std::uint32_t list=0;const std::uint16_t tag=kTagDocument;
    std::memcpy(d.variant,&doc,8);std::memcpy(d.variant+8,&list,4);std::memcpy(d.variant+kVariantTag,&tag,2);
}

double At(const Doc& d,unsigned node) { double v;std::memcpy(&v,d.file.data()+16+node*16,8);return v; }
const auto any=[](const void*,std::size_t,bool){return true;};

void Beams() {
    Doc d;Build(d,10,8);
    const std::vector<unsigned char> before=d.file;
    Edit e;
    const bool ok=Scale(d.variant,kBeams,0.25,0.5,&e,any);
    bool sizes=true,others=true;
    for(unsigned k=1;k<10;++k) {
        if(k==8)continue;
        const double want=(k==1 || k==2 || k==5 || k==6) ? (1.5+k)*0.25 : 1.5+k;
        (k==1 || k==2 || k==5 || k==6 ? sizes : others)&=std::fabs(At(d,1+k)-want)<1e-12;
    }
    const bool colour=std::fabs(At(d,11)-1.0)<1e-12 && std::fabs(At(d,12)-1.5)<1e-12 && std::fabs(At(d,13)-2.0)<1e-12 &&
                      std::fabs(At(d,14)-5.0)<1e-12;
    Expect(ok && e.n==7 && sizes,"beams: the lengths and widths [1] [2] [5] [6] scaled");
    Expect(others,"beams: the turn, the counts and the rest untouched");
    Expect(colour,"beams: the colour's r, g, b scaled, its alpha kept");
    Restore(e);
    Expect(d.file==before && e.n==0,"put back byte for byte");
}

void Sprite() {
    Doc d;Build(d,4,2);
    Edit e;
    const bool ok=Scale(d.variant,kSprite,0.25,0.5,&e,any);
    Expect(ok && e.n==4 && std::fabs(At(d,2)-2.5*0.25)<1e-12 && std::fabs(At(d,4)-4.5)<1e-12 && std::fabs(At(d,5)-1.0)<1e-12 &&
           std::fabs(At(d,8)-5.0)<1e-12,"sprite: the size [1] and the colour [2] scaled, the int [3] kept");
    Restore(e);
}

void NotTheLayout() {
    auto untouched=[](const char* what,Doc& d,const Layout& l) {
        const std::vector<unsigned char> before=d.file;
        Edit e;
        Expect(!Scale(d.variant,l,0.25,0.5,&e,any) && e.n==0 && d.file==before,what);
    };
    {Doc d;Build(d,4,2);untouched("a list shorter than the layout: native",d,kBeams);}
    {Doc d;Build(d,10,8);d.map[0]=0;untouched("an overridden list: native",d,kBeams);}
    {Doc d;Build(d,10,8);d.map[1+6]=0;untouched("an overridden size: native, the others not written either",d,kBeams);}
    {Doc d;Build(d,10,8);const std::uint32_t text=1;std::memcpy(d.file.data()+16+(1+5)*16+8,&text,4);
     untouched("a size that is not a double: native",d,kBeams);}
    {Doc d;Build(d,10,8,3);untouched("a colour that is not four: native",d,kBeams);}
    {Doc d;Build(d,10,8);const std::uint16_t tag=1;std::memcpy(d.variant+kVariantTag,&tag,2);untouched("another handle kind: native",d,kBeams);}
    {Doc d;Build(d,10,8);
     const auto readOnly=[&](const void* p,std::size_t,bool w){return !(w && p>=d.file.data() && p<d.file.data()+d.file.size());};
     Edit e;const std::vector<unsigned char> before=d.file;
     Expect(!Scale(d.variant,kBeams,0.25,0.5,&e,readOnly) && d.file==before,"a document that cannot be written: native");}
}
}  // namespace

int main() {
    Beams();
    Sprite();
    NotTheLayout();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
