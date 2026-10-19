"""Read-only native contracts for adding real HQ / room menu buttons.

The native HQ constructor adds its optional menu buttons from SGO members with
HUiLayout::CreateByName. The plugin follows the same path so an uninstantiated
member creates no invisible focus item when the plugin is absent.

This checks the shipped EDF.dll code, not live keyboard / mouse / gamepad feel.
No game process is opened, started, or patched. Missing DLL / pefile exits 77.
Usage: python -B tests/room_native_menu_contract_test.py [EDF_DLL_OR_GAME_DIR]
"""
from __future__ import annotations

import hashlib
import re
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "pylib"))
import gamedir


# Complete routines: checking only a prologue would not cover parent ownership,
# failure cleanup, the actual child-list insertion, or the label's native type.
FUNCTIONS = {
    0x7EB360: (0x29E, "fc5f0b9b4dc399f28583f4baa5f7ce868f39a8b3f4098a12836e92a03b5e1ff8"),
    0x7EBDB0: (0x3E, "3a92edb3c5502dfbc26a024f25cb19b243a3d551c67b355ec7e9a439aef2dcde"),
    0x7EE280: (0x4, "0378ce906c8c03cf00b445e260fa069cdbf729a7e55f39b8043787c6e3a50e1d"),
    0x811FC0: (0x9C, "b17a6d6c471f8d15d8d44e938b1bdcf440eed96623693a0b758c85fad75b40ed"),
    0x8FB930: (0x105, "b8b8194802d29019713a3fdd741c3bd33c278a641e9eb8825f44ab613523235d"),
    0x8F8A40: (0xA7, "adf4c9eb4ff46484b127cb13a7d2a4e9dd1d655ceedb9ce2f6d9d40ae5f1b824"),
    0x8F73C0: (0x58, "a9b4ffeac0f6666a99fdac48789e4b0b06995cd928b4f5408035d91b878f69d4"),
    0x7EAA40: (0x14C, "3901299e2326ee1220b083e7138a526624f5c00ed3458a685378a891454949aa"),
    0x7EAB90: (0x260, "3db896124d84802980c8cc0e2f018e2281d5a2bb9b5db46b9eb002030e7d1a1c"),
    0x8456D0: (0x4D1, "30ddb7cd47c5a0e7854520a60582db731aa7edaf7aab60d55ccc24c22a67dcc7"),
    0x83BFF0: (0x1C0, "9395be28d60fb67e66e3820e0c1239faa6736d61e0d52dc667d6117f59a230e6"),
    0x83D060: (0x3F0, "564aafd73abcbeb88fae35cd4016e0e4ac5c4b7e33f29068d8bd89ab5b0a43c3"),
    0x83E4F0: (0xE0, "cd467a0037381255c55688c0deba191292433f630f5396d1a1eda0ee1cc4b682"),
    0x8859C0: (0x1DF, "e6d5d45844c2df8d2369d4c268c25e77616492566490df9becc448ef49c9b5e4"),
    0x7F8BB0: (0x25A, "00c281e8ac6ddec686a4f80fd8d2d29068b95fbfff306e318c90a4cd7de51bac"),
}


