// Weapon records past 2048 (src/weapon_records.cpp, docs/loadout-re.md 8.8): the game status's per-row weapon records
// relocated so the weapon table can have up to wrec::kSlots rows; rows 0x800.. saved in a sidecar next to MAIN.GST.
#pragma once
#include <cstdint>

namespace crew {
// Patches the game (once, from EML6_Load, before the game status exists). False: refused, nothing relocated (logged).
bool InstallWeaponRecords() noexcept;
// Whether the records are relocated (the extension works).
bool WeaponRecordsActive() noexcept;
// How many records the game status holds: wrec::kSlots relocated, 0x800 natively.
std::uint32_t WeaponRecordCapacity() noexcept;
// Record `id` of the game status `gs` (12 bytes: u32 flags, 8 star bytes), wherever it lives now; nullptr past the
// capacity. The caller checks the memory (Readable) as before.
unsigned char* WeaponRecord(unsigned char* gs,std::uint32_t id) noexcept;
// The weapon table has `rows` rows: past the capacity the game writes past its records. Logs and tells the player
// once a session (a refused relocation with an installer that added rows past 0x800, or a table past kSlots).
void WeaponRecordsCheckRows(std::uint32_t rows) noexcept;
// A load's completion copied a file into the game status's save block (0x7BE970's tail copy, ResultCopyHook; only while
// the records are relocated): `fn` is called with the block right after the copy and the table's read-back, before the
// completion returns (on its thread). One listener: src/soldier_save.cpp. nullptr: none.
using SaveBlockLoadedFn=void(*)(unsigned char* block) noexcept;
void SetSaveBlockLoaded(SaveBlockLoadedFn fn) noexcept;
}  // namespace crew
