# TimeShip native skill contracts (2026-10-19)

Read-only evidence: supported Steam EDF.dll TimeDateStamp 0x678CCB46, Root.cpk original SGO/AC. No game process run or game directory writes. This document records native contracts; implementation validation is separate below. RVAs below are module-relative. Multiplayer acceptance/authority is not proven by native AS existence; that must be supplied by the host action bridge.

## Warp / Warp2

Registration 0x1F2AD9 references ASCII 0x1799638 `::Object& EDF6_TimeShip_Warp(float )`; function pointer 0x1CB6E0. Warp2 uses wrapper 0x1CB610 at registration 0x1F2B4F and text 0x1799688. Both wrappers acquire a real TimeShip via 0x1C6D30, copy current world matrix +0x60..+0x90 to an aligned local Mat4, multiply seconds by 60.0 (constant RVA 0x1765A58), truncate to integer frames, then call native:

- `void 0x3F3040(TimeShip* rcx, const alignas(16) Mat4* rdx, int frames r8d)` (Warp).
- `void 0x3F3160(TimeShip* rcx, const alignas(16) Mat4* rdx, int frames r8d)` (Warp2).

Native copies all four matrix rows to +0x60..+0x90 using MOVAPS, resets local deformation +0x1430..+0x146C, stores +0x1470=frames and switches the real state machine +0x10B0. There is no internal alive/busy/finite gate. Callers must validate the live supported root, aligned finite full matrix, allowed current state and bounded nonnegative frames.

Warp enters 0x3F1250: entry ORs +0x1168 with 7, shrinks deformation, disables model/physical components through 0x6C04B0/0x6ECCF0 and child visibility 0x3E9780; update waits state tick >=+0x1470 then enters 0x3F1C30. Exit restores the hidden components through 0x3F01F0 and deformation. 0x3F1C30 is a 120-frame reappearance with a local Y effect offset evolving from -3000 to zero, then switches idle 0x3F1170.

Warp2 enters 0x3F1430: same flags and hidden components, plus native begin effect/audio; it waits +0x1470 then enters 0x3F1FB0. The latter has 120-frame expansion/distortion and returns to idle 0x3F1170. The native pad button 0 at 0x3EE611 explicitly initializes +0x1470 to 0x258=600 and enters this same 0x3F1430 state. Thus 600 frames is a real pad default; it is not inferred from unknown current +0x1470.

Idle entry 0x3F117D clears +0x1168 to zero and resets native CAS. Do not manually clear flags early or replace native cancel/exit handlers. +0x1430..+0x146C is local deformation: 0x3F2CB0 multiplies it with native orientation and world +0x90 to produce model +0x860..+0x890; identity cannot relocate the object to world origin. With the AS wrapper's current Mat4 both warps are in-place. A selected target Mat4 would invent an extra player teleport feature and is not needed for skill parity.

Original Root.cpk source samples, rather than claimed campaign-wide BVM defaults:

- MISSION/SAMPLE/EDF6_TIMESHIPWARP/MISSION.AC calls Warp(0), Warp(1), Warp(2), Warp(10).
- MISSION/SAMPLE/EDF6_TIMESHIPWARP2/MISSION.AC calls Warp2(2), Warp2(4), Warp2(5), Warp2(10).
- SOUND_WORK/TIMESHIP_WORP2 uses Warp2(5), (6), (8), (10).

`WarpVanish()` wrapper 0x1CB830 calls 0x3F3280(root), which writes +0x1474=2. `WarpVanish2()` wrapper 0x1CB7B0 calls 0x3F3290(root), writing +0x1474=1 (same as pad button 3). Tick 0x3EF6E2 consumes this only when !(flags&4); value 1 selects 0x3F2920, value 2 selects 0x3F2690. These are terminal retreat/removal operations; end player control before native destruction.

## Anchor deployment

