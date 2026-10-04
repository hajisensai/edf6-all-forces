"""Self-test of the install chain, without the game (CI runs it; python tools/selftest.py, exit 1 on a failure):
  - tools/calls.py is consistent and src/calls.inc is what tools/gen_calls.py writes from it;
  - the call order rules: every released order is a prefix of CALLS, and installs of every order ever shipped
    (withdrawn ones too), with other mods' rows after ours and with uninstall placeholders, keep every row
    where it is (call_weapons.plan_rows / tail_start);
  - the copies kept by hand elsewhere agree with tools/calls.py and pylib/vcobjects.py (the player jets' marks
    in src/playerjet.cpp, the jets' marks in src/jet.cpp, the jet files in src/jet.cpp and tools/make_jets.py,
    the enum names in src/crew.h, the counts in README.md);
  - the weapon table transaction (call_weapons.commit / recover) writes all or nothing, and rolls back a run
    that died half way;
  - the ownership ledger (pylib/ledger.py) deletes a file only when nobody needs it, and never one someone
    else changed;
  - the installer's ini merge (installer.merge_ini) only adds settings and changes nothing of the player's.
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import tempfile
import traceback
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, HERE)
import call_weapons as cw  # noqa: E402
import calls  # noqa: E402
import gen_calls  # noqa: E402
import installer  # noqa: E402
import ledger  # noqa: E402
import make_jets  # noqa: E402
import modfiles  # noqa: E402
import vcobjects as vc  # noqa: E402

TESTS: list[Callable[[], None]] = []


def test(fn: Callable[[], None]) -> Callable[[], None]:
    TESTS.append(fn)
    return fn


def src(rel: str) -> str:
    with open(os.path.join(ROOT, *rel.split('/')), encoding='utf-8') as f:
        return f.read()


# ---------------------------------------------------------------- the data


@test
def calls_table_consistent() -> None:
    ids = [c.id for c in calls.CALLS]
    assert len(set(ids)) == len(ids), 'duplicate ids'
    assert all(i.startswith(calls.ID_PREFIX) for i in ids)
    marks = [c.mark for c in calls.CALLS]
    assert len(set(marks)) == len(marks), 'duplicate marks'
    crew_h = src('src/crew.h')
    roles = set(re.search(r'enum class JetRole \{([^}]*)\}', crew_h).group(1).replace(' ', '').split(','))
    bodies = set(re.search(r'enum class HeliBody \{([^}]*)\}', crew_h).group(1).replace(' ', '').split(','))
    for c in calls.CALLS:
        assert c.kind in calls.KINDS, c.id
        assert c.brings in ('jets', 'helis', 'sub', 'vehicle'), c.id
        assert bool(c.role) == (c.brings == 'jets') and (not c.role or c.role in roles), c.id
        assert bool(c.body) == (c.brings == 'helis') and (not c.body or c.body in bodies), c.id
        assert bool(c.vehicle) == bool(c.jet) == (c.brings == 'vehicle'), c.id
        assert (c.count > 0) == c.flown and bool(c.log) == c.flown, c.id
        for lang in cw.LANGS:
            assert calls.call_name(c, lang) and calls.call_description(c, lang)
            assert calls.retired_name(c, lang) != calls.call_name(c, lang)
        assert calls.slot_of(c.id) == c.id and calls.slot_of(calls.retired_id(c.id)) == c.id
        assert not calls.retired_id(c.id).startswith(calls.ID_PREFIX)   # GrantCalls never owns a placeholder
    names = [calls.call_name(c, 'SC') for c in calls.FLOWN]
    assert len(set(names)) == len(names), 'two calls with one banner label'


@test
def calls_inc_current() -> None:
    with open(gen_calls.OUT, encoding='utf-8', newline='') as f:
        assert f.read().replace('\r\n', '\n') == gen_calls.render(), 'src/calls.inc is stale: python tools/gen_calls.py'
    assert '#include "calls.inc"' in src('src/airstrike.cpp')


@test
def order_append_only() -> None:
    for name, order in calls.RELEASED.items():
        assert calls.IDS[:len(order)] == order, f'{name} is no longer a prefix of CALLS: calls only go at the end'
    last = max(len(o) for o in calls.RELEASED.values())
    assert last == len(calls.IDS), ('CALLS has calls no RELEASED order lists: add the order this release '
                                    'installs to tools/calls.py RELEASED')
    for name, order in calls.WITHDRAWN.items():
        assert set(order) <= set(calls.IDS), f'{name}: a call was removed (retire it instead)'


# ---------------------------------------------------------------- where rows go


STOCK = [f'eWeapon{i:03d}' for i in range(400)]
OTHER = ['OTHERMOD_A', 'OTHERMOD_B']


def _installs() -> dict[str, list[str]]:
    shipped = {**calls.RELEASED, **calls.WITHDRAWN}
    return {name: STOCK + list(order) for name, order in shipped.items()}


@test
def install_keeps_rows() -> None:
    for name, ids in _installs().items():
        for after in ([], OTHER):
            table = ids + after
            plan = cw.plan_rows(table)
            for i, x in enumerate(table):
                if calls.slot_of(x):
                    assert plan.at[x] == i, f'{name}: {x} moved'
            assert plan.appended == [c for c in calls.IDS if c not in table], name
            assert sorted(plan.at[c] for c in plan.appended) == list(range(len(table), len(table) + len(plan.appended)))
    fresh = cw.plan_rows(STOCK)
    assert [fresh.at[c] for c in calls.IDS] == list(range(len(STOCK), len(STOCK) + len(calls.IDS)))


@test
def placeholders_keep_rows() -> None:
    table = STOCK + [calls.retired_id(x) for x in calls.IDS[:19]] + OTHER
    plan = cw.plan_rows(table)
    for i, x in enumerate(calls.IDS[:19]):
        assert plan.at[x] == len(STOCK) + i
    assert cw.tail_start(table) == len(table)                      # another mod's rows end it: nothing to delete
    assert cw.tail_start(STOCK + list(calls.IDS)) == len(STOCK)    # ours end it: all of them can go
    mixed = STOCK + list(calls.IDS[:5]) + OTHER + list(calls.IDS[5:])
    assert cw.tail_start(mixed) == len(STOCK) + 5 + len(OTHER)
    try:
        cw.plan_rows(STOCK + [calls.IDS[0], calls.retired_id(calls.IDS[0])])
    except ValueError:
        pass
    else:
        raise AssertionError('two rows for one call not refused')


# ---------------------------------------------------------------- copies kept by hand


@test
def hand_copies_agree() -> None:
    pjet = dict(re.findall(r'\{"(\w+)",\s*(\d+)\.0f,', src('src/playerjet.cpp').split('kKinds[]={', 1)[1].split('};', 1)[0]))
    for c in calls.CALLS:
        if c.brings != 'vehicle':
            continue
        jet = vc.JETS[c.jet]
        assert jet.player and jet.mark == c.mark, c.id
        assert float(pjet[c.kind.removeprefix('pjet_')]) == c.mark, f'src/playerjet.cpp kKinds disagrees on {c.id}'
        assert make_jets.FILES[f'{c.vehicle}.SGO'] == c.jet, c.id
    jet_cpp = src('src/jet.cpp')
    kinds = jet_cpp.split('constexpr Kind kKinds[kRoleCount]={', 1)[1].split('};', 1)[0]
    marks = {float(m) for m in re.findall(r'\{"\w+",(\d+)\.0f,', kinds)}
    marks |= {float(m) for m in re.findall(r'\{(\d+)\.0f,Role::', jet_cpp.split('kCarrierMarks[]=', 1)[1].split(';', 1)[0])}
    npc = {name: j.mark for name, j in vc.JETS.items() if not j.player and 7001 <= j.mark <= 7099}
    for name, mark in npc.items():
        assert mark in marks, f'{name}: mark {mark} not in src/jet.cpp kKinds / kCarrierMarks'
    files = re.findall(r'L"(EDF6VC_[A-Z0-9_]+\.SGO)"', jet_cpp.split('kJetFile[kBodyCount]={', 1)[1].split('};', 1)[0])
    sgos = re.findall(r'L"app:/object/(edf6vc_[a-z0-9_]+\.sgo)"', jet_cpp.split('kJetSgo[kBodyCount]={', 1)[1].split('};', 1)[0])
    assert [f.lower() for f in files] == sgos, 'src/jet.cpp kJetFile and kJetSgo disagree'
    written = {n.split('/', 1)[1] for n in make_jets.names() if n.startswith('OBJECT/')}
    assert set(files) <= written, f'src/jet.cpp loads files tools/make_jets.py does not write: {set(files) - written}'


@test
def readme_counts() -> None:
    readme = src('README.md')
    assert f'{len(calls.FLOWN)} 种呼叫' in readme, f'README.md: say {len(calls.FLOWN)} 种呼叫 (tools/calls.py FLOWN)'
    assert f'共 {len(calls.CALLS)} 行' in readme, f'README.md: say 共 {len(calls.CALLS)} 行 (tools/calls.py CALLS)'


# ---------------------------------------------------------------- the transaction


def _mods(game: str, rel: str) -> str:
    return os.path.join(game, 'Mods', *rel.split('/'))


def _read(path: str) -> bytes | None:
    if not os.path.isfile(path):
        return None
    with open(path, 'rb') as f:
        return f.read()


@test
def commit_all_or_nothing() -> None:
    game = tempfile.mkdtemp(prefix='edf6vc-selftest-')
    try:
        before = {'WEAPON/A.SGO': b'a0', 'WEAPON/B.SGO': b'b0', 'WEAPON/D.SGO': b'd0'}
        for rel, data in before.items():
            modfiles.atomic_write(_mods(game, rel), data)
        changes: dict[str, bytes | None] = {'WEAPON/A.SGO': b'a1', 'WEAPON/B.SGO': b'b1', 'WEAPON/C.SGO': b'c1',
                                            'WEAPON/D.SGO': None}
        real = modfiles.atomic_write
        calls_made = []

        def failing(path: str, data: bytes) -> None:
            calls_made.append(path)
            if path.endswith('C.SGO'):
                raise OSError('disk full (test)')
            real(path, data)

        modfiles.atomic_write = failing
        try:
            cw.commit(game, changes)
        except OSError:
            pass
        else:
            raise AssertionError('the failing write did not fail')
        finally:
            modfiles.atomic_write = real
        for rel, data in before.items():
            assert _read(_mods(game, rel)) == data, f'{rel} not rolled back'
        assert _read(_mods(game, 'WEAPON/C.SGO')) is None
        assert not os.path.exists(_mods(game, cw.JOURNAL))
        # A run killed half way: the journal and its copies are left; the next run rolls it back.
        cw.commit(game, {'WEAPON/A.SGO': b'a2'})
        assert _read(_mods(game, 'WEAPON/A.SGO')) == b'a2'
        os.makedirs(_mods(game, f'{cw.BACKUP}/txn/WEAPON'))
        shutil.copy2(_mods(game, 'WEAPON/A.SGO'), _mods(game, f'{cw.BACKUP}/txn/WEAPON/A.SGO'))
        modfiles.atomic_write(_mods(game, cw.JOURNAL), b'{"WEAPON/A.SGO": true, "WEAPON/E.SGO": false}')
        modfiles.atomic_write(_mods(game, 'WEAPON/A.SGO'), b'half')
        modfiles.atomic_write(_mods(game, 'WEAPON/E.SGO'), b'half')
        assert cw.recover(game)
        assert _read(_mods(game, 'WEAPON/A.SGO')) == b'a2' and _read(_mods(game, 'WEAPON/E.SGO')) is None
        assert not cw.recover(game)
    finally:
        shutil.rmtree(game, ignore_errors=True)


# ---------------------------------------------------------------- the ledger


@test
def ledger_refcounts() -> None:
    game = tempfile.mkdtemp(prefix='edf6vc-selftest-')
    try:
        led = ledger.Ledger(game)
        led.put('jets', 'WEAPON/GUN.SGO', b'gun')
        led.put('sub', 'WEAPON/GUN.SGO', b'gun')
        led.put('jets', 'OBJECT/MODEL.MRAB', b'model')
        led.need('testrange', 'OBJECT/MODEL.MRAB')
        led.put('jets', 'OBJECT/MINE.SGO', b'mine')
        deleted, kept = ledger.Ledger(game).release('jets')
        assert [os.path.basename(p) for p in deleted] == ['MINE.SGO'], deleted
        assert os.path.isfile(_mods(game, 'WEAPON/GUN.SGO')) and os.path.isfile(_mods(game, 'OBJECT/MODEL.MRAB'))
        with open(_mods(game, 'OBJECT/MODEL.MRAB'), 'wb') as f:
            f.write(b'changed by someone')
        deleted, kept = ledger.Ledger(game).release('testrange')
        assert not deleted and [os.path.basename(p) for p in kept] == ['MODEL.MRAB']
        deleted, _ = ledger.Ledger(game).release('sub')
        assert [os.path.basename(p) for p in deleted] == ['GUN.SGO']
        assert not os.path.exists(os.path.join(game, 'Mods', ledger.MANIFEST)), 'empty ledger left behind'
        modfiles.atomic_write(_mods(game, 'OBJECT/OLD.SGO'), b'from before the ledger')
        led = ledger.Ledger(game)
        led.need('calls', 'OBJECT/OLD.SGO')
        deleted, _ = led.release('calls', ['OBJECT/OLD.SGO'])
        assert not deleted, 'a file from before the ledger deleted by a tool that only needed it'
        deleted, _ = ledger.Ledger(game).release('jets', ['OBJECT/OLD.SGO'], writer=True)
        assert deleted, 'a file from before the ledger is the one of its writer'
        modfiles.atomic_write(_mods(game, 'OBJECT/OLD2.SGO'), b'from before the ledger')
        deleted, _ = ledger.Ledger(game).release('testrange', ['OBJECT/OLD2.SGO'])
        assert not deleted, 'a file the ledger does not know deleted by a tool that does not write it'
        deleted, _ = ledger.Ledger(game).release('jets', ['OBJECT/OLD2.SGO'], writer=True)
        assert deleted
    finally:
        shutil.rmtree(game, ignore_errors=True)


# ---------------------------------------------------------------- the ini


@test
def ini_merge_only_adds() -> None:
    shipped = src('EDF6VehicleCrew.ini')
    user = '; mine\r\n[VehicleCrew]\r\nEnabled=0\r\n; my note\r\nHeliHeight=40\r\nHeliStandoff=60\r\n\r\n[Other]\r\nX=1\r\n'
    text, added, gone = installer.merge_ini(user, shipped)
    assert 'enabled' not in [a.lower() for a in added] and 'HeliHeight' not in added
    assert gone == ['HeliStandoff']
    assert text.startswith('; mine\r\n[VehicleCrew]\r\nEnabled=0\r\n; my note\r\nHeliHeight=40\r\nHeliStandoff=60\r\n')
    assert text.endswith('\r\n[Other]\r\nX=1\r\n') and '\r\n' in text and '\n' not in text.replace('\r\n', '')
    have = installer._keys(text.splitlines())
    assert set(installer._keys(shipped.splitlines())) <= set(have)
    assert installer.merge_ini(text, shipped)[1] == [], 'merge is not idempotent'
    fresh, added, _ = installer.merge_ini('', shipped)
    assert set(a.lower() for a in added) == set(installer._keys(shipped.splitlines()))


def main() -> int:
    failed = 0
    for fn in TESTS:
        try:
            fn()
            print(f'ok    {fn.__name__}')
        except Exception:
            failed += 1
            print(f'FAIL  {fn.__name__}')
            traceback.print_exc()
    print(f'{len(TESTS) - failed}/{len(TESTS)} passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
