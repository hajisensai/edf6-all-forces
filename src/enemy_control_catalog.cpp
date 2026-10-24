#include "enemy_control_catalog.h"
#include "enemy_control_deiroi.h"
#include "enemy_control_humanoid.h"
#include "enemy_control_timeship.h"
#include "enemy_control_walking.h"
#include "enemy_control_walking_guns.h"
#include "enemy_control_command_actions.h"
#include "enemy_control_types.h"
#include "crew.h"
#include "memory.h"
#include <cstring>
#include <cwchar>
#include <string>
#include <cstddef>
#include <array>
#include <vector>
#include <cstdio>
#include <map>
#include <mutex>

namespace crew::enemy_native {
namespace {
constexpr const char* kPadIds[]={"native_action_0","native_action_1","native_action_2","native_action_3",
    "native_action_4","native_action_5","native_action_6","native_action_7","native_action_8","native_action_9",
    "native_action_10","native_action_11","native_action_12","native_action_13","native_action_14","native_action_15"};
constexpr const wchar_t* kPadNames[]={L"动作一",L"动作二",L"动作三",L"动作四",L"主攻击",L"副攻击",L"特殊动作一",L"特殊动作二",
    L"特殊动作三",L"特殊动作四",L"特殊动作五",L"特殊动作六",L"特殊动作七",L"特殊动作八",L"特殊动作九",L"特殊动作十"};
constexpr unsigned kMaxCatalogue=4096;
struct StableId { char text[32]{}; };
const char* TableId(bool merman,unsigned index) noexcept {
    struct Ids {
        std::array<StableId,kMaxCatalogue> monster{},merman{};
        Ids() noexcept {
            for(unsigned i=0;i<kMaxCatalogue;++i) {
                std::snprintf(monster[i].text,sizeof(monster[i].text),"monster_attack_%u",i);
                std::snprintf(merman[i].text,sizeof(merman[i].text),"merman_action_%u",i);
            }
        }
    };
    static const Ids ids;
    return index<kMaxCatalogue ? (merman ? ids.merman[index].text : ids.monster[index].text) : nullptr;
}
struct DeiroiLabels {
    char fireId[32]{},autoId[32]{};
    wchar_t fireName[64]{},autoName[64]{};
};
const DeiroiLabels* PartLabels(unsigned ordinal) noexcept {
    // Encoded original (leg,part) identities must not change after another part
    // dies. Map nodes keep immutable labels/IDs at stable addresses across all
    // count/read calls, unlike a scratch label indexed by the surviving count.
    static std::mutex lock;
    static std::map<unsigned,DeiroiLabels> labels;
    try {
        std::lock_guard<std::mutex> hold(lock);
        auto item=labels.try_emplace(ordinal);
        auto& value=item.first->second;
        if(item.second) {
            std::snprintf(value.fireId,sizeof(value.fireId),"deiroi_fire_%08X",ordinal);
            std::snprintf(value.autoId,sizeof(value.autoId),"deiroi_auto_%08X",ordinal);
            const unsigned leg=(ordinal>>16)+1,part=(ordinal&0xFFFFu)+1;
            _snwprintf_s(value.fireName,_TRUNCATE,L"腿 %u 炮位 %u：准星射击",leg,part);
            _snwprintf_s(value.autoName,_TRUNCATE,L"腿 %u 炮位 %u：恢复自动",leg,part);
        }
        return &value;
    } catch(...) { return nullptr; }
}
struct HumanWeaponLabel { char id[32]{};wchar_t name[48]{}; };
const HumanWeaponLabel* HumanWeaponName(unsigned index) noexcept {
    struct Names {
        std::array<HumanWeaponLabel,64> slots{};
        Names() noexcept {
            for(unsigned i=0;i<64;++i) {
                std::snprintf(slots[i].id,sizeof(slots[i].id),"humanoid_weapon_%u",i);
                _snwprintf_s(slots[i].name,_TRUNCATE,L"武器 %u：独立开火",i+1);
            }
        }
    };
    static const Names names;
    return index<64 ? &names.slots[index] : nullptr;
}
struct NamedLabel { const wchar_t* state;const wchar_t* name; };
constexpr NamedLabel kAttackNames[]={
    {L"attack_chop_l",L"左手劈击"},
    {L"attack_chop_r",L"右手劈击"},
    {L"attack_step_chop_l",L"踏步左劈"},
    {L"attack_step_chop_r",L"踏步右劈"},
    {L"attack_step_chop_w",L"踏步双劈"},
    {L"attack_breath_t",L"上方吐息"},
    {L"attack_breath_b",L"下方吐息"},
    {L"attack_tail",L"甩尾"},
    {L"attack_tackle",L"冲撞"},
    {L"attack_tackle_long",L"长距离冲撞"},
    {L"shout_A",L"咆哮一"},
    {L"shout_B",L"咆哮二"},
    {L"attack_ground_l",L"左脚践踏"},
    {L"attack_ground_r",L"右脚践踏"},
    {L"attack_ground",L"双脚践踏"},
    {L"attack_dash",L"突进"},
    {L"attack_dash2",L"强力突进"},
    {L"attack_spring",L"跃击"},
    {L"attack_rolling_b",L"后方翻滚"},
    {L"attack_rolling_l",L"左侧翻滚"},
    {L"attack_rolling_r",L"右侧翻滚"},
    {L"attack_both_hand_stomp",L"双手重砸"},
    {L"attack_jump",L"跳跃扑击"},
    {L"attack_nearPress",L"近身压击"},
    {L"attack_rolling",L"翻滚连击一"},
    {L"attack_rolling2",L"翻滚连击二"},
    {L"attack_rolling_thorn",L"刺弹翻滚一"},
    {L"attack_rolling2_thorn",L"刺弹翻滚二"},
    {L"attack_bite",L"撕咬"},
    {L"attack_swing_l",L"左手横扫"},
    {L"attack_swing_r",L"右手横扫"},
    {L"attack_parm",L"掌击"},
    {L"attack_step_swing_l",L"踏步左横扫"},
    {L"attack_step_swing_r",L"踏步右横扫"},
    {L"attack_side_spark_l",L"左侧电击"},
    {L"attack_side_spark_r",L"右侧电击"},
    {L"attack_spark",L"电击齐射"},
    {L"attack_sniperSpark",L"远程电击"},
    {L"step_f",L"向前踏步"},
    {L"step_b",L"向后踏步"},
    {L"attack_rolling_b_vs_big",L"后方翻滚（巨型目标）"},
    {L"attack_rolling_l_vs_big",L"左侧翻滚（巨型目标）"},
    {L"attack_rolling_r_vs_big",L"右侧翻滚（巨型目标）"},
    {L"attack_rolling_vs_big",L"翻滚连击一（巨型目标）"},
    {L"attack_rolling2_vs_big",L"翻滚连击二（巨型目标）"},
    {L"attack_rolling_thorn_vs_big",L"刺弹翻滚一（巨型目标）"},
    {L"attack_rolling2_thorn_vs_big",L"刺弹翻滚二（巨型目标）"},
    {L"attack_side_spark_l_vs_big",L"左侧电击（巨型目标）"},
    {L"attack_side_spark_r_vs_big",L"右侧电击（巨型目标）"},
    {L"attack_spark_vs_big",L"电击齐射（巨型目标）"},
    {L"attack_sniperSpark_vs_big",L"远程电击（巨型目标）"},
};
const wchar_t* AttackName(const wchar_t* native) noexcept {
    for(const auto& label:kAttackNames)if(std::wcscmp(native,label.state)==0)return label.name;
    return native;
}
constexpr NativeAbility kCarrier{"carrier_hold_drop",L"保持开舱投送",0,0,AbilityKind::objectAction,nullptr,64};
constexpr NativeAbility kBoss[]={
    {"boss_hand_beam",L"手部光束",0,0,AbilityKind::objectAction,nullptr,4},
    {"boss_hand_homing",L"追踪光束",0,0,AbilityKind::objectAction,nullptr,5},
    {"boss_hand_spear",L"光枪齐射",0,0,AbilityKind::objectAction,nullptr,6},
    {"boss_rolling_laser",L"回旋激光",0,0,AbilityKind::namedAction,L"attack_rollingLazer",0},
    {"boss_bathing_kick",L"飞踢",0,0,AbilityKind::namedAction,L"attack_bathingKick",0},
    {"boss_dive",L"俯冲攻击",0,0,AbilityKind::namedAction,L"attack_dive",0},
    {"boss_genocide",L"灭绝光炮",0,0,AbilityKind::namedAction,L"attack_genocide",0},
    {"boss_meteor",L"陨石攻击",0,0,AbilityKind::namedAction,L"attack_meteorite",0}
};
struct Print { std::uint32_t at;unsigned char bytes[16]; };
constexpr Print kExtraPrints[]={
    {0x475BE0,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x57,0x48,0x83,0xEC,0x30,0x80,0xB9,0xE8,0x02}},
    {0x47D940,{0x40,0x53,0x48,0x83,0xEC,0x30,0xC7,0x44,0x24,0x28,0x00,0x00,0x00,0x00,0x48,0x8D}},
    {0x444C10,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x55}},
    {0x466E7C,{0x48,0x8B,0x01,0xFF,0xA0,0xE8,0x01,0x00,0x00,0xCC,0xCC,0xCC,0x48,0x8B,0x01,0xFF}},
    {0x11664F0,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x7C,0x24,0x10,0x8B,0xC2,0x45,0x33,0xC9,0x48}}
};
// Native1/101 and /102 construct this precise value at 4684F5/468607.
// The named-action virtual slot 61 (47A090, Boss override447A50) copies it and
// owns animation, MAB events, completion and exit cleanup. Source lifetime ends
// after 475BE0 returns; the native state constructor copied the complete value.
struct NamedAction {
    std::wstring name;
    std::int32_t priority=1;
    float blend=0.1f,speed=1.0f;
    bool additive=false;
};
static_assert(sizeof(std::wstring)==0x20 && offsetof(NamedAction,priority)==0x20 &&
    offsetof(NamedAction,blend)==0x24 && offsetof(NamedAction,speed)==0x28 && offsetof(NamedAction,additive)==0x2C,
    "native Humanoid named-action descriptor contract");
