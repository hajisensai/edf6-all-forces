# PR #27–#48 and #50–#52 integration review

This batch preserves all 25 original PR heads and merges their shared dependencies once. PR #49 and the draft NPC-AI series #53 onward are outside this batch.

## Corrections required by review

- Map commands publish validated position snapshots rather than dereferencing vehicle memory while holding the rendering lock. Destroyed units cannot strand the lock.
- Map pointer initialization waits for the first valid viewport. Mouse movement and command keys correctly switch away from gamepad targeting.
- Map HUD suppression follows the owning camera, preserving the other split-screen player's follower bars.
- A newly spawned same-kind rescue aircraft can enter player-flight control before its first player-flight record exists, with identity validation.
- Player-controlled rotor aircraft and summoned landing paths do not inherit the NPC-only soft goal boundary. The player's measured map boundary remains active.
- Gunship fire rejects an unreachable muzzle outside the airframe rather than silently firing from its centre.
- Replacing another mod's later edit creates a separate recovery backup before writing. Uninstall restores the accepted replacement's predecessor.
- Installation checks include the target-range mission script, route data and marker, which are outside the generated-asset ledger.
- Integration retains both sets of regression targets and localizes the new map-boundary warnings introduced after the HUD translation branch.

## Verification

Both Windows plugins built successfully with MSVC warnings treated as errors. All 42 CTest checks and 101 Python installation/data checks passed. Generated calls/stores match their sources; tracked Python sources compile and the whitespace check passes. PyInstaller produced the installer package; its embedded DLLs and INI files match both plugins' build outputs and the data builders are present. Tests read local game data where available; installation mutation tests use temporary fixtures. The split-missile hook's ten signatures were compared against the local EDF.dll.

No live game session was launched. In-game controls, rendering, injection and multi-machine online behavior remain unverified; offline fixtures and static signatures do not establish game E2E correctness.
