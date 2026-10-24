"""tools/testhub.py collect() sends only crash dumps this install wrote (2026-10-12: report #6 sent #5's dump of
10-07, written by another build of the plugin), and of those only the run the sent logs end with (dump_in_session:
the dump's write time and process start against the plugins' last `loading` line). Offline: minidumps and plugin DLLs
are made here (MINIDUMP_HEADER with its TimeDateStamp, one ModuleListStream, MINIDUMP_MODULE rows, optionally a
MiscInfoStream with ProcessCreateTime; a PE header with TimeDateStamp and SizeOfImage), the plugin's log is written
with its `loading` lines, LOCALAPPDATA points at a scratch CrashDumps. Exit code 1 when a check fails.

2026-10-18 (reports #19 / #20: a crash, no dump, no word on it; docs/feedback-2026-10-18-crash-dump.md): the plugin's
own dump (EDF6.exe.<pid>.plugin.dmp, src/crash_dump.cpp) is sent beside Windows' LocalDumps one; crash/dumps.txt is
always written, and with no dump it says why (the plugin's log: dump written / crash seen / installed / old plugin; the
LocalDumps state); Windows' Report.wer is attached as an unconfirmed Report.candidate.wer (the same path and a later
EventTime do not identify a process; ProgramData points at a scratch WER). Oversized dumps are rejected before any
dump file is opened, including hang dumps.

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
from unittest.mock import patch

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


def write_dump(path: str, modules: list[tuple[str, int, int]], written: float | None = None,
               created: float | None = None) -> None:
    """A minidump with one ModuleListStream: (full path, TimeDateStamp, SizeOfImage) per module; written: the header's
    TimeDateStamp (default now); created: a MiscInfoStream with ProcessCreateTime (MINIDUMP_MISC1_PROCESS_TIMES)."""
    streams = 2 if created is not None else 1
    names = io.BytesIO()
    name_base = 32 + 12 * streams + 4 + 108 * len(modules)
    rows = io.BytesIO()
    for i, (name, stamp, size) in enumerate(modules):
        rva = name_base + names.tell()
        raw = name.encode('utf-16-le')
        names.write(struct.pack('<I', len(raw)) + raw + b'\0\0')
        row = bytearray(108)
        struct.pack_into('<QIIII', row, 0, 0x140000000 + i * 0x1000000, size, 0, stamp, rva)
        rows.write(row)
    stamp = int(time.time() if written is None else written)
    body = struct.pack('<4sIIIIIQ', b'MDMP', 0xA793, streams, 32, 0, stamp, 0)
    body += struct.pack('<III', 4, 4 + 108 * len(modules), 32 + 12 * streams)
    misc_rva = name_base + names.tell()
    if created is not None:
        body += struct.pack('<III', 15, 24, misc_rva)
    body += struct.pack('<I', len(modules)) + rows.getvalue() + names.getvalue()
    if created is not None:   # MINIDUMP_MISC_INFO: SizeOfInfo, Flags1, ProcessId, ProcessCreateTime, user, kernel
        body += struct.pack('<IIIIII', 24, 0x3, 1234, int(created), 0, 0)
    with open(path, 'wb') as f:
        f.write(body)


def collected(game: str) -> tuple[list[str], str]:
    names, listing, _ = collected_all(game)
    return [n for n in names if n.startswith('crash/') and n.endswith('.dmp')], listing


def collected_all(game: str) -> tuple[list[str], str, bytes]:
    """Every name in the zip, crash/dumps.txt, and crash/Report.candidate.wer (b'' when not sent)."""
    data, names = testhub.collect(game, 'test')
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        listing = z.read('crash/dumps.txt').decode('utf-8') if 'crash/dumps.txt' in names else ''
        wer = z.read('crash/Report.candidate.wer') if 'crash/Report.candidate.wer' in names else b''
    return names, listing, wer


def write_wer(root: str, name: str, exe: str, at: float, code: str = 'c0000005', module: str = 'EDF.dll',
              offset: str = '00000000004c3a30') -> str:
    """A Windows Error Reporting Report.wer (UTF-16 with a BOM, as WER writes it) under <root>/Microsoft/Windows/WER/
    ReportArchive/<name>: EventTime a FILETIME, the APPCRASH signature (Sig[3] module, Sig[6] code, Sig[7] offset;
    the names are localized, the indices are not), the exe's path."""
    folder = os.path.join(root, 'Microsoft', 'Windows', 'WER', 'ReportArchive', name)
    os.makedirs(folder, exist_ok=True)
    lines = ['Version=1', 'EventType=APPCRASH', f'EventTime={int((at + 11644473600) * 1e7)}',
             'Sig[0].Name=应用程序名', 'Sig[0].Value=EDF6.exe', 'Sig[3].Name=故障模块名称', f'Sig[3].Value={module}',
             'Sig[6].Name=异常代码', f'Sig[6].Value={code}', 'Sig[7].Name=异常偏移', f'Sig[7].Value={offset}',
             f'UI[2]={exe}', f'LoadedModule[0]={exe}']
    path = os.path.join(folder, 'Report.wer')
    with open(path, 'wb') as f:
        f.write('\ufeff'.encode('utf-16-le') + '\r\n'.join(lines).encode('utf-16-le'))
    return path


