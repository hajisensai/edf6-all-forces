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

## 11. Added by the UfoMother301 finish (2026-10-15; src/edf41mother.cpp, real runs in docs/edf41-ufomother301.md「补完」)

- **Material parameters by name** (4.1 0x32FEF0 + per-frame writes; EDF6 the same machinery, H, real-run used by the 301):
  binding object 0x60 bytes, ctor `0x6DCBF0` fp `33 C0 48 89 41 08 48 89`, dtor `0x3799B0` fp `40 53 48 83 EC 20 48 8B`;
  `0x6DCC80(binding, model component + 0x160, const wchar_t* material or null = every material that has it, const wchar_t*
  parameter)` fp `40 55 53 56 57 41 54 41`; `+8` {material, index} pairs (data / capacity / count +0x18), `+0x28` defaults (count
  +0x38) copied from the first material, `+0x48` values; `0x6DD2F0(binding, const float v[4])` sets every value; `0x6DD420(binding)`
  writes them into the materials (0x6DCB30, dirty flag) fp `48 89 5C 24 08 48 89 74`. UfoSmall507 binds in its ctor and applies in
  **slot 3** (draw, 0x52EEC0) right before the model draw: do the same (EAO slot 3 0x5B6D10 = model draw; carrier 0x4F9530).
  Model component + 0x160: EAO +0x720, UfoCarrier508 +0x7B0 (the carrier's own material animation 0x6DC7B0 uses it).
- **UfoCarrier508 CAS**: players at +0x980 (array `*(+0x9E0)`, 0x160 each), main player index `[+0xB30]`; SetState
  `0x116C2D0(+0x980, [+0xB30], name, 0)`.
- **UfoCarrier508 slot 18** `0x4F9250` = OnDamaged(obj, GDI) (ApplyDamage calls it for a hit taken): alive, se_list[0] + material
  blink (0x4F8AF0) + the ダメージエフェクト flames (+0x1590, 0x4F8B80); dead (+0x2E8) its death state machine on +0x15A0 (0x4F9800
  explosion / hum stop / fall → 0x4F9A00 ground probe → 0x4F9C10 big explosion, area damage radius 60, se[2] [3], delete). The
  other entry: slot 9 message **0x1000000F** (0x4F94C0: HP = min(0, max), 0x54DDB0 unregister, same death). Its fly hum
  (se_fly_name, 0x4F7DE0) plays into the handle at **+0xE10**.
- **Script native 3020** (`0x2288C0`): `+0x2F0` (minimum durability) = pct / 100 × max; ApplyDamage clamps HP to it. M130 gives the
  301 100 %, M993 30 %: a mission's way of keeping a boss alive until a later phase.
- **Ragdoll body names** of a 4.1 SHKT are the rigid bodies' full names (`RagDollProxys.weak`), not the bone names (0x6EA670).
- **A bullet's hit sound**: InitParam +0x100 is a SePreset (0x1001D0 inits it with 0x7B3200; weapon key 14 fills it by SEPRESET
  name, 0x6CEF81 → 0x7B16F0). `Bullet::hitSe` / `FillSePreset` fill it from an `edf41_se` table the way PlayCue fills its preset.

---

## 联机（`src/edf41net.h` / `.cpp`，2026-10-15）

EDF.dll `0x678CCB46`，RVA。H = 指令里直接读到；M = 有代码证据的推断；L = 推测。来源：本仓 `docs/online-re.md`，EDF6Coop 的
`docs/net-re/world.md` / `damage.md`（W3 / W4 的静态逆向），本次补读的地址见下。
**没有在两台机器上联机实测过**：本机只有一个 Steam 账号、一台机器，开不出有两端的房间；见本节最后「D. 验证到了哪里」。

### A. 原版怎样跑脚本生成的敌人（H 除非注明）

