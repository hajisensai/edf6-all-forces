#pragma once

namespace crew::renderpose {
// Install at plugin load, before the game starts recording commands.
bool Install() noexcept;

// The current native queue recording's frozen matrices, when one is active.
using FrozenPose=const float*(*)(const unsigned char*) noexcept;
inline thread_local FrozenPose frozenPose=nullptr;

struct Override {const unsigned char* instance;const float* matrices;};
struct DrawOverrides {const Override* entries;int count;const DrawOverrides* parent;};
inline thread_local const DrawOverrides* drawOverrides=nullptr;

inline const Override* OverrideOf(const unsigned char* instance) noexcept {
    for(auto scope=drawOverrides;scope;scope=scope->parent)
        for(int i=0;i<scope->count;++i)
            if(scope->entries[i].instance==instance)return &scope->entries[i];
    return nullptr;
}
}