def write_log(game: str, lines: list[str], mtime: float) -> None:
    """The plugin's log (appended across runs, src/plugin.cpp Log: [hh:mm:ss.mmm] text) with its last write at mtime."""
    path = os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.log')
    with open(path, 'wb') as f:
        f.write(''.join(line + '\r\n' for line in lines).encode('utf-8'))
    os.utime(path, (mtime, mtime))


def tod(t: float, ms: str = '.123') -> str:
    return time.strftime('[%H:%M:%S', time.localtime(t)) + ms + ']'


def main() -> int:
    saved = {k: os.environ.get(k) for k in ('LOCALAPPDATA', 'ProgramData')}
    state = testhub.local_dumps_state
    try:
        return _main()
    finally:   # tools/selftest.py runs this in its own process: give LOCALAPPDATA / ProgramData back
        testhub.local_dumps_state = state
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


def _main() -> int:
    global failures, checks
    failures = checks = 0
    with tempfile.TemporaryDirectory() as root:
        game = os.path.join(root, 'EARTH DEFENSE FORCE 6')
        crew = os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.dll')
        write_pe(crew, 0x6AC9C50B, 9416704)
        os.environ['LOCALAPPDATA'] = os.path.join(root, 'local')
        os.environ['ProgramData'] = os.path.join(root, 'programdata')   # no machine's WER reports in the cases
        testhub.local_dumps_state = lambda: 'Windows LocalDumps：（测试）未配置'
        dumps = os.path.join(root, 'local', 'CrashDumps')
        os.makedirs(dumps)
        exe = os.path.join(game, 'EDF6.exe')
        now = time.time()
        # The run the logs end with began 10 minutes ago (its line in the log; an older run's line before it).
        run = int(now - 600)
        write_log(game, [f'{tod(run - 7200)} EDF6VehicleCrew 0.8.0 loading', f'{tod(run - 7100)} SUPPORT older run',
                         f'{tod(run)} EDF6VehicleCrew 0.8.0 loading', f'{tod(run + 30)} HELI this run'], now - 5)
        start = testhub.session_start(game)
        expect(start is not None and abs(start - run) < 1, 'the session starts at the last loading line, dated by the log')

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

        # Report #6 again, same install: a dump of this build but of an earlier run (2 h ago) goes with no log of its run.
        earlier = os.path.join(dumps, 'EDF6.exe.7000.dmp')
        write_dump(earlier, [(exe, 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)], written=run - 7000, created=run - 7201)
        expect(testhub.dump_matches(earlier, game) == '', 'the earlier run is this install')
        expect('早于本次游戏' in testhub.dump_in_session(earlier, start), "an earlier run's dump is not this run's")
        os.utime(mine, (run - 6900, run - 6900))     # mine is now the earlier run's too (no MiscInfo: write time alone)
        write_dump(mine, [(exe, 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)], written=run - 6900)
        sent, listing = collected(game)
        expect(sent == [], "no dump of this run: none is sent, though this install's earlier ones are newest")
        expect('EDF6.exe.7000.dmp' in listing and '不是本次游戏' in listing, 'dumps.txt names the earlier run and why')
        # This run's crash: written after its start by a process that started a second before its loading line.
        this = os.path.join(dumps, 'EDF6.exe.7100.dmp')
        write_dump(this, [(exe, 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)], written=run + 120, created=run - 1)
        os.utime(this, (run + 120, run + 120))
        sent, listing = collected(game)
        expect(sent == ['crash/EDF6.exe.7100.dmp'], "this run's dump is sent")
        expect('本次安装、本次游戏' in listing, 'dumps.txt marks it as this install and this run')
        # A later process whose plugins never logged (written after the start, but its process began after it).
        later = _dump(root, [(exe, 0x678CCB63, 401408)], written=run + 400, created=run + 300)
        expect('不是本次游戏' in testhub.dump_in_session(later, start), "a later run's dump is not this run's")
        # The log line is the time of day only: a run that began before midnight of a log written after it.
        write_log(game, [f'{tod(now - 3 * 3600)} EDF6VehicleCrew 0.8.0 loading'], now)
        expect(abs(testhub.session_start(game) - int(now - 3 * 3600)) < 1, 'a loading line is dated no later than the log')
        # No loading line at all: no run to match, no dump sent.
        write_log(game, ['[00:00:01.000] something else'], now)
        expect(testhub.session_start(game) is None, 'a log without a loading line has no session')
        expect(collected(game)[0] == [], 'without a session no dump is sent')
        # Report #22: the current builds end the line with their build id; an older build's bare line before it.
        build = 'loading (build 48bbd4685a, 23123456 bytes, written 2026-10-21 17:29:57 UTC)'
        write_log(game, [f'{tod(run - 7200)} EDF6VehicleCrew 0.8.0 loading', f'{tod(run - 7100)} SUPPORT older run',
                         f'{tod(run)} EDF6VehicleCrew 0.8.0 {build}', f'{tod(run + 30)} HELI this run'], now - 5)
        start = testhub.session_start(game)
        expect(start is not None and abs(start - run) < 1, 'a loading line with the build id starts the session')
        expect(testhub.dump_in_session(this, start) == '', "this run's dump matches the run its build-id line began")
        expect(collected(game)[0] == ['crash/EDF6.exe.7100.dmp'], "this run's dump is sent after a build-id line")
        write_log(game, [f'{tod(run)} EDF6VehicleCrew 0.8.0 loading'], now - 5)

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
        bounded_dump_metadata(root, exe, crew, run)
        crash_dump_cases(root, game, dumps, exe, crew, run)
    print(f'{checks - failures}/{checks} checks passed')
    return 1 if failures else 0


