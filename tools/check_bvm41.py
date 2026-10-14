"""Checks pylib/bvm41.py against the game files (read only; no game is started).

  python -B tools/check_bvm41.py [--edf41 DIR] [--edf5 DIR] [-q]

1. Every mission script in both EDF4.1's Root.cpk (MISSION/<id>/*.BVM) and EDF5's (MISSION/EDF4.1/<id>/*.BVM):
   to_edf5(4.1 file) against EDF5's file, IDENTICAL or DIFFERENT (first differing offset, hex around it); a total.
   parse + build of both files must give them back byte for byte as well.
2. The string rewrite hook: on a few pairs an app:/ string is made longer and shorter; the output must parse, read
   the new string back, and be EDF5's file again once the string is put back (everything else unchanged).
3. EDF4.1's DLC scripts (DLC/<hash>/dlc.cpk, <dlc>/MISSION/<id>/Mission.bvm): each converts, or is refused because
   it calls a native EDF5 / EDF6 has no counterpart for (bvm41.UnsupportedNative); per script, the calls no base-game
   pair shows, matched from the engines (bvm41.ENGINE_EVIDENCE) or NOT VERIFIED.
   Each converted script (and each EDF5 original) must balance its stack under the executor's native stack effects
   (bvm41.NET6) and pass each native arguments of the types its EDF6 handler reads (bvm41.SIG6, bvm41.type_check;
   a self-test swaps arguments and expects every swap reported).
4. The ids the plugin gives EDF6 (src/edf41natives.cpp): no BVM in EDF5's or EDF6's Root.cpk, and none of the
   repository's converted scripts but the DLC's own, calls a table-1 id through the jump-table entry it takes.
Exit status 1 when a pair differs, the rewrite check fails, a DLC script fails otherwise or an id clashes.
"""
from __future__ import annotations

import argparse
import collections
import glob
import os
import sys
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (os.path.join(HERE, '..', 'pylib'), HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)
import bvm41  # noqa: E402
import cpk  # noqa: E402
import crilayla  # noqa: E402
import gamedir  # noqa: E402
import rootcpk  # noqa: E402


def hex_context(data: bytes, at: int, span: int = 8) -> str:
    lo = max(0, at - span)
    return ' '.join(('[%02x]' if i == at else '%02x') % data[i] for i in range(lo, min(len(data), at + span + 1)))


def first_difference(a: bytes, b: bytes) -> int | None:
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    return None if len(a) == len(b) else min(len(a), len(b))


def pairs(edf41: str, edf5: str) -> list[tuple[str, str, bytes, bytes]]:
    """(mission id, file name, the 4.1 file, EDF5's) for every script both games have."""
    g4, g5 = rootcpk.Game(edf41), rootcpk.Game(edf5)
    five = {(d.upper(), n.upper()): (d, n) for d, n in g5.cpk.index}
    out = []
    for d, n in sorted(g4.cpk.index):
        parts = d.split('/')
        if len(parts) != 2 or parts[0].upper() != 'MISSION' or not n.upper().endswith('.BVM'):
            continue
        hit = five.get((f'MISSION/EDF4.1/{parts[1]}'.upper(), n.upper()))
        if hit:
            out.append((parts[1], n.upper(), g4.read(d, n), g5.read(*hit)))
    return out


def dlc_scripts(edf41: str) -> list[tuple[str, bytes]]:
    """(dlc.cpk folder / path inside, data) of every mission script in EDF4.1's DLC archives."""
    out = []
    for path in sorted(glob.glob(os.path.join(edf41, 'DLC', '*', 'dlc.cpk'))):
        c = cpk.Cpk(path)
        tag = os.path.basename(os.path.dirname(path))[:8]
        for (d, n), e in sorted(c.index.items()):
            if not n.lower().endswith('.bvm'):
                continue
            with open(c.path, 'rb') as h:
                h.seek(c.base + int(e['FileOffset']))
                data = h.read(int(e['FileSize']))
            if int(e['ExtractSize']) != int(e['FileSize']):
                data = crilayla.decompress(data)
            out.append((f'{tag}/{d}/{n}', data))
    return out


def inventory(prog: bvm41.Program) -> collections.Counter:
    """What a 4.1 script's own code (Main on, and the initialiser) uses: ops and natives (table, id)."""
    names = [f.name for f in prog.funcs]
    code = list(prog.init)
    for f in prog.funcs[names.index('Main'):]:
        code += f.code
    out: collections.Counter = collections.Counter()
    for x in code:
        out[('op', x.op)] += 1
        if x.op in bvm41.OP_NATIVE:
            out[('native', x.op, x.v)] += 1
    return out


unbalanced = [0]


