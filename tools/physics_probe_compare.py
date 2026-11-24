"""Compares runs of the autopilot's physics probe (tests/autopilot/physics_probe.cpp, `--cmd "probe physics"`): net-10,
does HavokMultithread change what the physics computes (docs/havok-mt-re.md §13 item 5)?

  python tools/physics_probe_compare.py ST=run1.log ST=run2.log MT=run3.log ...

Each LOG is an autopilot log kept by `drive.py run ... LOG`; the label before `=` names its group (e.g. the
HavokMultithread setting). For every pair of runs:
  - hash: the first scene tick whose bit-exact hash (every live non-static body's index, motion bits, position and
    velocity) differs, and how many hash rows agree;
  - rows: for the bodies both runs have at a row tick (same body index and same owner vtable), the position difference:
    median and P95 over all row ticks, and at the last common row tick (the "final" positions) median, P95 and how many
    differ by more than 1 m; bodies only one run has (or with another owner) are counted as unmatched.
Then the medians per group pair (ST-ST, MT-MT, ST-MT) and the ratio of the mixed pairs' to the same-setting pairs'.
Standard library only.
"""
from __future__ import annotations

import itertools
import math
import re
import statistics
import sys

H_ROW = re.compile(r'PHYS H t=(\d+) n=(\d+) dyn=(\d+) kf=(\d+) act=(\d+) hash=([0-9A-F]+)')
B_ROW = re.compile(r'PHYS B t=(\d+) i=(\d+) f=([0-9A-F]+) o=([0-9A-F]+) p=([^ ]+) v=([^ \r\n]+)')


class Run:
    def __init__(self, label: str, path: str) -> None:
        self.label, self.path = label, path
        self.hashes: dict[int, tuple[int, str]] = {}
        self.rows: dict[int, dict[int, tuple[str, tuple[float, ...], tuple[float, ...]]]] = {}
        self.end = ''
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = H_ROW.search(line)
                if m:
                    self.hashes[int(m[1])] = (int(m[2]), m[6])
                    continue
                m = B_ROW.search(line)
                if m:
                    p = tuple(float(x) for x in m[5].split(','))
                    v = tuple(float(x) for x in m[6].split(','))
                    self.rows.setdefault(int(m[1]), {})[int(m[2])] = (m[4], p, v)
                    continue
                if 'PHYS end' in line:
                    self.end = line.strip()


def pct(values: list[float], q: float) -> float:
    if not values:
        return math.nan
    s = sorted(values)
    k = (len(s) - 1) * q
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def compare(a: Run, b: Run) -> dict:
    common = sorted(set(a.hashes) & set(b.hashes))
    first = next((t for t in common if a.hashes[t] != b.hashes[t]), None)
    same = sum(1 for t in common if a.hashes[t] == b.hashes[t])
    ticks = sorted(set(a.rows) & set(b.rows))
    every: list[float] = []
    unmatched = matched = 0
    final: list[float] = []
    final_tick = ticks[-1] if ticks else None
    for t in ticks:
        ra, rb = a.rows[t], b.rows[t]
        for i in set(ra) | set(rb):
            if i not in ra or i not in rb or ra[i][0] != rb[i][0]:
                unmatched += 1
                continue
            matched += 1
            d = math.dist(ra[i][1], rb[i][1])
            every.append(d)
            if t == final_tick:
                final.append(d)
    return {'first_diff': first, 'hash_same': same, 'hash_rows': len(common), 'matched': matched,
            'unmatched': unmatched, 'median': pct(every, 0.5), 'p95': pct(every, 0.95), 'max': max(every, default=math.nan),
            'final_tick': final_tick, 'final_n': len(final), 'final_median': pct(final, 0.5),
            'final_p95': pct(final, 0.95), 'final_over_1m': sum(1 for d in final if d > 1.0)}


def main(argv: list[str]) -> int:
    runs = []
    for arg in argv:
        label, _, path = arg.partition('=')
        if not path:
            print(__doc__)
            return 2
        runs.append(Run(label, path))
    for r in runs:
        print(f'{r.label} {r.path}: {len(r.hashes)} hash rows, {len(r.rows)} row ticks, '
              f'{sum(len(x) for x in r.rows.values())} body rows; {r.end}')
    groups: dict[str, list[dict]] = {}
    print()
    print('pair                      first-diff  hash-same  matched unmatched   median      p95      max  '
          'final(t, n, median, p95, >1m)')
    for a, b in itertools.combinations(range(len(runs)), 2):
        ra, rb = runs[a], runs[b]
        c = compare(ra, rb)
        key = '-'.join(sorted((ra.label, rb.label)))
        groups.setdefault(key, []).append(c)
        print(f'{ra.label}#{a + 1} vs {rb.label}#{b + 1}'.ljust(26) +
              f'{c["first_diff"]!s:>10} {c["hash_same"]:>5}/{c["hash_rows"]:<5} {c["matched"]:>7} {c["unmatched"]:>9} '
              f'{c["median"]:>8.4f} {c["p95"]:>8.4f} {c["max"]:>8.2f}  '
              f'({c["final_tick"]}, {c["final_n"]}, {c["final_median"]:.4f}, {c["final_p95"]:.4f}, {c["final_over_1m"]})')
    print()
    print('group     pairs  median(of pair medians)  p95(median)  final median  final p95  final >1m (mean)  '
          'unmatched (mean)')
    summary = {}
    for key, cs in sorted(groups.items()):
        s = {k: statistics.median(c[k] for c in cs) for k in ('median', 'p95', 'final_median', 'final_p95')}
        s['over'] = statistics.mean(c['final_over_1m'] for c in cs)
        s['unmatched'] = statistics.mean(c['unmatched'] for c in cs)
        summary[key] = s
        print(f'{key:<9} {len(cs):>5}  {s["median"]:>23.4f}  {s["p95"]:>11.4f}  {s["final_median"]:>12.4f}  '
              f'{s["final_p95"]:>9.4f}  {s["over"]:>16.1f}  {s["unmatched"]:>16.1f}')
    labels = sorted({r.label for r in runs})
    if len(labels) == 2:
        x, y = labels
        mixed, same = summary.get(f'{x}-{y}'), [summary.get(f'{x}-{x}'), summary.get(f'{y}-{y}')]
        same = [s for s in same if s]
        if mixed and same:
            base = max(s['median'] for s in same)
            base95 = max(s['p95'] for s in same)
            print()
            print(f'{x}-{y} median / max same-setting median: '
                  f'{mixed["median"] / base if base else math.inf:.3f}; p95 ratio: '
                  f'{mixed["p95"] / base95 if base95 else math.inf:.3f}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
