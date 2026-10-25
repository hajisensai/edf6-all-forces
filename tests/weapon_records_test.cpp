// src/weapon_records_logic.h: the site list, the relocated constants, the sidecar, the two copies of the table.
#include "weapon_records_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
using namespace crew::wrec;

void Sites() {
    std::size_t perValue[12]{};
    const std::uint32_t values[]={0xEB50,0xEB54,0xEB44,0x7CE8,0x7CEC,0x7CF4,0x139C,0xA69,0x800,0x7FF,0x150A0};
    bool consistent=true,ordered=true,known=true;
    for(std::size_t i=0;i<kSiteCount;++i) {
        const Site& s=kSites[i];
        consistent=consistent && SiteConsistent(s);
        if(i)ordered=ordered && kSites[i-1].rva+kSites[i-1].size<=s.rva;   // sorted, no two rows overlap
        bool found=false;
        for(std::size_t v=0;v<std::size(values);++v)if(values[v]==s.value){++perValue[v];found=true;}
        known=known && found;
    }
    Check(consistent,"every row: the constant sits at +field of its own bytes, and is one we relocate");
    Check(ordered,"rows sorted by address, none overlapping another");
    Check(known,"every row's constant is in the relocation list");
    Check(kSiteCount==100,"100 sites (docs/loadout-re.md 8.8)");
    // The weapon select screen's reads are jumped over whole (UiStub), never relocated in place: listing 0x8B4F70 as a
    // constant made the jump's write find patched bytes and roll the whole relocation back (build 471bfc5).
    bool uiApart=true;
    for(const Site& s:kSites)uiApart=uiApart && !Overlap(s.rva,s.size,kUiOwned,kUiReadSize) && !Overlap(s.rva,s.size,kUiStatus,kUiReadSize);
    Check(uiApart,"no constant site shares a byte with the screen's two redirected reads");
    Check(PatchesApart(),"no redirected call or read shares a byte with a constant site or another patch");
    Check(Overlap(0x8B4F70,8,kUiOwned,kUiReadSize) && !Overlap(0x8B4F68,8,kUiOwned,kUiReadSize),"the overlap test itself");
    Check(perValue[10]==12,"twelve allocations of the game status (the singleton's inlined new)");
    Check(perValue[8]+perValue[9]==9,"nine record counts: 0xDF130/50/70, 0xDF600/20/40, 0xDF6C0, 0xDF720, 0xAECDE");
    Check(perValue[6]==7 && perValue[7]==10,"the folded forms: 0x139C seven times, 0xA69 ten times");
    Check(perValue[2]==1,"one loop pointer one record behind (0xAECFB)");
    // the block init 0xDEDA0 (stack and record copies too) is not in the list
    bool blockInit=false;
    for(const Site& s:kSites)blockInit=blockInit || (s.rva>=0xDEDA0 && s.rva<0xDEF86);
    Check(!blockInit,"0xDEDA0 is not patched (its blocks have no relocated table)");
    std::uint8_t out[12];
    bool patched=true;
    for(const Site& s:kSites) {
        Patched(s,out);
        std::uint32_t v=0;
        std::memcpy(&v,out+s.field,4);
        bool rest=true;
        for(std::size_t b=0;b<s.size;++b)if(b<s.field || b>=s.field+4u)rest=rest && out[b]==s.code[b];
        patched=patched && v==Relocated(s.value) && rest;
    }
    Check(patched,"a patched instruction differs only in its constant");
}

void Constants() {
    Check(Relocated(0xEB50)==0x150A8 && Relocated(0xEB54)==0x150AC && Relocated(0xEB44)==0x1509C,"GS-based offsets");
    Check(Relocated(0x7CE8)==0xE240 && Relocated(0x7CEC)==0xE244 && Relocated(0x7CF4)==0xE24C,"block-based offsets (block = GS+0x6E68)");
    Check(Relocated(0x7CF4)-Relocated(0x7CEC)==0x7CF4-0x7CEC,"0xDF640's memset bound keeps its distance to the stars");
    Check(Relocated(0x139C)==0x1C0E && Relocated(0xA69)==0x12DB,"folded K constants");
    bool folded=true;
    for(std::uint32_t id=0;id<kSlots;id+=97) {
        folded=folded && (id+Relocated(0x139C))*kRecord==kNewTable+id*kRecord;
        folded=folded && (id+Relocated(0xA69))*kRecord==kNewInBlock+4+id*kRecord;
        folded=folded && (id+0x139Cu)*kRecord==0xEB50+id*kRecord && (id+0xA69u)*kRecord==0x7CEC+id*kRecord;
    }
    Check(folded,"(id + K) * 12 lands on the relocated records for every id");
    Check(Relocated(0x800)==kSlots && Relocated(0x7FF)==kSlots-1,"record counts");
    Check(Relocated(0x150A0)==0x150A8+kSlots*12,"the object grows by the table");
    Check(kSlots>=0x1000,"a generous bound (tools/call_weapons.py SAVE_WEAPONS follows it)");
    Check(Relocated(0x1234)==0,"an unrelated constant is not relocated");
}