bool Same(const char* a,const char* b) noexcept { return a && b && std::strcmp(a,b)==0; }
bool IsCarrier(const NativeLease& lease) noexcept {
    return lease.profile && (Same(lease.profile->name,"UfoCarrier508") || Same(lease.profile->name,"UfoCarrier"));
}
bool IsBoss(const NativeLease& lease) noexcept {
    return lease.profile && lease.profile->vtableRva==0x17BEC68 && Same(lease.profile->name,"Humanoid_BigGreyBoss");
}
bool Identity(const NativeLease& lease) noexcept {
    std::size_t count=0;const auto profiles=Profiles(&count);bool known=false;
    for(std::size_t i=0;i<count;++i)if(lease.profile==profiles+i){known=true;break;}
    return known && lease.object && lease.control && Readable(lease.object,0x355,true) &&
        At<const void*>(lease.object,0x30)==lease.control && Readable(lease.control,12) &&
        At<LONG>(lease.control,8)>0 && !(lease.object[0x18]&4) && !lease.object[0x2E8];
}
bool ExtraReady() noexcept {
    if(!image)return false;
    for(const auto& p:kExtraPrints)
        if(!Readable(image+p.at,sizeof(p.bytes)) || std::memcmp(image+p.at,p.bytes,sizeof(p.bytes))!=0)return false;
    return true;
}
bool BossReady(const NativeLease& lease) noexcept {
    return IsBoss(lease) && ExtraReady() && Readable(lease.object,0x26B0,true) &&
        Readable(At<const void*>(lease.object,0x17E8),16);
}
bool HasState(const NativeLease& lease,const wchar_t* state) noexcept {
    const auto model=At<unsigned char*>(lease.object,0xA70);
    const int casPlayer=At<int>(lease.object,0xEB0);
    if(!model || casPlayer<0 || casPlayer>128 || !Readable(model+0x330,0x80))return false;
    return reinterpret_cast<int(__fastcall*)(void*,int,const wchar_t*,bool)>(image+0x11664F0)(model+0x330,casPlayer,state,true)>=0;
}
const wchar_t* PadName(const NativeProfile& p,unsigned bit) noexcept {
    if(Same(p.name,"Humanoid_BigGreyBoss") && bit==7)return L"快速回避";
    if(p.kind==Kind::humanoid) {
        switch(bit) {
            case 0:return L"跳跃";case 3:return L"进入／退出掩体";case 4:return L"举枪瞄准";
            case 5:return L"装备武器齐射";case 6:return L"翻滚回避";case 7:return L"特殊移动";case 8:return L"蹲伏";
            default:break;
        }
    }
    if(Same(p.name,"GiantAnt") || Same(p.name,"GiantAntEX"))return bit==4 ? L"喷酸／投掷捕获物" : L"抓取／咬甩";
    if(Same(p.name,"GiantBee") || Same(p.name,"GiantBeeEX"))return bit==4 ? L"起飞" : L"针刺射击";
    if(Same(p.name,"GiantSpider") || Same(p.name,"GiantSpiderEX"))return bit==4 ? L"跳跃" : L"吐丝";
    if(Same(p.name,"GiantDango"))return bit==4 ? L"开始翻滚" : L"滚动攻击";
    if(Same(p.name,"DragonSmall"))return bit==4 ? L"起飞" : bit==5 ? L"地面攻击／滑翔飞扑／投掷" : L"捕获后的咬甩";
    if(Same(p.name,"EDF6_GiantDangoLarge"))return bit==0 ? L"滚动" : L"针弹射击";
    if(Same(p.name,"EDF6_ShellFish"))return bit==0 ? L"打开甲壳" : bit==1 ? L"关闭甲壳" : L"炮口射击";
    if(Same(p.name,"EDF6_SpinnerUfo"))return bit==0 ? L"吸附／脱离" : L"旋转炮击";
    if(Same(p.name,"EDF6_SquidSmall"))return L"触手射击";
    if(Same(p.name,"EDF6_Berserker_Large"))return bit==0 ? L"跳跃" : bit==4 ? L"臂炮齐射" : L"主攻击（依装备）";
    if(p.kind==Kind::walking)return bit==0 ? L"跳跃" : bit==4 ? L"攻击／抓取保持" : L"主攻击（依装备）";
    if(Same(p.name,"AlienTrailer") && bit==0)return L"切换高功率护盾";
    if(Same(p.name,"EDF6_TimeShip"))return bit==0 ? L"跃迁消失／再现" : bit==1 ? L"目标指令" : L"跃迁撤离（结束操控）";
    if(Same(p.name,"Nephila"))return L"吐丝";
    if(Same(p.name,"Deiroi"))return bit==4 ? L"足部践踏" : L"主炮射击";
    if(Same(p.name,"EDF6_FlyingDyno")) {
        switch(bit) {
            case 0:return L"火球";case 1:return L"水平火焰";case 2:return L"反向水平火焰";
            case 3:return L"垂直火焰";case 6:return L"俯冲";case 7:return L"向左飞步";
            case 11:return L"向右飞步";case 12:return L"二阶段水平火焰";case 13:return L"二阶段反向火焰";
            case 8:return L"二阶段垂直火焰";case 10:return L"进入第二形态";default:break;
        }
    }
    if(p.kind==Kind::monster) {
        if(bit==4)return L"侧步回避";
        if(bit==8)return L"奔跑";
    }
    if(Same(p.name,"UfoSmall507") || Same(p.name,"UfoSmall515"))return L"机载武器开火";
    if(Same(p.name,"DropBoat512"))return L"打开舱门／登陆";
    return kPadNames[bit];
}
bool MonsterTable(const NativeLease& lease,const unsigned char** rows,std::size_t* count) noexcept {
    if(lease.profile->kind!=Kind::monster || !Readable(lease.object,0x14E1))return false;
    *rows=At<const unsigned char*>(lease.object,0x14A8);*count=At<std::size_t>(lease.object,0x14B8);
    return *count>0 && *count<=kMaxCatalogue && Readable(*rows,*count*0x38);
}
bool MermanTable(const NativeLease& lease,const unsigned char** rows,std::size_t* count) noexcept {
    if(!Same(lease.profile->name,"EDF6_Merman") || !Readable(lease.object,0x17F8))return false;
    *rows=At<const unsigned char*>(lease.object,0x16F8);*count=At<std::size_t>(lease.object,0x1708);
    return *count>0 && *count<=kMaxCatalogue && Readable(*rows,*count*0x88);
}
const wchar_t* MermanName(const wchar_t* native) noexcept {
    constexpr NamedLabel labels[]={
        {L"dive",L"站立：俯冲"},{L"splash",L"站立：喷吐"},{L"both_hand",L"站立：双手攻击"},{L"encount",L"站立：咆哮"},
        {L"wakeup",L"爬行：起身"},{L"sliding",L"爬行：滑铲"},{L"hopping",L"爬行：跃起"},
        {L"roll_l",L"爬行：左翻滚"},{L"roll_r",L"爬行：右翻滚"},{L"dive_form_water",L"水中：跃出水面"}
    };
    for(const auto& label:labels)if(std::wcscmp(native,label.state)==0)return label.name;
    return native;
}
bool PadAllowed(const NativeLease& lease,unsigned i) noexcept {
    // Native Monster debug pads 6/7 force down/damage reactions, while 10/11
    // cycle its attack table. List each actual attack instead, preserving every
    // entry (including King variants' shorter tables).
    if(lease.profile->kind==Kind::monster)return i==4 || i==8;
    if(Same(lease.profile->name,"EDF6_Merman"))return false;
    if(Same(lease.profile->name,"Nephila"))return i==4;
    if(Same(lease.profile->name,"EDF6_TimeShip"))return false; // canonical commands replace local-camera pad rays
    const char* family=lease.profile->name;
    if(Same(family,"GiantAnt") || Same(family,"GiantAntEX") || Same(family,"GiantBee") || Same(family,"GiantBeeEX") ||
        Same(family,"GiantSpider") || Same(family,"GiantSpiderEX"))return i==4 || i==5;
    if(Same(family,"GiantDango"))return i==4 || i==6;
    if(Same(family,"DragonSmall"))return i>=4 && i<=6;
    if(lease.profile->kind==Kind::walking && i==5 && !WalkingNativeMainAttackAvailable(lease))return false;
    if(lease.profile->kind==Kind::walking && !Same(family,"EDF6_Berserker_Large") && !Same(family,"EDF6_Berserker_TypeGrapple"))return i==0 || i==5;
    if(Same(lease.profile->name,"EDF6_SquidSmall"))return i==5;
    if(Same(lease.profile->name,"EDF6_SpinnerUfo"))return i<2;
    if(Same(lease.profile->name,"EDF6_ShellFish"))return i==0 || i==1 || i==5;
    if(Same(lease.profile->name,"EDF6_FlyingDyno")) {
        constexpr int actionOf[]={0,1,2,3,-1,-1,4,5,9,-1,-1,6,7,8,-1,-1};
        if(!Readable(lease.object,0x1410))return false;
        const std::size_t count=At<std::size_t>(lease.object,0x1408);
        if(i==10)return count>7 && count<=64;
        return actionOf[i]>=0 && static_cast<std::size_t>(actionOf[i])<count && count<=64;
    }
    return true;
}
unsigned List(const NativeLease& lease,NativeAbility* out,unsigned capacity) noexcept {
    if(!Identity(lease))return 0;
    if((lease.profile->kind==Kind::monster || Same(lease.profile->name,"EDF6_Merman") || IsBoss(lease) || IsCarrier(lease)) &&
       !InstallCommandAcceptedActions())return 0;
    const unsigned char* attacks=nullptr;std::size_t attackCount=0;
    if(lease.profile->kind==Kind::monster && !MonsterTable(lease,&attacks,&attackCount))return 0;
    if(IsBoss(lease) && !BossReady(lease))return 0;
    const unsigned char* merman=nullptr;std::size_t mermanCount=0;
    if(Same(lease.profile->name,"EDF6_Merman") && !MermanTable(lease,&merman,&mermanCount))return 0;
    unsigned walkingCount=0;
    if(lease.profile->kind==Kind::walking && !ReadWalkingActionCount(lease,&walkingCount))return 0;
    unsigned count=0;
    const auto append=[&](const NativeAbility& a) noexcept { if(out && count<capacity)out[count]=a;++count; };
    for(unsigned i=0;i<kButtonCount;++i) {
        const auto bit=static_cast<std::uint16_t>(1u<<i);
        if(!(lease.profile->buttonMask&bit) || !PadAllowed(lease,i))continue;
        NativeAbility a{kPadIds[i],PadName(*lease.profile,i),bit,bit};
        // The base humanoid deliberately separates aim-ready(button4) from
        // trigger(button5). A selected fire ability supplies both, like its AI.
        if(lease.profile->kind==Kind::humanoid && i==5)a.held|=1u<<4;
        append(a);
    }
    if(MonsterTable(lease,&attacks,&attackCount))for(std::size_t i=0;i<attackCount;++i) {
        const wchar_t* text=At<const wchar_t*>(attacks+i*0x38,0);
        if(!Readable(text,2))return 0;
        std::size_t length=0;while(length<128 && Readable(text+length,2) && text[length])++length;
        if(!length || length>=128)return 0;
        append(NativeAbility{TableId(false,static_cast<unsigned>(i)),AttackName(text),0,0,AbilityKind::monsterAction,text,static_cast<std::int32_t>(i)});
    }
    if(MermanTable(lease,&merman,&mermanCount))for(std::size_t i=0;i<mermanCount;++i) {
        const wchar_t* text=At<const wchar_t*>(merman+i*0x88,0);
        if(!Readable(text,2) || At<int>(merman+i*0x88,0x1C)<0)return 0;
        std::size_t length=0;while(length<128 && Readable(text+length,2) && text[length])++length;
        if(!length || length>=128)return 0;
        append(NativeAbility{TableId(true,static_cast<unsigned>(i)),MermanName(text),0,0,AbilityKind::mermanAction,text,static_cast<std::int32_t>(i)});
    }
    if(lease.profile->kind==Kind::deiroi) {
        unsigned guns=0;if(!ReadDeiroiGunCount(lease,&guns))return 0;
        if(guns>(kMaxCatalogue-count)/2)return 0;
        for(unsigned i=0;i<guns;++i) {
            unsigned key=0;
            if(!DeiroiGunOrdinal(lease,i,&key) || key>0x7FFFFFFFu)return 0;
            const auto label=PartLabels(key);if(!label)return 0;
            append(NativeAbility{label->fireId,label->fireName,0,0,AbilityKind::deiroiFire,nullptr,static_cast<std::int32_t>(key)});
            append(NativeAbility{label->autoId,label->autoName,0,0,AbilityKind::deiroiAuto,nullptr,static_cast<std::int32_t>(key)});
        }
    }
    if(lease.profile->kind==Kind::humanoid) {
        unsigned weapons=0;if(!ReadHumanoidWeaponCount(lease,&weapons))return 0;
        if(weapons>64)return 0;
        for(unsigned i=0;i<weapons;++i) {
            unsigned index=0;
            if(!HumanoidWeaponIndex(lease,i,&index))return 0;
            const auto label=HumanWeaponName(index);if(!label)return 0;
            append(NativeAbility{label->id,label->name,0x30,0x30,AbilityKind::humanoidWeapon,nullptr,static_cast<std::int32_t>(index)});
        }
    }
    if(lease.profile->kind==Kind::walking) {
        const unsigned actions=ListWalkingActionAbilities(lease,out && count<capacity ? out+count : nullptr,count<capacity ? capacity-count : 0);
        if(actions!=walkingCount || actions>kMaxCatalogue-count)return 0;
        count+=actions;
        const unsigned guns=ListWalkingGunAbilities(lease,out && count<capacity ? out+count : nullptr,count<capacity ? capacity-count : 0);
        if(guns>kMaxCatalogue-count)return 0;
        count+=guns;
    }
    if(Same(lease.profile->name,"EDF6_TimeShip")) {
        const unsigned extra=ListTimeShipAbilities(lease,out && count<capacity ? out+count : nullptr,count<capacity ? capacity-count : 0);
        if(!extra || extra>kMaxCatalogue-count)return 0;
        count+=extra;
    }
    if(IsCarrier(lease))append(kCarrier);
    if(BossReady(lease))for(const auto& a:kBoss) {
        if(a.kind==AbilityKind::namedAction && !HasState(lease,a.action))return 0;
        append(a);
    }
    return count<=kMaxCatalogue ? count : 0;
}
bool Definition(const NativeAbility& a,const NativeAbility& b) noexcept {
    return Same(a.id,b.id) && a.held==b.held && a.pressed==b.pressed && a.kind==b.kind &&
        a.argument==b.argument && a.action==b.action;
}
bool NativeStringIs(const unsigned char* value,const wchar_t* name) noexcept {
    if(!Readable(value,32))return false;
    const std::size_t n=std::wcslen(name);
    if(At<std::size_t>(value,0x10)!=n)return false;
    const wchar_t* text=At<std::size_t>(value,0x18)<8 ? reinterpret_cast<const wchar_t*>(value) : At<const wchar_t*>(value,0);
    return Readable(text,(n+1)*2) && std::memcmp(text,name,(n+1)*2)==0;
}
bool MermanCommand(const NativeLease& lease,const NativeAbility& ability,std::uint64_t seed) noexcept {
    __try {
        const unsigned char* rows=nullptr;std::size_t count=0;
        constexpr unsigned char signature[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
        if(!Identity(lease) || !image || !Readable(image+0x3A6710,sizeof(signature)) ||
            std::memcmp(image+0x3A6710,signature,sizeof(signature))!=0 || !MermanTable(lease,&rows,&count) ||
            ability.argument<0 || static_cast<std::size_t>(ability.argument)>=count || (lease.object[0x17F4]&1))return false;
        const unsigned char* entry=rows+static_cast<std::size_t>(ability.argument)*0x88;
        if(ability.action!=At<const wchar_t*>(entry,0) || At<int>(entry,8)!=At<int>(lease.object,0x17F0) ||
            At<int>(entry,0x1C)<0)return false;
        // Native pad indices are relative to the current ground/crawl/water
        // mode. The catalogue retains absolute stable IDs and reproduces that
        // exact mode/ability gate before using its complete action starter.
        if(!SetSeed(lease,seed))return false;
        reinterpret_cast<void(__fastcall*)(void*,int,bool)>(image+0x3A6710)(lease.object,ability.argument,true);
        return Identity(lease) && At<const void*>(lease.object,0x1710)==entry;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool MonsterCommand(const NativeLease& lease,const NativeAbility& ability,std::uint64_t seed) noexcept {
    __try {
        const unsigned char* attacks=nullptr;std::size_t count=0;
        const unsigned char code[]={0x48,0x83,0xEC,0x38,0xF6,0x81,0xB0,0x08,0x00,0x00,0x04};
        if(!Identity(lease) || !image || !Readable(image+0x4B7AC0,sizeof(code)) ||
            std::memcmp(image+0x4B7AC0,code,sizeof(code))!=0 || !MonsterTable(lease,&attacks,&count) ||
            ability.argument<0 || static_cast<std::size_t>(ability.argument)>=count ||
            ability.action!=At<const wchar_t*>(attacks+static_cast<std::size_t>(ability.argument)*0x38,0) ||
            !(At<unsigned>(lease.object,0x8B0)&4u))return false;
        // Exactly the native debug fire/AI entry. It admits the attack under
        // the class's own current-state gate and passes its table index as the
        // state-machine parameter to 4BEC00, retaining rate/MAB/exit cleanup.
        if(!SetSeed(lease,seed))return false;
        reinterpret_cast<void(__fastcall*)(void*,int)>(image+0x4B7AC0)(lease.object,ability.argument);
        return Identity(lease) && At<int>(lease.object,0x14D0)==ability.argument;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CarrierCommand(const NativeLease& lease,const NativeAbility& ability,std::uint64_t seed) noexcept {
    __try {
        constexpr unsigned char signature[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
        if(!Identity(lease) || !IsCarrier(lease) || ability.argument!=64 || !image ||
            !Readable(image+0x4F6FB0,sizeof(signature)) || std::memcmp(image+0x4F6FB0,signature,sizeof(signature))!=0 ||
            !Readable(lease.object,0xD8C,true))return false;
        int code=64;
        // The original carrier accepts exactly ObjectAction64. It sets mode1:
        // keep the hatch open and ask its existing native generator for the next
        // wave. There is no invented close-hatch action65 or fabricated wave.
        if(!SetSeed(lease,seed))return false;
        reinterpret_cast<bool(__fastcall*)(void*,std::uint32_t,void*)>(image+0x4F6FB0)(lease.object,0x1000000D,&code);
        return Identity(lease) && At<int>(lease.object,0xD88)==1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Command(const NativeLease& lease,const NativeAbility& ability,const NamedAction* action,std::uint64_t seed) noexcept {
    __try {
        if(!Identity(lease) || !BossReady(lease) ||
            reinterpret_cast<bool(__fastcall*)(void*)>(image+0x47D940)(lease.object))return false;
        if(ability.kind==AbilityKind::objectAction) {
            int code=ability.argument;
            // The original E516 ai_script's Native1/230 dispatches the complete
            // native OnMessage, not a guessed state number or a CAS-only play.
            if(!SetSeed(lease,seed))return false;
            reinterpret_cast<bool(__fastcall*)(void*,std::uint32_t,void*)>(image+0x444C10)(lease.object,0x1000000D,&code);
            return Identity(lease);
        }
        if(ability.kind!=AbilityKind::namedAction || !action || !HasState(lease,ability.action) ||
            At<std::uint32_t>(lease.object,0x10F0)!=0 || lease.object[0x11F2] ||
            (!lease.object[0x1090] && At<std::uint32_t>(lease.object,0x9C0)<At<std::uint32_t>(lease.object,0xD6C)))return false;
        if(!SetSeed(lease,seed))return false;
        reinterpret_cast<void(__fastcall*)(void*,const NamedAction*)>(image+0x475BE0)(lease.object,action);
        return Identity(lease) && NativeStringIs(lease.object+0x1190,ability.action);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
unsigned Abilities(const NativeLease& lease,NativeAbility* out,unsigned capacity) noexcept {
    __try { return List(lease,out,capacity); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool ReadAbilityState(const NativeLease& lease,const NativeAbility& ability,enemy_control::Ability* out) noexcept {
    if(!out)return false;
    out->state=enemy_control::AbilityState::unavailable;out->cooldownKnown=false;out->cooldownSeconds=0;
    __try {
        if(!Identity(lease))return false;
        if(ability.kind==AbilityKind::deiroiFire || ability.kind==AbilityKind::deiroiAuto)
            return ability.argument>=0 && ReadDeiroiAbilityState(lease,static_cast<unsigned>(ability.argument),ability.kind==AbilityKind::deiroiAuto,out);
        if(ability.kind==AbilityKind::humanoidWeapon)
            return ability.argument>=0 && ReadHumanoidWeaponState(lease,static_cast<unsigned>(ability.argument),out);
        if(ability.kind==AbilityKind::walkingAction)
            return ability.argument>=0 && ReadWalkingActionState(lease,static_cast<unsigned>(ability.argument),out);
        if(ability.kind==AbilityKind::walkingGun)
            return ability.argument>=0 && ReadWalkingGunState(lease,static_cast<unsigned>(ability.argument),out);
        if(ability.kind==AbilityKind::timeShipCommand)return ReadTimeShipAbilityState(lease,ability,out);
        if(ability.kind==AbilityKind::monsterAction) {
            const unsigned char* rows=nullptr;std::size_t count=0;
            if(!MonsterTable(lease,&rows,&count) || ability.argument<0 || static_cast<std::size_t>(ability.argument)>=count ||
                ability.action!=At<const wchar_t*>(rows+static_cast<std::size_t>(ability.argument)*0x38,0))return false;
            out->state=At<int>(lease.object,0x14D0)==ability.argument ? enemy_control::AbilityState::active :
                (At<unsigned>(lease.object,0x8B0)&4u) ? enemy_control::AbilityState::native : enemy_control::AbilityState::cooldown;
            return true;
        }
        if(ability.kind==AbilityKind::mermanAction) {
            const unsigned char* rows=nullptr;std::size_t count=0;
            if(!MermanTable(lease,&rows,&count) || ability.argument<0 || static_cast<std::size_t>(ability.argument)>=count)return false;
            const auto entry=rows+static_cast<std::size_t>(ability.argument)*0x88;
            if(ability.action!=At<const wchar_t*>(entry,0) || At<int>(entry,0x1C)<0)return false;
            if(At<int>(entry,8)!=At<int>(lease.object,0x17F0))return true;
            out->state=(lease.object[0x17F4]&1) ? enemy_control::AbilityState::cooldown : enemy_control::AbilityState::native;
            return true;
        }
        if(IsCarrier(lease) && ability.kind==AbilityKind::objectAction && ability.argument==64) {
            if(!Readable(lease.object,0xD8C))return false;
            out->state=At<int>(lease.object,0xD88)==1 ? enemy_control::AbilityState::active : enemy_control::AbilityState::native;
            return true;
        }
        if(ability.kind==AbilityKind::namedAction || ability.kind==AbilityKind::objectAction) {
            if(!BossReady(lease) || (ability.kind==AbilityKind::namedAction && (!ability.action || !HasState(lease,ability.action))))return false;
            if(ability.kind==AbilityKind::namedAction &&
                (At<unsigned>(lease.object,0x10F0)!=0 || lease.object[0x11F2] ||
                 (!lease.object[0x1090] && At<unsigned>(lease.object,0x9C0)<At<unsigned>(lease.object,0xD6C))))return true;
            out->state=reinterpret_cast<bool(__fastcall*)(void*)>(image+0x47D940)(lease.object) ?
                enemy_control::AbilityState::cooldown : enemy_control::AbilityState::native;
            return true;
        }
        if(ability.kind!=AbilityKind::pad)return false;
        out->state=enemy_control::AbilityState::native;
        if(lease.profile->kind==Kind::monster && (ability.held&(1u<<4)) && !(At<unsigned>(lease.object,0x8B0)&4u))
            out->state=enemy_control::AbilityState::cooldown;
        if(lease.profile->kind==Kind::humanoid && Same(ability.id,"native_action_5")) {
            unsigned count=0;if(!ReadHumanoidWeaponCount(lease,&count))return false;bool ready=false,busy=false;
            for(unsigned i=0;i<count;++i) {
                unsigned index=0;enemy_control::Ability actual{};
                if(!HumanoidWeaponIndex(lease,i,&index) || !ReadHumanoidWeaponState(lease,index,&actual))continue;
                ready=ready || actual.state==enemy_control::AbilityState::native;
                busy=busy || actual.state==enemy_control::AbilityState::active || actual.state==enemy_control::AbilityState::cooldown;
            }
            out->state=ready ? enemy_control::AbilityState::native : busy ? enemy_control::AbilityState::cooldown : enemy_control::AbilityState::unavailable;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ExecuteAbility(const NativeLease& lease,const NativeAbility& ability,std::uint64_t sequence,std::uint64_t seed) noexcept {
    try {
        const unsigned count=Abilities(lease,nullptr,0);
        if(!count || count>kMaxCatalogue)return false;
        std::vector<NativeAbility> all(count);
        if(Abilities(lease,all.data(),count)!=count)return false;
        bool found=false;
        for(const auto& definition:all)if(Definition(ability,definition)){found=true;break;}
        if(!found || ability.kind==AbilityKind::pad || ability.kind==AbilityKind::humanoidWeapon)return false;
        bool handled=false;const bool accepted=ExecuteAcceptedCommand(lease,ability,sequence,seed,&handled);
        if(handled || !accepted)return accepted;
        if(ability.kind==AbilityKind::deiroiFire || ability.kind==AbilityKind::deiroiAuto)
            return ability.argument>=0 && ExecuteDeiroiAbility(lease,static_cast<unsigned>(ability.argument),
                ability.kind==AbilityKind::deiroiAuto,sequence,seed);
        if(ability.kind==AbilityKind::walkingAction)
            return ability.argument>=0 && ExecuteWalkingAction(lease,static_cast<unsigned>(ability.argument),sequence,seed);
        if(ability.kind==AbilityKind::walkingGun)
            return ability.argument>=0 && ExecuteWalkingGun(lease,static_cast<unsigned>(ability.argument),sequence,seed);
        if(ability.kind==AbilityKind::timeShipCommand)return ExecuteTimeShipAbility(lease,ability,sequence,seed);
        if(ability.kind==AbilityKind::mermanAction)return MermanCommand(lease,ability,seed);
        if(ability.kind==AbilityKind::monsterAction)return MonsterCommand(lease,ability,seed);
        if(ability.kind==AbilityKind::objectAction)return IsCarrier(lease) ? CarrierCommand(lease,ability,seed) : Command(lease,ability,nullptr,seed);
        if(ability.kind!=AbilityKind::namedAction || !ability.action)return false;
        NamedAction action;action.name=ability.action;
        return Command(lease,ability,&action,seed);
    } catch(...) { return false; }
}
} // namespace crew::enemy_native
