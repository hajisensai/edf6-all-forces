# EDF6 water detection - static RE notes (EDF.dll TimeDateStamp 0x678CCB46)

H = read from code, M = strong inference, L = guess. Static only, game never run.

## Chain (H)
1. Map loader 0x121470: for each entry in map+0x160 (count map+0x170, stride 0x30; +0x10 quat, +0x20 pos, +0 shape ref)
   calls 0x5A9B20(MoveAreaManager*, transform, &shape)  [0x122987].
2. 0x5A9B20 builds a trigger-body wrapper (alloc 0x78, ctor 0x100350, vtable 0x179F218), creates the body
   (0x11B88D0), sets filter = layer 25 (0x106140 -> 0xD929F0(0x19)) via 0x11B1840(bodyHandle, filter, 1),
   0x11B0940(bodyHandle), then push_back node{+0x10 area, +0x18 refcnt} into std::list at mgr+0x30 (size mgr+0x38).
3. MoveAreaManager (RTTI .?AVMoveAreaManager@@, vtable 0x17D3970). Singleton: mgr = *(u64*)(image+0x20B2998) - 8
   (0x11BD90 / 0x1229C8). Slot 4 = 0x5AA140 per-frame water update.
4. 0x5AA140: for each area: for each overlapping body id (area+0x08 = u32*, area+0x10 = count):
   obj = 0x108260(id) dyncast GameObjectBase; compute obj AABB; vertical ray at obj.x/z from
   aabbTop+0.1 to aabbBottom-0.1, cast ONLY against the water body:
     0x11A7480(g+0x10, hknpClosestHitCollector*, EdfRayInput*{from,to,filter=0}, const u32* bodyIds, int count=1)
     bodyId = *(u32*)(*(area+0x58) + 0xF0)
   hit -> send message 0x10000025 with payload float = collector+0x34 (hit y = water surface y).
5. Receivers: HumanBase OnMessage 0x572810 (0x5728FE): human+0x118C = surfaceY, human+0x1187 = 1.
   0x572DF0: human+0x1188 = +0x1187 (in water this frame); splash/sound 0x7B4510 on entry when falling;
   if surfaceY - human.y(+0x94) > 1.5 (const 0x1769100) -> swim state (HumanBase slot 73 0x57B470, writes +0x5D0 = 0xC).
   Also heli 0x652E70, car 0x673850, monster 0x3A4940 react to 0x10000025.

## Layers
Layer 25 collides with {1,2,3,6,7} only; layer 22 (map ray) does not -> explains seabed hit on M082.

## Other
- 0x1082E0 returns low byte of hit shape userData (debug print 0x102979 "userData=0x%x").
  SoundPreset 0x7B3840 maps surface sound names soil/grass/metal/water to slots +0x30..+0x60 (water = 4th, M).
  Whether the seabed shape carries a "water" userData is unknown (L).
- No water plane / WaterHeight / sea string in EDF.dll (ASCII + UTF-16 searched).
