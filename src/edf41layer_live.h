// The game's collision layer machinery the ported 4.1 parts use, read from the running EDF.dll (src/edf41mother.cpp, where 301's
// parts first needed it). The pure part (the tables, the choices) is src/edf41_hitlayer.h.
#pragma once

#include <cstddef>
#include <cstdint>

namespace crew::edf41layer {

bool LivePrims() noexcept;                                                // the functions below match their fingerprints (logged once if not)
int LiveAreaQueryLayer() noexcept;                                        // the area damage query's layer (the byte in 0x106070)
int LiveRagdollLayer(void* ragdoll,std::size_t* count=nullptr) noexcept;  // the layer of a ragdoll's first body (-1: none)
const std::uint32_t* LiveTable() noexcept;                                // the group filter's lookup table (null: not found)
void LiveSetRagdollLayer(void* ragdoll,int layer) noexcept;               // every body of the ragdoll to `layer`, groups kept (0x6E9320)

}  // namespace crew::edf41layer
