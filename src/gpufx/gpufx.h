// GPU particle effects (src/gpufx): the game glue.
// Our own particles are simulated on the game thread (gpufx_sim.h Pool, the global budget) and drawn on the render thread
// at the world's end of each world view (thermal.cpp EndHook), soft against the scene depth, lit by sun + ambient
// (gpufx_gpu.h Renderer). Purely visual: every machine makes its own from its own events, no network message.
//
// The stock ParticleRender (smoke, dust, fire cards; vtable 0x17A8F70) draws through its slot 1 (0x302420). With ini
// GpuFxNative=1 a whitelisted emitter's draw is taken over: its render snapshot (the 0x40-byte entries SyncRender built)
// is copied into a batch with the stock texture and drawn by our renderer at that view's world end, the stock draw
// skipped. The stock simulation, culling and lifetime are untouched. Off by default until parity is shown.
//
// ini (Config): GpuFx (the module), GpuFxTest (test cards and a puff column 25 m ahead of the player at mission start),
// GpuFxNative (the stock cards' takeover), GpuFxProbe (render state, matrices, the light block, stock particle state
// to the log).
#pragma once
struct ID3D11DeviceContext;

namespace crew::gpufx {

// Plugin setup, once: the stock particle draw slot (fingerprinted) and the flipbook's generation (a worker thread).
bool Install() noexcept;
// Game thread, once a frame (crew.cpp SharedFrameTick): the pool's step, the test emitter, the publish to the renderer.
void Tick() noexcept;
// Mission shutdown (mission.cpp): our particles and the test anchor are dropped.
void Reset() noexcept;

// Render thread (thermal.cpp's world begin / end redirect, firstperson.cpp's mesh callback).
void WorldBegin(ID3D11DeviceContext* context,const void* view) noexcept;
void WorldEnd(ID3D11DeviceContext* context,const void* view) noexcept;
void ObserveSurface(ID3D11DeviceContext* context,int pass) noexcept;

} // namespace crew::gpufx
