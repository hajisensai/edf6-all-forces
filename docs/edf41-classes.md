# EDF4.1 classes on EDF6: the shared machinery (src/edf41classes.h / .cpp)

UfoMother301 (src/edf41mother.cpp), the 401 mothership and UfoRobo are classes EDF6 no longer has. The plugin registers them in
the game's own factory table (AddFactory: RegisterSceneObjectFactory 0x119AF00 right after the game registers its own at
0x701859 -> 0x79D570), builds each object with a stock class's constructor (EventAnimationObject 0x5B5FB0 for parts and plain
bodies, UfoCarrier508 0x4F8690 for the 301 hull) and points its vtable at a clone with slots 0 / 5 / 9 (and 16, the damage
filter) replaced. Section 1 below is how that was settled (docs/edf41-ufomother301.md, phase 1); section 2 is every EDF6
primitive the classes call, read from EDF.dll (static reading, 2026-10-15; the plugin fingerprints each before calling it).

Sounds: EDF6's SEPRESET has none of 4.1's preset names; tools/make_edf41_objects.py se41_table copies 4.1's SOUND/SEPRESET.SGO
entries (name -> [cue, volume, pitch, three distances]) into the object's SGO key edf41_se, and PlaySe plays them by 4.1's name
on the cues EDF6 ships in its tikyuu4_* banks (TIKYUU4_EN_MOTHERSHIP, _EARTHEATER, _UFOROBO ... are all in SOUND/PC).

## 0. Offsets from another 4.1 -> 6 port (an EDF modder, relayed by the user 2026-10-15; check against EDF.dll before use)

- 4.1 SHKT files can be used 1:1 in EDF6 without editing.
- EDF.dll+0x6B6E30 "QuickAddMABCallback" (CASController*, std::wstring* name, AnimationModel* model, long long* fnPtr);
  EDF.dll+0x116BE30 is commonly used too (MAB event callbacks).
- Melee damage helper: initialised at EDF.dll+0x6C7CF0 (size at least 0x2E); 0x6C8140 sets up the damage regions, 0x6C8840
  enables a region, 0x6C8760 resets them, 0x6C8A50 is its OnUpdate.
- EDF.dll+0x542670 ApplyAreaDamage; EDF.dll+0x6E40B0 appears to be Activate Ragdoll.

## 1. How section 2 was read

## 2. EDF6 primitives for the plugin's EDF4.1 classes

EDF.dll TimeDateStamp 0x678CCB46, all addresses RVAs. Static analysis only (tools/edfre.py + capstone; scripts in this
directory, h.py / t*.py). The game was not run. Confidence: H = read in the code (and used by a stock class),
M = strong inference, U = unverified guess.
"fp" = the first 8 bytes at the RVA, for a fingerprint check.

Object bases (from the constructors, H):

| | EventAnimationObject (ctor 0x5B5FB0, 0xC90) | UfoCarrier508 (ctor 0x4F8690, 0x1630) |
|---|---|---|
| model component ("animation_model", init 0x6BB890) | +0x5C0 | +0x650 |
| skeleton instance (= model+0xA0) | +0x660 | +0x6F0 |
| CAS players (= model+0x330), main player index | +0x8F0, [+0xC80] | +0x980 (main idx elsewhere) |
| ragdoll component (ctor 0x6E3B10, init 0x6EB4C0) | +0xAA0 | +0xB60 |
| lock-point component (0x6C74E0) | none: EAO never registers lock points | +0xD40 |
| se_list presets (vector, data +8, count +0x18, stride 0x80) | none | +0x1600 (data +0x1608, count +0x1618) |
| team after ctor | SetTeam(this, 6, true) at 0x5B6418 | none: +0x314 stays -1, not registered |
| +0x380 after ctor | GOB default 0x4044420 **\| 1** (0x5B6406: bit 0 = no HP loss, see 7) | GOB default 0x4044420 |