Root SGO `EDF6_TimeShip.Anchor` is {link_node:anchorTarget, object:e612_anchor.sgo}; full_light/light variants use corresponding e612 variants. This is not a free-standing map-only ability. Root constructor 0x3EAEF0 reads this member at 0x3EB2BD, creates link/payload via 0x3EC1A0 and an anchor controller via 0x3EACB0, saved at root+0x1110. That controller resolves CAS player `anchor`, states anchor_open/anchor_close/hatch_open/hatch_close.

Canonical request: `void 0x3ECA80(TimeShip* rcx, const Vec4* rdx)` saves +0x1410=1 and +0x1420=*target when byte[controller+0x120]==0. Natural pad button 1 uses the same fields after a downward world ray and forces target.w=1. Native method returns no bool and has no null/busy validation. Callers need root/controller/link weak liveness, flags&4 clear, finite target and w=1. The native consumer 0x3EF651 clears +0x1410 and switches 0x3F04D0, which aligns to target, then switches 0x3F0300. This copies target to controller+0x110 and switches the controller to 0x3F0A40. 0x3F0A40 calls 0x3F3780(link,target).

Weak source: 0x3E93D0 acquires a strong reference from link+0x48/+0x50. 0x3ED3C0(link,root,root+0x8A0,SGOentry,bool stack) reads `link_node` and `object`, looks up the actual model bone, creates the configured child through 0x11945E0, registers ownership through 0x118AF20 and stores its weak ref. 0x3F3780 sends the actual target/release message through child's vtable slots 10/9/11 (native message lifecycle), sets link+0x70=1, unparents via 0x118AEA0 and clears link weak ref. Never replace this with CAS playback.

Deployment is terminal for the ship: after 60 ticks controller switches 0x3F0B90 (anchor close), completion sets controller+0x121=1; root 0x3F0300 detects this and switches 0x3F0820, climbs and calls native removal 0x118A1B0. Hand back player control before this lifecycle reaches removal. controller+0x120 is a native child-death/broken flag, not a general cooldown.

AS `DropAnchor(const string&)` wrapper 0x1CB300 resolves the named scene transform then invokes canonical 0x3ECA80 with its position. `DropAnchor2()` wrapper 0x1CB280 calls 0x3ECAB0, which computes the downward ray target. `SetForceHatchOpen(bool)` wrapper 0x1CB570 sets controller+0x123; it changes the existing anchor controller lifecycle and does not create a new projectile.

## Capsule creation, preparation, release

All three original E611 root SGO variants declare eight Capsule entries, capsuleTarget_A..H, each with object e613_capsule.sgo. This describes resources/mounts, not eight already existing children. Constructor initializes root+0x1140/+0x1148/+0x1150 to zero. Original missions explicitly call CreatePods; no automatic initial count should be inferred from eight SGO entries.

`void 0x3EC680(TimeShip* rcx,uint32 count edx)` is AS CreatePods(uint32) wrapper 0x1CB0A0's native target. It reads `EDF6_TimeShip/Capsule`, creates min(requested,8) link/payloads through 0x3ED3C0, appends 16-byte shared-link handles to root+0x1140 array, increments +0x1150 count, and inherits root team +0x314 through 0x54EE70. The native call is append-only: repeated calls exceed total eight. A player adapter needs an explicit bounded once-only preparation state, or only when the original count is zero; preserve existing mission-created pods and their occupants.

`void 0x3ECC10(root,uint32 index,int delayFrames,const Vec4* target)` directly indexes array[index*16] without bounds. delay<=0 calls 0x3F3780, otherwise saves link+0x60 target and link+0x58 countdown. This entry does not run the extra activation in DropAllPods.

`void 0x3ECC40(root,const Vec4* center,float minRadius,float maxRadius,int seed)` (Windows x64: rcx,rdx,xmm2,xmm3,stack5) is AS DropAllPods wrapper 0x1CB130's target. It divides a full turn by root+0x1150, so reject count zero. It activates each child through 0x3ED9C0, computes evenly spaced directions with radius min+(max-min)*random, corrects target via vertical +/-1000 world ray, and releases at delay 0,30,60,... frames through 0x3F3780 / link countdown. Use the real pipeline; do not separately replay each peer's camera/input. Existing pending or released links must not be replayed. link+0x70 denotes already released and +0x48/+0x50 weak refs become empty; array and count remain.

