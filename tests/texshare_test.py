"""pylib/texshare.py without the game: merging archives keeps one copy of each texture, renames what clashes, rewrites
the models' filenames, keeps TEXTURE -> MODEL -> HD-TEXTURE; SGOs are redirected with their LOD tables; and the ledger
(pylib/ledger.py) moves from the separate archives to the merged one on an incremental install, keeps a file another
owner still needs, and deletes everything on uninstall. Exit code 1 on a failure."""
from __future__ import annotations

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import ledger  # noqa: E402
import sgo  # noqa: E402
import texshare  # noqa: E402
from mdb import Mdb, Rab, RabFile, Texture, folder_order_ok, insert_member, mdb_read, mdb_write, rab_read, rab_write  # noqa: E402

failures = 0


def expect(ok: bool, what: str) -> None:
    global failures
    print(('ok    ' if ok else 'FAIL  ') + what)
    failures += 0 if ok else 1


def raises(fn, what: str) -> None:  # noqa: ANN001
    try:
        fn()
    except texshare.TexShareError as e:
        expect(True, f'{what} ({e})')
    else:
        expect(False, what)


def model(*textures: str) -> bytes:
    return mdb_write(Mdb(0x20, ['mdl'], [], [], [], [Texture(k, t.replace('.', '_'), t, 0) for k, t in enumerate(textures)]))


def archive(textures: dict[str, bytes], models: dict[str, bytes]) -> bytes:
    """An archive in the stock layout: each texture a lod (TEXTURE) and an HD (HD-TEXTURE) member, then `models`."""
    rab = Rab(0x200, list(texshare.FOLDERS), [])
    for name, data in models.items():
        insert_member(rab, RabFile(name, texshare.MODEL, 0, data))
    for name, data in textures.items():
        insert_member(rab, RabFile(name, texshare.HD, 1, data + b'-hd'))
        insert_member(rab, RabFile(name.replace('.dds', '.lod.dds'), texshare.TEXTURE, 0, data + b'-lod'))
    assert folder_order_ok(rab)
    return rab_write(rab)


def textures_of(rab: Rab, name: str) -> list[str]:
    return [t.filename for t in mdb_read(next(f for f in rab.files if f.name == name).data).textures]


def vehicle(arc: str, mdb: str, lods: list[str]) -> bytes:
    return sgo.write(2, {'xgs_scene_object_class': 'Vehicle506_Helicopter',
                         'animation_model': [[f'app:/object/{arc.lower()}', mdb, [[n, 100.0 * (k + 1)] for k, n in enumerate(lods)]],
                                             'app:/object/v506_heli.cas']})


A = archive({'body.dds': b'BODY', 'a_paint.dds': b'PAINT-A', 'glass.dds': b'GLASS'},
            {'a.mdb': model('body.dds', 'a_paint.dds', 'glass.dds'), 'a_lod1.mdb': model('body.dds', 'a_paint.dds')})
# body under another name, its own paint, glass of other content under the same name
B = archive({'edf6vc_b_body.dds': b'BODY', 'b_paint.dds': b'PAINT-B', 'glass.dds': b'GLASS-B'},
            {'b.mdb': model('edf6vc_b_body.dds', 'b_paint.dds', 'glass.dds'), 'b_lod1.mdb': model('edf6vc_b_body.dds')})


