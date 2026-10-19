"""Read-only EDF.dll 678CCB46 Merman actual Unit emission contract.

Does not load the DLL, attach to a process, start a game, or write game files.
"""
from pathlib import Path
import argparse
import re
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--dll", type=Path, required=True)
parser.add_argument("--game-dir", type=Path, help="Optional Root.cpk directory; defaults to --dll parent")
args = parser.parse_args()
if not args.dll.is_file():
    print("SKIP: supported EDF.dll unavailable")
    raise SystemExit(77)
try:
    import pefile
except ImportError:
    print("SKIP: pefile unavailable")
    raise SystemExit(77)
pe = pefile.PE(str(args.dll), fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
checks = 1
root = Path(__file__).resolve().parents[1]
source = (root / "src/enemy_control_merman_net.cpp").read_text(encoding="utf-8-sig")


def raw(at, expected):
    global checks
    expected = bytes.fromhex(expected)
    assert pe.get_data(at, len(expected)) == expected, hex(at)
    checks += 1


def branch(at, target):
    global checks
    data = pe.get_data(at, 5)
    assert data[0] in (0xE8, 0xE9), hex(at)
    assert at + 5 + struct.unpack_from("<i", data, 1)[0] == target, (hex(at), hex(target))
    checks += 1


# Runtime admission must remain exact, not merely an available vtable slot.
for at, values in re.findall(r"\{(0x[0-9A-F]+),\{([^}]+)\}\}", source):
    expected = bytes(int(v.strip(), 16) for v in values.split(","))
    assert len(expected) == 16 and pe.get_data(int(at, 16), 16) == expected, at
    checks += 1

# Both admitted Merman final battery updates return only AFTER native caches,
# callbacks and optional native packed-ID installation have completed.
branch(0x3AA41C, 0x6D51C0)
branch(0x3AA4AC, 0x6D51C0)
branch(0x6D5734, 0x6D2640)
branch(0x6D331D, 0x1194280)

# Stable local parts: root FireControl and PoisonMist own genuine shared pairs.
raw(0x39CFB9, "49 89 86 10 1c 00 00")
raw(0x39D17C, "49 89 86 20 1c 00 00")
raw(0x3A9407, "48 8d 46 10 48 89 03 48 8b 7b 08 48 89 73 08")
raw(0x3A964A, "48 8d 47 10 48 89 86 f0 00 00 00 48 8b 9e f8 00 00 00 48 89 be f8 00 00 00")
branch(0x3A93FD, 0x6D0D90)
branch(0x3A9641, 0x6D0D90)

# Host final spread and per-instance InitParam are downstream of aim intent.
branch(0x6D30CA, 0x4E820)
branch(0x6D30E1, 0x4E220)
raw(0x6D30E6, "0f 10 20")
raw(0x6D3106, "0f 28 8d e0 00 00 00")
raw(0x6D3299, "89 85 20 03 00 00 8b 86 e8 04 00 00 89 85 24 03 00 00 ff c0 89 86 e8 04 00 00")
raw(0x6D32B3, "4c 89 b5 f0 01 00 00")
raw(0x6D3301, "4c 8d 8d 80 01 00 00")
raw(0x6D3308, "4c 8b 86 f0 02 00 00")
raw(0x6D330F, "48 8d 95 f0 00 00 00")

# A real native object is followed by muzzle weak cache/list/callback/ID work.
raw(0x6D34BE, "83 bb 30 0c 00 00 01")
raw(0x6D3518, "4c 8d b6 90 02 00 00")
raw(0x6D35B6, "48 8b 8e 50 06 00 00 48 85 c9 74 16 44 89 65 90 48 89 5d d8 48 8b 01 4c 8d 45 90 48 8d 55 d8 ff 50 10")
raw(0x6D35D8, "4c 8b b6 58 06 00 00 4d 85 f6")
branch(0x6D3621, 0x784510)
branch(0x6D3645, 0x7639B0)
raw(0x7639FC, "41 c1 e1 08 45 0b c8 44 89 4a 08")

# GunBattery2's other-family target/seed replay exists, but Merman's root
# receiver does not consume that tag8 route. Do not claim it sends final matrix.
branch(0x36D105, 0x6D4630)
branch(0x365713, 0x6D4150)
raw(0x36D100, "48 8b ca b2 08")
branch(0x6D4303, 0x6D2640)
branch(0x3A65A8, 0x318350)
raw(0x3A6449, "3c 06")
assert struct.unpack("<Q", pe.get_data(0x17B5530 + 0x78, 8))[0] - pe.OPTIONAL_HEADER.ImageBase == 0x3A6410
checks += 1

# The family registers the proven call, never takes ownership of central hooks.
assert "RegisterNativeUnitFactoryCallsite(kSite)" in source and "kSite=0x6D331D" in source
assert "NativeUnitFactoryCallsiteReady(kSite)" in source and "NativeUnitFactoryProviderReady(provider)" in source
assert "sizeof(NativeProjectileSample)" not in source and "resourceMask,1" in source
assert "pending.complete=true" in source and "if(!p.complete)return true" in source
assert "InvokeOriginalNativeUnitFactory" in source and "ReplayNativeProjectileSample" in source
checks += 5
# Persistent beam refresh is a genuine BulletBase path, separate from muzzle FX.
# InitParam+70 points to muzzle+70; BulletBase's root+140 state copies the
# InitParam+30 subrecord and retains that pointer at root+B20.
raw(0x6D3114, "41 0f 11 26 41 0f 11 5e 10 41 0f 11 56 20 41 0f 11 4e 30")
branch(0x22EA45, 0x231CC0)
branch(0x231CFC, 0x2307F0)
raw(0x231CEF, "49 8b d0 48 81 c1 a0 09 00 00")
raw(0x230895, "48 8b 47 40 48 8b d5 48 89 43 40")
raw(0x6D31BA, "48 8b 01 ff 50 60 e9 31 05 00 00")
# slot12 void Refresh(BulletBase* RCX): C30==1 && !C34.bit14 -> bit13.
# Acceptance does not require a 0->1 bit13 edge: initialization already sets it.
raw(0x22F390, "83 b9 30 0c 00 00 01 75 19 8b 91 34 0c 00 00 8b c2 c1 e8 0e 24 01 75 0a 0f ba ea 0d 89 91 34 0c 00 00 c3")
raw(0x23538E, "81 8f f4 0a 00 00 00 20 00 00")
for slot in (0x179E0A8, 0x179F878):  # BulletBase / BeamBullet01 primary slot12
    assert struct.unpack("<Q", pe.get_data(slot, 8))[0] - pe.OPTIONAL_HEADER.ImageBase == 0x22F390
    checks += 1
# void ContinuousStep(state RCX=root+140, UpdateContext* RDX). bit13 and
# optional B30/B38 guard gate the actual 64B B20 read; D30 owns local tail data.
branch(0x704ED4, 0x237CF0)
branch(0x237FF7, 0x233BD0)
branch(0x233C92, 0x234420)
raw(0x234574, "0f ba e0 0d 73 54 48 83 b9 f8 09 00 00 00 74 12 48 8b 81 f0 09 00 00 48 85 c0 74 3e 83 78 08 00 74 38")
raw(0x234596, "48 8b 81 e0 09 00 00 48 8b 89 f0 0b 00 00 0f 10 00 0f 11 01 0f 10 48 10 0f 11 49 10 0f 10 40 20 0f 11 41 20 0f 10 48 30 0f 11 49 30")
branch(0x2345C9, 0x511C0)
# Missing bit13 latches bit14. Native tail retraction precedes terminal bits0/1;
# do not replace this accepted stop with immediate scene-object destruction.
raw(0x23446E, "8b 81 f4 0a 00 00 40 80 e6 01 0f ba e0 0d 0f 82 c3 00 00 00 0f ba e8 0e 89 81 f4 0a 00 00")
raw(0x234513, "8b 81 f4 0a 00 00 a8 01 0f 85 90 04 00 00 83 c8 03 89 81 f4 0a 00 00")
# E74.bit0 can retain bit13. A missed emitter observation is not itself a stop.
raw(0x234467, "0f b6 b1 34 0d 00 00")
raw(0x23499A, "40 84 f6 75 0a 81 a3 f4 0a 00 00 ff df ff ff")
branch(0x24D6A0, 0x235D50)
raw(0x24D6A5, "f6 83 34 0c 00 00 01 74 12")
branch(0x24D6BB, 0x118A1B0)
raw(0x118A273, "80 4b 18 04")
# 6D57B1..58CD follows holder+20 0x58 entries to effect::EfsObject matrices.
# The separate actual BulletBase cache is muzzle+B0/B8 (checked above).
raw(0x6D57B1, "48 8b 46 20")
raw(0x6D57FE, "48 8b 78 20 4c 6b 78 30 58 4c 03 ff")
raw(0x6D585E, "0f 28 44 24 40 0f 11 43 60 0f 28 4c 24 50 0f 11 4b 70")
branch(0x6D3CF3, 0x2EC5C0)
branch(0x2EC91B, 0x2EBE40)
branch(0x2EBE9C, 0x2ED6E0)
raw(0x2ED714, "48 8d 05 5d ad 4b 01 49 89 06")
raw(0x2016438, ".?AVEfsObject@effect@@\0".encode("ascii").hex())


def wide_ref(at, target, value):
    """Require the parser's exact RIP-relative LEA and UTF-16 key."""
    global checks
    data = pe.get_data(at, 7)
    assert data[:3] in (b"\x48\x8d\x15", b"\x4c\x8d\x05"), hex(at)
    assert at + 7 + struct.unpack_from("<i", data, 3)[0] == target, hex(at)
    raw(target, (value.encode("utf-16le") + b"\0\0").hex())
    checks += 1


# Constructor and config-reload both reach the cadence parsers. 6D4B90
# handles muzzle.shape, not the per-update counters. No physics Hz is assumed.
branch(0x39CFAB, 0x3A92A0)
branch(0x39D16E, 0x3A9470)
branch(0x6D111E, 0x6D4B90)
branch(0x6D1211, 0x6D7500)
branch(0x6D1219, 0x6D7C90)
branch(0x6D6529, 0x6D7500)
branch(0x6D6531, 0x6D7C90)
for at, target, value in (
    (0x39CB73, 0x17B52D8, "EDF6_Merman"),
    (0x39CEC6, 0x17ADEA0, "gun_battery"),
    (0x39D089, 0x17B5630, "poison_mist"),
    (0x3A95B4, 0x17ADEA0, "gun_battery"),
    (0x6D4BC0, 0x17CE9E8, "muzzle"),
    (0x6D4C2A, 0x17B2308, "shape"),
    (0x6D752B, 0x17B6140, "charge"),
    (0x6D759D, 0x179E8C0, "time"),
    (0x6D7CC6, 0x17A5EE8, "fire"),
    (0x6D7D3D, 0x17B0DE0, "count"),
    (0x6D7E5E, 0x17ACD50, "accuracy"),
    (0x6D7F51, 0x17AD2A0, "interval"),
    (0x6D805D, 0x17CE9F8, "scattered"),
    (0x6D8169, 0x179E648, "random_speed_ratio"),
):
    wide_ref(at, target, value)
raw(0x6D7658, "89 83 40 01 00 00")  # charge.time -> battery+140
raw(0x6D7E07, "89 43 30")           # fire.count
raw(0x6D8006, "89 43 38")           # fire.interval
raw(0x6D8112, "89 43 3c")           # fire.scattered
raw(0x6D7633, "0f 57 c9")           # default charge.time = 0
raw(0x6D7DD3, "0f 28 cf")           # default count = xmm7 (double 1)
raw(0x6D7FE7, "0f 57 c9")           # default interval = 0
raw(0x6D80F3, "0f 28 cf")           # default scattered = xmm7 (double 1)
raw(0x1C369B0, struct.pack("<d", 1.0).hex())
raw(0x6D56A4, "83 bf e0 02 00 00 00 0f 86 e2 00 00 00 8b 87 e4 02 00 00 85 c0 74 0d ff c8 89 87 e4 02 00 00 e9 cb 00 00 00")
raw(0x6D5739, "8b 47 38 89 87 e4 02 00 00 ff 8f e0 02 00 00")

# The consumer can reject bit0 even while bit13 is set. The native getter is
# deliberately different: it checks bit13 and the optional guard, not bit14/0.
raw(0x234553, "a8 01 0f 85 3f 04 00 00")
raw(0x2319F0, "40 53 48 83 ec 60 f7 81 f4 0a 00 00 00 20 00 00 48 8b da 74 7c 48 83 b9 f8 09 00 00 00 74 12 48 8b 81 f0 09 00 00 48 85 c0 74 66 83 78 08 00 74 60 48 8b 81 e0 09 00 00 48 8d 4c 24 20 0f 28 00 0f 29 44 24 20 0f 28 48 10 0f 29 4c 24 30 0f 28 40 20 0f 29 44 24 40 0f 28 48 30 0f 29 4c 24 50 e8 6b f7 e1 ff 0f 28 44 24 20 48 8b c3 0f 28 4c 24 30 0f 29 03 0f 28 44 24 40 0f 29 4b 10 0f 28 4c 24 50 0f 29 43 20 0f 29 4b 30 48 83 c4 60 5b c3 48 8b 81 f0 0b 00 00 0f 10 00 0f 10 48 10 0f 29 02 0f 10 40 20 0f 29 4a 10 0f 10 48 30 48 8b c3 0f 29 42 20 0f 29 4a 30 48 83 c4 60 5b c3")
# Stop invokes a reentrant callback at state+CD8 (root+E18) before returning.
raw(0x23452A, "48 8b 89 d8 0c 00 00 48 85 c9 0f 84 77 04 00 00 48 8b 01 ff 50 10")
branch(0x6D4DA3, 0x6BADD0)  # native muzzle.shape name resolver
branch(0x6D1229, 0x6D6550)
wide_ref(0x6D6599, 0x179E508, "bullet")
wide_ref(0x6D6607, 0x17AE6A0, "factory")
branch(0x6D66AD, 0x1195C50)  # bullet.factory string -> local factory lookup
raw(0x6D66B2, "48 89 87 f0 02 00 00")  # battery+2F0 feeds actual spawn


def require(condition, label):
    global checks
    assert condition, label
    checks += 1


def source_function(signature):
    """Read one current production body, including nested SEH/initializers."""
    definition = re.search(re.escape(signature) + r"[^;{]*\{", source)
    assert definition is not None, signature
    opening = definition.end() - 1
    depth = 1
    at = opening + 1
    while depth:
        depth += (source[at] == "{") - (source[at] == "}")
        at += 1
    return re.sub(r"\s+", "", source[opening:at])


step = source_function("void BeamStep(")
consumed = re.findall(r"\bconsumed=([^;]+);", step)[-1]
require("!(flags&1u)" in consumed, "consumer must reject native bit0 early branch")
require("(flags&0x6000u)==0x2000u" in consumed and "ConsumerGate(bullet)" in consumed,
        "consumed refresh must pass bit13/14 and native weak guard")
require("MatrixAt(At<constvoid*>(bullet,0xB20),input)" in consumed and
        "MatrixAt(At<constvoid*>(bullet,0xD30),output)" in step,
        "refresh receipt requires native input and consumed output matrices")
rehome = source_function("bool RehomeBeam(")
require("At<float*>(b.ref.object,0xD30)" in rehome and
        "crew::Put<float*>(b.ref.object,0xB20,owned);b.detached=false;" in rehome,
        "detached B20 must rehome to bullet-owned D30 before Beam slot reuse")
require("!b.stopped" in rehome and "At<constvoid*>(b.ref.object,0xB20)!=b.persistent" in rehome and
        "reinterpret_cast<std::uintptr_t>(owned)&15u" in rehome,
        "rehome requires stopped owned aligned native storage")
require("!RehomeBeam(b)||!DropStrong(s,b.ref,&b.held)" in source_function("bool ReleaseBeams("),
        "release must rehome before dropping the tracked bullet")
require("if(s)++s->beamDepth;__try{" in step and "__finally{if(s)--s->beamDepth;}" in step,
        "stop callback reentrancy must hold beamDepth across every native step exit")
for signature, token in (
    ("bool Capture(", "s->beamDepth"),
    ("Result Apply(", "s->beamDepth"),
    ("bool Empty(", "s->beamDepth"),
    ("bool Release(Stream&", "s.beamDepth"),
    ("bool Abort(", "s->beamDepth"),
):
    require(token in source_function(signature), signature + " must reject reentrant beam lifecycle work")
# Stream is larger than a game thread's safe aggregate-temporary budget. Reset
# only released slots in place; Before -> Bind must not create nested Stream{}.
require("std::is_trivially_copyable_v<Stream>" in source, "in-place Stream reset requires trivial storage")
reset = source_function("void ResetStream(")
require("std::memset(&s,0,sizeof(s));s.next=1;" in reset, "Stream reset must use in-place zeroing")
bind = source_function("Stream* Bind(")
before = source_function("bool Before(")
require("ResetStream(s);" in bind and "ResetStream(*old);" in before,
        "Bind and retired Before must use in-place reset")
require("!s.held&&!s.executing&&!s.beamDepth" in bind and
        bind.index("!s.executing") < bind.index("ResetStream(s)"),
        "Bind must not reset a slot during a native beam callback")
require("if(old->executing||old->beamDepth)returnfalse;ResetStream(*old);" in before,
        "retired Before must not reset a stream during its native callback")
require(not re.search(r"\bStream(?:\{\}|\(\))|(?:\*old|\*s|\bs)=\{\}", bind + before),
        "Bind/Before must not rebuild half-megabyte aggregate temporaries")

# Optional, strictly read-only original Root.cpk evidence. Resource absence may
# skip this section, but cannot skip the mandatory PE checks above.
pe_checks = checks
resource_files = 0
game_dir = args.game_dir or args.dll.parent
if not (game_dir / "Root.cpk").is_file():
    print("SKIP resources: Root.cpk unavailable")
else:
    sys.path.insert(0, str(root / "pylib"))
    import dsgo
    import rootcpk
    from mab_legacy import mab_layout

    game = rootcpk.Game(str(game_dir))
    for filename, mist_count, mist_interval in (
        ("E603_MERMAN.SGO", 15, 15),
        ("E603_MERMAN_LIGHT.SGO", 5, 45),
        ("E603_MERMAN_M063.SGO", 15, 15),
    ):
        try:
            data = game.read("OBJECT", filename)
        except KeyError:
            print(f"SKIP resource: OBJECT/{filename} unavailable")
            continue
        document = dsgo.parse(data)
        actor = document.root.get("EDF6_Merman")
        mist = actor.get("poison_mist")
        # Native muzzle lookup uses MAB list0 locator names, not arbitrary
        # occurrences in other model tables or the UTF-16 pool.
        model = document.root.get("animation_model").items[2].data
        require(model[:4] == b"MAB\0" and mab_layout(model).flag == 0x83,
                (filename, "native animation_model MAB"))
        header = struct.unpack_from("<I", model, 0x14)[0]
        kind, locator_count, relative_first = struct.unpack_from("<HHi", model, header)
        require(kind == 0 and locator_count == 3, (filename, "native MAB list0"))

        def model_text(at):
            end = at
            while model[end:end + 2] != b"\0\0":
                end += 2
            return model[at:end].decode("utf-16le")

        records = range(header + relative_first,
                        header + relative_first + 0x20 * locator_count, 0x20)
        locator_names = [model_text(at + struct.unpack_from("<i", model, at)[0]) for at in records]
        for node, setting, count, interval, accuracy, shape, factory in (
            (actor.get("gun_battery"), "gun_battery_setting", 30, 1, 0.1, "\u767a\u5c04\u53e3", "AcidBullet01"),
            (mist.get("gun_battery"), "poison_mist_gun_battery_setting", mist_count, mist_interval, 3.14, "\u6bd2\u9727\u767a\u751f", "EfsBullet"),
        ):
            assert node == document.root.get(setting), (filename, setting)
            fire = node.get("fire")
            assert fire.get("count") == count, (filename, setting, "count")
            assert fire.get("interval") == interval, (filename, setting, "interval")
            assert fire.get("accuracy") == struct.unpack("<f", struct.pack("<f", accuracy))[0], (filename, setting, "accuracy")
            assert node.get("muzzle").get("shape") == shape, (filename, setting, "muzzle.shape")
            assert "charge" not in node.names.values(), (filename, setting, "charge")
            assert "scattered" not in fire.names.values(), (filename, setting, "scattered")
            assert "random_speed_ratio" not in fire.names.values(), (filename, setting, "random_speed_ratio")
            checks += 8
            require(locator_names.count(shape) == 1, (filename, setting, "native shape count"))
            require(node.get("bullet").get("factory") == factory, (filename, setting, "actual factory"))
        # This sibling charge curve belongs to Merman's poison warning, outside
        # poison_mist.gun_battery; it is not GunBattery2 charge.time/+140.
        warning_time = mist.get("charge").get("time")
        assert isinstance(warning_time, dsgo.Blob) and len(warning_time.data) == 44, filename
        checks += 1
        resource_files += 1
print(f"Merman actual Unit factory native audit: {checks} checks "
      f"(PE/source={pe_checks}, resources={checks - pe_checks}, files={resource_files})")
