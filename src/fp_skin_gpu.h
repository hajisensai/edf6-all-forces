#pragma once
#include "fp_skin.h"
struct ID3D11DeviceContext;
namespace crew::fpskin {
// Scope is set only around the local body colour draw. Original IB/VB/poses are never modified.
void Begin(const Mask& mask) noexcept;
void End() noexcept;
bool Active() noexcept;
struct MaskState { bool active=false;Mask selected{}; };
MaskState SaveMask() noexcept;
void RestoreMask(const MaskState&) noexcept;
// Every model starts outside the caller's body-only mask, including native/foreign and shadow fast paths.
// A nested local body may select its own palette and then return the enclosing model's exact mask state.
class ModelScope {
    MaskState before_;
public:
    ModelScope() noexcept : before_(SaveMask()) { End(); }
    ~ModelScope() noexcept { RestoreMask(before_); }
    ModelScope(const ModelScope&)=delete;
    ModelScope& operator=(const ModelScope&)=delete;
};

void Submit(ID3D11DeviceContext* context,unsigned count,unsigned first,int base);
void Clear() noexcept;
struct Stats { unsigned source=0,kept=0; bool valid=false; };
Stats Last() noexcept;
} // namespace crew::fpskin
