# Tank barrel oscillation: native actuator readback

The supplied game log around 18:27 shows a stationary-view Blacker continuing to report
approximately ±0.5° stabilizer yaw correction while the hull turn rate falls toward zero.
The turret-camera ownership/high-view refresh issues are reviewed separately; this change
does not alter turretcam's view geometry, input controller or zoom.

## Verified second writer

All addresses refer to the supported EDF.dll `678CCB46`.

- CarBase's `6696A0` obtains the two physical joint frames, measures their relative angle,
  and calls `5ECB40` to update the native constraint angle.
- At `669A1F`, only motor type **1 (velocity motor)** continues to `669A52 → 5FC140`.
  The real Root `VEHICLE403_TANK.SGO` uses type 1 on `cannon_main` yaw and type 2 on
  `cannon` pitch, consistent with the observed yaw correction dominating the log.
- `5FC140(aim,axisIndex,boneIndex,measuredAngle)` transforms that measured local joint
  angle back through the authored axis/bone curve. `5FC230` writes the actual axis angle;
  `5FC239` writes the difference from the previous target. It remaps bones through
  `5FC280(axis,false)`. It does not advance the input motor rate at axis `+C`.
- The subsequent `5FBDA0 → 5FBC00` input step adds the rider/NPC's new command, and the
  stabilizer runs afterward. Previously its world reference still represented the old
  actuator command, so it treated the native physical tracking difference as additional
  stabilization error. That created a second position controller commanding the same motor.

The older offline vehicle model set the physical yaw joint directly to its requested angle
after each frame. That zero-tracking-error assumption could not exercise this feedback path.

## Correction and phase contract

The only native callsite `669A52` is redirected through `ReadbackHook`. It captures the
old requested axis angle, calls the native readback unchanged, then applies exactly the
measured **local-axis difference** to the held reference, in `hold.seen` from the previous
pose. It never overwrites `hold.last`, `hold.seen`, the current vehicle matrix, motor rate,
authored limits or physical state. The next regular stabilizer step therefore still sees
this frame's true parent rotation and rider input as separate inputs.

This reconciliation is available before `StabHeld` is queried by turretcam/AutoTurret,
so they use the same native measured state instead of one controller using the old target
while another uses the physical readback. Non-velocity/animation-only axes do not pass this
native call and remain unchanged. Remote or inactive seats are not reconciled.

Installation checks the original call/continuation, motor-type branch, readback prologue,
exact angle/delta writes, input-step writes and bone-map signatures. A mismatch or failed
call redirect disables stabilization; existing hooks then pass through to native code.

## Executed verification

`tests/stab_native_readback_test.cpp <EDF.dll>` loads a private image without DllMain and
installs the production readback hook. It executes the real native readback, full plain aim
step, axis step and bone mapping on private axis/bone buffers. The skeletal mount probe is
left at the known seat-0 hull basis; the fixture does not create a Havok world.

- Old-path negative control: fixed hull, -0.5° native actuator readback produces +0.459°
  additional stabilizer command. Corrected path produces less than 0.0001°.
- Combined frame: -0.3° tracking difference, +0.2° hull yaw, and nonzero player input.
  Correction equals only the hull term, and the original motor command/rate are retained.
- Moving the hull matrix refresh to either side of the native readback produces the same
  final result; the previous parent bases remain byte-identical.
- 180 native frames with repeated physical readbacks produce no added stabilization jitter
  (maximum measured correction 0.000000° in the fixture).
- A changed native readback-write instruction rejects installation and disables stabilization.

The pure `stab_check` adds the combined tracking/hull/input contract for CI without game
files. Existing moving-hull, limits, turret-camera and Grape checks and 330 bone-interpolation
samples still pass. The complete plugin builds with `/W4 /WX`.

This proves removal of the redundant native-readback correction, not a full Havok response
model or live-game visual acceptance. The supplied game session has not been rerun, no game
process was started, and no installed file was changed. Missing native DLL returns skip 77.
