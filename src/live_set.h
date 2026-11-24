#pragma once
// The objects the game itself handed the plugin this step (GameFrame): the enemies and friends its team walks visit
// (npcai Gather) and the soldiers it calls Think on. A pointer the game handed over this step is in use by the game this
// step, so asking whether its pages can be read (edf::Readable: a working-set call when the page cache's tick has run
// out, which in a long step it always has) tells nothing more. A page that reads is not a live object anyway: memory
// freed back to the game's heap stays mapped and reads. Game thread only.
//
// Not an answer for an object deleted later in the same step (after it was handed over): no worse than Readable, which
// answers yes for that one too, and every reader is under its __try (a fault costs the frame's AI, as before).
namespace liveset {
void Add(const void* object) noexcept;   // handed over this step
bool Has(const void* object) noexcept;   // handed over this step (an object of an earlier step is not)
}  // namespace liveset
