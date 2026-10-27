"""Generate measured optic bones and redirect known stock SGOs transactionally.

The stock tail pass also supplies a missing native V607 aim declaration from its
identical official AI variants. Other-mod models are never assigned guessed data.
Build is read-only; install/remove operate under their own write-ahead journal.
"""
from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import ledger
import model_lod
import modfiles
import rootcpk
import dsgo
import sgo
import vehicle_optics as optics

OWNER = 'optics'
MANIFEST = '.edf6vc_optics.json'
BACKUP = '.edf6vc_optics_backup'
redirect = optics.redirect
FOLDERS = ('OBJECT', 'WEAPON')   # where stock SGOs name a model of optics.MODELS (WEAPON: the gold weapons)
AI_REL = 'OBJECT/V607_ROBOTRUCK.SGO'
AI_KEY = 'v607_aim_info'
AI_DONORS = ('V607_ROBOTRUCK_AI.SGO', 'V607_ROBOTRUCK_F_AI.SGO',
             'V607_ROBOTRUCK_G_AI.SGO', 'V607_ROBOTRUCK_M_AI.SGO')
AI_CONTRACT = ('animation_model', 'car_aim_node', 'vehicle_weapon_setting')


def _fields(data: bytes) -> dict:
    if data[:4] == b'DSGO':
        root = dsgo.parse(data).root
        return {name: root.items[index] for index, name in root.names.items()}
    return sgo.read(data)[1]


def _same_fields(left: bytes, right: bytes) -> bool:
    if left[:4] == right[:4] == b'DSGO':
        return dsgo.parse(left).root == dsgo.parse(right).root
    return left[:4] == right[:4] and sgo.read(left) == sgo.read(right)


def _plain(value):
    if isinstance(value, dsgo.Node):
        return {name: _plain(value.items[index]) for index, name in value.names.items()} if value.names else [
            _plain(item) for item in value.items]
    if isinstance(value, list):
        return [_plain(item) for item in value]
    if isinstance(value, sgo.Float):
        return value.value
    if isinstance(value, dsgo.Blob):
        return value.data
    return value


def _field_digest(data: bytes) -> str | None:
    try:
        value = _plain(_fields(data)[AI_KEY])
        return modfiles.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode('utf-8'))
    except (KeyError, TypeError):
        return None  # a removed/custom raw declaration is not our written value


def _field(data: bytes, value=None) -> bytes:
    """Set the missing declaration, or remove only that field; preserve other nodes/blobs."""
    if data[:4] != b'DSGO':
        version, members = sgo.read(data)
        if value is None:
            members.pop(AI_KEY, None)
        else:
            members[AI_KEY] = copy.deepcopy(value)
        return sgo.write(version, members)
    doc = dsgo.parse(data)
    if value is None:
        index = next((i for i, name in doc.root.names.items() if name == AI_KEY), None)
        if index is None:
            return data
        doc.root.items.pop(index)
        doc.root.names = {i - (i > index): name for i, name in doc.root.names.items() if i != index}
    else:
        def node(item):
            return dsgo.Node([node(child) for child in item]) if isinstance(item, list) else item
        doc.root.set(AI_KEY, node(value))
    return dsgo.write(doc)


def _contract(fields: dict, stock_path: str) -> dict:
    private = 'app:/object/edf6vc_optic_v607_robotruck.mrab'
    def normalize(value):
        if isinstance(value, list):
            return [normalize(item) for item in value]
        return stock_path if isinstance(value, str) and value.lower() == private else value
    return {key: normalize(_plain(fields[key])) for key in AI_CONTRACT}


def _native_aim(game) -> tuple[dict, str, list]:
    player = game.read('OBJECT', 'V607_ROBOTRUCK.SGO')
    base = _fields(player)
    if base.get('xgs_scene_object_class') != 'Vehicle607_RoboTruck':
        raise ValueError('official V607 class changed')
    path = optics.model_path(player)
    contract = _contract(base, path)
    aim = None
    for name in AI_DONORS:
        donor = _fields(game.read('OBJECT', name))
        if (donor.get('xgs_scene_object_class') != base['xgs_scene_object_class'] or
                _contract(donor, path) != contract or AI_KEY not in donor):
            raise ValueError('official V607 AI model/mount contract changed: ' + name)
        declaration = _plain(donor[AI_KEY])
        if aim is not None and declaration != aim:
            raise ValueError('official V607 AI variants disagree: ' + name)
        aim = declaration
    return contract, path, aim