GameObjectBase fields used below (GOB ctor 0x545670 unless noted, H): +0x18 bit 2 deleted; +0x1A AI/flags byte (0x118A4B0);
+0x28/+0x30 own weak ptr; +0x60 matrix (rows x, y, z, pos); +0x90 position; +0x2E8 destroyed byte; +0x2EC base
durability (SGO game_object_durability, 0x546215); +0x2F0 min durability (0); **+0x2F4 max**; +0x2F8 current; +0x2FC = 0
(int, **not** the max: the first version's kMaxDurabilityAt=0x2FC was wrong, fixed; ApplyDamage clamps to [+0x2F0, +0x2F4] at 0x548162);
+0x314 team, +0x318 last team != -1; +0x360 lock points (vector of shared_ptr: data +0x368, count +0x378); +0x380 flags;
+0x394 damage-taken multiplier (1.0); +0x498 raw level; +0x49C level (default 10.0, 0x54648A); +0x4A0 activated byte
(0x548D50); +0x510/+0x514 retarget counter / interval (60, 0x5464F2); +0x518/+0x520 current target weak ptr;
+0x528/+0x530 forced target weak ptr; +0x590 items dropped byte.

---

## 1. Bone / MAB locator world matrix by name

### 1a. Locator handle by name: `0x6BADD0` (H) fp `40 53 55 56 57 41 56 41`
`bool __fastcall FindLocator(void* model, LocatorHandle* out, int mabIndex /*0*/, const wchar_t* name)`
- `model` = the object's model component (EAO +0x5C0, carrier +0x650; DragonSmall +0x630). `out` = 16 bytes
  `{const LocatorEntry* entry; BoneRecord* bone}`, zeroed first; false (handle {0,0}) when the name or its parent bone is missing.
- Reads the MAB block `[model+0x280]` (+[+0x14]), finds the name (0x68710/0x68740), then the parent bone by name in the
  skeleton `model+0xA0` (0x11002A0) and stores the bone record (0x1100280).
- Stock: UfoCarrier508 ctor `0x4F875B` (`rcx=this+0x650, rdx=this+0x1580, r8=0, r9=L"ダメージエフェクト"`), UfoCarrier init
  `0x4F60F1` (排出口), DragonSmall `0x32E79C` (発射口 → +0x1798), GrandMother weak `0x3FFDF3` (弱点), MovingFortress parts
  `0x4CF4E5..0x4CF51F` (AIM_TARGET / AIM / AIM_AXIS). Do it once at construction / first Update and keep the handle; it
  stays valid while the model lives.

### 1b. Evaluate a locator: `0x6BB5A0` (H, real-run proven by the dragon) fp `48 8B C4 48 89 70 10 48`
`float* __fastcall LocatorMatrix(const LocatorHandle* h, float out[16])` → out = locator local (type 0/1: translation only,
2: euler, 3/4: identity rows) × bone world (`bone+0xB0..+0xEF`). Returns `out`. No null check: never pass an empty handle.
- Locator box half-extents: `0x6BB4A0(const LocatorHandle*, float out[4])` fp `4C 8B 01 41 8B 40 08 FF` (H; types 1/2 only,
  else (0,0,0,1)); used by GrandMother weak part 0x37C83E.

### 1c. Bone by name in the model (H)
- `int __fastcall BoneIndex(void* skeleton /*model+0xA0*/, const std::wstring* name)` = `0x11002A0` fp `48 8B 09 E9 B8 7D 00 00`
  (jmp 0x1108060: FNV-1a over the wstring bytes, -1 when absent). Takes a **std::wstring**, not `wchar_t*` (callers build one
  with 0x3D450 = assign(ptr,len)). Vehicle example `0x61A946..0x61A964` (L"cannon" on veh+0xEE0).
- `BoneRecord* __fastcall BoneRecord(void* skeleton, int idx)` = `0x1100280` fp `48 63 C2 48 69 C0 10 01`: `[skel+0x10] + idx*0x110`.
  **No -1 check** (docs/jet-model-re.md). Count = u64 `[skel+0x20]` (0x1100F15).
- Bone world matrix = `record+0xB0..+0xEF` (rows x,y,z,pos; H: 0x6BB66A reads it, docs/mdb-format.md).
- When it is current: EAO's Update `0x5B6D20` = GOB::Update 0x54BE40 → ragdoll 0x6EDCA0 → **skeleton update
  `0x1100B90(obj+0x660, obj+0x60)`** → CAS step 0x116C700. So bones follow the +0x60 you write **before** calling the stock
  Update, and are fresh right after it returns (H); the CAS pose is the one stepped last frame (M).

```cpp
struct LocatorHandle { const void* entry; void* bone; };
using FindLocFn=bool(__fastcall*)(void*,LocatorHandle*,int,const wchar_t*);
using LocMatFn=float*(__fastcall*)(const LocatorHandle*,float*);
LocatorHandle h{};
if(reinterpret_cast<FindLocFn>(image+0x6BADD0)(obj+0x5C0,&h,0,L"発生位置") && h.bone) {   // once
    alignas(16) float m[16];
    reinterpret_cast<LocMatFn>(image+0x6BB5A0)(&h,m);                                    // each frame, after stock Update
}
// a bone ("hatch", "cannon", "waist", ...):
std::wstring n=L"hatch";
int i=reinterpret_cast<int(__fastcall*)(void*,const std::wstring*)>(image+0x11002A0)(obj+0x660,&n);
// MSVC std::wstring layout must match the game's (it does: same toolset; plugin already does this in npcpost.cpp)
if(i>=0 && i<*reinterpret_cast<std::int64_t*>(obj+0x660+0x20)) {
    char* rec=*reinterpret_cast<char**>(obj+0x660+0x10)+std::size_t(i)*0x110;
    const float* world=reinterpret_cast<const float*>(rec+0xB0);   // 4x4
}
```

---

## 2. Damage message 0xF0000000, the hit body, rejecting damage

### 2a. Path (H)
SendMessage 0x1DA480 → slot 10 / **slot 9** / 11. GOB slot 9 `0x54A530` (fp `48 89 5C 24 10 56 57 41`): msg-0xF0000000 jump
table 0x54A928; case 0 = `0x547C30(obj, GDI*)` (ApplyDamage, fp `48 8B C4 48 89 58 18 55`), then RequestDead on the edge
of +0x2E8 and sets GDI+3 |= 1. Data of 0xF0000000 = **GameDamageInfo\*** (the queue entry +0x10).

### 2b. GameDamageInfo (GDI) layout (H unless noted; docs/decoy-blast-re.md §1.2 + filters below)
| off | type | meaning |
|---|---|---|
| +0x10/+0x18 | weak_ptr {obj, ctrl} | attacker (ApplyDamage 0x547C8D locks it; damage-dealt msg 0x10000007 goes to it) |
| +0x20 | int | attacker kind for the per-team multiplier 0x54F050 (M) |
| +0x24 | int | attacker team (-1 = none) |
| +0x30 | vec4 | hit centre / blast centre (world) |
| +0x40 | vec4 | impulse = bullet velocity × AmmoHitImpulseAdjust (direction, M) |
| +0x50 | float | damage (>0 hurts; <0 path at 0x548284 heals) |
| +0x58 | float | explosion radius |
| +0x60 | u16 flags | 0x02 explosive, 0x20 friendly-fire allowed, **0x40 forced** (bypasses slot 16/17 and the +0x380 bit0/0x800 blocks), 0x200 → no damage at all (0x5480DA), 0x400 resets the slot-17 multiplier |
| +0x78 / +0x88 | HitRecord* / size_t | **the sub-hits**: records of 0x30 bytes, `+0x00 vec4 world point`, `+0x20 u32 Havok body id` |

### 2c. Order inside ApplyDamage 0x547C30 (H)
1. deleted → return; self-hit with +0x1A bit3 → return.
2. relation[GDI+0x24][obj+0x314] == 1 (friend) → return unless GDI 0x20 or obj+0x380 0x100000.
3. authority only: slot 35 (+0x118).
4. if `!(obj+0x380 & 0x800)`: **slot 16 (+0x80) `bool Accept(obj, GDI*)`**; if true, **slot 17 (+0x88)
   `bool Scale(obj, float out[2], GDI*)`** (out[1] survives as a multiplier); if both true, xmm7 = 0x54F050 team multiplier.
   Either false (or 0x800 set) → only GDI 0x40 (forced) damage continues, otherwise return.
5. `+0x380 & 1` (EAO!) and not forced → no HP change, but steps 7-8 still run (activation 0x548D50, slot 18).
6. hp = clamp(hp − dmg × [+0x394] × friendly × out[1] × xmm7, +0x2F0, +0x2F4).
7. hp ≤ 0 first time: +0x2E8 = 1, drop items (§8), unregister / 0x54DDB0, 0x10000006 to the attacker.
8. **slot 18 (+0x90) `void OnDamaged(obj, GDI*)`** (MovingFortress 0x4C60E0 plays its se and hit reaction here).

### 2d. How stock classes reject damage (H)
- **UfoCarrier508 slot 16 `0x4F7190`** fp `40 55 48 83 EC 50 80 B9`: false unless `[+0xD85]` (hatch open); picks the sub-hit
  nearest GDI+0x30, `bodyIdx = 0x6EA430(this+0xB60, bodyId)`, true only if == `[+0xD8C]` (core index from SGO
  `UfoCarrier_CorePartsName` via 0x6EA670 at 0x4F6189). **The cloned 508 hull therefore only takes damage while its hatch flag
  +0xD85 is set and only on the core body.**
- **MovingFortress slot 16 `0x4C6650`** fp `48 89 6C 24 20 41 56 48`: same nearest-sub-hit → 0x6EA430(this+0xB00) and
  `== [+0xE64]` (from `core_parts_name`, 0x4C1983), plus core-open [+0xE68] and attacker hostile (relation 2).
- **UfoMother511PartsBase slot 16 `0x51EFB0`**: base accept && [+0xE50] && [+0xE51] (state flags).
- **EDF6_GrandMotherParts_Weak slot 17 `0x37C7A0`**: first sub-hit point transformed into the 弱点 locator (0x6BB5A0 + inverse
  0x71810), inside the half-extents 0x6BB4A0 → true; team 3 → false.
- The EDF6 counterpart of 4.1's slot-10 filter is **slot 16** (or 17). In a cloned vtable replace slot 16; `0x54AA40` /
  `0x54AA30` are the defaults (`mov al,1; ret`).

### 2e. Ragdoll bodies (H)
- `int __fastcall RagdollBodyByName(void* ragdoll, const wchar_t* name)` = `0x6EA670` fp `48 89 5C 24 18 48 89 6C`
  (NUL-terminated name; -1 absent). Bodies at `[ragdoll+0x78]`, count `[ragdoll+0x88]`, 0xC0 each.
- `int __fastcall RagdollBodyById(void* ragdoll, u32 havokBodyId)` = `0x6EA430` fp `4C 8B 41 78 45 33 C9 48` (-1 absent).
- EAO: ragdoll = obj+0xAA0 (bodies from the SGO `ragdoll` key). That bullets' sub-hit ids match EAO's bodies is M (H for
  carrier/fortress, same component class).

```cpp
// cloned EAO vtable slot 16: only hits on body "weak" count
bool __fastcall AcceptHit(char* self,const char* gdi) {
    static thread_local int weak=-2;                     // per object in practice: cache in your record
    if(weak==-2) weak=reinterpret_cast<int(__fastcall*)(void*,const wchar_t*)>(image+0x6EA670)(self+0xAA0,L"weak");
    const char* recs=*reinterpret_cast<char* const*>(gdi+0x78);
    const size_t n=*reinterpret_cast<const size_t*>(gdi+0x88);
    const float* c=reinterpret_cast<const float*>(gdi+0x30);
    std::uint32_t body=0xFFFFFF; float best=3.4e38f;
    for(size_t i=0;i<n;++i){ const float* p=reinterpret_cast<const float*>(recs+i*0x30);
        float d=(p[0]-c[0])*(p[0]-c[0])+(p[1]-c[1])*(p[1]-c[1])+(p[2]-c[2])*(p[2]-c[2]);
        if(d<best){best=d; body=*reinterpret_cast<const std::uint32_t*>(recs+i*0x30+0x20);} }
    return weak>=0 && reinterpret_cast<int(__fastcall*)(void*,std::uint32_t)>(image+0x6EA430)(self+0xAA0,body)==weak;
}
// and clear EAO's bit 0, or nothing ever loses HP:  *(uint32_t*)(obj+0x380) &= ~1u;
```

---

## 3. Level and durability (H)

- `void __fastcall SetLevel(obj, float level)` = `0x54E740` fp `48 8B C4 48 89 58 18 55`:
  raw = level × SGO `game_object_level_adjust` (default 1) → **+0x498**; **+0x49C = 0xD7AE0(*(EDF+0x20B2890), raw)**
  (difficulty-scaled; `D7AE0`: if [gs+0x44] then raw×10, else lerp of the difficulty table × raw); writes SGO-tree
  `game_object_level = +0x49C/10` and `game_object_raw_level = +0x49C`; then `0x118A4B0(obj, &8)` sets +0x1A bit 3 (AI list) and,
  the first time, calls **slot 6 (+0x30) `bool OnLevel(obj)`** — where classes size themselves.
- +0x49C is a GameObjectBase field (yes, DragonSmall's +0x49C is the same; default 10.0 = level 1).
- Script natives: BVM 2000 `0x2154C0(ctx, point, sgo, xmm3 level, bool active)`: level ×= ctx+0x49C, CreateFromPath,
  SetTeam(1,true) 0x21589B, SetLevel 0x2158A6, active → 0x548D50. AngelScript CreateEnemy 0x1AD220 (docs/debug-spawn.md):
  SetLevel(ctx[0x280+team*4] × level).
- **Stock durability formula** — UfoCarrier508 slot 6 `0x4F91C0` → `0x4F6D00` (fp `40 53 48 83 EC 20 F3 0F`):
  `max = 0xD7720(gs, base(+0x2EC) × +0x49C / 10)`; +0x2F4 = max; +0x2F8 = max; +0x484 = max × +0x480 / base.
  `0xD7720(gs, x)` fp `48 89 5C 24 10 48 89 74` = x × difficulty-durability × online-player factor. Same shape in AlienTrailer
  0x310900 and MovingFortress 0x4BE9A0.
- **EAO slot 6 = 0x125270 (`return true`): an EAO never scales its durability by level.** A plugin EAO class must do the
  carrier's computation itself (replace slot 6, or do it on the first Update), with `s = +0x49C / 10` (= 4.1's level s).
- Parts made by CreateFromPath get no SetLevel (+0x49C = 10, +0x1A bit3 clear). To copy the hull: `SetLevel(part, hull+0x498)`
  (the raw level; passing +0x49C would apply the difficulty twice).
- Script durability % (4.1 table-1 2001 → 2000 + 3019): `0x228850(bvm, handle, pct)`: +0x2F4 ×= pct/100, +0x2F8 = min(+0x2F8×pct/100, max).

```cpp
float s=*(float*)(obj+0x49C)/10.0f;
float max=reinterpret_cast<float(__fastcall*)(void*,float)>(image+0xD7720)(*(void**)(image+0x20B2890),*(float*)(obj+0x2EC)*s);
*(float*)(obj+0x2F4)=max; *(float*)(obj+0x2F8)=max;
```

---

## 4. Sound by name (H; docs/sound-re.md §2, and these stock uses)

Globals: `SeManager* = *(EDF+0x20B2950)`, `SoundSystem* = *(EDF+0x20B2948)`. SePreset = 0x80 bytes (no destructor needed);
SoundHandle = 16 bytes `{ctrl*, inst*}`, zero-init; its release is 0x7A8730.
- One-shot by **SEPRESET name**: `bool 0x7B2A80(SeManager*, const float pos[4], const wchar_t* name, SoundHandle* outOrNull)`
  fp `40 53 55 56 57 48 81 EC` = 0x7B3200 init + 0x7B16F0 fill + 0x7B4510 play. Many callers pass out = null (0x1C1140).
- Looping, following, stoppable — **UfoCarrier508's fly hum `0x4F7DE0`** (from SGO `se_fly_name`):
  `0x7B3200(&p)` fp `48 83 EC 18 45 33 C9 C7`; `0x7B16F0(SeManager, &p, name, false)` fp `40 53 56 57 48 81 EC D0`;
  `0x7B4510(&p, pos, &handle)` fp `48 83 EC 48 48 8B 41 20` (returns at once when bank [+0x20] is null; **writes pos into
  p+0**; plays slot 0 {bank +0x20, cue +0x28} with pitch +0x70, volume +0x74). Each frame `0x7A8C20(&handle, pos)`
  (carrier 0x4F7BFD); stop `0x7A8CD0(&handle, fadeFrames)` (0x4F6323); `0x7A8A20(&handle)` = playing.
- The object's se_list: `0x7B3310(vector* out, {SgoNode*, u16 tag=1}*)` builds SePresets from SGO `se_list` (carrier ctor
  0x4F890B → +0x1600). Carrier death `0x4F9C10` plays entries [2], [3] (`[+0x1608]+0x100/+0x180`) via 0x7B4510(…, +0x90, 0);
  damage state `0x4F10D0` plays [0] with handle +0xDF8; MovingFortress OnDamaged `0x4C614D` plays its [0].
