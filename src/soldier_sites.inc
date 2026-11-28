// The EDF.dll code the new classes' player object (src/soldier_factory.cpp) and the per-type table patches
// (src/soldier_type_patches.cpp) stand on, as the file 0x678CCB46 has it (docs/newclass-re/inject.md, spawn.md). One list for
// the plugin's own checks (src/soldier_type_logic.h decodes it) and tests/soldier_native_audit.py (reads it as text).
//   SOLDIER_PRINT(rva, "bytes", what): the code there, checked before anything is called or hooked.
//   SOLDIER_PATCH(rva, "from", "to", what): the bytes written over it (the same length), all of them or none.
// Byte strings: lowercase hex, two digits a byte, spaces between instructions.

// ---- the player object: AssultSoldier's create / constructor / destructor (soldier_factory.cpp) ----
SOLDIER_PRINT(0x54FE50,"4053 4883ec20 488bda b990200000 e84d87d800","AssultSoldier create: new(0x2090) via 0x12D85B0")
SOLDIER_PRINT(0x54FE6D,"488bd3 488bc8 e898000000","AssultSoldier create: ctor 0x54FF10(obj, createInfo)")
SOLDIER_PRINT(0x54FF10,"48895c2418 55 56 57 4883ec60","AssultSoldier ctor 0x54FF10 head")
SOLDIER_PRINT(0x54FF39,"488d05e8df2701 488903 488d05d6e22701 488983 20010000 488d0580e32701 488983 40190000",
              "AssultSoldier ctor: vptr 0x17CDF28, +0x120 = 0x17CE220, +0x1940 = 0x17CE2D8")
SOLDIER_PRINT(0x54FF5F,"33ed","AssultSoldier ctor: ebp = 0 (written to +0xD40 below)")
SOLDIER_PRINT(0x55004D,"89ab400d0000","AssultSoldier ctor: +0xD40 (the key config set, 0x56FC85 / 0x570690) = 0")
SOLDIER_PRINT(0x5505B0,"4057 4883ec20 488d056bd92701","AssultSoldier deleting dtor 0x5505B0 head (flags kept in ebp)")
SOLDIER_PRINT(0x550654,"40f6c501 488b6c2438 740d ba90200000 488bcf e8807fd800 488bc7",
              "AssultSoldier deleting dtor tail: `test bpl,1` then operator delete 0x12D85EC(this, 0x2090)")
SOLDIER_PRINT(0x550414,"4881e920010000 e990010000","+0x120 table slot 0: sub rcx,0x120; jmp 0x5505B0")
SOLDIER_PRINT(0x550420,"4881e940190000 e984010000","+0x1940 table slot 0: sub rcx,0x1940; jmp 0x5505B0")
SOLDIER_PRINT(0x550A20,"33c0c3","AssultSoldier slot 83 (+0x298, the soldier type): return 0")
SOLDIER_PRINT(0x12D85EC,"e9b70b0000","operator delete 0x12D85EC: jmp 0x12D91A8 (free)")

// ---- the HUD nameplate table 0x806FE0 (HUD+0xB0: data +8, capacity +0x10, size +0x18; 0x30 bytes an entry) ----
SOLDIER_PRINT(0x804875,"e866270000","the HUD constructor's call of the nameplate fill 0x806FE0 (rcx = HUD+0xB0)")
SOLDIER_PRINT(0x806FE0,"488bc4 48895810 48897018 48897820","nameplate fill 0x806FE0 head")
SOLDIER_PRINT(0x8073A4,"ffc6 4983c630 83fe04 7327","nameplate fill: the loop ends after entry 3 (entries 4 and 5 left as resized)")
SOLDIER_PRINT(0x3D450,"48895c2408 48896c2410","std::wstring assign 0x3D450(str, text, count)")
SOLDIER_PATCH(0x8071EC,"48837f1004","48837f1006","nameplate reserve: capacity >= 4 -> >= 6")
SOLDIER_PATCH(0x8071F7,"b9c0000000","b920010000","nameplate reserve: new(4 * 0x30) -> new(6 * 0x30)")
SOLDIER_PATCH(0x807211,"4883fb04","4883fb06","nameplate reserve: entries moved, at most 4 -> 6")
SOLDIER_PATCH(0x807222,"bb04000000","bb06000000","nameplate reserve: entries moved, clamp 4 -> 6")
SOLDIER_PATCH(0x8072AC,"48c7471004000000","48c7471006000000","nameplate reserve: capacity = 4 -> 6")
SOLDIER_PATCH(0x8072B8,"ba04000000","ba06000000","nameplate resize 0x8082B0(table, 4) -> 6")

// ---- the damage number colour 0x81F6F0 (r15 this, r12 the soldier, rbx the column 0..2, r14 out) ----
SOLDIER_PRINT(0x81F837,"498b0424 498bcc ff9098020000","damage colour: eax = soldier slot 83 (the type)")
SOLDIER_PRINT(0x81F844,"4863c8 488d5149 488d1453 4803d1 4803d2 410f1004d7",
              "damage colour: xmm0 = [r15 + 0x920 + type*0x30 + column*0x10] (rows 0..3; row 4 is the in-vehicle row)")
SOLDIER_PRINT(0x81F85A,"498bc6 410f1106","damage colour: the colour stored to [r14] (the stub's way back)")

// ---- the script boundary: GetPlayerSoldierType 0x1B5030 and the old BVM native 1005 0x222740 ----
SOLDIER_PRINT(0x1B5092,"488b01 ff9098020000 8bf0","GetPlayerSoldierType: esi = soldier slot 83")
SOLDIER_PRINT(0x222810,"488b01 ff9098020000 8bf0","BVM native 1005: esi = soldier slot 83")
