// The new classes' save (src/soldier_save_logic.h; docs/plan-2026-11-28-new-classes-impl.md 模块 D): MAIN.GST never
// holds a class past the stock four; the real one comes back after a load.
#pragma once

namespace crew {
// Hooks the save job's queueing (0x7BCFAF) and the weapon records' load completion (SetSaveBlockLoaded). Once, from
// EML6_Load, after InstallWeaponRecords. False: refused (logged), saves are the game's own.
bool InstallSoldierSave() noexcept;
// Whether a save writes a stand-in for class 4 / 5. Without it a class past the stock ones must not be offered: the
// file would hold it, and the stock game (the plugin removed, or refused after an EDF.dll update) indexes SoldierInit
// with it unchecked.
bool SoldierSaveActive() noexcept;
}  // namespace crew