AS wrapper resolves a named scene transform, passes its position, preserves caller min/max radius and derives the deterministic seed from position+min+max with a native scale constant. Sample MISSION/SAMPLE/EDF6_TIMESHIP_DROPCAPSULE calls CreatePods(4), explicitly fills pods 0 and 2 with e503_frog_af (3) / e503_frog_sg (2), then DropAllPods(center,50.0,100.0). DROPCAPSULE2 calls CreatePods(4), fills all four and calls individual DropPod targets. These are source examples, not universal runtime defaults.

SGO e613 has sheets, sounds and destruction object, no creature table. Occupants are created separately by `EDF6_TimeShip_CreatePodCrue` wrapper 0x1CABB0 (index, SGO string, count, level, encounter) and attached with 0x3FAE90. The wrapper is script-context ABI and cannot receive a root pointer directly. Creating empty pods is not equivalent to summoning enemy squads. Preserve actual existing occupants, native team and parent ownership.

## Integration / authority boundary

Native APIs accept raw objects and do not authenticate a sender or synchronize a command. The action system must validate host ownership/lease, sender, same object generation, sequence, resource availability, finite targets/matrices, alive flags and native ability availability before invoking. A root +0x10B0 state transition is evidence for Warp/Anchor request consumption; capsule appends/activation/releases have separate native child/link state and require typed accepted events with stable object identities/ordinals, and guest acknowledgment only after one actual successful native apply. No cross-machine replication was tested or established by this research.

## Implemented front door and independent validation

`src/enemy_control_timeship.cpp` supplies stable mask-zero Warp, Warp2, Anchor, Vanish and Vanish2 catalogue entries. The sibling pods adapter contributes four fixed entries (Hatch open/close, prepare empty pods, drop all); missing child support refuses the whole catalogue instead of silently omitting skills. Resource removal changes availability rather than renumbering commands. Root commands require the original admitted profile/weak identity, current accepted input sequence, native idle/move-ready state, fingerprints and finite aligned current Mat4 / canonical weapon aim. Warp uses the exact native600 pad default, keeps the current transform, and retains the native effect/cancel/120-frame reappearance lifecycle. Terminal commands keep the real destruction path. Online input is host-only with an active NativeActionRole; lack of a role cannot be confused with offline.

`RestoreTimeShipControl` clears per-identity sequence state only after child restoration succeeds, leaving native already accepted actions and world children to finish. Partial or failed native apply keeps the identity faulted until a successful restore; stale input cannot replay it.

- `/W4 /WX /utf-8 /permissive- /MT` production-cpp boundary fixture:69 checks,0 failures.
- `tests/enemy_control_timeship_native_audit.py`:179 independent checks against local supported EDF.dll and original Root.cpk, covering AS wrappers, full native lifecycle, original three E611 variants, all24 capsule mounts, factory classes, native initialcount0 and original sample arguments.
- No live game, native child networking or two-machine acceptance is inferred from those checks. Pods/hatch typed event integration is a separate adapter concern.

AS ownership gates in `enemy_control_timeship_script.cpp` patch only the six verified wrapper call sites (Warp/Warp2/Anchor/AnchorBelow/Vanish/Vanish2), preserving raw native helpers for accepted guest replay. Offline or uncontrolled vanilla script behavior is preserved. An existing authenticated action role takes precedence over a failed session probe, and quiescing hosts cannot start new script actions. Installation validates all sites before mutation and exact rollback retains any foreign-mutated live thunk. The independent executable-call fixture passes43 checks.

Final authority check uses the exact retained core session through `EnemyControlNetworkContext(target,&online)` after any existing authenticated action role. A missing/unknown core context cannot authorize a manual command by a global false session probe. For AS wrappers, only a genuinely unbound/autonomous actor keeps vanilla behavior; bound guest and quiescing roles still reject new actions even after the ordinary root input vtable is restored.