void Sidecar() {
    std::vector<std::uint8_t> ext(kExtBytes,0),back(kExtBytes,1),file(kSidecarSize+16);
    Check(UsedRows(ext.data())==0,"no rows past 0x800");
    Check(Serialize(ext.data(),file.data())==kHeader,"no rows: a header only");
    Check(Deserialize(file.data(),kHeader,back.data())==Parse::kOk && back==ext,"a header-only file reads back empty");
    ext[0]=1;                                    // row 0x800 owned
    ext[5*kRecord]=5;ext[5*kRecord+4]=3;         // row 0x805 NEW|owned, 3 stars on the first stat
    Check(UsedRows(ext.data())==6,"rows up to the last one in use");
    const std::size_t size=Serialize(ext.data(),file.data());
    Check(size==kHeader+6*kRecord,"only the rows in use are written");
    Check(SidecarRows(file.data(),size)==6,"the file says how many rows");
    Check(Deserialize(file.data(),size,back.data())==Parse::kOk && back==ext,"round trip");
    Check(Deserialize(file.data(),size-1,back.data())==Parse::kShort,"truncated");
    bool cleared=true;
    for(std::uint8_t b:back)cleared=cleared && b==0;
    Check(cleared,"a refused file leaves the rows zero");
    file[kHeader+1]^=0x40;
    Check(Deserialize(file.data(),size,back.data())==Parse::kChecksum,"a changed record");
    file[kHeader+1]^=0x40;
    file[0]='X';
    Check(Deserialize(file.data(),size,back.data())==Parse::kMagic,"not ours");
    file[0]='E';
    std::uint32_t first=0x900;
    std::memcpy(file.data()+12,&first,4);
    Check(Deserialize(file.data(),size,back.data())==Parse::kLayout,"rows counted from another first row");
    std::vector<std::uint8_t> last(kExtBytes,0);
    last[kExtBytes-kRecord]=1;                   // the very last slot
    Check(Serialize(last.data(),file.data())==kSidecarSize && Deserialize(file.data(),kSidecarSize,back.data())==Parse::kOk && back==last,
          "the last slot round-trips (a full file)");
}

void Copies() {
    std::vector<std::uint8_t> gs(kNewObjectSize,0);
    for(std::size_t i=0;i<kTableBytes;++i)gs[kNewTable+i]=static_cast<std::uint8_t>(i*7+3);
    MirrorToBlock(gs.data());
    Check(std::memcmp(gs.data()+kOldTable,gs.data()+kNewTable,kNativeBytes)==0,"mirror: rows 0..0x7FF into the block's table");
    Check(kBlock+kBlockSize==0x14B50 && kOldTable+kNativeBytes==0x14B50,"the block's table ends at GS+0x14B50");
    for(std::size_t i=0;i<kNativeBytes;++i)gs[kOldTable+i]=static_cast<std::uint8_t>(i*5+1);
    const std::uint8_t ext0=Ext(gs.data())[0];
    ReadFromBlock(gs.data());
    Check(std::memcmp(gs.data()+kOldTable,gs.data()+kNewTable,kNativeBytes)==0,"read back: rows 0..0x7FF from the block");
    Check(Ext(gs.data())[0]==ext0 && Ext(gs.data())==gs.data()+kNewTable+kNativeBytes,"rows 0x800.. untouched by either");
    const std::uint64_t before=Hash(gs.data()+kOldTable,kNativeBytes);
    gs[kOldTable+kNativeBytes-1]^=1;   // the last star byte of row 0x7FF
    Check(Hash(gs.data()+kOldTable,kNativeBytes)!=before,"the copy hash tells two tables apart by one byte");
    Check(kNewInBlock==kNewTable-kBlock && kOldInBlock==0x7CE8,"block-relative offsets");
}
}  // namespace

void SavePairing() {
    const std::uint64_t none[1]{};
    Check(PickSave(none,0,7)==0,"no request: none taken");
    const std::uint64_t one[]={7};
    Check(PickSave(one,1,7)==0 && PickSave(one,1,8)==1,"the one request with the write's table, else none");
    // A job that ended before its write (request 0), then a request with the same table (1): the write is request 1's.
    const std::uint64_t stale[]={7,7};
    Check(PickSave(stale,2,7)==1,"a request never written does not shadow a later one with the same table");
    // Interleaved tables: each write takes the newest request with its own table.
    const std::uint64_t mixed[]={7,9,7,9};
    Check(PickSave(mixed,4,7)==2 && PickSave(mixed,4,9)==3,"the newest with the write's table");
}

int main() {
    Sites();
    Constants();
    Sidecar();
    Copies();
    SavePairing();
    std::printf("weapon_records: %d checks, %d failures\n",checks,failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
