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
