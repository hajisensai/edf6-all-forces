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
