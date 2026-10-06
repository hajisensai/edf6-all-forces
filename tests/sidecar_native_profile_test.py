"""Read EDF.dll as a file only; verify the sidecar's runtime ABI without loading or running the game."""
from pathlib import Path
import re
import struct
import sys

import pefile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "pylib"))
import gamedir


def main() -> None:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(gamedir.find_or_dev()) / "EDF.dll"
    pe = pefile.PE(str(path), fast_load=True)
    assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, "unsupported EDF.dll profile"
    image = pe.get_memory_mapped_image()
    source = (ROOT / "src/sidecar.cpp").read_text(encoding="utf-8")
    count = 0

    def check(rva: int, expected: bytes, label: str) -> None:
        nonlocal count
        assert image[rva:rva + len(expected)] == expected, (hex(rva), label)
        count += 1

    def signature(rva: int, name: str) -> None:
        match = re.search(rf"\b{name}\[\]=\{{([^}}]+)\}}", source)
        assert match, name
        check(rva, bytes(int(n, 16) for n in re.findall(r"0x([0-9A-Fa-f]+)", match[1])), name)

    def call(rva: int, target: int) -> None:
        check(rva, b"\xe8" + struct.pack("<i", target - rva - 5), f"call {target:x}")

    for rva, name in (
        (0x11B8DD0, "kPositionSig"), (0x11B9870, "kWarpSig"),
        (0x57B17C, "kExitWarpSig"), (0x573B6F, "kMoveCallSig"),
        (0x11B8D90, "kAddStepSig"), (0x11B9A92, "kStepUseSig"),
        (0x11B9CB7, "kStepClearSig"), (0x542FC7, "kBlastDamageSig"),
        (0x543600, "kBlastListDamageSig"), (0x114251, "kAttackerCopySig"),
    ):
        signature(rva, name)
    # Boarding's native state gate applies while the ragdoll/attachment integer is still zero.
    check(0x56D72C, bytes.fromhex("f681d005000004"), "native human state bit 2 boarding gate")
    check(0x5791DA, bytes.fromhex("c786d00500002d060000"), "native state 72 sets 0x62D")
    # There is no class-dependent half-height to add: native ride exit uses human+0x90 verbatim,
    # while pre-update only refreshes that transform AFTER the plugin's MoveIntent hook.
    check(0x57B12F, bytes.fromhex("0f108790000000"), "ride exit reads the body/foot transform")
    call(0x57B187, 0x11B9870)
    check(0x573D88, bytes.fromhex("488d5660488d8e80060000"), "refresh into human+0x60")
    call(0x573D93, 0x11AF6F0)
    call(0x11B8DE8, 0x11AF720)
    check(0x11AF73C, bytes.fromhex("0f1040300f1103"), "getter copies native transform translation")
    for vtable in (0x17CDF28, 0x17D0FF8, 0x17CF5B8, 0x17CF100):
        entry = struct.unpack_from("<Q", image, vtable + 4 * 8)[0] - pe.OPTIONAL_HEADER.ImageBase
        if image[entry] == 0xE9:
            entry += 5 + struct.unpack_from("<i", image, entry + 1)[0]
        assert entry == 0x572DF0, (hex(vtable), hex(entry))
        count += 1
    # Bullet owner and damage attacker originate from the same weak reference; explosion copies it.
    check(0x235623, bytes.fromhex("48898fa8090000"), "bullet owner pointer")
    check(0x235671, bytes.fromhex("4889af4007000048899748070000"), "damage attacker pointer/control")
    call(0x5429B9, 0x114210)
    call(0x5433AD, 0x114210)
    call(0x542FD4, 0x541FF0)
    call(0x54360E, 0x541FF0)
    print(f"PASS: {count} native sidecar profile/coordinate/attacker checks (file read only)")


if __name__ == "__main__":
    main()
