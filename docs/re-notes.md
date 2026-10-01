# EDF.dll RE notes (TimeDateStamp 0x678CCB46, SizeOfImage 0x22CE000)

[C] = seen in disassembly, [I] = inferred, needs cdb.

## Spawning a bullet by class name
- `System*` global: `[EDF.dll+0x20B2958]` (non-null only in a mission) [C]
- `Factory* GetSceneObjectFactory(System*, const wchar_t*)` = `+0x1195C50` (FNV-1a map at sys+0x2D0) [C]
- `SceneObject* Spawn(System*, const float* mtx16 /*16B aligned*/, Factory*, InitParam*)` = `+0x1194280` [C]
- `BulletBase::InitParam` (0x1E0 bytes): ctor `+0x1001D0(ip)`, dtor `+0x101510(ip, flags)` [C]
  - +0x30 damage mul (1.0) · +0x38/+0x40 owner weak_ptr (from owner obj +0x28/+0x30) · +0x70 follow-frame mtx ptr
  - +0x90 team (-1 default; weapon+0x214) · +0x94 speed · +0x98 alive frames (int) · +0x9C damage
  - +0xB0 explosion · +0xB4 penetration · +0xC0 size · +0xC4 hit size adjust · +0xC8 impulse adjust
  - +0xD0 color float4 · +0xE0 gravity · +0xE8 custom param variant · +0xF8 variant tag (0xFFFF empty) · +0x1A0 seed
- ShockWaveBullet01Exp factory vtable `+0x17A28A0`, Create `+0x274980`; ctor `+0x2750C0` REQUIRES custom param
  (cp[0], cp[1]); empty variant → bad_variant_access crash. Exp radius = size*HitSizeAdjust*1.25 [C]
- Custom param tag 1 = raw SGO node ptr, node = 12 bytes {type,a,b}; type0 array (children at node+b+i*12), 1 int, 2 float [C]

## Hammer charged knock path [C]
charge `+0x6A0460` → knock `+0x6A0790` → anim event L"発射" `+0x69FF00` → `+0x690BB0` → vtbl+0x80 `+0x69FDB0`
→ `+0x690CC0` → fire `+0x696FD0` (copies IP weapon+0x800 → +0x9E0, applies dmg/alive/size multipliers ×1.5/×3/×1)
→ Spawn call site `+0x69799F`.
HIMPACTHAMMER01: Damage 67.5 (star-scaled by `+0xD7CE0`), Alive 20 (charged 60), Size 2.0, HitSizeAdjust 1.75,
Speed 2.0, Penetration 1, Color [1.3,1.5,1,1], Ammo_CustomParameter [1.0, 30.0].

## Open questions (cdb)
team value, owner = vehicle vs pilot, ip+0x70 nullable?, fake cp read-back, real damage at +0x69799F, thread safety.

## Explosion bullets (v0.2.1)

- Bullet velocity = matrix row 2 (forward) * InitParam speed (BulletBase init, EDF+0x231DC7).
- Every bullet ctor reads Ammo_CustomParameter through a variant whose tag lives at
  InitParam+0xF8. Tag 0xFFFF (absent) throws bad_variant_access (SolidExpBullet01 ctor ->
  0x28E405/0x28E483 -> 0x12DA768). Weapons always pass a node; most pass an empty array.
  The plugin therefore always publishes an array node, with zero children when the ini is empty.
- GrenadeBullet01(+_MapNoDamage), BombBullet01/02 and MissileBullet01 copy AmmoModel handles
  from InitParam+0x180/+0x190 and dereference them every frame (Grenade update 0x2648F0 ->
  0x1100B90 with rcx=[bullet+0x1170]=null => AV). The plugin has no model, so these are refused.
- SolidExpBullet01 (AWEAPON001 etc., AmmoModel 0, Ammo_CustomParameter []) has no model reads
  and detonates on contact with radius AmmoExplosion. RocketBullet01 and ShockWaveBullet01Exp
  also have no model reads.

## Instant detonation and death blast (v0.3.0)

- The v0.2.1 delay: SolidExpBullet01 only bursts on contact; with alive>1 and speed it travelled
  before hitting anything. Expiry path EDF+0x236385/0x236899 (age [+0xAF8] vs alive [+0xA08])
  tests BulletBase flag 0x20 (`test cl,20` at 0x2363B0): set -> explode in place via
  0x542670/0x542490, clear -> vanish. Flags live at bullet+0xC34 (BulletBase subobject +0x140,
  field +0xAF4). Only grenades/Volcanicrock set 0x20. v0.3.0 spawns speed 0, alive 1 and ORs 0x20
  after Spawn (vtable check 0x17A4498) -> detonates on its first update.
- Death: Balam vtable 0x17D98C8 slot 9 = 0x60ACC0 (message handler, bool) -> VehicleBase 0x62ECB0
  -> GameObjectBase 0x54A530, which calls damage 0x547C30 and sets dead byte +0x2E8
  (`movzx ebx,[rdi+2E8]` at 0x54A579). The plugin swaps slot 9 and fires once on 0 -> 1.
  Position +0x90, object id +0x314. Only fires where the damage message is processed locally.