def _native_geometry(root: str, model: str, game) -> bool:
    """Prove the model actually loaded now, allowing texture-only archive changes."""
    spec = next(spec for spec in optics.MODELS if spec.stem == 'V607_ROBOTRUCK')
    if model.lower() == 'app:/object/' + optics.output(spec).lower():
        rel = 'OBJECT/' + optics.output(spec)
        led = ledger.Ledger(root)
        entry = _load(root)['files'].get(rel, {})
        current = modfiles.sha256_file(led.disk(rel))
        # Grafted geometry has a different digest. Only our unchanged generated
        # model, whose builder checked the source profile, inherits that proof.
        return (current is not None and current in (entry.get('written'), entry.get('pending')) and
                OWNER in led.owners(rel) and led.written_by(OWNER, rel))
    loose = Path(root, 'Mods', 'OBJECT', spec.stem + '.MRAB')
    try:
        arc = optics.rab_read(loose.read_bytes() if loose.is_file() else game.read('OBJECT', spec.stem + '.MRAB'))
        models = [item for item in arc.files if item.name.lower() == spec.stem.lower() + '.mdb']
        return len(models) == 1 and modfiles.sha256(models[0].data) == spec.digest
    except (AssertionError, ValueError, IndexError, KeyError, struct.error):
        return False  # unknown/corrupt geometry cannot acquire a native aim table


def _prepare_stock(root: str, rel: str, source: bytes, entry=None, game=None):
    if rel == AI_REL and set(ledger.Ledger(root).owners(rel)) - {OWNER, ledger.LEGACY} and not entry:
        return source, (), None, False  # another registered writer owns this stock SGO
    made, needs = redirect(source)
    claim = (entry or {}).get('npc_ai_added')
    if rel != AI_REL:
        return made, needs, None, bool(needs)
    members = _fields(made)
    if AI_KEY in members and not claim:
        return made, needs, None, bool(needs)  # a user's declaration is never ours
    model = optics.model_path(made)
    if (members.get('xgs_scene_object_class') != 'Vehicle607_RoboTruck' or not model or
            model.lower() not in ('app:/object/v607_robotruck.mrab', 'app:/object/edf6vc_optic_v607_robotruck.mrab')):
        return made, needs, None, bool(needs) and not claim
    game = game or rootcpk.Game(root)
    contract, path, aim = _native_aim(game)
    try:
        matches = _contract(members, path) == contract
    except KeyError:
        matches = False
    if not matches or not _native_geometry(root, model, game):
        # Withdraw a stale write-set through the normal field-aware undo path.
        return made, needs, None, bool(needs) and not claim
    if claim:
        return made, needs, None, True  # retain an AI-only claim; never undo a user's deletion/edit
    made = _field(made, aim)
    return made, needs, _field_digest(made), True


def _build_one(root: str, spec: optics.Model) -> bytes:
    return optics.build_model(rootcpk.Game(root), spec)


def build_models(root: str) -> dict[str, bytes]:
    from model_workers import run_jobs
    models = [s for s in optics.MODELS if optics.ready(s)]
    jobs = [(_build_one, (root, spec), f'{index}/{len(models)} {spec.stem}')
            for index, spec in enumerate(models, 1)]
    return {optics.rel(spec): data for spec, data in zip(models, run_jobs(jobs))}


def build_stock_redirects(root: str) -> dict[str, bytes]:
    """Cheap current-SGO preparation, deliberately excluded from the model cache."""
    game = rootcpk.Game(root)
    state = _load(root)
    files = {}
    for folder in FOLDERS:
        # Outside OBJECT only a stock SGO that names one of that folder's models can be redirected (the gold
        # weapons): the bytes are searched before anything is parsed (thousands of weapon SGOs).
        needles = None if folder == 'OBJECT' else [
            n for spec in optics.MODELS if spec.folder == folder
            for n in (spec.stem.lower().encode(), spec.stem.lower().encode('utf-16le'))]
        for name in game.names(folder):
            if not name.upper().endswith('.SGO'):
                continue
            loose = Path(root, 'Mods', folder, name)
            source = loose.read_bytes() if loose.is_file() else game.read(folder, name)
            if needles is not None and not any(n in source.lower() for n in needles):
                continue
            rel = folder + '/' + name.upper()
            entry = state['files'].get(rel) if loose.is_file() else None
            made, needs, _added, keep = _prepare_stock(root, rel, source, entry, game)
            if keep:
                files[rel] = made
    return files


def build(root: str) -> dict[str, bytes]:
    # the models merged by material and given their LODs where a vehicle SGO here draws them (pylib/model_lod.py)
    return model_lod.finish({**build_models(root), **build_stock_redirects(root)})


def _load(root: str) -> dict:
    path = Path(root, 'Mods', MANIFEST)
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {'version': 1, 'files': {}}


