# PR integration review — 2026-10-06

This integration preserves the original commits of PRs #5, #16, #18, #19, #20, #21 and #23. The vehicle stack already includes #20. All seven changes are validated together before entering main.

## Correctness fixes

- Boarding rifle: queue boarding only after the native shape cast confirms a hit, at the verified direct-hit damage call. Broadphase candidates behind cover or merely overlapping a swept AABB no longer board the player. Preserve native hit bookkeeping and bullet lifetime.
- Nix and Proteus: use each seat's embedded aim object instead of reading its vtable as an object pointer. Synchronize the paired cannon after native aim update and before firing. Install TurretCam, Stabilizer and Proteus hooks in that order.
- Sidecar: check the stock three-dimensional boarding radius and consume the full traversal after a successful boarding press. The boarding rifle uses native seats only, avoiding unrecognized virtual sidecar boarding.
- Map: invalidate camera ownership by mission generation; discard old camera matrices on the camera thread, including reused addresses.
- Vehicle ramming: do not evict unexpired target cooldowns when contacting a crowd. A full cache defers new targets instead of multiplying damage every frame.
- Primer creatures: select a real reachable trajectory rather than clamp a ballistic angle; do not dereference expired linked creatures; disarm before reading an absent aim target.
- Ragdoll fitting: retain the physics-system type while reading constraint types, so all skeleton reference poses are loaded, validated and transformed.
- Online detection: synchronize the signature's one-time initialization across picker and game threads.
- Integration: preserve both 506 message handlers, existing weapon row identities, aircraft mass metadata and hostile creature classification. Package dynamically imported creature builders and update simulator stubs for the combined runtime.

## Executed validation

- Both Windows plugins built with MSVC and the project's warning-as-error options.
- 21 CTest checks passed, including production-path boarding and sidecar fixtures, camera lifecycle, crowded ram cooldowns, seat aim, ballistics, sound, HUD layout and logging.
- Python install-chain self-tests: 69/69 passed. Includes real local Root.cpk ragdoll reference-pose corruption and refit checks.
- Seven building-avoidance scenarios: zero building entries and zero ground hits. A 90-second Primer chain-break simulation completed.
- Tracked Python sources compile; generated calls/stores match their source definitions; git diff whitespace check passes.
- PyInstaller release packaging succeeded. The executable archive contains centipede_model, dragonfly_model, procmesh, make_emc and ragdoll_fit.
- Native offline checks now run in GitHub Actions instead of relying on plugin compilation alone.

## Remaining validation limits

No game session was launched. Live visuals, vehicle controls, hooking in a running game, and multi-machine online behavior remain unverified. Offline fixtures and matching native signatures do not replace game E2E testing.
