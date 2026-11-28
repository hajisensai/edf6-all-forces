// 整备「外观」页的游戏侧：armory_look.h。EDF.dll 0x678CCB46，签名 src/armory_look_signatures.inc
// （tools/armory_look_sigs.py；tests/armory_look_native_audit.py 核对偏移与代码一致）。
#include "armory_look.h"
#include "code_signatures.h"
#include "memory.h"
#include "plugin_log.h"
#include <cstdint>
#include <cstring>

namespace crew::armorylook {
namespace {
namespace lk=crew::armory::look;
constexpr std::size_t kGameStatus=0x20B2890;   // GS*
constexpr std::size_t kPlayerOutfit=0xD8700;   // (GS, player) -> 现在穿的外观
constexpr std::size_t kGsUnlocked=0x300;       // 字节：外观切换解锁（0x8AB300）
constexpr int kLocalPlayers=2;

using OutfitFn=std::int32_t(__fastcall*)(void*,std::int32_t);
using Signature=CodeSignature;
#include "armory_look_signatures.inc"

bool ready=false;
unsigned char* Gs() noexcept { return ready ? At<unsigned char*>(image,kGameStatus) : nullptr; }
bool ValidWho(int who) noexcept { return who>=0 && who<kLocalPlayers; }
}  // namespace

bool Install() noexcept {
    ready=CheckSignatures(kSignatures,"ARMORY look");
    Log("ARMORY look ready=%d",ready);
    return ready;
}
bool Ready() noexcept { return ready && Gs()!=nullptr; }

int Outfit(int who) noexcept {
    unsigned char* gs=Gs();
    if(!gs || !ValidWho(who))return -2;
    __try { return At<std::int32_t>(gs,lk::kGsOutfit+static_cast<std::size_t>(who)*lk::kPlayerStride); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
int WornOutfit(int who) noexcept {
    unsigned char* gs=Gs();
    if(!gs || !ValidWho(who))return -1;
    __try {
        const std::int32_t o=reinterpret_cast<OutfitFn>(image+kPlayerOutfit)(gs,who);
        return lk::ValidOutfit(o) ? o : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int Unlocked() noexcept {
    unsigned char* gs=Gs();
    if(!gs)return -1;
    __try { return At<unsigned char>(gs,kGsUnlocked)!=0 ? 1 : 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool ReadColours(int who,int cls,int outfit,lk::Palette* out) noexcept {
    unsigned char* gs=Gs();
    if(!gs || !ValidWho(who) || !lk::ValidColour(cls,outfit,0,0))return false;
    __try {
        for(int ch=0;ch<lk::kChannels;++ch){
            const unsigned char* at=gs+lk::ChannelOffset(who,cls,outfit,ch);
            if(!Readable(at,lk::kChannelStride))return false;
            std::memcpy(out->rgba[ch],at,sizeof(out->rgba[ch]));
            const std::int32_t sel=At<std::int32_t>(at,lk::kSelected);
            out->selected[ch]=sel>=0 && sel<lk::kSwatches ? sel : -1;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteOutfit(int who,int outfit) noexcept {
    unsigned char* gs=Gs();
    if(!gs || !ValidWho(who) || outfit<lk::kMission || outfit>=lk::kOutfits)return false;
    unsigned char* at=gs+lk::kGsOutfit+static_cast<std::size_t>(who)*lk::kPlayerStride;
    if(!Readable(at,4,true))return false;
    Put<std::int32_t>(at,0,outfit);
    return true;
}
bool WriteColour(int who,int cls,int outfit,int ch,int idx) noexcept {
    unsigned char* gs=Gs();
    if(!gs || !ValidWho(who) || !lk::ValidColour(cls,outfit,ch,idx))return false;
    unsigned char* at=gs+lk::SelectedOffset(who,cls,outfit,ch);
    if(!Readable(at,4,true))return false;
    Put<std::int32_t>(at,0,idx);
    return true;
}
}  // namespace crew::armorylook
