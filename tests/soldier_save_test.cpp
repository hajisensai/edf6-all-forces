// src/soldier_save_logic.h: the new classes' save (a stock stand-in in the file, the real class in class 9's last
// slot), and the restore after a load.
#include "soldier_save_logic.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int failures=0,checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
using namespace crew::soldier_save;

// A block as the game status's constructor leaves it (0xDEDA0): class 0, every slot -1.
std::vector<std::uint8_t> FreshBlock() {
    std::vector<std::uint8_t> b(kBlockSize,0);
    for(std::size_t p=0;p<kLocalPlayers;++p) {
        WriteInt(b.data(),ClassOffset(p),0);
        for(int c=0;c<kReservedClasses;++c)
            for(int s=0;s<kClassSlots;++s)WriteInt(b.data(),p*kPlayerStride+kSlotsAt+4u*(std::size_t(c)*kClassSlots+s),kNoRecord);
    }
    return b;
}
int ClassOf(const std::vector<std::uint8_t>& b,std::size_t p) { return ReadInt(b.data(),ClassOffset(p)); }
std::uint32_t RecordOf(const std::vector<std::uint8_t>& b,std::size_t p) { return static_cast<std::uint32_t>(ReadInt(b.data(),RecordOffset(p))); }

void Layout() {
    Check(RecordOffset(0)==0x11C && 0x6E68+RecordOffset(0)==0x6F84,"player 0's record slot: GS+0x6F84 (class 9 slot 5)");
    Check(0x6E68+RecordOffset(1)==0xADE4,"player 1's record slot: GS+0xADE4");
    Check(0x6E68+ClassOffset(0)==0x6E90 && 0x6E68+ClassOffset(1)==0xACF0,"the current class: GS+0x6E90+p*0x3E60");
    // GS + 0x6E68 + 4*(p*0xF98 + 6*(class+2) + slot) (inject.md §4.1)
    Check(RecordOffset(1)==4u*(0xF98+6*(9+2)+5),"inject.md §4.1's formula");
    Check(RecordOffset(1)+4<=kBlockSize,"inside the saved block");
}

void Codes() {
    int r=0,s=0;
    Check(Decode(Encode(4,0),&r,&s) && r==4 && s==0,"round trip 4 / 0");
    Check(Decode(Encode(5,0),&r,&s) && r==5 && s==0,"round trip 5 / 0");
    Check(!Decode(kNoRecord,&r,&s),"the slot's initial -1 is no record");
    Check(!Decode(0,&r,&s) && !Decode(123,&r,&s),"a weapon id is no record");
    Check(!Decode(Encode(2,0),&r,&s),"a stock real class is no record");
    Check(!Decode(Encode(4,4),&r,&s),"a stand-in past the stock classes is no record");
}

void StandIn() {
    auto b=FreshBlock();
    WriteInt(b.data(),ClassOffset(0),4);
    SaveResult out[kLocalPlayers];
    Check(PrepareSaveCopy(b.data(),out)==1,"one stand-in");
    Check(ClassOf(b,0)==kStandIn && out[0].was==4 && out[0].now.standIn,"class 4 saved as Ranger");
    Check(RecordOf(b,0)==Encode(4,kStandIn),"the real class in class 9's last slot");
    Check(ClassOf(b,1)==0 && RecordOf(b,1)==kNoRecord && !out[1].now.standIn,"player 1 (Ranger) untouched, no record");
    // nothing else in the block moved
    auto ref=FreshBlock();
    WriteInt(ref.data(),RecordOffset(0),Encode(4,kStandIn));
    Check(b==ref,"only the class and the slot written");
    // a stock class clears a stale record
    auto c=FreshBlock();
    WriteInt(c.data(),ClassOffset(0),3);
    WriteInt(c.data(),RecordOffset(0),Encode(5,0));
    Check(PrepareSaveCopy(c.data(),out)==0 && ClassOf(c,0)==3 && RecordOf(c,0)==kNoRecord,"Fencer saved as is, the slot cleared");
    // an absurd class: never in the file
    Check(ForSave(0x1234).cls==kStandIn && ForSave(0x1234).record==kNoRecord,"a class past a byte: stand-in, no record");
    Check(ForSave(-1).cls==-1 && ForSave(-1).record==kNoRecord,"a negative class is not a new class");
}

void RestoreAfterLoad() {
    auto b=FreshBlock();
    WriteInt(b.data(),ClassOffset(0),5);
    SaveResult s[kLocalPlayers];
    PrepareSaveCopy(b.data(),s);
    LoadResult out[kLocalPlayers];
    Check(RestoreLoaded(b.data(),6,out)==1,"one restored");
    Check(ClassOf(b,0)==5 && out[0].now.what==Loaded::kRestored && out[0].was==kStandIn,"class 5 back");
    Check(RecordOf(b,0)==kNoRecord,"the slot cleared after the restore");
    Check(out[1].now.what==Loaded::kNone && ClassOf(b,1)==0,"player 1 without a record: as the file says");
}