- Raw ACB cue name (not in SEPRESET), what FireSe does at 0x7B3EA0: `0x7B3200(&p)`; `0x7AEAE0(SoundSystem, &cue{Bank*,int},
  0, name)` fp `48 89 5C 24 18 55 56 57`; copy the 16-byte cue into p+0x20, +0x30, +0x40, +0x50, +0x60; set +0x74 volume,
  +0x70 pitch, +0x10 inner radius, +0x14 reference distance (default 1000), +0x18 cull (4); then 0x7B4510. With
  edf41sound.cpp's fallback, 4.1's own cue names resolve this way.
- The ACBs named in `game_sound` are loaded with the object's resources; the lookups search all loaded banks (M).

```cpp
alignas(16) unsigned char p[0x80]; struct { void* c; void* i; } h{};       // keep h per object for a loop
call<void*(void*)>(0x7B3200,p);
call<bool(void*,void*,const wchar_t*,bool)>(0x7B16F0,*(void**)(image+0x20B2950),p,L"SEPRESET名",false);
alignas(16) float pos[4]={x,y,z,1};
call<void(void*,const float*,void*)>(0x7B4510,p,pos,&h);                 // then 0x7A8C20(&h,pos) / 0x7A8CD0(&h,4)
```

---

## 5. Explosion / smoke / fire effects (H for calls/signatures, M for the visual meaning)

