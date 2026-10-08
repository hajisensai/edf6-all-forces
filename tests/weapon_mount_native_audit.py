"""Read-only weapon-mount contracts for the supported EDF6 installation.

Usage: python tests/weapon_mount_native_audit.py <game-directory-or-EDF.dll>
Missing input, EDF.dll, or Root.cpk returns 77. Requires pefile. No DLL loading,
native execution, game process, or installation writes occur. The artillery
object is generated only in memory, with the stock model explicitly retained.
"""
from pathlib import Path
import math
import struct
import sys


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print("SKIP: pass the game directory or its EDF.dll path")
        return 77
    supplied = Path(argv[0])
    root = supplied.parent if supplied.name.lower() == "edf.dll" else supplied
    dll = root / "EDF.dll"
    if not dll.is_file() or not (root / "Root.cpk").is_file():
        print("SKIP: EDF.dll and Root.cpk are required")
        return 77
    try:
        import pefile
    except ImportError:
        print("SKIP: pefile is required for read-only DLL inspection")
        return 77

    repo = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(repo / "pylib"), str(repo / "tools")]
    import dsgo
    import mdb
    import rootcpk
    import sgo
    import make_artillery

    checked = 0

    def check(condition: bool, label: str) -> None:
        nonlocal checked
        if not condition:
            raise ValueError(label)
        checked += 1
        print(f"PASS {checked:02d}: {label}")

    pe = pefile.PE(str(dll), fast_load=True)
    try:
        check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46
              and pe.FILE_HEADER.Machine == 0x8664, "supported EDF.dll build")
        signatures = {
            # Holder's vehicle bone and weapon. Subsequent rows are copied to +150.
            0x633DF4: ("holder bone +18 / weapon +10", "488b4118488b5110"),
            0x633E57: ("holder update calls weapon model update", "488b4b10e8a0be0500"),
            # ModelInstance::BoneAt: bones +10, runtime stride 110.
            0x1100280: ("model bone array and stride", "4863c24869c01001000048034110c3"),
            # Model source index/parent/child/sibling -> runtime +C/+10/+14/+18.
            0x1110FCE: ("runtime bone index and parent fields",
                        "8b0289410c8b42048941108b420c8941148b4208894118"),
            # rdx is runtime record+88 here: require auto-world +8, read parent +10.
            0x1100080: ("descendant update checks inheritance and reads parent",
                        "807a80010f852e01000048634288"),
            0x5FB3A0: ("axis entry stores bone index and limits",
                       "8b4b08498b4728f20f1003f20f110402894c0208"),
            0x5FC348: ("axis mapping array +28 count +38 stride 38",
                       "486b773838488b5f284803f3"),
            # weapon+E88 attachment world is copied into weapon+60.
            0x68FD1D: ("mode-one attachment world bridge",
                       "488b83880e0000488d53600f2880b00000000f2902"
                       "0f2888c00000000f294a100f2880d00000000f294220"
                       "0f2888e00000000f294a30"),
            0x68FDBC: ("weapon ModelInstance +FD0 receives attachment world",
                       "488d8bd00f00004883c4205be9c30da700"),
            0x1100B90: ("SetWorld writes root then updates descendants",
                        "c681b0000000010f2802488b41100f2980b0000000"
                        "0f284a100f2988c00000000f2842200f2980d0000000"
                        "0f284a3033d20f2988e0000000e943f4ffff"),
            0x11B2870: ("weapon model presence gate +F40", "48837910000f95c0c3"),
        }
        for address, (label, hex_bytes) in signatures.items():
            expected = bytes.fromhex(hex_bytes)
            check(pe.get_data(address, len(expected)) == expected,
                  f"native {address:#x}: {label}")
    finally:
        pe.close()

    game = rootcpk.Game(str(root))

    def plain(folder: str, name: str) -> dict:
        return sgo.load(data=game.read(folder, name))

    def model(folder: str, name: str):
        archive = mdb.rab_read(game.read(folder, name))
        member = next(f for f in archive.files if f.name.lower().endswith(".mdb"))
        return mdb.mdb_read(member.data)

    def chain(md, name: str) -> list[str]:
        current = next(b.index for b in md.bones if md.name_of(b.name) == name)
        names = []
        seen = set()
        while current != -1:
            if current in seen or not 0 <= current < len(md.bones):
                raise ValueError(f"invalid model parent chain for {name}")
            seen.add(current)
            bone = md.bones[current]
            if bone.index != current:
                raise ValueError(f"model index mismatch for {name}")
            names.append(md.name_of(bone.name))
            current = bone.parent
        return names

    def muzzle(name: str) -> tuple[str, tuple[float, ...], dict]:
        blob = game.read("WEAPON", name)
        if blob[:4] == b"DSGO":
            block = dsgo.parse(blob).root.get("animation_model").items[2].data
        else:
            block = sgo.read(blob)[1]["animation_model"][2]
        head = struct.unpack_from("<I", block, 0x14)[0]
        count, relative = struct.unpack_from("<Hi", block, head + 2)
        if count != 1:
            raise ValueError(f"expected one muzzle in {name}, got {count}")
        record = head + relative
        node = record + struct.unpack_from("<i", block, record + 4)[0]
        end = node
        while block[end:end + 2] != b"\0\0":
            end += 2
        position = record + struct.unpack_from("<i", block, record + 12)[0]
        child = struct.unpack_from("<i", block, record + 28)[0]
        return (block[node:end].decode("utf-16le"),
                struct.unpack_from("<3f", block, position),
                sgo.load(data=block[record + child:]) if child else {})

    def near(got: tuple[float, ...], want: tuple[float, ...]) -> bool:
        return len(got) == len(want) and all(
            math.isclose(a, b, abs_tol=1e-5) for a, b in zip(got, want))

    heli506 = plain("OBJECT", "V506_HELI.SGO")
    check(heli506["vehicle_weapon_setting"][:3] == [["body", 0]] * 3
          and "heli_gatling_ctrl" not in heli506, "506 guns and missiles are body-mounted")
    left = muzzle("V_506HELI_GATLING01_L.SGO")
    right = muzzle("V_506HELI_GATLING01_R.SGO")
    check(left[0] == right[0] == "v_Null"
          and near(left[1], (2.5, -0.24, 2.53))
          and near(right[1], (-2.5, -0.24, 2.53)), "506 fixed gun muzzle positions")

    heli409 = plain("OBJECT", "VEHICLE409_HELI.SGO")
    check(heli409["heli_gatling_ctrl"] == [-360, 360, -90, 0]
          and heli409["vehicle_weapon_setting"][:2] == [["balkan_tilt", 0], ["body", 0]],
          "409 same-seat movable gun / fixed rocket counterexample")
    md409 = model("OBJECT", "VEHICLE409_HELI.MRAB")
    check(chain(md409, "balkan_tilt")[:3] == ["balkan_tilt", "balkan_roll", "body"]
          and "balkan_roll" not in chain(md409, "body"), "409 holder ancestor distinguishes its guns")

    heli410 = plain("OBJECT", "VEHICLE410_HELI.SGO")
    check(heli410["heli_gatling_ctrl"] == [0, 180, -90, 90]
          and heli410["vehicle_weapon_setting"][:2] == [["canon_barrel_l", 1], ["canon_barrel_r", 2]],
          "410 door guns belong to seats one and two")
    md410 = model("OBJECT", "VEHICLE410_HELI.MRAB")
    check(all(chain(md410, f"canon_barrel_{side}")[:3]
              == [f"canon_barrel_{side}", f"gun_emplacement_{side}", "body"]
              for side in ("l", "r")), "410 door gun parent chains")

    mg = plain("OBJECT", "V610_PTRUCK_GUN.SGO")
    constraints = mg["car_base_constraint_data"]
    check(mg["vehicle_weapon_setting"] == [["gun", 0]]
          and constraints[0][3][1][1] == [0]
          and constraints[1][3][1][1] == [1, -90, 90], "V610 movable yaw and pitch constraints")
    mg_muzzle = muzzle("V610_MACHINEGUN.SGO")
    check(mg_muzzle[0] == "v_Null" and near(mg_muzzle[1], (0, 0.28, 1.6))
          and mg_muzzle[2].get("fire_model_direction") == 1, "V610 uses real mode-one muzzle direction")
    null_model = model("WEAPON", "V_NULL.RAB")
    check(chain(null_model, "v_Null") == ["v_Null", "mdl"]
          and plain("WEAPON", "V610_MACHINEGUN.SGO")["animation_model"][1] == 0,
          "V610 weapon muzzle inherits its model root without a CAS")

    tank = plain("OBJECT", "V505_TANK.SGO")
    constraints = tank["car_base_constraint_data"]
    check(constraints[0][3][1][1] == [0]
          and constraints[1][3][1][1] == [1, -40, 5]
          and chain(model("OBJECT", "V505_TANK.MRAB"), "cannon_slide")[:3]
          == ["cannon_slide", "cannon", "cannon_main"], "ordinary tank traverse and holder ancestor chain")

    artillery = dsgo.to_py(dsgo.parse(make_artillery.vehicle_sgo(game, own_model=False)).root)
    constraints = artillery["car_base_constraint_data"]
    check(constraints[0][3][1][1] == [1, 0, 0]
          and constraints[1][3][1][1] == [1, -60, 5]
          and artillery["vehicle_weapon_setting"] == [["cannon_slide_l", 0], ["cannon_slide_r", 0]],
          "in-memory artillery fixes yaw but preserves independent elevation")
    print(f"PASS: {checked} read-only native/resource contracts; no game or DLL execution")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as error:
        print(f"FAIL: {type(error).__name__}: {error}", file=sys.stderr)
        raise SystemExit(1)
