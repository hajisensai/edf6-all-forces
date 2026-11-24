"""ctest's hail_glide_<I>: the I-th of N parts of tools/hail_glide_check.cpp's units, of about equal measured time.

  python tests/hail_glide_shard.py HAIL_GLIDE_CHECK_EXE --shard I/N
  python tests/hail_glide_shard.py HAIL_GLIDE_CHECK_EXE --seconds     every unit, one after another: their seconds
                                                                       written to tests/hail_glide_seconds.json

The units are what `hail_glide_check --list` prints (the logged hails, each wing's sweep, each wing on the two walled
maps); each runs in its own process. Packed by pylib/testshard.py: one run of the whole check took 141 s (2026-11-24,
check.py on a loaded machine), the longest test of a src/-only change's ctest.
"""
from __future__ import annotations

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pylib'))
import testshard  # noqa: E402

SECONDS = os.path.join(HERE, 'hail_glide_seconds.json')


def main(argv: list[str]) -> int:
    if not argv or argv[0].startswith('-'):
        print(__doc__)
        return 2
    exe, rest = os.path.abspath(argv[0]), argv[1:]
    shard, count = 0, 1
    if '--shard' in rest:
        shard, count = (int(x) for x in rest[rest.index('--shard') + 1].split('/'))
        if not 0 <= shard < count:
            raise SystemExit(f'--shard {shard}/{count}: I must be 0..N-1')
    r = subprocess.run([exe, '--list'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    listed = r.stdout.split()
    if r.returncode or not listed:
        sys.stdout.write(r.stdout)
        print(f'FAIL hail_glide_check --list: exit {r.returncode}, {len(listed)} units')
        return 1
    mine = testshard.pack(listed, count, testshard.load(SECONDS), lambda u: u)[shard]
    took: dict[str, float] = {}
    failed: list[str] = []
    for unit in mine:
        started = time.perf_counter()
        # its stderr too, in order with its stdout: a unit that dies (an assert, a crash message) says why
        r = subprocess.run([exe, unit], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        took[unit] = time.perf_counter() - started
        sys.stdout.write(r.stdout.replace('hail_glide_check: all passed\n', ''))
        if r.returncode:
            print(f'FAIL {unit}: exit {r.returncode}')
            failed.append(unit)
    if '--seconds' in rest and count == 1:
        testshard.save(SECONDS, "Each tools/hail_glide_check.cpp unit's seconds (tests/hail_glide_shard.py --seconds, one "
                       "after another): ctest's hail_glide_<I> balance their shards by them.", took)
    print(f'shard {shard}/{count}: {len(mine)} of {len(listed)} units, {sum(took.values()):.0f} s')
    print(f'{len(failed)} FAILED: {" ".join(failed)}' if failed else 'all passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