def merging() -> None:
    m = rab_read(texshare.merge([('EDF6VC_A.MRAB', A), ('EDF6VC_B.MRAB', B)]))
    folders = [f.folder for f in m.files]
    expect(folders == sorted(folders) and m.folders == texshare.FOLDERS, 'members TEXTURE -> MODEL -> HD-TEXTURE')
    names = sorted(f.name for f in m.files if f.folder == texshare.HD)
    expect(names == ['a_paint.dds', 'b_paint.dds', 'body.dds', 'edf6vc_b_glass.dds', 'glass.dds'],
           f'one HD texture per content, a clashing name prefixed: {names}')
    lods = sorted(texshare.tex_key(f.name) for f in m.files if f.folder == texshare.TEXTURE)
    expect(lods == names, 'each HD texture its lod')
    expect([f.name for f in m.files if f.folder == texshare.MODEL] == ['a.mdb', 'a_lod1.mdb', 'b.mdb', 'b_lod1.mdb'],
           'every model of every source, in order')
    expect(next(f for f in m.files if f.name == 'a.mdb').stored == next(f for f in rab_read(A).files if f.name == 'a.mdb').stored,
           "the first source's models untouched")
    expect(textures_of(m, 'b.mdb') == ['body.dds', 'b_paint.dds', 'edf6vc_b_glass.dds'], f"b.mdb's filenames: {textures_of(m, 'b.mdb')}")
    expect(textures_of(m, 'b_lod1.mdb') == ['body.dds'], "its LOD's too")
    total, dup, groups = texshare.duplicates({'A': A, 'B': B})
    expect(dup == len(b'BODY-hd') + len(b'BODY-lod') and groups[0][2] == ('A', 'B'), f'duplicates: {dup} bytes {groups}')
    apart, merged = texshare.merged_size({'A': A, 'B': B})
    expect(apart - merged == dup, f'merged size: {apart} -> {merged}')
    raises(lambda: texshare.merge([('EDF6VC_A.MRAB', A), ('EDF6VC_A2.MRAB', A)]), 'a model name in two sources refused')
    stray = archive({'x.dds': b'X'}, {'c.mdb': model('x.dds', 'glass.dds')})
    raises(lambda: texshare.merge([('EDF6VC_A.MRAB', A), ('EDF6VC_C.MRAB', stray)]),
           "a filename bound to nothing at home never binds another source's texture")


def applying() -> dict[str, bytes]:
    texshare.SHARED['EDF6VC_AB.MRAB'] = ('EDF6VC_A.MRAB', 'EDF6VC_B.MRAB')
    texshare.SHARED_IN.update({'EDF6VC_A.MRAB': 'EDF6VC_AB.MRAB', 'EDF6VC_B.MRAB': 'EDF6VC_AB.MRAB'})
    files = {'OBJECT/EDF6VC_A.MRAB': A, 'OBJECT/EDF6VC_B.MRAB': B,
             'OBJECT/EDF6VC_A.SGO': vehicle('EDF6VC_A.MRAB', 'a.mdb', ['a_lod1.mdb']),
             'OBJECT/EDF6VC_B.SGO': vehicle('EDF6VC_B.MRAB', 'b.mdb', ['b_lod1.mdb']),
             'OBJECT/OTHER.SGO': vehicle('V506_HELI.MRAB', 'v506_heli.mdb', [])}
    out = texshare.apply(files, 'EDF6VC_AB.MRAB')
    expect(sorted(out) == ['OBJECT/EDF6VC_A.SGO', 'OBJECT/EDF6VC_AB.MRAB', 'OBJECT/EDF6VC_B.SGO', 'OBJECT/OTHER.SGO'],
           f'the sources replaced by the target: {sorted(out)}')
    b = sgo.read(out['OBJECT/EDF6VC_B.SGO'])[1]['animation_model'][0]
    expect(b[0] == 'app:/object/edf6vc_ab.mrab' and b[1] == 'b.mdb' and b[2][0][0] == 'b_lod1.mdb',
           f'an SGO redirected, its model and LOD table kept: {b}')
    expect(out['OBJECT/OTHER.SGO'] == files['OBJECT/OTHER.SGO'], 'an SGO on another archive untouched')
    expect(texshare.names(['OBJECT/EDF6VC_A.MRAB', 'OBJECT/X.SGO', 'OBJECT/EDF6VC_B.MRAB']) == ['OBJECT/EDF6VC_AB.MRAB', 'OBJECT/X.SGO'],
           'names: the sources one target')
    bad = dict(out)
    bad['OBJECT/STALE.SGO'] = vehicle('EDF6VC_A.MRAB', 'a.mdb', [])
    bad['OBJECT/WRONG.SGO'] = vehicle('EDF6VC_AB.MRAB', 'zz.mdb', [])
    got = texshare.problems(bad)
    expect(len(got) == 2 and 'STALE' in got[0] and 'WRONG' in got[1], f'problems: a stale and a wrong SGO: {got}')
    expect(texshare.installed('EDF6VC_A.MRAB', lambda f: f == 'EDF6VC_AB.MRAB') == 'EDF6VC_AB.MRAB'
           and texshare.installed('EDF6VC_A.MRAB', lambda f: False) == 'EDF6VC_A.MRAB', 'installed: the target when there')
    return files


