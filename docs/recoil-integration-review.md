# Recoil PR #49 / #61 integration review

Both original PR heads are preserved. #49 aligns mission and test-range gun mounts with the matching player-call recoil parameters. #61 applies remote-gunner recoil on the vehicle authority using the original weapon messages and class-specific recoil callback.

Review corrections:

- Recognize classic SGO call setups whose floating-point values use the lossless `sgo.Float` wrapper, as well as DSGO calls. A cross-format regression reproduces the old rejection and checks identical mount values.
- Restore temporary weapon recoil strength with SEH `finally`, including when the native recoil callback faults. The production-hook test injects a native exception and verifies restoration.

Validation: both Windows plugins built; 43 CTest cases passed, followed by the modified gunner regression; all 103 Python checks passed. The recoil audit against the local Root.cpk passed. PyInstaller packaging succeeded and the frozen archive contains both recoil modules and the exact built plugin DLLs. The eight class callback addresses, authority ABI, operator network flags and stale-message rejection were checked against the local EDF.dll. Existing relevant stock weapon definitions have FireCount=1; the received counter represents projectile sequence.

No game session or two-machine E2E was run. Driver displacement and the remote gunner's visual correction remain live acceptance checks. No installed game files were modified by this review.
