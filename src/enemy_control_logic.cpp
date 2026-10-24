#include "enemy_control_types.h"
#include <cmath>
#include <cwchar>

namespace crew::enemy_control {
const wchar_t* ReasonText(Reason reason) noexcept {
    switch(reason) {
    case Reason::none:return L"可使用该单位的原生操控";
    case Reason::disabled:return L"敌方单位操控已关闭";
    case Reason::invalidRequester:return L"需要当前本地玩家角色";
    case Reason::invalidTarget:return L"目标已离开当前场景";
    case Reason::sameObject:return L"不能操控自己的角色";
    case Reason::friendly:return L"这不是敌方单位";
    case Reason::building:return L"该目标是建筑或依附部件，没有独立角色控制";
    case Reason::unsupportedFamily:return L"该家族的原生操控尚未验证";
    case Reason::incompleteSkills:return L"该变种仍有未验证的技能，暂不开放操控";
    case Reason::nativeMismatch:return L"原生对象或输入版本不匹配";
    case Reason::noIdentity:return L"无法确认目标的当前场景身份";
    case Reason::busy:return L"玩家或目标已有另一个操控会话";
    case Reason::stale:return L"目标或技能列表已变化，请重新选择";
    case Reason::nativeFailure:return L"单位操控或恢复失败，请查看当前状态";
    case Reason::notOwner:return L"此操控属于另一名玩家";
    case Reason::networkUnavailable:return L"房间尚未准备好敌方操控同步";
    case Reason::networkCapacity:return L"敌方操控需要配套新版 EDF Coop，请所有玩家更新";
    case Reason::riding:return L"请先下车：只有步行时才能操控敌方单位";
    case Reason::dead:return L"角色或受控单位已死亡";
    case Reason::sceneChanged:return L"场景已切换，操控已结束";
    }
    return L"操控不可用";
}
bool ValidInput(const Input& input) noexcept {
    for(float value:input.move)if(!std::isfinite(value) || std::fabs(value)>1.0f)return false;
    for(float value:input.look)if(!std::isfinite(value) || std::fabs(value)>1.0f)return false;
    if(!std::isfinite(input.aimYaw) || !std::isfinite(input.aimPitch))return false;
    if(input.absoluteAim) {
        if(std::fabs(input.aimYaw)>3.141593f || std::fabs(input.aimPitch)>1.2f)return false;
    } else if(input.aimYaw!=0 || input.aimPitch!=0)return false;
    for(unsigned i=20;i<24;++i)if(input.fullFocusId[i])return false;
    if(!input.absoluteAim) {
        for(float value:input.move)if(value!=0)return false;
        for(float value:input.look)if(value!=0)return false;
        for(const auto& active:input.active)if(active.id || active.phase)return false;
        for(auto byte:input.fullFocusId)if(byte)return false;
    }
    bool ended=false;
    for(unsigned i=0;i<kConcurrentAbilities;++i) {
        const ActiveAbility& a=input.active[i];
        if(!a.id) {if(a.phase)return false;ended=true;continue;}
        if(ended || (a.phase!=1 && a.phase!=2))return false;
        for(unsigned j=0;j<i;++j)if(input.active[j].id==a.id)return false;
    }
    return true;
}
std::uint32_t AbilityId(const char* profile,const wchar_t* name,unsigned channel) noexcept {
    if(!profile || !*profile || !name || !*name)return 0;
    std::uint32_t hash=2166136261u;
    const auto byte=[&](unsigned char value) { hash=(hash^value)*16777619u; };
    for(const char* p=profile;*p;++p)byte(static_cast<unsigned char>(*p));
    byte(0);
    for(const wchar_t* p=name;*p;++p) {
        const std::uint32_t value=static_cast<std::uint32_t>(*p);
        byte(static_cast<unsigned char>(value));byte(static_cast<unsigned char>(value>>8));
    }
    byte(0);
    for(unsigned shift=0;shift<32;shift+=8)byte(static_cast<unsigned char>(channel>>shift));
    return hash ? hash : 1;
}
}
