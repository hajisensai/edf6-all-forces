#pragma once
#include "fp_skin.h"
struct ID3D11DeviceContext;
namespace crew::fpskin {
// Scope is set only around the local body colour draw. Original IB/VB/poses are never modified.
void Begin(const Mask& mask) noexcept;
void End() noexcept;
bool Active() noexcept;
void Submit(ID3D11DeviceContext* context,unsigned count,unsigned first,int base);
void Clear() noexcept;
struct Stats { unsigned source=0,kept=0; bool valid=false; };
Stats Last() noexcept;
} // namespace crew::fpskin