def bounded_dump_metadata(root: str, exe: str, crew: str, run: int) -> None:
    """Module/session checks may seek in a full dump, but never allocate its entire memory payload."""
    probe = os.path.join(root, 'metadata.dmp')
    write_dump(probe, [(exe, 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)], written=run + 60, created=run - 1)
    real_open = open
    reads = []

    class MetadataReader:
        def __init__(self, f):
            self.f = f

        def __enter__(self):
            return self

        def __exit__(self, *args):
            return self.f.__exit__(*args)

        def seek(self, *args):
            return self.f.seek(*args)

        def read(self, size=-1):
            assert 0 <= size <= (1 << 20), 'dump metadata parsing must use bounded reads'
            data = self.f.read(size)
            reads.append(len(data))
            return data

    def metadata_open(path, *args, **kwargs):
        f = real_open(path, *args, **kwargs)
        return MetadataReader(f) if os.fspath(path) == probe else f

    with patch('builtins.open', metadata_open):
        modules = testhub.dump_modules(probe)
        expect(modules is not None and modules['edf6vehiclecrew.dll'][1:] == (0x6AC9C50B, 9416704),
               'bounded metadata reads retain the module identity')
        expect(testhub.dump_times(probe) == (run + 60, run - 1), 'bounded metadata reads retain both session times')
    expect(sum(reads) < (1 << 20), 'only dump metadata is read, independent of the full memory payload')


