# Runtime vehicle feedback, 2026-10-06

This batch follows the user's screenshots and installed c871652 build, rather than treating the previous offline checks as game acceptance.

- Proteus: register the shared vehicle pipeline on its real slot-4 update. The previous slot-55 AI path never ran the player's rework, despite successful hook installation.
- Titan: preserve the corrected angle delta when remapping axis bones, without feeding that correction back into the native motor rate. Correct embedded gunner aim access and full-circle yaw wrapping.
- Helicopters: separate the attitude callback's pitch input from the translation speed command for local mouse-aim control. Recover the mouse aim inside downward-looking views; label forward speed, vertical hold, ground clearance and the threat direction display separately.
- Drill and twin cannon: generate dedicated CAS animations whose absolute translation channels match the custom model bindings. Preserve intended recoil and drill spin. Replaying the original CAS reproduced the supplied misplaced geometry; the corrected assets remove the displacement.
- Katyusha: remove cargo sideboards while retaining the bed, chassis and launcher anchors.
- Sidecar: lower the tub and shield, join the deck to the shell without a crack, and place support members below the passenger floor. Preserve the passenger's location and collision floor.
- Aircraft audio: stop loops for unused/parked aircraft; distinguish quiet occupied ground idle from powered zero-speed hover.
- Aircraft collision: use model-derived Havok compounds for both contact proxies and the moving flight body, covering wings and sharing geometry between NPC/player variants. See `aircraft-collision.md` for approximation bounds and runtime fallback diagnostics.
- First-use boarding: all unused parked vehicles wait for their first player driver. Visiting a passenger seat alone does not permit auto-crew. After the player drives and exits, the existing delay/range rules permit NPC takeover. Mission NPCs remain untouched.
- Aircraft entrances: publish the real native seat locator, radius and distance to the HUD. A visible marker leads the on-foot player to large aircraft's distant entrance; it becomes a boarding prompt only within the native radius. Rendering never dereferences vehicle objects.

Regression coverage includes production crew/entrance, helicopter input, Proteus update, gunner aim, axis interpolation and sound paths; actual game-data model/CAS replay and compound reconstruction complement portable geometry checks. These checks do not replace live Havok loading, controls, audio listening or multi-machine online acceptance.

## Follow-up feedback and NPC AI integration

- Catch aircraft converge on the actual native boarding point using relative translation and rotation instead of a one-second absolute lead. Landing clears the air-cruise state, uses bottom clearance and transfers throttle control to ground handling.
- Aircraft controls display the configured keyboard bindings and actual seat gamepad buttons. Unguided plugin rockets now have an impact prediction matching their production motor/guide steps.
- New map orders discard obsolete attack targets and reach their duty area before combat; pursuit remains bounded by that duty area. Carrier-owned drones retain their launch/return ownership rather than pretending to accept direct independent orders.
- Sidecar knockdown cancels virtual occupancy before movement; recovery cannot pull the passenger back. Following reads the current native controller position. Projectile and explosion filters only exclude that passenger's currently occupied vehicle and seated driver, preserving other targets and self-damage.
- NPC AI PRs #53–#59 are integrated with the current localized command panel and snapshot lifetime rules. Core review fixes protect script squads, faction ownership, recruitment cooldowns, both-hand blast safety, native boarding distance and cancelled boarding orders. Tank return-to-post respects native steering and forgets old posts after scripts/player control. Selected units retain control-block identity rather than an address alone.

The user explicitly stopped in-game testing. Subsequent validation is limited to source review, builds, binary/resource inspection and offline production fixtures; no live-game verification is claimed.