`EffectGenUtil* = *(EDF+0x20B2980)` (RTTI `.?AVEffectGenUtil@@`, made by the scene init 0x701396). Visual only: no damage, no
sound (none of 0x7B4510/0x7B2A80 inside). Names from the debug table `db::CreateEffectTest` (0x950CB..0x9564A), whose lambdas
call each with `pos`, `dir = (0,1,0,1)` (0x1C36A80) and a size:

| name (test lambda) | RVA, fp | args after (egu, const float pos[4], const float dir[4], float size) |
|---|---|---|
| Flame (0xB3520) | 0x2E7FF0 `48 8B C4 48 89 58 08 48` | — (火炎_01) — 4.1 fire burst |
| Flame2 (0xB3550) | 0x2E7D00 `48 8B C4 48 89 58 08 48` | bool 0 |
| Blazing (0xB3590) | 0x2E0B70 `48 89 5C 24 18 55 56 57` | bool 0, int 0 (smoke+fire) — 4.1 0x1b1ba0 |
| Explosion (0xB35D0) | 0x2E5ED0 `48 8B C4 55 53 56 57 41` | float 1.0, const float color[4] (1,1,1,1), float 1.0 — the script's `Explosion` native (0x2112CB, 0x21FA67) |
| Explosion_Big (0xB3650) | 0x2E7A30 `48 8B C4 55 53 56 57 41` | float a (1.0; carrier 2.0), bool 0 — calls ExplosionWind + Blazing + quake |
| ExplosionWind (0xB36A0) | 0x2E4D90 `48 8B C4 48 89 58 10 48` | — (煙_01) |
| ShockWave (0xB36D0) | 0x2E9B60 `48 8B C4 48 89 58 08 48` | float a, float 0.1, int 50, float 0 |
| camera quake | 0x2E9B30 `F3 0F 10 05 D0 EC 47 01` | (egu, pos, float strength, float radius, int) |