def _save(root: str, state: dict) -> None:
    if not state['files']:
        Path(root, 'Mods', MANIFEST).unlink(missing_ok=True)
    else:
        modfiles.atomic_write(str(Path(root, 'Mods', MANIFEST)), json.dumps(state, sort_keys=True).encode('utf-8'))


def _backup(root: str, rel: str) -> str:
    return str(Path(root, 'Mods', BACKUP, *rel.split('/')))


def install(root: str, files: dict[str, bytes], scope: str = 'all') -> list[str]:
    led, state = ledger.Ledger(root), _load(root)
    outputs = {ledger.key(rel): data for rel, data in files.items()}
    if any(rel.split('/')[0] not in FOLDERS or '..' in rel.split('/') for rel in outputs):
        raise ValueError('optic outputs must be relative OBJECT / WEAPON paths')
    if scope not in ('all', 'models', 'stock') or any(
            (scope == 'models' and not r.endswith(('.MRAB', '.RAB'))) or (scope == 'stock' and not r.endswith('.SGO'))
            for r in outputs):
        raise ValueError('optic install scope does not match its outputs')
    paths, accepted = [], set()
    for rel in sorted(outputs, key=lambda r: (r.endswith('.SGO'), r)):
        data, path = outputs[rel], led.disk(rel)
        entry = state['files'].get(rel)
        if entry and entry.get('restoring'):
            _restore(root, led, state, rel)
            entry = state['files'].get(rel)
        current = modfiles.sha256_file(path)
        model = not rel.endswith('.SGO')
        if not entry and model and current is not None and current != modfiles.sha256(data):
            raise RuntimeError(f'瞄具资源路径已有不匹配模型，已保留：{rel}')
        changed = entry and current not in (entry.get('written'), entry.get('pending'), entry.get('original'))
        if changed and model:
            raise RuntimeError(f'瞄具模型被其他程序修改，已保留：{rel}')
        added = None
        if rel.endswith('.SGO'):
            # Mutable SGO writers run before this pass. Always patch their
            # CURRENT bytes, never an initial-install snapshot or cached SGO.
            source = modfiles.read(path)
            loose_source = source is not None
            if source is None and rel == AI_REL:
                source = rootcpk.Game(root).read('OBJECT', 'V607_ROBOTRUCK.SGO')
            data, needs, added, keep = _prepare_stock(root, rel, source if source is not None else data,
                                                     entry if loose_source else None)
            if not keep:
                continue
            if any(not os.path.isfile(led.disk(dep)) for dep in needs):
                raise ValueError('optic model must be installed before its consumers')
        if not entry:
            original = modfiles.read(path)
            if original is not None:
                modfiles.atomic_write(_backup(root, rel), original)
            entry = {'original': modfiles.sha256(original) if original is not None else None,
                     'ledger': copy.deepcopy(led.files.get(rel))}
            if rel.endswith('.SGO'):
                entry['source_model'] = optics.model_path(original) if original is not None else None
            state['files'][rel] = entry
        if changed and rel.endswith('.SGO'):
            entry['preserve_consumer'] = True  # an intervening writer supplied non-model state
        if added is not None:
            entry['npc_ai_added'] = added  # checkpoint the field write-set before writing the SGO
        entry['pending'] = modfiles.sha256(data)
        _save(root, state)
        paths.append(led.put(OWNER, rel, data))
        accepted.add(rel)
        entry['written'] = entry.pop('pending')
        _save(root, state)
    previous = {r for r in state['files'] if scope == 'all' or (r.endswith('.SGO') == (scope == 'stock'))}
    _remove(root, led, state, previous - accepted)
    return paths


def install_models(root: str, files: dict[str, bytes]) -> list[str]:
    return install(root, files, 'models')


def install_stock_redirects(root: str, files: dict[str, bytes]) -> list[str]:
    return install(root, files, 'stock')


def _restore(root: str, led: ledger.Ledger, state: dict, rel: str) -> bool:
    entry, path = state['files'][rel], led.disk(rel)
    if rel.endswith('.SGO'):
        return _restore_consumer(root, led, state, rel)
    if set(led.owners(rel)) - {OWNER, ledger.LEGACY}:
        return False
    if modfiles.sha256_file(path) not in {entry.get('written'), entry.get('pending'), entry.get('original')}:
        return False
    original = modfiles.read(_backup(root, rel)) if entry.get('original') else None
    if entry.get('original') and (original is None or modfiles.sha256(original) != entry['original']):
        raise ValueError('optic original backup missing or corrupt: ' + rel)
    entry['restoring'] = True
    _save(root, state)
    if original is None:
        Path(path).unlink(missing_ok=True)
    else:
        modfiles.atomic_write(path, original)
    if entry.get('ledger') is None:
        led.files.pop(rel, None)
    else:
        led.files[rel] = entry['ledger']
    led._save()
    del state['files'][rel]
    _save(root, state)
    return True


