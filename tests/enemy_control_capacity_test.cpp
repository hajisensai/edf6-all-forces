#include "../src/enemy_control_capacity.h"
#include <cstdio>
#include <cstdlib>
namespace {unsigned checks=0;void Check(bool value){++checks;if(!value)std::exit(1);}}
int main(){
    EDF6CoopExtensionCapacity c{sizeof(c),EDF6COOP_EXTENSION_CAPACITY_VERSION,1024,32,4096,1024*1024,1024*1024,EDF6COOP_EXTENSION_CAPACITY_RELIABLE_ORDERED};
    Check(crew::ValidEnemyControlCapacity(c));
    for(unsigned bad=0;bad<8;++bad){auto next=c;switch(bad){case 0:--next.size;break;case 1:++next.version;break;case 2:--next.maxPayload;break;case 3:--next.maxPeers;break;case 4:next.inboxMessages=256;break;case 5:next.pendingBytes=256*1024;break;case 6:next.congestedBacklogBytes=256*1024;break;default:next.flags=0;break;}Check(!crew::ValidEnemyControlCapacity(next));}
    EDF6CoopExtensionCapacity old{};Check(!crew::ValidEnemyControlCapacity(old));c.flags|=0x80000000u;Check(crew::ValidEnemyControlCapacity(c));
    std::printf("enemy_control_capacity: %u checks passed (old SDK explicitly refused for enemy control)\n",checks);return 0;
}