Stock: **UfoCarrier508 death `0x4F9C10`**: `0x2E7A30(egu, &(pos.y−20), &obj+0x70 (up row), 1.0, 2.0, 0)`, then an area-damage
GDI built on the stack → `0x542490(…, radius 60)`, then se [2] [3]. Carrier damage state 0x4F131E: quake `0x2E9B30(egu,
+0x90, 0.025, 100, 1)`. MovingFortress death 0x4CAF76: `0x2E7A30(egu, pos, −up, 0.72, 1.0, 0)`. UfoMother511 0x507C2E /
0x507D3D: Explosion_Big + Blazing.
Mapping for 4.1: 0x1b16b0 fire burst → 0x2E7FF0; 0x1b1ba0 smoke+fire → 0x2E0B70; 0x1b2070 directed explosion → 0x2E5ED0
(dir = the direction); 0x1b2ab0 big → 0x2E7A30 (M on look). Effects are per-peer local, like 4.1's.
**Fallback not needed**: a bullet's expiry explodes only with core flag 0x20 (GrenadeBullet01 CP#0=1, docs/carrier-laser-re.md),
and its visual is class-specific; the EffectGenUtil call is simpler and does no damage.

```cpp
void* egu=*(void**)(image+0x20B2980); if(!egu) return;            // null outside a mission
alignas(16) float p[4]={x,y,z,1}, up[4]={0,1,0,1}, white[4]={1,1,1,1};
call<void(void*,const float*,const float*,float,float,const float*,float)>(0x2E5ED0,egu,p,up,size,1.0f,white,1.0f);
call<void(void*,const float*,const float*,float,float,bool)>(0x2E7A30,egu,p,up,size,1.0f,false);
```

