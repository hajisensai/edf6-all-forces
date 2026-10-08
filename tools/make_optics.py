"""Generate measured optic bones and redirect known stock SGOs transactionally.

Only model paths change. Other-mod models are never assigned an invented optic.
Build is read-only; install/remove operate under their own write-ahead journal.
"""
from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import ledger
import modfiles
import rootcpk
import vehicle_optics as optics

OWNER = 'optics'
MANIFEST = '.edf6vc_optics.json'
BACKUP = '.edf6vc_optics_backup'
redirect = optics.redirect


def build_models(root: str) -> dict[str, bytes]:
    game = rootcpk.Game(root)
    return {'OBJECT/'+optics.output(s): optics.build_model(game, s) for s in optics.MODELS}


def build_stock_redirects(root: str) -> dict[str, bytes]:
    """Cheap current-SGO pass, deliberately excluded from the model cache."""
    game = rootcpk.Game(root)
    files = {}
    stems = tuple(s.stem for s in optics.MODELS)
    for name in game.names('OBJECT'):
        if not name.upper().endswith('.SGO') or not name.upper().startswith(stems):
            continue
        loose = Path(root, 'Mods', 'OBJECT', name)
        source = loose.read_bytes() if loose.is_file() else game.read('OBJECT', name)
        made, needs = redirect(source)
        if needs:
            files['OBJECT/'+name.upper()] = made
    return files


def build(root: str) -> dict[str, bytes]:
    return {**build_models(root), **build_stock_redirects(root)}


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
    if any(not rel.startswith('OBJECT/') or '..' in rel.split('/') for rel in outputs):
        raise ValueError('optic outputs must be relative OBJECT paths')
    if scope not in ('all', 'models', 'stock') or any(
            (scope == 'models' and not r.endswith('.MRAB')) or (scope == 'stock' and not r.endswith('.SGO')) for r in outputs):
        raise ValueError('optic install scope does not match its outputs')
    paths = []
    for rel in sorted(outputs, key=lambda r: (r.endswith('.SGO'), r)):
        data, path = outputs[rel], led.disk(rel)
        entry = state['files'].get(rel)
        if entry and entry.get('restoring'):
            _restore(root, led, state, rel)
            entry = state['files'].get(rel)
        current = modfiles.sha256_file(path)
        if not entry and rel.endswith('.MRAB') and current is not None and current != modfiles.sha256(data):
            raise RuntimeError(f'瞄具资源路径已有不匹配模型，已保留：{rel}')
        changed = entry and current not in (entry.get('written'), entry.get('pending'), entry.get('original'))
        if changed and rel.endswith('.MRAB'):
            raise RuntimeError(f'瞄具模型被其他程序修改，已保留：{rel}')
        if rel.endswith('.SGO'):
            # Mutable SGO writers run before this pass. Always patch their
            # CURRENT bytes, never an initial-install snapshot or cached SGO.
            source = modfiles.read(path)
            if source is not None:
                data, needs = redirect(source)
            else:
                data, needs = redirect(data)
            if not needs:
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
        entry['pending'] = modfiles.sha256(data)
        _save(root, state)
        paths.append(led.put(OWNER, rel, data))
        entry['written'] = entry.pop('pending')
        _save(root, state)
    previous = {r for r in state['files'] if scope == 'all' or (r.endswith('.SGO') == (scope == 'stock'))}
    _remove(root, led, state, previous - set(outputs))
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
    for path in Path(root, 'Mods', 'OBJECT').glob('*'):
        if path.suffix.upper() != '.SGO':
            continue
        raw = path.read_bytes().lower()
        for spec in optics.MODELS:
            name = optics.output(spec)
            needle = 'app:/object/' + name.lower()
            if needle.encode('utf-16le') in raw or needle.encode() in raw:
                kept.add('OBJECT/'+name)
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
        spec = next(s for s in optics.MODELS if rel == 'OBJECT/'+optics.output(s))
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