def run(path: Path, pefile) -> int:
    pe = pefile.PE(str(path), fast_load=True)
    checks = 0

    def require(ok: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not ok:
            raise AssertionError(message)

    require(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, "unsupported EDF.dll timestamp")
    require(pe.FILE_HEADER.Machine == 0x8664, "requires AMD64 EDF.dll")
    img = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase

    def u32(at: int) -> int:
        return struct.unpack_from("<I", img, at)[0]

    def u64(at: int) -> int:
        return struct.unpack_from("<Q", img, at)[0]

    def bytes_at(at: int, expected: str, message: str) -> None:
        blob = bytes.fromhex(expected)
        require(img[at:at + len(blob)] == blob, f"{message} ({at:#x})")

    def call(at: int, target: int, message: str) -> None:
        require(img[at] == 0xE8 and at + 5 + struct.unpack_from("<i", img, at + 1)[0] == target,
                f"{message} ({at:#x})")

    def rip(at: int, target: int, message: str) -> None:
        require(at + 7 + struct.unpack_from("<i", img, at + 3)[0] == target,
                f"{message} ({at:#x})")

    def wide(at: int, value: str) -> None:
        text = (value + "\0").encode("utf-16le")
        require(img[at:at + len(text)] == text, f"native string {value}")

    def bases(vtable: int) -> list[str]:
        col = u64(vtable - 8) - base
        require(u32(col) == 1 and u32(col + 4) == 0, "complete-object RTTI locator")
        hierarchy = u32(col + 16)
        descriptors = u32(hierarchy + 12)
        names = []
        for i in range(u32(hierarchy + 8)):
            descriptor = u32(descriptors + 4 * i)
            type_descriptor = u32(descriptor)
            names.append(img[type_descriptor + 16:type_descriptor + 160].split(b"\0")[0].decode("ascii"))
        return names

    for at, (size, sha) in FUNCTIONS.items():
        require(hashlib.sha256(img[at:at + size]).hexdigest() == sha,
                f"complete native routine changed at {at:#x}")

    # CreateByName(this, Shared* out, Shared* parentByValue, wchar_t* member).
    # The third parameter is consumed on success, null parent, and missing name.
    bytes_at(0x8456F7, "498bf14d8be04c8bfa4c8be9", "four native argument registers")
    bytes_at(0x8457D5, "4d8b8038010000", "read the parent's current child list")
    bytes_at(0x845898, "4c89742430488d44244848894424284c896424204c8bcb4c8d4538488d5580498bcd",
             "pass preceding sibling and parent to the complete SGO factory")
    call(0x8458BA, 0x841800, "complete SGO factory")
    call(0x8457BF, 0x7D4580, "consume null parent argument")
    bytes_at(0x845B33, "498bcc", "successful path releases parent copy")
    call(0x845B36, 0x7D4580, "consume successful parent argument")
    bytes_at(0x845B3D, "33f649893749897708498b5c2408", "missing-name result null and parent cleanup")
    bytes_at(0x845B59, "f00fc14308", "missing-name path decrements parent strong count")
    bytes_at(0x845B7E, "498bc7", "return out pointer")

    # The original HQ menu uses this exact API to append six normal menu entries.
    for at in (0x89E264, 0x89E29F, 0x89E462, 0x89E577, 0x89E5EF, 0x89E72E, 0x89E79D):
        call(at, 0x8456D0, "native HQ runtime button creation")
    rip(0x89DFD6, 0x1804EF8, "HQ native parent name")
    wide(0x1804EF8, "LeftButtonsBase2")

    # Complete Button creation stores a strong reference in parent.children and a
    # weak reference in the screen's named component table, shared with native focus.
    call(0x84253A, 0x83BFF0, "SGO Button branch")
    call(0x83C088, 0x83D060, "construct actual HUiButton")
    bytes_at(0x83D32D, "4881c338010000", "native parent child-list address")
    bytes_at(0x83D36F, "f0ff4708488970104889781848ff4308", "retain child and increase list size")
    bytes_at(0x83D37F, "488b45084889294889410848894d08488908", "native child-list tail insertion")
    bytes_at(0x83C103, "498d8d80040000", "screen named component map")
    bytes_at(0x83C137, "48896a18488b4a204c894220", "registered component weak reference")
    require(u64(0x17FD7F0 + 2 * 8) - base == 0x7E7220, "button focusable virtual slot")
    bytes_at(0x7E7220, "b001c3", "native button is focusable")

    button_bases = bases(0x17FD7F0)
    require(button_bases[:3] == [".?AVHUiButton@ui@@", ".?AVButton@ui@@", ".?AVComponent@ui@@"],
            "HUiButton has its native Button inheritance")
    require(".?AVHUiTextField@ui@@" not in button_bases, "a button must not be cast as HUiTextField")

    # The button's *inner* label is ui::TextField. HUiTextField::SetText uses extra
    # derived font fields; it must not be used on this base TextField label.
    rip(0x7E778B, 0x2062F28, "button inner label RTTI is ui::TextField")
    require(img[0x2062F38:0x2062F38 + len(b".?AVTextField@ui@@\0")] == b".?AVTextField@ui@@\0", "base TextField RTTI name")
    bytes_at(0x7E7847, "4989b720020000498b8f2802000049898728020000", "label weak pointer at button +220/+228")
    bytes_at(0x7E7820, "4533c0", "label initialization uses null optional font parameters")
    call(0x7E782B, 0x7F8BB0, "native label text setter")
    bytes_at(0x83E506, "4c8b4208", "native WeakLock reads weak control block")
    bytes_at(0x83E523, "f0410fb14808", "WeakLock acquires a live strong reference")
    bytes_at(0x7F8BDA, "4d8bf04c8bfa488bf1", "TextField setter third argument is optional font parameters")
    bytes_at(0x7F8CA2, "4d85f6", "TextField accepts null optional font parameters")
    bytes_at(0x7F8DDB, "c6862402000001", "native TextField setter marks text layout dirty")
    # Visibility is solely a draw flag. Native navigation filters current +70;
    # SetEnabled writes pending +71, committed by the child update after its
    # parent has navigated. The plugin must disable current participation before
    # hiding and use native focus transfer when its entry owned the focus.
    bytes_at(0x7EC815, "80b9f801000000", "shown flag gates drawing")
    bytes_at(0x7F02D9, "4180797000", "next navigation checks current enabled")
    bytes_at(0x7F03E9, "4180797000", "previous navigation checks current enabled")
    bytes_at(0x7EFE91, "80797000", "selection checks current enabled")
    bytes_at(0x7EE280, "885171c3", "native SetEnabled only writes pending enabled")
    bytes_at(0x7EE5D6, "0fb646713a4670", "child update compares pending with current enabled")
    bytes_at(0x7EE603, "884670", "child update commits current enabled")
    call(0x7EFEA9, 0x7EB360, "native navigation uses the same focus transfer")
    bytes_at(0x7EBDB9, "488b8988000000", "HasFocus reads native FocusAgent")
    bytes_at(0x7EB4C1, "488b01ff5070", "native transfer notifies previous kill-focus virtual slot")

    # All three event names are case-sensitive. The button's own virtual focus
    # handlers and the SGO callback binder pass these exact strings to AddAction.
    wide(0x17FE020, "Click")
    wide(0x17F5B00, "focus")
    wide(0x17FD890, "kill_focus")
    rip(0x84277D, 0x17FE020, "SGO Click callback binding")
    call(0x842787, 0x7EAA40, "SGO uses the native action list")
    require(u64(0x17FD7F0 + 13 * 8) - base == 0x83B6A0, "button focus virtual handler")
    require(u64(0x17FD7F0 + 14 * 8) - base == 0x83B790, "button kill-focus virtual handler")
    rip(0x83B737, 0x17F5B00, "button gains focus")
    call(0x83B741, 0x7EAB90, "native focus dispatch")
    rip(0x83B827, 0x17FD890, "button loses focus")
    call(0x83B831, 0x7EAB90, "native kill-focus dispatch")

    # Callback construction owns a weak screen reference, while attaching the
    # callback copies a strong action reference. No plugin-created MSVC control
    # block crosses the CRT boundary. Expired screens skip the member invocation.
    bytes_at(0x81200D, "48890248895a08f0ff430c", "screen weak output and weak count increment")
    bytes_at(0x8FB98A, "0f104500", "member callback is a 16-byte function/adjustment pair")
    bytes_at(0x8FB9A5, "488953104889431848894b20f0ff410c0f114328",
             "native callback stores vtable, screen weak reference, and member pointer")
    bytes_at(0x8FBA06, "498b4e084885c97410f00fc1710c", "callback factory consumes caller weak copy")
    bytes_at(0x7EAAF4, "f0ff4008", "AddAction retains its own strong action reference")
    bytes_at(0x7EADAF, "ff5008", "event dispatch calls native action virtual slot one")
    call(0x8F8A61, 0x8F1DA0, "native action weak-locks screen before callback")
    bytes_at(0x8F8AC4, "4885f6740d", "expired screen does not invoke plugin member")
    bytes_at(0x8F8AC9, "48634d20498bd64803ceff5518", "native member this-adjustment and invocation")
    bytes_at(0x8F73DD, "f00fc1410c", "native callback destructor releases screen weak reference")

    # Check the actual feature gates too: a mistranscribed prologue would leave
    # all behavioral tests green while silently disabling the entry in the game.
    source = (ROOT / "src/room_ui.cpp").read_text(encoding="utf-8")
    constants = dict(re.findall(r"\b(k[A-Za-z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+)", source))
    gates = source.split("const Sig kNativeMenuSigs[]={", 1)[1].split("};", 1)[0]
    entries = re.findall(r"\{(k[A-Za-z0-9_]+|0x[0-9A-Fa-f]+),\s*(k[A-Za-z0-9_]+),sizeof\(", gates)
    require(bool(entries), "native feature gate table is nonempty")
    for address, name in entries:
        at = int(constants.get(address, address), 16)
        match = re.search(r"const unsigned char " + name + r"\[\]=\{([^}]+)\};", source)
        require(match is not None, f"native feature signature is declared: {name}")
        sig = bytes(int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", match[1]))
        require(img[at:at + len(sig)] == sig, f"native feature gate agrees with EDF.dll: {name}")
    return checks


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(gamedir.find_or_dev()) / "EDF.dll"
    if path.is_dir():
        path /= "EDF.dll"
    if not path.is_file():
        print("room_native_menu_contract_test: EDF.dll unavailable (skipped)")
        return 77
    try:
        import pefile
    except ImportError:
        print("room_native_menu_contract_test: pefile unavailable (skipped)")
        return 77
    count = run(path, pefile)
    print(f"room_native_menu_contract_test: {count} native menu creation / ownership / focus / label checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
