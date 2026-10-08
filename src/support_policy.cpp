// Read the same state as native Weapon::effective-condition (EDF.dll 0x690DB0), without pretending
// NO_SUPPORT is a particular campaign or map name. Scripts can change it while a mission is running.
#include "crew.h"
#include "memory.h"
#include "support_policy.h"
namespace crew {
support::Policy SupportMissionPolicy() noexcept {
    constexpr unsigned condition=0x20B3658,manager=0x20B2958;
    constexpr unsigned char signature[]={0x8B,0x15,0xA2,0x28,0xA2,0x01,0x83,0xFA,0x02,0x74,0x1E,
        0x48,0x8B,0x05,0x96,0x1B,0xA2,0x01,0x83,0xB8,0x00,0x0B,0x00,0x00,0x00,0x74,0x0E,0x80,0xB9,0x28,0x02,0x00,
        0x00,0x00,0xB8,0x01,0x00,0x00,0x00,0x74,0x02,0x8B,0xC2,0xC3};
    support::Policy policy;
    if(!image || !Matches(0x690DB0,signature,sizeof(signature)) || !Readable(image+condition,4) || !Readable(image+manager,8))return policy;
    __try {
        const auto* world=At<const unsigned char*>(image,manager);
        if(!world || !Readable(world+0xB00,4))return policy;
        const int value=At<int>(image,condition);
        const int underground=At<int>(world,0xB00);
        if(value<0 || value>2 || underground<0 || underground>1)return policy;
        policy.environment=value==0 ? support::Environment::normal : value==1 ? support::Environment::noExternalSupport : support::Environment::forceEnable;
        policy.underground=underground!=0;
        return policy;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
}
