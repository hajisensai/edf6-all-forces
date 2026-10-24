"""Read-only audit of the production native enemy network-pose admission table.

Usage: python tests/enemy_control_native_net_audit.py <supported EDF.dll>
An absent or nonexistent explicit DLL path returns CTest skip code 77. No DLL is
loaded or executed, no manifest is generated, and only Python's standard library
is required. First-16-byte matches include raw base thunks; they establish binary
identity, not serializer semantics or in-game movement acceptance. The production
verified flags remain explicit reviewed decisions; the native-net fixture tests
their fail-closed behavior, including unknown classes.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import struct
import sys


EXPECTED_ROWS = 34
EXPECTED_SLOT_FINGERPRINTS = 45
EXPECTED_SHARED_TABLE = 0x17ABA10
EXPECTED_SHARED_WRITER = 0x77D500
EXPECTED_SHARED_READER = 0x77C2C0
REQUIRED_SHARED_FINGERPRINTS = frozenset((
    0x77D500, 0x77C2C0, 0x761B20, 0x7617B0, 0x760C50,
    0x760A40, 0x77C8E0, 0x6DAE50, 0x6DB170,
))
EXPECTED_MACHINE = 0x8664
EXPECTED_TIMESTAMP = 0x678CCB46
EXPECTED_IMAGE_SIZE = 0x22CE000
POSE_SLOTS = (5, 6, 7, 8)
NUMBER = r"(?:0[xX][0-9a-fA-F]+|[0-9]+)"


class AuditError(RuntimeError):
    pass


class Checks:
    def __init__(self) -> None:
        self.count = 0

    def require(self, condition: bool, message: str) -> None:
        self.count += 1
        if not condition:
            raise AuditError(message)


@dataclass(frozen=True)
class PoseRow:
    name: str
    primary: int
    profile_hash: int
    secondary: int
    col: int
    type_descriptor: int
    hierarchy: int
    slots: tuple[int, ...]
    verified: bool


@dataclass(frozen=True)
class Profile:
    name: str
    primary: int
    profile_hash: int


def uncomment(text: str) -> str:
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)


def array_body(source: str, type_name: str, array_name: str) -> str:
    match = re.search(
        rf"\bconstexpr\s+{type_name}\s+{array_name}\s*\[\s*\]\s*=\s*\{{(.*?)\}}\s*;",
        uncomment(source),
        flags=re.S,
    )
    if match is None:
        raise AuditError(f"production array not found: {array_name}")
    return match.group(1)


def parsed_entries(body: str, pattern: re.Pattern[str], description: str):
    matches = list(pattern.finditer(body))
    residue = pattern.sub("", body)
    if residue.strip(" \t\r\n,"):
        raise AuditError(f"unparsed {description} initializer: {residue[:100]!r}")
    return matches


def parse_rows(source: str) -> list[PoseRow]:
    pattern = re.compile(
        r'\{\s*"([^"]+)"\s*,\s*'
        + (rf"({NUMBER})\s*,\s*" * 6)
        + r"\{\s*"
        + (rf"({NUMBER})\s*,\s*" * 3)
        + rf"({NUMBER})\s*\}}\s*,\s*(true|false)\s*\}}"
    )
    rows = []
    for match in parsed_entries(array_body(source, "PoseRow", "kPoseRows"), pattern, "pose row"):
        name, *values, flag = match.groups()
        numbers = [int(value, 0) for value in values]
        rows.append(PoseRow(name, *numbers[:6], tuple(numbers[6:]), flag == "true"))
    return rows


def parse_profiles(source: str) -> list[Profile]:
    pattern = re.compile(
        r'\{\s*"([^"]+)"\s*,\s*'
        + (rf"({NUMBER})\s*,\s*" * 6)
        + r"Kind::[A-Za-z_][A-Za-z_0-9]*\s*,\s*"
        + rf"{NUMBER}\s*,\s*{NUMBER}\s*\}}"
    )
    profiles = []
    for match in parsed_entries(uncomment(source), pattern, "native profile"):
        name, primary, _input, _ai, profile_hash, _slots, _mask = match.groups()
        profiles.append(Profile(name, int(primary, 0), int(profile_hash, 0)))
    return profiles


def parse_fingerprints(source: str, array_name: str = "kPoseFingerprints") -> dict[int, bytes]:
    pattern = re.compile(rf"\{{\s*({NUMBER})\s*,\s*\{{([^}}]*)\}}\s*\}}")
    fingerprints = {}
    for match in parsed_entries(
        array_body(source, "PoseFingerprint", array_name), pattern, "pose fingerprint"
    ):
        rva = int(match.group(1), 0)
        tokens = [value.strip() for value in match.group(2).split(",")]
        if not all(re.fullmatch(NUMBER, value) for value in tokens):
            raise AuditError(f"invalid fingerprint byte at {rva:#x}")
        values = [int(value, 0) for value in tokens]
        if len(values) != 16 or any(not 0 <= value <= 255 for value in values):
            raise AuditError(f"fingerprint must contain 16 bytes at {rva:#x}")
        if rva in fingerprints:
            raise AuditError(f"duplicate fingerprint RVA {rva:#x}")
        fingerprints[rva] = bytes(values)
    return fingerprints


class PeImage:
    """Map file-backed RVAs without loading the binary or resolving imports."""

    def __init__(self, path: Path, checks: Checks) -> None:
        self.data = path.read_bytes()
        self.checks = checks
        checks.require(self.raw(0, 2) == b"MZ", "DOS signature")
        nt = self.unpack_raw(0x3C, "<I")[0]
        checks.require(0x40 <= nt <= 0x1000, "PE header offset")
        checks.require(self.raw(nt, 4) == b"PE\0\0", "PE signature")
        machine, section_count, timestamp = self.unpack_raw(nt + 4, "<HHI")
        checks.require(machine == EXPECTED_MACHINE, "supported AMD64 machine")
        checks.require(timestamp == EXPECTED_TIMESTAMP, "supported PE timestamp")
        optional_size = self.unpack_raw(nt + 20, "<H")[0]
        optional = nt + 24
        checks.require(optional_size >= 112, "complete PE32+ optional header")
        checks.require(self.unpack_raw(optional, "<H")[0] == 0x20B, "PE32+ magic")
        self.base = self.unpack_raw(optional + 24, "<Q")[0]
        self.size = self.unpack_raw(optional + 56, "<I")[0]
        self.headers = self.unpack_raw(optional + 60, "<I")[0]
        checks.require(self.size == EXPECTED_IMAGE_SIZE, "supported SizeOfImage")
        checks.require(0 < self.headers <= len(self.data), "file-backed PE headers")
        checks.require(0 < section_count <= 96, "bounded PE section count")
        self.sections = []
        for index in range(section_count):
            section = self.unpack_raw(optional + optional_size + index * 40, "<8sIIIIIIHHI")
            name, virtual_size, rva, raw_size, raw_offset, *_, flags = section
            checks.require(rva < self.size and max(virtual_size, raw_size) <= self.size - rva,
                           f"section image bounds {name!r}")
            checks.require(raw_offset <= len(self.data) and raw_size <= len(self.data) - raw_offset,
                           f"section file bounds {name!r}")
            self.sections.append((rva, raw_size, raw_offset, flags))

    def raw(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise AuditError(f"truncated PE file at {offset:#x} size {size:#x}")
        return self.data[offset:offset + size]

    def unpack_raw(self, offset: int, layout: str):
        return struct.unpack(layout, self.raw(offset, struct.calcsize(layout)))

    def read(self, rva: int, size: int) -> bytes:
        if rva < 0 or size < 0 or rva >= self.size or size > self.size - rva:
            raise AuditError(f"out-of-image RVA {rva:#x} size {size:#x}")
        if rva < self.headers and size <= self.headers - rva:
            return self.raw(rva, size)
        matches = [(start, offset) for start, raw_size, offset, _ in self.sections
                   if start <= rva and rva - start <= raw_size and size <= raw_size - (rva - start)]
        if len(matches) != 1:
            raise AuditError(f"RVA lacks one file-backed section: {rva:#x} size {size:#x}")
        start, offset = matches[0]
        return self.raw(offset + rva - start, size)

    def unpack(self, rva: int, layout: str):
        return struct.unpack(layout, self.read(rva, struct.calcsize(layout)))

    def pointer_rva(self, rva: int) -> int:
        address = self.unpack(rva, "<Q")[0]
        self.checks.require(self.base <= address < self.base + self.size,
                            f"in-image pointer at {rva:#x}")
        return address - self.base

    def executable(self, rva: int, size: int) -> bool:
        return any(start <= rva and rva - start <= raw_size
                   and size <= raw_size - (rva - start) and flags & 0x20000000
                   for start, raw_size, _, flags in self.sections)


def audit_col(pe: PeImage, row: PoseRow, col: int, offset: int, checks: Checks) -> None:
    actual = pe.unpack(col, "<6I")
    wanted = (1, offset, 0, row.type_descriptor, row.hierarchy, col)
    checks.require(actual == wanted, f"{row.name}: exact RTTI COL at +{offset:#x}")
    name = b".?AV" + row.name.encode("ascii") + b"@@\0"
    checks.require(pe.read(row.type_descriptor + 16, len(name)) == name,
                   f"{row.name}: exact RTTI descriptor name")


def audit_hierarchy(pe: PeImage, row: PoseRow, checks: Checks) -> None:
    signature, _attributes, count, array = pe.unpack(row.hierarchy, "<4I")
    checks.require(signature == 0 and 1 <= count <= 256, f"{row.name}: RTTI hierarchy")
    descriptors = pe.unpack(array, "<" + "I" * count)
    root = pe.unpack(descriptors[0], "<IIiiiII")
    checks.require(root[0] == row.type_descriptor and root[2:5] == (0, -1, 0)
                   and root[6] == row.hierarchy, f"{row.name}: RTTI most-derived base")
    network_bases = 0
    network_name = b".?AVNetworkObject@net@@\0"
    for descriptor in descriptors:
        type_descriptor, _contained, mdisp, pdisp, vdisp, _flags, _hierarchy = pe.unpack(descriptor, "<IIiiiII")
        if pe.read(type_descriptor + 16, len(network_name)) == network_name:
            network_bases += 1
            checks.require((mdisp, pdisp, vdisp) == (0x120, -1, 0),
                           f"{row.name}: NetworkObject nonvirtual subobject +0x120")
    checks.require(network_bases == 1, f"{row.name}: one NetworkObject RTTI base")


def audit_catalog(rows: list[PoseRow], profiles: list[Profile],
                  fingerprints: dict[int, bytes], checks: Checks) -> None:
    checks.require(len(rows) == EXPECTED_ROWS, "34 explicit production pose rows")
    checks.require(len(profiles) == EXPECTED_ROWS, "34 native input profiles")
    for field in ("name", "primary", "secondary"):
        checks.require(len({getattr(row, field) for row in rows}) == len(rows),
                       f"unique pose {field}")
    checks.require(0 < sum(row.verified for row in rows) < len(rows),
                   "explicit verified and denied decisions; no blanket admission")
    for row, profile in zip(rows, profiles):
        checks.require((row.name, row.primary, row.profile_hash)
                       == (profile.name, profile.primary, profile.profile_hash),
                       f"{row.name}: ordered primary/profile-hash identity")
        checks.require(len(row.slots) == len(POSE_SLOTS), f"{row.name}: exact slots 5..8")
    checks.require(len(fingerprints) == EXPECTED_SLOT_FINGERPRINTS, "45 unique slot fingerprints")
    wanted = {rva for row in rows for rva in row.slots}
    checks.require(set(fingerprints) == wanted,
                   "fingerprints cover exactly every unique slot 5..8 RVA")


def audit_secondary_map(path: Path | None, rows: list[PoseRow], checks: Checks) -> bool:
    if path is None:
        return False
    manifest = json.loads(path.read_text(encoding="utf-8-sig"))
    checks.require(set(manifest) == {row.name for row in rows}, "secondary analysis covers all 34 rows")
    for row in rows:
        entry = manifest[row.name]
        actual = tuple(int(entry[key], 0) for key in
                       ("secondary_vtable", "col", "writer5", "reader6", "slot7", "slot8"))
        checks.require(actual == (row.secondary, row.col, *row.slots),
                       f"{row.name}: existing secondary analysis agrees")
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("edf_dll", nargs="?", help="explicit supported EDF.dll path; absent file skips with 77")
    parser.add_argument("--secondary-map", type=Path, help="optional explicit secondary-analysis JSON to cross-check")
    args = parser.parse_args(argv)
    if not args.edf_dll or not Path(args.edf_dll).is_file():
        print("SKIP: pass an existing supported EDF.dll path explicitly")
        return 77
    root = Path(__file__).resolve().parents[1]
    checks = Checks()
    try:
        source = (root / "src/enemy_control_native_net.cpp").read_text(encoding="utf-8-sig")
        profiles = parse_profiles((root / "src/enemy_control_native_profiles.inc").read_text(encoding="utf-8-sig"))
        rows = parse_rows(source)
        fingerprints = parse_fingerprints(source)
        shared_fingerprints = parse_fingerprints(source, "kPoseSharedFingerprints")
        checks.require(REQUIRED_SHARED_FINGERPRINTS <= set(shared_fingerprints),
                       "all reviewed shared serializer fingerprints are present")
        audit_catalog(rows, profiles, fingerprints, checks)
        for constant, expected in (("kPoseImageSize", EXPECTED_IMAGE_SIZE),
                                   ("kPoseTimestamp", EXPECTED_TIMESTAMP),
                                   ("kPoseSubobject", 0x120),
                                   ("kPoseSharedTable", EXPECTED_SHARED_TABLE),
                                   ("kPoseSharedWriter", EXPECTED_SHARED_WRITER),
                                   ("kPoseSharedReader", EXPECTED_SHARED_READER)):
            match = re.search(rf"\b{constant}\s*=\s*({NUMBER})", uncomment(source))
            checks.require(match is not None and int(match.group(1), 0) == expected,
                           f"production {constant} agrees with supported binary")
        mapped = audit_secondary_map(args.secondary_map, rows, checks)
        pe = PeImage(Path(args.edf_dll), checks)
        checks.require(pe.pointer_rva(EXPECTED_SHARED_TABLE) == EXPECTED_SHARED_WRITER,
                       "shared serializer table slot 0 writer")
        checks.require(pe.pointer_rva(EXPECTED_SHARED_TABLE + 8) == EXPECTED_SHARED_READER,
                       "shared serializer table slot 1 reader")
        for row in rows:
            audit_col(pe, row, pe.pointer_rva(row.primary - 8), 0, checks)
            checks.require(pe.pointer_rva(row.secondary - 8) == row.col,
                           f"{row.name}: secondary COL pointer")
            audit_col(pe, row, row.col, 0x120, checks)
            audit_hierarchy(pe, row, checks)
            for slot, rva in zip(POSE_SLOTS, row.slots):
                checks.require(pe.pointer_rva(row.secondary + slot * 8) == rva,
                               f"{row.name}: exact secondary slot {slot} RVA")
        for label, table in (("pose", fingerprints), ("shared serializer", shared_fingerprints)):
            for rva, expected in table.items():
                checks.require(pe.executable(rva, len(expected)), f"executable {label} code {rva:#x}")
                checks.require(pe.read(rva, len(expected)) == expected, f"raw {label} fingerprint {rva:#x}")
    except (AuditError, OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    verified = sum(row.verified for row in rows)
    print(f"{checks.count} enemy-control native-net binary checks passed; "
          f"{len(rows)} exact classes, {verified} verified / {len(rows) - verified} denied decisions, "
          f"{len(fingerprints)} slot + {len(shared_fingerprints)} shared raw fingerprints; "
          f"secondary analysis {'matched' if mapped else 'not requested'}; "
          "no DLL loading or game execution")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