def install(root: str, owner: str, files: dict[str, bytes]) -> None:
    """tools/make_jets.py install: put every file, release what this owner wrote before and not now."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(owner))
    for rel, data in files.items():
        led.put(owner, rel, data)
    led.release(owner, sorted(before - {ledger.key(rel) for rel in files}))


def range_install(root: str, sources: list[str]) -> None:
    """testrange/gen.py _write_derived for vehicles on `sources`: needs the archive each is in on disk (installed),
    writes its SGO redirected (moved), releases what it needed before and not now."""
    led = ledger.Ledger(root)
    before = set(led.owned_by('testrange'))
    exists = lambda f: os.path.isfile(led.disk(f'OBJECT/{f}'))  # noqa: E731
    held = set()
    for src in sources:
        rel = f'OBJECT/{texshare.installed(src, exists)}'
        led.need('testrange', rel)
        held.add(ledger.key(rel))
        sgo_rel = f'OBJECT/EDF6TR_{src.removeprefix("EDF6VC_").removesuffix(".MRAB")}.SGO'
        mdb = src.removeprefix('EDF6VC_').removesuffix('.MRAB').lower() + '.mdb'
        led.put('testrange', sgo_rel, texshare.redirect(vehicle(src, mdb, []), texshare.moved(exists)))
        held.add(ledger.key(sgo_rel))
    led.release('testrange', sorted(before - held))


def on_disk(root: str) -> set[str]:
    d = os.path.join(root, 'Mods', 'OBJECT')
    return set(os.listdir(d)) if os.path.isdir(d) else set()


def ledger_moves(files: dict[str, bytes]) -> None:
    with tempfile.TemporaryDirectory() as root:
        old = {k: v for k, v in files.items() if k != 'OBJECT/OTHER.SGO'}
        install(root, 'jets', old)
        range_install(root, ['EDF6VC_A.MRAB'])
        expect(ledger.Ledger(root).owners('OBJECT/EDF6VC_A.MRAB') == ['jets', 'testrange'], 'before: A the jets\' and the range\'s')
        install(root, 'jets', texshare.apply(old, 'EDF6VC_AB.MRAB'))   # the incremental install
        led = ledger.Ledger(root)
        expect('EDF6VC_AB.MRAB' in on_disk(root) and 'EDF6VC_B.MRAB' not in on_disk(root),
               'incremental install: the target written, a source nobody else needs deleted')
        expect('EDF6VC_A.MRAB' in on_disk(root) and led.owners('OBJECT/EDF6VC_A.MRAB') == ['testrange'],
               'a source the range still needs kept, the range its only owner')
        a = sgo.read(open(led.disk('OBJECT/EDF6VC_A.SGO'), 'rb').read())[1]['animation_model'][0][0]
        expect(a == 'app:/object/edf6vc_ab.mrab', f"the jets' SGO on the target: {a}")
        range_install(root, ['EDF6VC_A.MRAB'])
        led = ledger.Ledger(root)
        tr = sgo.read(open(led.disk('OBJECT/EDF6TR_A.SGO'), 'rb').read())[1]['animation_model'][0][0]
        expect(tr == 'app:/object/edf6vc_ab.mrab' and 'EDF6VC_A.MRAB' not in on_disk(root),
               f"the range again: its SGO on the target ({tr}), the old source released and gone")
        expect(led.owners('OBJECT/EDF6VC_AB.MRAB') == ['jets', 'testrange'], 'the target the jets\' and the range\'s')
        led.release('jets', sorted(set(led.owned_by('jets'))), writer=True)   # make_jets.py --remove
        expect('EDF6VC_AB.MRAB' in on_disk(root) and ledger.Ledger(root).owners('OBJECT/EDF6VC_AB.MRAB') == ['testrange'],
               'uninstalling the jets keeps the target the range needs')
        ledger.Ledger(root).release('testrange', sorted(set(ledger.Ledger(root).owned_by('testrange'))), writer=True)
        expect(on_disk(root) == set(), f'uninstalling both leaves nothing: {sorted(on_disk(root))}')
        expect(not os.path.isfile(os.path.join(root, 'Mods', ledger.MANIFEST)), 'and an empty ledger')


def main() -> int:
    merging()
    ledger_moves(applying())
    print(f'{failures} failure(s)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
