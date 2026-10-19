// Execute the production NPC look writer at native action ownership boundaries.
#define main ExistingNpcCoreMain
#include "../tools/npc_core_check.cpp"
#undef main
int main() {
    using namespace crew;
    alignas(16) unsigned char actor[0x1600]{};
    const float target[3]={10,4,-8};
    Put<float>(actor,kAimPitch,0.1f);Put<float>(actor,kAimYaw,-0.2f);
    const float beforePitch=At<float>(actor,kAimPitch),beforeYaw=At<float>(actor,kAimYaw);
    float wanted[2]{};
    Expect(npc::AimDelta(beforePitch,beforeYaw,target,1.0f,kLookMost,wanted),"valid native look direction");
    Expect(std::fabs(wanted[1])>0.3f,"the repro would otherwise inject a repeated turn");
    Look(actor,target);
    Expect(At<float>(actor,kLookPitch)==wanted[0] && At<float>(actor,kLookYaw)==wanted[1],"ordinary NPC aim retains its bounded native input path");
    Expect(At<float>(actor,kAimPitch)==beforePitch && At<float>(actor,kAimYaw)==beforeYaw,"the writer never bypasses native integration by modifying angles");
    for(const std::uint32_t limit:{2u,0x29Eu,0xFFFFFFFFu}) {
        Put<std::uint32_t>(actor,kActionLimits,limit);
        Put<float>(actor,kLookPitch,0.0f);Put<float>(actor,kLookYaw,0.0f);
        for(int frameIndex=0;frameIndex<300;++frameIndex)Look(actor,target);
        Expect(At<float>(actor,kLookPitch)==0 && At<float>(actor,kLookYaw)==0,"an action-owned look never accumulates the plugin turn intent into CAS");
        Put<float>(actor,kLookPitch,0.013f);Put<float>(actor,kLookYaw,-0.021f);
        Look(actor,target);
        Expect(At<float>(actor,kLookPitch)==0.013f && At<float>(actor,kLookYaw)==-0.021f,"action ownership preserves the stock writer's existing inputs");
    }
    for(const std::uint32_t limit:{0u,1u,4u,0xE3Cu}) {
        Put<std::uint32_t>(actor,kActionLimits,limit);Look(actor,target);
        Expect(At<float>(actor,kLookPitch)==wanted[0] && At<float>(actor,kLookYaw)==wanted[1],"only the native look ownership bit blocks aiming; motion and pack flags remain independent");
    }
    std::printf("NPC action look: %d failures\n",failures);
    return failures ? 1 : 0;
}
