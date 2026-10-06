"""Draws what the Primer creatures did in the game, from the trace the plugin writes with PrimerTrace=1
(Mods/Plugins/EDF6VehicleCrew.primer.csv, src/primer.cpp Trace: a line per creature every 0.1 s).

    python tools/primer_trace_view.py [CSV] [--run -1] [--out build/primer_trace.png]

CSV defaults to the game's. A run is one stretch of lines without a jump back in game time or a gap over 10 s (a
mission); --run picks one (-1: the last). The sheet: the top view (x, z) of every creature's track (centipedes green,
dragonflies violet, the player black), where they fired (red) and linked (stars); height over time; the longest
chain over time. A summary goes to the console: how long each spent doing what, shots, links, the longest chain.
"""
from __future__ import annotations

import argparse
import csv
import os
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'pylib'))


def runs(rows: list[dict]) -> list[list[dict]]:
    out: list[list[dict]] = []
    last = None
    for r in rows:
        ms = int(r['ms'])
        if last is None or ms < last or ms - last > 10000:
            out.append([])
        out[-1].append(r)
        last = ms
    return out


def chains(rows: list[dict]) -> list[tuple[float, int]]:
    """(seconds, the longest chain) at each trace tick: fronts followed through `behind`."""
    by_ms: dict[int, dict[int, dict]] = defaultdict(dict)
    for r in rows:
        if r['kind'] == 'centipede':
            by_ms[int(r['ms']) // 100][int(r['id'])] = r
    out = []
    t0 = min(by_ms) if by_ms else 0
    for tick in sorted(by_ms):
        at = by_ms[tick]
        best = 0
        for i, r in at.items():
            if int(r['ahead']) != -1:
                continue
            n, j, seen = 1, int(r['behind']), {i}
            while j != -1 and j in at and j not in seen:
                seen.add(j)
                n += 1
                j = int(at[j]['behind'])
            best = max(best, n)
        out.append(((tick - t0) / 10.0, best))
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('csv', nargs='?')
    ap.add_argument('--run', type=int, default=-1)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'primer_trace.png'))
    a = ap.parse_args(argv)
    path = a.csv
    if not path:
        import gamedir
        path = os.path.join(gamedir.find_or_dev(), 'Mods', 'Plugins', 'EDF6VehicleCrew.primer.csv')
    with open(path, newline='', encoding='utf-8') as h:
        all_rows = list(csv.DictReader(h))
    if not all_rows:
        raise SystemExit(f'{path}: 没有记录（ini 里 PrimerTrace=1 了吗？）')
    rs = runs(all_rows)
    rows = rs[a.run]
    t0 = int(rows[0]['ms'])
    print(f'{path}: {len(rs)} run(s); run {a.run}: {len(rows)} lines, {(int(rows[-1]["ms"]) - t0) / 1000:.1f} s')

    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    tracks: dict[int, list[dict]] = defaultdict(list)
    for r in rows:
        tracks[int(r['id'])].append(r)
    fig = plt.figure(figsize=(16, 9))
    top = fig.add_subplot(1, 2, 1)
    alt = fig.add_subplot(2, 2, 2)
    chn = fig.add_subplot(2, 2, 4)
    greens = plt.get_cmap('Greens')
    purples = plt.get_cmap('Purples')
    player = [(float(r['px']), float(r['pz']), (int(r['ms']) - t0) / 1000, float(r['py'])) for r in rows]
    top.plot([p[0] for p in player], [p[1] for p in player], color='black', lw=2, label='player')
    alt.plot([p[2] for p in player], [p[3] for p in player], color='black', lw=2, label='player')
    for k, (i, tr) in enumerate(sorted(tracks.items())):
        kind = tr[0]['kind']
        col = (greens if kind == 'centipede' else purples)(0.45 + 0.5 * ((k * 0.37) % 1.0))
        xs, zs = [float(r['x']) for r in tr], [float(r['z']) for r in tr]
        top.plot(xs, zs, color=col, lw=1)
        top.annotate(f'{kind[0]}{i}', (xs[0], zs[0]), fontsize=7, color=col)
        fx = [(float(r['x']), float(r['z'])) for r in tr if r['fire'] == '1']
        if fx:
            top.scatter([p[0] for p in fx], [p[1] for p in fx], s=4, color='red', zorder=3)
        links = [(float(b['x']), float(b['z'])) for a_, b in zip(tr, tr[1:]) if a_['ahead'] == '-1' and b['ahead'] != '-1']
        if links:
            top.scatter([p[0] for p in links], [p[1] for p in links], s=90, marker='*', color='gold', edgecolor='k', zorder=4)
        alt.plot([(int(r['ms']) - t0) / 1000 for r in tr], [float(r['y']) for r in tr], color=col, lw=1)
    top.set_aspect('equal', adjustable='datalim')
    top.set_title('top view (x, z): tracks, shots (red), links (stars)')
    alt.set_title('height (y) over time [s]')
    ch = chains(rows)
    if ch:
        chn.step([c[0] for c in ch], [c[1] for c in ch], where='post', color='green')
    chn.set_title('longest centipede chain over time [s]')
    fig.tight_layout()
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    fig.savefig(a.out, dpi=90)
    print(f'wrote {a.out}')

    for i, tr in sorted(tracks.items()):
        whats = Counter(r['what'] for r in tr)
        shots = sum(r['fire'] == '1' for r in tr)
        span = (int(tr[-1]['ms']) - int(tr[0]['ms'])) / 1000
        doing = ', '.join(f'{w} {n / 10:.1f}s' for w, n in whats.most_common())
        print(f'  {tr[0]["kind"]} {i}: {span:.1f} s seen, hp {tr[0]["hp"]} -> {tr[-1]["hp"]}, firing {shots / 10:.1f}s; {doing}')
    if ch:
        print(f'  longest chain: {max(c[1] for c in ch)}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