def crash_dump_cases(root: str, game: str, dumps: str, exe: str, crew: str, run: int) -> None:
    """2026-10-18: the plugin's own dump, crash/dumps.txt always, why no dump, Windows' Report.wer."""
    for name in os.listdir(dumps):
        os.remove(os.path.join(dumps, name))
    loading = f'{tod(run)} EDF6VehicleCrew 0.9.0 loading'
    installed = f'{tod(run)} CRASHDUMP installed: x (dbghelp 1, dumping thread 1, filter kept first by redirect, ...)'
    now = time.time()

    # No dump anywhere, an old plugin (no CRASHDUMP line): dumps.txt still written, saying so.
    write_log(game, [loading, f'{tod(run + 30)} HELI this run'], now - 5)
    names, listing, wer = collected_all(game)
    expect('crash/dumps.txt' in names, 'dumps.txt is written even when there is no dump at all')
    expect('没有上传任何崩溃转储' in listing and '旧版插件' in listing and 'LocalDumps' in listing,
           'with no dump, dumps.txt says the plugin had no crash dump and the LocalDumps state')
    expect('没有这个游戏目录的 EDF6.exe 崩溃报告' in listing and wer == b'', 'no WER report: said, none attached')

    # The plugin's dump installed but nothing reached it (a fast fail): said.
    write_log(game, [loading, installed], now - 5)
    expect('已装好' in collected(game)[1], 'an installed crash dump that saw no crash: said')
    # It saw the crash but the dump failed: the crash line is quoted.
    write_log(game, [loading, installed, f'{tod(run + 60)} CRASH unhandled exception: exception C0000005 at EDF.dll+0x4c3a30',
                     f'{tod(run + 60)} CRASH dump not written: x (error 5)'], now - 5)
    listing = collected(game)[1]
    expect('看到了崩溃但转储没写成' in listing and 'EDF.dll+0x4c3a30' in listing and 'error 5' in listing,
           'a crash the plugin saw but could not dump: its lines in dumps.txt')

    # The allocation-free native crash writer writes both lines directly, without the normal timestamp prefix.
    raw_crash = 'CRASH unhandled exception: exception C0000005 at EDF.dll+0x4c3a30, thread 123; writing x'
    write_log(game, [loading, installed, raw_crash, 'CRASH dump not written: x (error 5)'], now - 5)
    listing = collected(game)[1]
    expect('看到了崩溃但转储没写成' in listing and 'EDF.dll+0x4c3a30' in listing and 'error 5' in listing,
           'both raw crash failure lines are recognized without timestamp prefixes')
    write_log(game, [loading, installed, raw_crash, 'CRASH dump written: x, 3000 KB (exception C0000005 at EDF.dll+0x4c3a30)'],
              now - 5)
    listing = collected(game)[1]
    expect('已写出（CRASH dump written: x, 3000 KB' in listing and '看到了崩溃但转储没写成' not in listing,
           'a raw crash line followed by a raw success line is recognized as written')
    write_log(game, [loading, installed, f'{tod(run + 60)} DEBUG mentions CRASH dump written: bogus'], now - 5)
    listing = collected(game)[1]
    expect('已装好' in listing and '已写出' not in listing,
           'a non-crash line mentioning CRASH is not accepted as crash evidence')
    write_log(game, [loading, installed, raw_crash, 'CRASH dump not written: x (error 5)'], now - 5)

    # A same-folder WER after loading is useful evidence, but path and time cannot confirm its process/session.
    progdata = os.environ['ProgramData']
    write_wer(progdata, 'AppCrash_EDF6.exe_old', exe, run - 3600, offset='0000000000111111')
    write_wer(progdata, 'AppCrash_EDF6.exe_other', r'E:\other\EDF6.exe', run + 90, offset='0000000000222222')
    mine = write_wer(os.environ['LOCALAPPDATA'], 'AppCrash_EDF6.exe_mine', exe, run + 60)
    names, listing, wer = collected_all(game)
    with open(mine, 'rb') as f:
        expect(wer == f.read(), 'the WER candidate is attached as it is')
    expect('crash/Report.candidate.wer' in names and 'crash/Report.wer' not in names and '会话未确认' in listing,
           'the archive name and explanation both retain the WER session uncertainty')
    expect('c0000005' in listing and 'EDF.dll+0x4c3a30' in listing and 'AppCrash_EDF6.exe_mine' in listing,
           "dumps.txt names the report's exception code and module+offset")
    expect('111111' not in listing and '222222' not in listing, "an earlier run's or another folder's report is not taken")

    # A later launch can crash before it logs loading. Even when the plugin log has a CRASH line from the earlier
    # launch, newest-after-loading alone must not label this later process's WER as belonging to the logged session.
    later = write_wer(progdata, 'AppCrash_EDF6.exe_later', exe, run + 400, module='later.dll', offset='0000000000333333')
    names, listing, wer = collected_all(game)
    with open(later, 'rb') as f:
        expect(wer == f.read(), "a later launch's report remains available only as a candidate")
    expect('crash/Report.wer' not in names and '会话未确认' in listing and '后续未记 loading 的启动' in listing,
           "a later launch's WER is not asserted to be this session's crash")
    expect(testhub.wer_report(game, None)[0] is None, 'without a loading line no WER is attributed or attached')
    os.remove(later)

    # The plugin's dump and Windows' dump of this run: both sent.
    plugin = os.path.join(dumps, 'EDF6.exe.7300.plugin.dmp')
    windows = os.path.join(dumps, 'EDF6.exe.7300.dmp')
    for path in (plugin, windows):
        write_dump(path, [(exe, 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)], written=run + 60, created=run - 1)
        os.utime(path, (run + 60, run + 60))
    write_log(game, [loading, installed, f'{tod(run + 60)} CRASH dump written: {plugin}, 3000 KB'], now - 5)
    sent, listing = collected(game)
    expect(sorted(sent) == ['crash/EDF6.exe.7300.dmp', 'crash/EDF6.exe.7300.plugin.dmp'],
           "the plugin's dump and Windows' dump of this run are both sent")
    expect('没有上传任何崩溃转储' not in listing, 'with a dump sent, no "no dump" note')
    # An older plugin dump of this install does not displace this run's.
    older = os.path.join(dumps, 'EDF6.exe.7000.plugin.dmp')
    write_dump(older, [(exe, 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)], written=run - 7000, created=run - 7201)
    os.utime(older, (run - 7000, run - 7000))
    expect('crash/EDF6.exe.7300.plugin.dmp' in collected(game)[0], "an earlier run's plugin dump is not sent in its place")

    # Dumps over DUMP_MAX: named with their size, not dropped without a word (this machine's LocalDumps wrote
    # 100 MB ones, 2026-10-18).
    limit = testhub.DUMP_MAX
    hang = os.path.join(game, 'Mods', 'Plugins', 'EDF6VehicleCrew.hang.dmp')
    write_dump(hang, [(exe, 0x678CCB63, 401408), (crew, 0x6AC9C50B, 9416704)], written=run + 60, created=run - 1)
    opened = []
    real_open = open

    def no_dump_open(path, *args, **kwargs):
        if os.fspath(path).lower().endswith('.dmp'):
            opened.append(path)
            raise AssertionError('an oversized dump must be rejected before parsing or reading: ' + path)
        return real_open(path, *args, **kwargs)

    try:
        testhub.DUMP_MAX = os.path.getsize(windows) - 1
        with patch('builtins.open', no_dump_open):
            sent, listing = collected(game)
        expect(sent == [], 'a dump over the limit is not sent')
        expect('EDF6.exe.7300.dmp: ' in listing and 'EDF6.exe.7300.plugin.dmp: ' in listing and '超过' in listing,
               'a dump over the limit is named in dumps.txt with its size')
        expect('EDF6VehicleCrew.hang.dmp: ' in listing and not opened,
               'all oversized sources, including hang dumps, are rejected without opening any dump')
    finally:
        testhub.DUMP_MAX = limit
        os.remove(hang)


_made = 0


def _dump(root: str, modules: list[tuple[str, int, int]], **times: float) -> str:
    global _made
    _made += 1
    path = os.path.join(root, f'case{_made}.dmp')
    write_dump(path, modules, **times)
    return path


if __name__ == '__main__':
    sys.exit(main())