- **脚本在每台机器上都跑，每台都 `CreateObject` 出自己那份。** 4.1 关卡用的建对象原生函数 CreateEnemy 2000 / 2001（`0x2154C0`，2001 经
  `0x217610` 调它）、`0x7D1` → `0x215120`、CreateNeutral 1020（`0x21B590`）都在 `CreateFromPath` 之后调 `0x7834F0`：给对象一个由**脚本字节码位置**
  推出的网络 ID，`remoteOwned = IsGuest`（`0x784120`）——房主上 `+0x128` 置 bit1（本机的），客人上置 bit0（别人的）。所以 301 船体、401 船体、
  UfoRobo 在每台机器上都由我们的工厂造出，并且都是**房主拥有的注册对象**。
- **每台机器都跑对象自己的 Update（AI 也跑），拥有者校正**：位姿由该类 NetworkObject 的 slot 5 / 6（默认转 7 / 8）写 / 读；周期发送器 `0x75D2E0`
  对每个注册对象调 slot 4（要不要发）/ slot 5（写进 0x200 字节的流，`0x75DA9B`），接收端按 ID 找对象调 slot 6。
  - EventAnimationObject 的 NetworkObject 虚表 `0x17D4E90`：slot 7 = `0x125A80`（返回 0，不写），slot 8 = `0x125190`（空）——**EAO 没有任何周期状态复制**。
  - UfoCarrier508 的 `0x17C9F50`：slot 7 `0x4F9700` → `0x4F7F70`：只在本机拥有（`NetObj+8 & 1 == 0`）时写掩码，bit1 每 60 帧位置（`obj+0x90`），
    bit2 每 120 帧 `+0x604`，bit4 每 40 帧（`+0x1A & 8` 时）`0x54E440`；再写 CAS 主播放器时间。读 `0x4F8E00` → `0x4F6600`（只在远端且没死时）。
    301 船体沿用它（船体的路线移动本来就是 508 的）。
- **目标**：GameObjectBase::Update `0x54BE40` 每 60 帧重选最近的敌对对象（**远端副本也重选**）；拥有者在 `+0x591` 置位时另发消息 1（目标的网络 ID）。
  `+0x591` 只有部分类置 1（UfoMother511 的部件 `0x51DE0A` 置 0）。EDF6Coop 的 W4（`WorldAuthority`）让远端副本保留拥有者的目标。
- **耐久**：伤害由**开枪方的机器**判（GameObjectBase slot 34 `0x54F8D0`：攻击者是远端就只收网络来的），HP 变化经 slot 11 累计、slot 40 `0x54CBE0`
  以对象事件类型 2（`int 差, int 纪元`）广播；接收 `0x54D770` 类型 2：纪元相同、`+0x2E8 == 0` 时 `0x54FCF0`（夹到 [min, max] 写 HP；**它还会
  `SetTeam(+0x318, true)` 重新登记队伍、清 `+0x2E8`**）。击杀消息由 `0x7746C0` 排队、各机按强制标志 0x40 重放（M，Coop damage.md §3）。
- **敌人开火**：每台机器上的副本都自己开火（AI 不停，Coop world.md §3 / §7）；打到玩家的伤害在**被打玩家自己的机器**上判（SoldierBase slot 34
  `0x5A3600`：玩家是远端时只收网络来的）。所以原版的「敌弹」是每台机器各自生成、各自只伤本机的玩家。
- **Boss 放出的子对象**：`0x781630(mgr [0x20B2AC8], weak 子*, weak 父*, int index, int sub, byte remoteOwned)`，ID = `u16 index, u16 sub, u32 父身份哈希`
  （`0x7766B0`，类型 4）。MovingFortress 的部件 `0x4C79C0`：会话中、父活着时，父是远端就先 `0x54EDB0(子)`，`index = 0`、`sub = 父的计数 +0x1D4C`，
  `remoteOwned = 父 +0x128 & 1`，然后计数加一。**两个 weak 参数由被调者释放**（`0x781878` / `0x781894` 各减一个弱引用），调用者传的是各自多持
  一个弱引用的副本。每台机器各自造、靠可推导的 ID 绑成同一个网络对象，没有生成包。
