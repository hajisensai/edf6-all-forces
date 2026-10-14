"""tools/testhub.py collect() sends only crash dumps this install wrote (2026-10-12: report #6 sent #5's dump of
10-07, written by another build of the plugin). Offline: minidumps and plugin DLLs are made here (MINIDUMP_HEADER,
one ModuleListStream, MINIDUMP_MODULE rows; a PE header with TimeDateStamp and SizeOfImage), LOCALAPPDATA points
at a scratch CrashDumps. Exit code 1 when a check fails.

  python tests/testhub_dump_source_test.py
"""
from __future__ import annotations

import io
import os
import struct
import sys
import tempfile
import time
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import testhub  # noqa: E402

failures = 0
checks = 0


def expect(ok: bool, what: str) -> None:
    global failures, checks
    checks += 1
    if not ok:
        failures += 1
        print('FAIL', what)


def write_pe(path: str, stamp: int, size: int) -> None:
    """A file whose PE header says `stamp` and `size` (all _pe_image reads)."""
    head = bytearray(0x200)
    head[0:2] = b'MZ'
    struct.pack_into('<I', head, 0x3C, 0x80)
    head[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<I', head, 0x80 + 8, stamp)
    struct.pack_into('<I', head, 0x80 + 24 + 56, size)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(head)


def write_dump(path: str, modules: list[tuple[str, int, int]]) -> None:
    """A minidump with one ModuleListStream: (full path, TimeDateStamp, SizeOfImage) per module."""
    names = io.BytesIO()
    name_base = 32 + 12 + 4 + 108 * len(modules)
    rows = io.BytesIO()
    for i, (name, stamp, size) in enumerate(modules):
        rva = name_base + names.tell()
        raw = name.encode('utf-16-le')
        names.write(struct.pack('<I', len(raw)) + raw + b'\0\0')
        row = bytearray(108)
        struct.pack_into('<QIIII', row, 0, 0x140000000 + i * 0x1000000, size, 0, stamp, rva)
        rows.write(row)
    body = struct.pack('<4sIIIIIQ', b'MDMP', 0xA793, 1, 32, 0, int(time.time()), 0)
    body += struct.pack('<III', 4, 4 + 108 * len(modules), 32 + 12)
    body += struct.pack('<I', len(modules)) + rows.getvalue() + names.getvalue()
    with open(path, 'wb') as f:
        f.write(body)


def collected(game: str) -> tuple[list[str], str]:
    data, names = testhub.collect(game, 'test')
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        listing = z.read('crash/dumps.txt').decode('utf-8') if 'crash/dumps.txt' in names else ''
    return [n for n in names if n.startswith('crash/') and n.endswith('.dmp')], listing


def main() -> int:
    with tempfile.TemporaryDirectory() as root:
        game = os.path.join(root, 'EARTH DEFENSE FORCE 6')
        crew = os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.dll')
        write_pe(crew, 0x6AC9C50B, 9416704)
        os.environ['LOCALAPPDATA'] = os.path.join(root, 'local')
        dumps = os.path.join(root, 'local', 'CrashDumps')
        os.makedirs(dumps)
        exe = os.path.join(game, 'EDF6.exe')

        # Report #6's case: the only dump is an older build's (#5's, same game folder).
        old = os.path.join(dumps, 'EDF6.exe.5468.dmp')
        write_dump(old, [(exe, 0x678CCB63, 401408), (crew, 0x6AC5DF87, 7159808)])
        sent, listing = collected(game)
        expect(sent == [], "an older plugin build's dump is not sent")
        expect('EDF6.exe.5468.dmp' in listing and '来源不符' in listing and '0x6AC5DF87' in listing,
               'dumps.txt names it and why')

        # A dump of this install, older on disk than the stale one: it is the one sent.
        mine = os.path.join(dumps, 'EDF6.exe.9000.dmp')
        write_dump(mine, [(exe, 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)])
        os.utime(mine, (time.time() - 60, time.time() - 60))
        sent, listing = collected(game)
        expect(sent == ['crash/EDF6.exe.9000.dmp'], "this install's dump is sent, the newer stale one is not")
        expect('本次安装' in listing, 'dumps.txt marks it as this install')

        # Another game folder (another copy of the game on the same machine).
        expect('另一个游戏目录' in testhub.dump_matches(
            _dump(root, [(r'E:\other\EDF6.exe', 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)]), game),
            'a dump of another game folder is refused')
        # A crash before the plugin loaded: nothing to disagree with, so it is this install's.
        expect(testhub.dump_matches(_dump(root, [(exe, 0x678CCB63, 401408)]), game) == '',
               'a dump without the plugin loaded is kept')
        # Not a minidump.
        junk = os.path.join(root, 'junk.dmp')
        with open(junk, 'wb') as f:
            f.write(b'not a dump')
        expect(testhub.dump_matches(junk, game) != '', 'a file that is no minidump is refused')

        # The real dump of report #5 when it is on this machine (the testers' logs pulled by tools/testhub.py pull).
        real = os.environ.get('EDF6_REPORT5_DUMP', r'D:\APP\edf6-share\.work\testhub-1012\report-6\logs-20261010-145538\crash\EDF6.exe.5468.dmp')
        if os.path.isfile(real):
            mods = testhub.dump_modules(real)
            expect(mods is not None and mods.get('edf6vehiclecrew.dll', ('', 0, 0))[1] == 0x6AC5DF87,
                   "report #5's real dump reads its plugin's stamp")
    print(f'{checks - failures}/{checks} checks passed')
    return 1 if failures else 0


_made = 0


def _dump(root: str, modules: list[tuple[str, int, int]]) -> str:
    global _made
    _made += 1
    path = os.path.join(root, f'case{_made}.dmp')
    write_dump(path, modules)
    return path


if __name__ == '__main__':
    sys.exit(main())