def check_pairs(items: list[tuple[str, str, bytes, bytes]], quiet: bool) -> int:
    same = 0
    for mid, name, a, b in items:
        for label, data in (('4.1', a), ('EDF5', b)):
            if bvm41.build(bvm41.parse(data)) != data:
                print(f'{mid}/{name}: parse + build does not give the {label} file back')
        for problem in bvm41.stack_check(bvm41.parse(b))[:3]:
            print(f'{mid}/{name}: EDF5 file unbalanced under NET6: {problem}')
            unbalanced[0] += 1
        for problem in bvm41.type_check(bvm41.parse(b))[:3]:
            print(f'{mid}/{name}: EDF5 file argument types against SIG6: {problem}')
            unbalanced[0] += 1
        out = bvm41.to_edf5(a)
        at = first_difference(out, b)
        if at is None:
            same += 1
            if not quiet:
                print(f'{mid}/{name}: IDENTICAL')
            continue
        print(f'{mid}/{name}: DIFFERENT at {at:#x} (sizes {len(out)} / {len(b)})')
        print(f'    ours: {hex_context(out, at)}')
        print(f'    EDF5: {hex_context(b, at)}')
    print(f'pairs: {same} of {len(items)} IDENTICAL; problems in EDF5 files under bvm41.NET6 / SIG6: {unbalanced[0]}')
    return len(items) - same


def literal_strings(prog: bvm41.Program) -> list[str]:
    return [x.v for f in prog.funcs for x in f.code if x.op == bvm41.OP_PUSHS]


def check_rewrite(items: list[tuple[str, str, bytes, bytes]], count: int = 6) -> int:
    """The rewrite hook on `count` pairs that name an app:/ path: longer and shorter."""
    bad = tried = 0
    for mid, name, a, b in items:
        if tried >= count:
            break
        old = next((s for s in literal_strings(bvm41.parse(a)) if s.lower().startswith('app:/object/')), None)
        if old is None:
            continue
        tried += 1
        for new in (old.replace('app:/', 'app:/very/much/longer/path/', 1), 'app:/x'):
            def there(s: str, old: str = old, new: str = new) -> str:
                return new if s == old else s

            def back(s: str, old: str = old, new: str = new) -> str:
                return old if s == new else s

            out = bvm41.to_edf5(a, there)
            prog = bvm41.parse(out)
            got = literal_strings(prog)
            problems = []
            if new not in got or old in got:
                problems.append('the string does not read back changed')
            restored = rewrite_program(prog, back)
            if bvm41.build(restored) != b:
                problems.append("with the string put back it is not EDF5's file")
            if problems:
                bad += 1
            print(f'rewrite {mid}/{name}: {old!r} -> {new!r} ({len(old)} -> {len(new)} chars, file {len(b)} -> '
                  f'{len(out)} bytes): ' + ('; '.join(problems) if problems else 'ok'))
    print(f'rewrite: {tried * 2 - bad} of {tried * 2} ok')
    return bad


def rewrite_program(prog: bvm41.Program, fn: Callable[[str], str]) -> bvm41.Program:
    for f in prog.funcs:
        for x in f.code:
            if x.op == bvm41.OP_PUSHS:
                x.v = fn(x.v)
    return prog


def native_name(key: tuple) -> str:
    return f'op {key[1]:#04x}' if key[0] == 'op' else f'native {key[1] - bvm41.OP_NAT0}/{key[2]}'


def check_dlc(scripts: list[tuple[str, bytes]], seen: collections.Counter) -> int:
    """Each DLC script: converted, refused (it calls what EDF5 / EDF6 cannot do) or failed; per script the calls the
    base-game pairs do not cover, as matched from the engines (bvm41.ENGINE_EVIDENCE) or still unverified."""
    failed = refused = 0
    unseen: dict[tuple, list[str]] = collections.defaultdict(list)
    prefixes: collections.Counter = collections.Counter()
    for where, data in scripts:
        mission = where.split('/')[-2]
        try:
            prog = bvm41.parse(data)
        except Exception as ex:      # report every script, then fail
            failed += 1
            print(f'DLC {where}: FAILED to parse: {ex}')
            continue
        for s in literal_strings(prog):
            if ':/' in s:
                prefixes[s.split(':/')[0] + ':/'] += 1
        new = [k for k in inventory(prog) if k not in seen and not (k[0] == 'op' and k[1] in bvm41.OP_NATIVE)]
        for k in new:
            unseen[k].append(mission)
        engine = [k for k in new if k[0] == 'native' and (k[1], k[2]) in bvm41.ENGINE_EVIDENCE]
        unverified = [k for k in new if k[0] == 'native' and k not in engine]
        try:
            out = bvm41.build(bvm41.convert(prog, None, []))
            problems = bvm41.stack_check(bvm41.parse(out)) + bvm41.type_check(bvm41.parse(out))
            if problems:
                failed += 1
                status = 'FAILED: ' + '; '.join(problems[:3])
            else:
                status = f'converted, stack balanced, argument types fit ({len(data)} -> {len(out)} bytes)'
        except bvm41.UnsupportedNative as ex:
            refused += 1
            status = f'REFUSED: {ex}'
        except Exception as ex:      # report every script, then fail
            failed += 1
            status = f'FAILED: {ex}'
        print(f'DLC {where}: {status}')
        if engine:
            print('    matched from the engines: ' + ', '.join(native_name(k) for k in engine))
        if unverified:
            print('    NOT VERIFIED: ' + ', '.join(native_name(k) for k in unverified))
    print(f'DLC: {len(scripts) - failed - refused} of {len(scripts)} converted, {refused} refused, {failed} failed')
    print('DLC constructs no base-game pair has:')
    for k, where in sorted(unseen.items()):
        how = ''
        if k[0] == 'native':
            ev = bvm41.ENGINE_EVIDENCE.get((k[1], k[2]))
            how = f' -- {ev}' if ev else ' -- NOT VERIFIED'
        else:
            how = ' -- a plain VM instruction, passed through'
        print(f'    {native_name(k)}: {len(where)} scripts ({", ".join(sorted(set(where)))}){how}')
    print('DLC path prefixes in string constants: ' + ', '.join(f'{p} {c}' for p, c in sorted(prefixes.items())))
    return failed