---

## 6. Targeting (H)

- GOB::Update `0x54BE40` (called first by EAO Update 0x5B6D20) every [+0x514]=60 frames: target = forced target +0x528 if
  alive, else **slot 14 (+0x70)**; result (dynamic_cast GameObjectBase) stored as weak ptr **+0x518/+0x520** (0x54C130).
- Default slot 14 `0x54C4B0` fp `48 83 EC 68 0F B6 81 A0`: `0x5E0F20(TeamManager *(EDF+0x20B2978), myTeam +0x314, &cb)`
  with `GameObjectBase::EnumNearObjectCallback` (vt 0x17C2948, slot 1 0x54A3B0): nearest by distance from +0x90 (no range
  limit, FLT_MAX), divided by the target's +0x47C when active, preferring an object already targeting us.
  `0x5E0F20` walks every team with relation 2 to myTeam and their registered objects, skipping `+0x2E8` dead and
  **`+0x380 & 0x80`**. So the stock target of an object = nearest live, registered, hostile object.
- `GameObject* __fastcall GetTarget(obj)` = `0x30C0F0` fp `48 89 5C 24 08 48 89 74` (locks +0x518/+0x520; null = none; the
  pointer is good for this frame). Used by MovingFortress parts (0x4D0399), UfoSmall507 (0x52A0B1), edf41ai.cpp.