- **对象事件通道**：NetworkObject slot 16（`+0x80`，`0x773DA0`）发（没有注册描述符时只进本地队列），slot 17 收；GameObjectBase 的 `0x54D770`
  读一个小整数（`0x12B4BB0` → `0x12B4660`）作标签，只认 0..3，其余读完标签就返回。流对象 0x5F8 字节：`+8` 读位置，`+0x10` 数据，`+0x5F0` 大小；
  写：`0x79A460` 构造、`0x12B5790` 小整数（-14..15 一个字节）、`0x12B5200` 块（≤ 8191，无越界检查，缓冲 0x5E0）；读：`0x12B4900` 块（≤ 0x800，读进
  另一个流）、`0x12B48A0` / `0x12B4FD0` 记 / 放回读位置；`0x12B4530` 由字节造读流；`0x760180` 析构只改虚表。本仓的钻头 / Proteus / 沙扎比用标签 15
  （载具的 slot 17），Coop 用 13（伤害）/ 14（随机数）。

### B. 改动之前客人那边是什么样（静态）

| 类 | 客人上 | 结果 |
|---|---|---|
| UfoRobo | 脚本造、注册（远端）；工厂、Update 全跑；随机数种子 = 地址 ^ 时钟 | 每台机器各自的 AI：形态、移动、攻击、变形时刻都不同；HP 原版同步 |
| 401 | 船体、部件的逻辑只在 `OnlineRunsHere` 处跑：客人上船体什么也不排、不造部件 | **客人上脚本的 ObjectAction 落到 EventAnimationObject 的 OnMessage `0x5B6540`**：船体 SGO 没有 `action` 键（核过 `UFOMOTHER401*.SGO`：只有 `action_list`），UfoRobo 在 M411 上走同一函数、同一消息时抛过异常（`EDF+0x5B6A01`，实机 2026-10-15）——**客人第一次 ObjectAction 就会崩**（M：同函数同条件，没在客人上跑过）；GetPartsObject 返回空 |
| 301 | 工厂里造全部部件（每台机器一样）；部件没有网络身份 | 每台机器各打各的部件：客人打掉的部件房主那边还在，反之亦然；舱门放的敌人只在放的那台机器上有 |

### C. 改成什么样

一条原则：**房主说了算，尽量走原版的路。**

1. **子对象身份（原版的路）**：`edf41net::Child(父, 子, index, sub)` 照 `0x4C79C0` 调 `0x781630`。301 的部件在工厂里造（这时船体还没被脚本
   登记），所以先排队，船体之后每帧 `Pump` 时一有身份就补上；401 的部件、炮台和两类生成器放的敌人在造出时登记（父 = 船体）。

   | 子 | index | sub |
   |---|---|---|
   | 301 部件 | `kChildPart` 1 | 在 `h.parts` 里的创建序号 |
   | 301 舱门放的敌人 | `kChildSpawn` 0x100 + 舱门序号 | 该舱门的计数 `made` |
   | 401 面板 | 1 | 该船体所有布局里的面板序号（每台机器同样的 ObjectAction 序列 → 同样的序号） |
   | 401 炮台 | `kChildBattery` 0x4000 + 挂点序号 | 所在面板的序号 |
   | 401 生成器放的敌人 | `kChildSpawn401` 0x8000 \| 面板序号 | 该生成器的计数 |

   登记后部件的耐久 / 受伤 / 击毁 / 掉落都按原版：开枪方的机器判、HP 差广播、击毁重放；会瞄准的部件（301 的中型炮与板甲、401 的
   FinalCannon01 与炮台）置 `+0x591`，房主发它们的目标。同一父下同一 (index, sub) 只给一次（第二次记日志、不登记）。离线什么也不做。