def check_type_checker(scripts: list[tuple[str, bytes]]) -> int:
    """The type check catches a call's arguments in the wrong order: in the converted 1/2001 expansions (CreateEnemy,
    dup, percentage, 3019) the object handle is replaced by a float; every 3019 call must then be reported."""
    for where, data in scripts:
        prog = bvm41.convert(bvm41.parse(data))
        sites = 0
        for f in prog.funcs:
            for i, x in enumerate(f.code):
                if x.op == bvm41.OP_NAT0 and x.v == bvm41.NAT_SCALE_DURABILITY and i >= 2 and f.code[i - 2].op == 0x03:
                    f.code[i - 2] = bvm41.Ins(bvm41.OP_PUSH, 0x3F800000, 4)
                    sites += 1
        if not sites:
            continue
        found = sum(1 for m in bvm41.type_check(prog) if f'native 0/{bvm41.NAT_SCALE_DURABILITY} ' in m)
        print(f'type check self-test ({where}): {sites} swapped 3019 calls, {found} reported')
        return 0 if found == sites else 1
    print('type check self-test: no 1/2001 expansion to try')
    return 1


def bvm_files(edf5: str, edf6: str | None) -> list[tuple[str, bytes]]:
    """Every BVM in EDF5's and EDF6's Root.cpk and the converted 4.1 scripts the repository carries (edf41port)."""
    out = []
    for game in [edf5] + ([edf6] if edf6 else []):
        g = rootcpk.Game(game)
        out += [(f'{os.path.basename(game)}:{d}/{n}', g.read(d, n)) for d, n in sorted(g.cpk.index)
                if n.upper().endswith('.BVM')]
    port = os.path.join(HERE, '..', 'edf41port')
    for root, _, names in os.walk(port):
        out += [(os.path.relpath(os.path.join(root, n), port), open(os.path.join(root, n), 'rb').read())
                for n in sorted(names) if n.upper().endswith('.BVM')]
    return out


def check_plugin_ids(files: list[tuple[str, bytes]]) -> int:
    """The ids the plugin claims (bvm41.TABLE1 'plugin'): its stub takes one exact id, every other id of that jump-table
    entry (table 1, id & 7) still ends in the executor's no-op; no stock script may call any id of the entry."""
    claimed = {(a[1], a[2]) for a in bvm41.TABLE1.values() if a[0] == 'plugin'}
    entries = {(op, nid & 7) for op, nid in claimed}
    bad = 0
    calls: collections.Counter = collections.Counter()
    for where, data in files:
        prog = bvm41.parse(data)
        for f in prog.funcs:
            for x in f.code:
                if x.op in bvm41.OP_NATIVE and (x.op, x.v & 7) in entries:
                    calls[(x.op, x.v)] += 1
                    if (x.op, x.v) not in claimed or not where.startswith(('mp0', 'missions')):
                        bad += 1
                        print(f'plugin id check: {where} calls {x.op - bvm41.OP_NAT0}/{x.v}')
    print(f'plugin ids {sorted((op - bvm41.OP_NAT0, nid) for op, nid in claimed)}: {len(files)} scripts, '
          f'calls through their jump-table entries {dict(calls) or "none"}, {bad} outside the converted DLC')
    return bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--edf41', default=gamedir.find_other(gamedir.EDF41))
    ap.add_argument('--edf5', default=gamedir.find_other(gamedir.EDF5))
    ap.add_argument('--edf6', default=gamedir.find())
    ap.add_argument('-q', '--quiet', action='store_true', help='list only the pairs that differ')
    args = ap.parse_args()
    if not args.edf41 or not args.edf5:
        ap.error('EDF4.1 / EDF5 not found: pass --edf41 / --edf5')
    items = pairs(args.edf41, args.edf5)
    differ = check_pairs(items, args.quiet)
    bad_rewrite = check_rewrite(items)
    seen: collections.Counter = collections.Counter()
    for _, _, a, _ in items:
        seen.update(inventory(bvm41.parse(a)))
    failed = check_dlc(dlc_scripts(args.edf41), seen)
    clash = check_plugin_ids(bvm_files(args.edf5, args.edf6))
    blind = check_type_checker(dlc_scripts(args.edf41))
    return 1 if differ or bad_rewrite or failed or clash or unbalanced[0] or blind else 0


if __name__ == '__main__':
    sys.exit(main())