- **Aim point** (MovingFortress part `0x4D0390` → its +0xF20): `t+0x378 == 0 ? t+0x90 : (*(char**)t[+0x368])+0x10` = the
  target's first lock point position, else its origin. Same rule in ApplyDamage's drop position (0x548462).

```cpp
char* t=call<char*(void*)>(0x30C0F0,obj);
if(t){ const float* aim=*(size_t*)(t+0x378) ? (const float*)(**(char***)(t+0x368)+0x10) : (const float*)(t+0x90); }
```

---

## 7. Team, lock-on, invulnerable, registration (H unless noted)

- `bool __fastcall SetTeam(obj, int team, bool reg)` = `0x54EE70` fp `48 89 5C 24 08 48 89 74`: unregisters the old team
  (0x5E1C60, clears +0x380 bit 6), writes +0x314 (and +0x318 if != -1); with `reg` and not `+0x380 & 0x400000` sets **bit 6
  (0x40) = registered** and inserts into the team's set (0x5E0B70). Does not touch children. `crew::SetObjectTeam` calls it
  with `reg` = the current bit 6 (keeps the registration state; for a never-registered object that means reg=false — use
  the raw call with true for a new enemy).
- Relations (TeamManager ctor 0x5E03C0, docs/swarm-team-re.md): 1 on the diagonal; 0↔1, 1↔2, 1↔4 = 2 (hostile), 0↔2 = 1;
  everything with 3, 5, 6 = 0. `0x5E1540(tm, a, b)` = rel[a][b]==2, false if either is -1.
- **CreateFromPath without SetTeam**: EAO → team 6, registered: not hostile to anyone, never a target (no team has rel 2 with
  6), not lockable (no lock points; lock filter 0x689E40 = rel==2), not counted by GetTeamObjectCount(ENEMY) (0x5E13E0 counts
  team 1's set), and **immune to splash** (area filter 0x543DD0 accepts only rel[target+0x318][GDI+0x24]==2 or +0x380
  0x100000); direct hits are not refused by team (rel 0) but EAO's +0x380 bit 0 removes the HP loss. UfoCarrier508 → team -1,
  unregistered: same invisibility (the -1 checks return "not enemy"). A CreateEnemy object gets SetTeam(1, true).
  So parts must get `SetTeam(part, 1, true)` to be hostile/targetable/counted/splash-damaged (and lockable if they have lock points).
- Enemy count: `0x5E13E0(tm, team, true)` (script GetTeamObjectCount 0x1B5260, EventFactor_TeamObjectCount 0x1F9318, a BVM
  native at 0x22289F — likely 9110, M) counts team-set members with `+0x2E8 == 0` and **not `+0x380 & 0x2000`**.
  Set 0x2000 on parts that must not hold up "all enemies dead".
- Invulnerable: `+0x380 |= 0x800` → slots 16/17 skipped and only forced (GDI 0x40) damage applies; `+0x380 & 1` → no HP loss
  but hit activation and slot 18 still run (EAO sets bit 0 in its ctor). Or slot 16 returning false.
- Not a target: `+0x380 |= 0x80` (EnumConflict skips it, H). Not lockable: an EAO has no lock points; a class with them
  (carrier: 0x6C74E0 → AddLockTarget 0x547A50 per MAB locator, list +0x360) — set lock point `+0x2A = 0`: the lock search
  0x22DF30 rejects it (0x22DF43) (M: no stock writer).
- Radar: assumed to follow the team registry (M/U, not traced).

---

## 8. Drop items (H)
`void __fastcall DropItems(obj, const float pos[4])` = `0x548AE0` fp `48 8B C4 48 89 58 18 48`. Called by ApplyDamage on death
(0x54849E) only when authority, `+0x590 == 0` and **`+0x380 & 0x20`** (GOB default has it), pos = first lock point or +0x90.
Itself: returns if `+0x380 & 0x10000000`; sets +0x590 = 1; reads the object's own SGO `game_object_drop_item` ([1] spread,
[2] amount) → DropItemManager (docs/itembox-re.md). A class that dies through ApplyDamage needs nothing; one that kills itself
by writing HP should call it once (check +0x590 and authority 0x7748F0(obj+0x120) first).