2. **房主的决定（事件）**：在我们自己的 NetworkObject 虚表副本上（`Attach`：对象 `+0x120` 的 vptr 换成 EAO / 508 表的副本，22 槽照抄活表，
   slot 17 换成我们的接收）。标签 12 + 24 字节头（`"E41N"`、版本 1、种类、长度、发送进程的随机 ID、序号）+ ≤ 192 字节。接收：记读位置，读到 12
   就读块、校验、按 (对象, 控制块) 记发送方水位（拒重复 / 旧包），只给**客人副本**排队，放回读位置；别的标签原样交给活表原来的 slot 17。
   类在自己的 Update 里 `Drain`。
   - UfoRobo（`kNetBody` / `kNetGun` / `kNetState`）：房主每次身体状态变化（齐射、变形、每一步、转身、攻击选择、死亡）发状态号、片段、链、
     死亡帧数、形态、步子起点；机器人形态膝炮 / 肩炮自己的计时器开火时发一条；每 15 帧发位姿、速度、目标点（88 字节，约 350 B/s）。客人副本
     **不跑 AI、不跑炮的计时器**，进入房主给的状态（片段、MAB 事件、音效、范围伤害、炮的 G1 → G2 → G0 在本机照跑，瞄房主的目标点），位姿每帧拉向
     房主的（飞船 8 %/帧并用房主的速度外推，机器人在两步之间 3 %/帧，偏差过大直接对齐）。
   - 401（`kPartEnter`）：三处随机的攻击起点——BigCannon01 开盖、FinalCannon01 选哪种炮、炮台开火——只在房主上决定，发到该部件自己的
     NetworkObject；客人副本在对应的待机状态等它（600 帧没等到就作废）。其余（布局、飞入、生成器、死亡编排）每台机器同样跑。
   - 301（`kCannonFire`）：中型炮 1800–3599 帧的等待由房主决定（它的随机数和束射的散布共用，束射在没目标时不掷骰子，两台机器会走散）。
     其余（主炮、板甲、舱门、船体自转）由脚本动作驱动，每台机器一样。
3. **子弹**：每台机器的副本自己开火（原版敌人就是这样），**何时**开火由上面的事件决定；打玩家只在玩家自己的机器上算一次（`0x5A3600`）。
4. **击毁**：部件的 HP 只经 HP 差到达的那台机器（开枪的人在别的机器上），401 在 `NetPartIn` 里补跑 `PartDamaged` 的击毁（炮台进摧毁态；
   弱点面板爆炸 + 200 m 连锁；核心大爆炸；`Destroy`）；301 的 `PartFrame` 本来就按 HP ≤ 0 判死；UfoRobo 的 `Tick` 本来就按耐久判死。
5. **401 在每台机器上跑**：去掉三处 `OnlineRunsHere` 门（修掉 B 表里那个崩溃），布局以船体位置为种子，每台机器一样。
6. **开关**：`Edf41Net=1`（发布开）；0 = 不挂、不发、不登记，回到各算各的（401 仍在每台机器上跑）。房间里每人要同一版本、同一设置
   （`Edf41MotherParts` 不同 → 部件序号不同 → 身份错配）。开发开关 `Edf41NetMirror`（只在单机生效，发布 0）见 D。

### D. 验证到了哪里

- `tests/edf41net_test.cpp`（ctest `edf41net`，36 项）：头的校验、水位（重复 / 旧包 / 回绕 / 第 9 个发送方）、浮点守卫、ID 区间、标签不撞。
- `tests/edf41net_native_test.cpp`（`edf41net_native_test <EDF.dll>`，84 项，照 `drill_net_native_test` 的做法：不执行 DllMain 地映射真 EDF.dll）：
  两张 NetworkObject 表的 slot 16 / 17 是 `0x773DA0` / `0x54D770`；生产代码的签名、表副本（RTTI 与其余 21 槽照抄）；**用游戏自己的写流 /
  读流函数**从房主副本发到客人副本、排队、`Drain` 原样取出；重复包拒收；拥有者副本不收；13 / 14 / 15 原样交给**原版 `0x54D770`**（读一个值、
  对象一字节不变）；原版 `0x54D770` 读到 12 也一字节不变（没装 / 关了本层的机器不受影响）；别人的标签 12（魔数不对）拒收、读位置复原；离线
  开发回环；没有网络管理器时子对象登记在调用前就放弃、弱引用计数不变；等待中的子对象死了就放掉它的弱引用。发送端的 `0x773DA0`（要真房间）
  由一个把写好的字节交给读流的替身代替。