void ChangedInStockGame() {
    auto b=FreshBlock();
    WriteInt(b.data(),ClassOffset(0),4);
    SaveResult s[kLocalPlayers];
    PrepareSaveCopy(b.data(),s);
    // the plugin removed, the player picks Wing Diver and saves: the stock game leaves class 9's slots alone
    WriteInt(b.data(),ClassOffset(0),1);
    LoadResult out[kLocalPlayers];
    Check(RestoreLoaded(b.data(),6,out)==0,"nothing restored");
    Check(ClassOf(b,0)==1 && out[0].now.what==Loaded::kChanged && out[0].now.real==4,"the stock game's Wing Diver kept");
    Check(RecordOf(b,0)==kNoRecord,"the record cleared");
    // loaded again: no record any more
    Check(RestoreLoaded(b.data(),6,out)==0 && out[0].now.what==Loaded::kNone && ClassOf(b,0)==1,"a second load: no record");
}

void NoRecord() {
    auto b=FreshBlock();
    WriteInt(b.data(),ClassOffset(0),2);
    WriteInt(b.data(),ClassOffset(1),3);
    LoadResult out[kLocalPlayers];
    Check(RestoreLoaded(b.data(),6,out)==0,"a stock save: nothing restored");
    Check(ClassOf(b,0)==2 && ClassOf(b,1)==3 && out[0].now.what==Loaded::kNone && out[1].now.what==Loaded::kNone,"classes as saved");
    Check(b==[&]{auto r=FreshBlock();WriteInt(r.data(),ClassOffset(0),2);WriteInt(r.data(),ClassOffset(1),3);return r;}(),
          "a stock save's block unchanged");
    // a save written while the class field held 4 itself (no stand-in): as the file says
    auto c=FreshBlock();
    WriteInt(c.data(),ClassOffset(0),4);
    Check(RestoreLoaded(c.data(),6,out)==0 && ClassOf(c,0)==4 && out[0].now.what==Loaded::kNone,"no record, class kept");
}

void Unavailable() {
    auto b=FreshBlock();
    WriteInt(b.data(),ClassOffset(0),4);
    SaveResult s[kLocalPlayers];
    PrepareSaveCopy(b.data(),s);
    LoadResult out[kLocalPlayers];
    Check(RestoreLoaded(b.data(),4,out)==0,"four classes this session: nothing restored");
    Check(ClassOf(b,0)==kStandIn && out[0].now.what==Loaded::kUnavailable && out[0].now.real==4,"the stand-in kept");
    Check(RecordOf(b,0)==kNoRecord,"the record cleared");
    auto c=FreshBlock();
    WriteInt(c.data(),ClassOffset(0),5);
    PrepareSaveCopy(c.data(),s);
    Check(RestoreLoaded(c.data(),0,out)==0 && ClassOf(c,0)==kStandIn,"class count unknown: no restore");
}

void TwoPlayers() {
    auto b=FreshBlock();
    WriteInt(b.data(),ClassOffset(0),4);
    WriteInt(b.data(),ClassOffset(1),5);
    SaveResult s[kLocalPlayers];
    Check(PrepareSaveCopy(b.data(),s)==2,"two stand-ins");
    Check(ClassOf(b,0)==kStandIn && ClassOf(b,1)==kStandIn,"both saved as Ranger");
    Check(RecordOf(b,0)==Encode(4,0) && RecordOf(b,1)==Encode(5,0),"each record in its own player's slot");
    // player 1 changed in a stock game, player 0 not
    WriteInt(b.data(),ClassOffset(1),2);
    LoadResult out[kLocalPlayers];
    Check(RestoreLoaded(b.data(),6,out)==1,"one restored");
    Check(ClassOf(b,0)==4 && out[0].now.what==Loaded::kRestored,"player 0: class 4 back");
    Check(ClassOf(b,1)==2 && out[1].now.what==Loaded::kChanged,"player 1: the stock game's Air Raider");
    Check(RecordOf(b,0)==kNoRecord && RecordOf(b,1)==kNoRecord,"both slots cleared");
    // one player's new class, the other's stock: the records do not cross
    auto c=FreshBlock();
    WriteInt(c.data(),ClassOffset(0),1);
    WriteInt(c.data(),ClassOffset(1),5);
    PrepareSaveCopy(c.data(),s);
    Check(ClassOf(c,0)==1 && RecordOf(c,0)==kNoRecord && ClassOf(c,1)==kStandIn && RecordOf(c,1)==Encode(5,0),"player 1 only");
    Check(RestoreLoaded(c.data(),6,out)==1 && ClassOf(c,0)==1 && ClassOf(c,1)==5,"player 1 restored, player 0 kept");
}
}  // namespace

int main() {
    Layout();
    Codes();
    StandIn();
    RestoreAfterLoad();
    ChangedInStockGame();
    NoRecord();
    Unavailable();
    TwoPlayers();
    std::printf("soldier_save_test: %d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