## 9. Ground probe (H, docs/raycast-re.md)
`CastRay 0x11A7EE0(*(EDF+0x20B2958)+0x10, hknpClosestHitCollector*, EdfRayInput{from,to,filter 0x16})` fp `40 53 56 57 48 81 EC A0`;
hit if collector+0x0C != 0, point +0x30. Already wrapped: `float MapRay(const float* a,const float* b,float* hit)` (heli.h,
metres or -1) and `MapGroundNear(x,z,y,&h)`; `ObjectRay(a,b,hit,&bodyId)` for object bodies. Stock: GetGroundPosition 0x208860.

## 10. Scale (H)
**No size argument exists.** BVM 2000 `0x2154C0` pops 4 values (point, sgo, float level, bool) (dispatcher 0x213DF8..0x213E14);
the variant 0x217610 (5th float) and 4.1's table-1 2001 use it as a **durability percentage** (0x228850 / native 3019), not a
size. GameObjectBase has no scale field; no `game_object_scale` key. The only route would be scaling the rows of +0x60 before
the stock Update (0x1100B90 builds bones from it) — visual only, ragdoll/collision unscaled (U).

---

### Notes found on the way
- `edf41classes.h kMaxDurabilityAt=0x2FC` should be 0x2F4 (0x2FC is an int set to 0).
- EAO parts never lose HP until `+0x380 &= ~1` (ctor 0x5B6406), and never scale with level (slot 6 is a stub).
- The cloned UfoCarrier508 hull only accepts damage on its core body while +0xD85 is set (slot 16 0x4F7190).
- GDI sub-hit records (+0x78/+0x88, 0x30 bytes, body id at +0x20) are how every stock part tells which body was hit.

## 12. Added for the 401's finishing pass (2026-10-15, src/edf41classes.h last block; H unless noted)
(Numbered 12 to sit next to the 301 finish's section 11 on its branch. Its `Bullet::hitSe` / `FillSePreset` fill the same InitParam
+0x100 preset from an `edf41_se` name table; the 401 data carries raw cues per se_table entry (`edf41_401_se`), so `FireHit` takes a
cue directly. Its material *parameter* binding (0x6DCBF0 ..) writes values by name; the 401's `material_animation` MTAB tracks go
through EDF6's material *animation* component below, the one UfoCarrier508 uses for its own `material_animation`.)
- `FireHit(owner, matrix, Bullet, cue, SeParams)`: Fire with the bullet's hit sound. EDF6's InitParam +0x100 is a SePreset (0x7B3200;
  weapons fill it from key 14 with 0x7B16F0; 4.1's +0xB0); filled here from a raw cue name as PlayCue does.
- `MaterialAnims*`: EDF6's material-animation component (0x38 bytes; ctor 0x6DBE90, bind 0x6DC7B0 to the model's material set =
  EventAnimationObject +0x720 (model +0x5C0 + 0x160; UfoCarrier508 binds +0x650 + 0x160), add 0x6DC050(blob, kind), play
  0x6DC830(index, mode 0 once / 1 hold / 2 loop), advance 0x6DC8B0(frame info, 1.0), free 0x29D120(+8)). 4.1's SGO entries are bare
  MTAB blobs (the same 0x200 format and 16-byte header as EDF6's [blob, kind] entries).
- `StencilLight(pos, colour, radius, frames)`: InitParam@StencilLight (vtable 0x1762108; +0x30 pos, +0x40/+0x50 colour, +0x60 radius,
  +0x64 frames, the same layout as 4.1's) made by 0x84880 with the numbers as given (EffectGenUtil 0x2EBC00 and 4.1's 0x1b5080 both scale
  colour x2.5 and radius x5 first).
- `SmallBurst(pos, dir, size)`: EffectGenUtil 0x2E7D00 = 4.1 0x1b1910 (every constant the same); FireBurst's Flame 0x2E7FF0 = 4.1 0x1b16b0.
- Ragdoll body names: 0x6EA670 compares the SHKT proxy name `RagDollProxys.<name>`, not the bare name.
- `Ragdoll::Disable` 0x6E97C0 (+0xA4 cleared, mode 0 -> 0x11B1710 removes the body from the world); `Enable` 0x6E9840; the relayed
  0x6E40B0 sets every body to mode 4 under the world lock.
- EDF6's SpawnerObject (vtable 0x17E7658, Update 0x6E1080 -> 0x6E3250) draws nothing of its own; generators hand it std::functions
  (+0x640 effect, +0x680 the new enemy). It delivers its messages to the enemy as slots 10, 9 (OnMessage), 11.