Capsule fall replication was independently traced in the original image: vtable0x17BA800 slots9/10/11 are0x3FB4D0/0x54AA50/0x54A970. Fall uses private message variable0x20B33DC, initialized at0xBB70 by generator0x11BAF30, whose counter0x1FEEEC8 starts at0x80000000. Generic pre/post helpers0x774260/0x7746C0 broadcast damage0x10000000, not this private fall message. The fall handler0x3FB67E itself has no idle deduplication gate, so the typed accepted guest stream still requires ordinal idempotency. This proves no extra stock fall broadcast on this supported native path; it does not substitute for live cross-machine acceptance or an audit of arbitrary external plugin hooks.

## Final module validation after exact-session integration

The complete TimeShip group now consists of `enemy_control_timeship`, `enemy_control_timeship_script`, `enemy_control_timeship_pods`, `enemy_control_timeship_pods_script` and `enemy_control_timeship_pods_net`.

| Production boundary fixture | Checks |
| --- | ---: |
| Main Warp/Anchor/Vanish commands and restore/install failures | 69 |
| Main AS call-site gates / exact rollback | 43 |
| Pods, Hatch and their actual AS trampolines | 242 |
| Typed child creation/drop/identity/network outcome stream | 1515 |
| Total C++ checks | 1869 |

Independent native audits cover179 main/AS/resource assertions,61 Pods assertions and84 network contract assertions (324 total). All strict fixtures passed without NOMINMAX. The child group numbers were executed by its owning implementation agents; the final repository integration still runs the registered CTest targets.

The typed network path preserves host-generated native IDs, original child/link ownership, actual accepted targets/delays, retry idempotency and successful local apply before acknowledgment. These are offline production-code fixtures and native-image audits, with no real-game or two-machine acceptance claimed. Existing task-provided passengers remain intact. Arbitrary `CreatePodCrue` SGO recipe injection is still a separate mission-script factory surface and is blocked for a controlled online root rather than pretending that empty capsule preparation provides a configured squad.


## 接受结果晚于根对象逻辑死亡（2026-10-19）

胶囊网络扩展现在在已绑定租期持有原生 root strong pin；生命值与逻辑死亡照原生处理，只把析构推迟到接受结果排空。实际 `3EC000` 析构在 `3EC077/7E` 清理 `+1138` roster、在 `3EC083/8A` 清理 `+1128` 原型，因此新旧胶囊仍可沿真实 `3EC680` 创建和 `3ECC10→3F3780` 释放。只有事先绑定并保留同 weak 身份的会话允许 dead replay，未知尸体不能因一包消息被重新采用。

Quiesce 额外发布 typed kind4 对账结果：保留每个已接受胶囊的真实 NativeID、目标点及原生剩余 delay；实际释放过的胶囊在对端仍调用真实 Fall/Detach，已释放者不会重复发动。原生延迟释放由 `3EF96B..3EF977` 的倒计时/调用驱动；房主死亡前尚未释放的胶囊不会被误当成已投放，必要时按该真实子体 ID 原生取消。强、弱计数以及析构基线在页不可写时保留，不先清标志后丢引用。

这一层仍使用现有 host-accepted extension 批次，不改通用 wire。生产夹具新增 host 已投放/guest 收包前 root 死亡、birth 晚到、延迟胶囊已实际释放与未释放混合、重复包以及真实 readonly 控制块恢复；精确运行数以最新 `enemy_control_timeship_pods_net_test` 输出为准。DLL 审计当前 84 项通过，仍无实机/双机验收。

Late capsule construction also uses the typed controlled-PVP birth bridge before registration: read-only Can/Prepare, actual native Create, full child-set preflight, Adopt for every body, then the first RegisterChild. Pending ownership makes no native birth; a failed later adoption deletes the complete unregistered new set and retains any failed rollback journal. Stable mode-off/unleased normal control does not depend on optional PVP hook installation. The separate transport/generic integration fixture covers the real sealed PUID, dead-but-retained parent, immediate child team application and final baseline restoration; the capsule fixture explicitly stubs only that boundary and verifies ordering/rollback.