def _restore_consumer(root: str, led: ledger.Ledger, state: dict, rel: str) -> bool:
    """Remove our redirect, retaining all changes from later shared writers."""
    entry, path = state['files'][rel], led.disk(rel)
    current = modfiles.read(path)
    original = modfiles.read(_backup(root, rel)) if entry.get('original') else None
    if entry.get('original') and (original is None or modfiles.sha256(original) != entry['original']):
        raise ValueError('optic original backup missing or corrupt: ' + rel)
    owners = [o for o in led.owners(rel) if o != OWNER]
    unchanged = current is None or modfiles.sha256(current) in {entry.get('written'), entry.get('pending')}
    if current is None:
        target = original
    elif original is not None and redirect(original)[0] == current:
        target = original  # exact byte restoration when there are no other changes
    elif original is None and unchanged and not entry.get('preserve_consumer') and not owners:
        target = None
    else:
        target = optics.restore_path(current, entry.get('source_model'))
        if entry.get('npc_ai_added') and _field_digest(target) == entry['npc_ai_added']:
            target = _field(target)
        # Field-only preparation can restore the pre-install bytes exactly too.
        if entry.get('npc_ai_added') and original is not None and _same_fields(target, original):
            target = original
    entry['restoring'] = True
    _save(root, state)
    if target is None:
        Path(path).unlink(missing_ok=True)
    elif target != current:
        modfiles.atomic_write(path, target)
    # Retain the CURRENT owners, not an old snapshot that could resurrect a
    # removed writer. Restore a pre-existing legacy claim that our put removed.
    if target is not None and entry.get('ledger') and ledger.LEGACY in entry['ledger'].get('owners', []):
        if ledger.LEGACY not in owners:
            owners.append(ledger.LEGACY)
    if target is not None and owners:
        led.files[rel] = {'owners': owners, 'sha': modfiles.sha256(target)}
    else:
        led.files.pop(rel, None)
    led._save()
    del state['files'][rel]
    _save(root, state)
    return True


def _references(root: str) -> set[str]:
    kept = set()
    for folder in FOLDERS:
        for path in Path(root, 'Mods', folder).glob('*'):
            if path.suffix.upper() != '.SGO':
                continue
            raw = path.read_bytes().lower()
            for spec in optics.MODELS:
                needle = optics.app_path(spec, True)
                if needle.encode('utf-16le') in raw or needle.encode() in raw:
                    kept.add(optics.rel(spec))
    return kept


def _remove(root: str, led: ledger.Ledger, state: dict, rels: set[str]) -> tuple[list[str], list[str]]:
    restored, kept = [], []
    for rel in sorted(r for r in rels if r.endswith('.SGO')):
        (restored if _restore(root, led, state, rel) else kept).append(led.disk(rel))
    needed = _references(root)
    for rel in sorted(r for r in rels if not r.endswith('.SGO')):
        if rel in needed or not _restore(root, led, state, rel):
            kept.append(led.disk(rel))
        else:
            restored.append(led.disk(rel))
    return restored, kept


def remove(root: str) -> tuple[list[str], list[str]]:
    led, state = ledger.Ledger(root), _load(root)
    return _remove(root, led, state, set(state['files']))


def range_vehicle(led: ledger.Ledger, game, data: bytes, owner: str = 'testrange') -> tuple[bytes, tuple[str, ...]]:
    """Redirect a generated SGO and own its dependencies without altering stock SGOs."""
    made, needs = redirect(data)
    for rel in needs:
        spec = next(s for s in optics.MODELS if rel == optics.rel(s))
        if not os.path.isfile(led.disk(rel)):
            led.put(owner, rel, optics.build_model(game, spec))
        # A resource edited by somebody else cannot become a back door to using
        # coordinates from a different geometry. Full round-trip checks live in
        # the generator test; changed resources remain their owner's contract.
        if not led.owners(rel) or led.changed(rel):
            raise RuntimeError('瞄具模型被其他程序修改，已保留：' + rel)
        if not Path(led.disk(rel)).stat().st_size:
            raise ValueError('empty optic model')
        led.need(owner, rel)
    return made, needs


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    root = args[0] if args else rootcpk.DEFAULT_GAME
    if '--remove' in sys.argv:
        print(remove(root))
    else:
        for path in install(root, build(root)):
            print(path)