- 实机第一次（`gametest/net1_M007`）本层自己关掉了：游戏里装着 EDF6Coop，它把 `0x54D770` 的第一条指令换成了跳转（W3 / W4 的钩子），
  指纹对不上。`0x54D770` 本层只「往下调」不读，所以去掉了它的指纹（不同就记一行），之后的实机都开着；这也说明 Coop 的钩子与本层的 slot 17
  包装是串起来工作的（我们先看标签，不是 12 就交给活表里的 `0x54D770`，即 Coop 的钩子）。
- 实机（单机，排队跑 `run_m126.py`）：见各类文档的实机表——UfoRobo 的「镜像」（`Edf41NetMirror=1`：在原机器人右边 120 米造第二台，它是客人
  副本，原机器人的事件经**游戏自己的写流 / 读流**进它的 slot 17），以及 301 / 401 / UfoRobo 各关默认设置下不回归。
- **没有验证、也无法在这里验证**：两台机器的房间——事件真的经 `0x773DA0` → 网络 → 对方的 slot 17；子对象 ID 在两边一致地绑定；HP 差 / 击毁重放
  对我们的部件生效；客人看到的位姿延迟与修正手感。前两条走的是原版自己用的路（GameObjectBase 的 HP 差就是 slot 16 → slot 17；MovingFortress 的
  部件就是这样登记的），可信度 M。EDF6Coop 的 `tests/gamenet`（GameNet_*）只替身任务开始同步，没有任务对象、没有事件分发器，跑不到这些类。
- 已知会不一致的：子弹散布（各机随机数）；没装 Coop W4 时目标每 60 帧被本机重选（原版敌人也这样）；Coop W3 转发的命中事件不带 GDI 的
  子命中记录（`+0x78/+0x88`），**按刚体判弱点的类在拥有者上会拒收转发来的伤害**——我们的 301 船体 / 401 面板，以及原版 UfoCarrier508、
  MovingFortress 的核心都是这样。**已在 EDF6Coop 那边修（分支 `fix/forward-hit-body`，hajisensai/edf-coop-stable，提交 `2d49bbd`，未合并）**：
  转发事件升到版本 3（网络协议号 2 → 3，新旧构建混房时整房间退回原版规则），带上最多 4 条子命中，每条是「刚体在目标里的位置」
  （ragdoll 组件在对象里的偏移 + 刚体序号 + 点；Havok id 是各机自己的，不能直接发），拥有者用本机这份对象的刚体 id 重建 GDI 的子命中表。
  验证：`HitAuthTests <EDF.dll>` 里用真 `0x6EA430` 和**原版 UfoCarrier508 的弱点过滤 `0x4F7190`**——带子命中 → 收，核心序号不对 → 拒，
  去掉子命中（旧版本的样子）→ 拒；Coop 全部 ctest 91/91（含 GameNet_*）。我们的类用的是同一个 `HitBody` 读法（最近子命中 → `0x6EA430`），
  合并后客人打 301 船体弱点 / 401 弱点面板与核心也能在拥有者上生效。没在两台机器上实测。
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
- `AtGameShutdown(fn)` (edf41classes.h): up to 8 callbacks run right before the game's application destructor 0x7021F0 destroys its singletons
  (redirects the call at 0x70293F, fingerprint checked). The plugin's own log is written by a thread that does not outlive this: write a
  marker file (%TEMP%) for anything you need to see. Give back counted references into game caches here (see the exit crash below). The exit leak reporter 0x1750870 (atexit; logs `SoundBank::Release error ref count` per
  bank with a non-zero count, then runs the sound bank cache destructor 0x7A9850) can be redirected at its log call 0x17508B5 -> 0x3E4A0.
- Emitter effects (`ChargeEffect_StdParticle`, and the 301's vent emitters): queue the update job from Update, the render job (vt[0]) from the
  sync-render phase (clone slot 3, EAO 0x5B6D10). Queued together from Update they run concurrently and the render job throws `bad_alloc`
  (EDF+0x30316D). After each job set `[fx+8]=[fx+0xC]`, `[fx+0x10]=0` (MovingFortress_Battery 0x4D1B96 does).
- **The exit crash (EDF+0x7ADCFB), solved 2026-10-17 (branch fix/edf41-exit-crash): it was edf41sound's weapon bank, not the emitters.**
  - The static bank cache EDF+0x20B38A0 is a `std::map<wstring, entry>` of 0xA0-byte nodes: key at +0x20, the bank at +0x48 (its byte +8 =
    "registered with the sound system", set by 0x7AA2BF), the reference count at +0x98. The cached loader 0x7AA440 hands out
    `BankRef{node, cache}`; 0x7AAC80 gives one back (locks cache+0x28, counts down, erases the node at 0 through 0x7AB070 -> 0x7A99D0 ->
    0x7AABC0, which unregisters the bank from `*(EDF+0x20B2948)` with 0x7ADCE0 / 0x7AF300).
  - Every stock owner gives its references back inside the application destructor 0x7021F0, before that destructor deletes the sound system
    (vtable call 0x70248C) and nulls 0x20B2948 (0x702493). Measured with an exit marker (diagnostic build, M019): 12–19 nodes, all count 1 and
    registered, at its entry; 0 at the atexit. edf41sound kept its reference "for the process", so its node stayed: the atexit 0x1750870 reports
    it (`SoundBank::Release error ref count 1 APP:\SOUND\PC\EDF41_WEAPON_SE.ACB`), then the cache destructor 0x7A9850 -> 0x7A9670 -> 0x7A99D0 ->
    0x7AABC0 finds it still registered and calls 0x7ADCE0 with the null sound system: `mov rdi,[rcx+40h]`, rcx 0, at 0x7ADCFB; EDF6Coop's
    vectored handler turns that into FAST_FAIL_FATAL_APP_EXIT. The dumps agree: rbx = node+0x48 and the logged weapon bank pointer is that node
    (M019 h1: bank 0x294D598D460, dump rbx 0x294D598D4A8), rsi = EDF+0x20B38A0.
  - Why it looked tied to the emitters: the process often never gets to that atexit. An earlier EDF.dll atexit destructor (the object at
    0x111822C: 0x113CBE0 -> 0x175280 -> 0x175690 releases an array of D3D11 objects) can make d3d11 / dxcore end the process with
    NtTerminateProcess(-1, 0xC000004B STATUS_THREAD_IS_TERMINATING) (stack captured by a diagnostic NtTerminateProcess hook on a plain M019);
    that exit is silent and clean (no Application-log event 1000 either). With emitters alive the exit went on to the bank cache and crashed
    (Application log: event 1000, EDF6Coop.dll +0x217EED, 0xC0000409). Which way an exit goes presumably depends on the D3D objects that static
    still holds (inferred, not traced further), not on anything the emitters do with sound; the bank reference was the bug either way.
  - Proof runs (diagnostic build, `jobs/14618f05/tmp/gametest/xc_d*`, markers in %TEMP%\edf41exit.txt): bank kept + glows (`xc_d2c`): the
    cache destructor reached with 1 node (ours) -> dump at 0x7ADCFB; bank given back + glows (`xc_d1b`): reached with 0 nodes, returned, clean;
    bank kept, no glows (`xc_d3d` / `xc_d3e`): ends in the d3d11 NtTerminateProcess before reaching it.
  - Fix: `ReleaseWeaponBank` (edf41sound.cpp) gives the reference back with 0x7AAC80 (fingerprinted) from `AtGameShutdown`, before the sound
    system goes. Rule for every class: a counted reference into a game cache must be given back there too.
  - With that, the 401's charge glows and the 301's vent emitters are on by default (dev bits 4194304 / Edf41MotherForce 2048 turn them off);
    the vent emitters are deleted again (graveyard, then their deleting destructor 0x301460; the rest at `AtGameShutdown`).
