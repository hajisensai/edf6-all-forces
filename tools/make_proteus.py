"""Build Proteus visual shield resources from Root.cpk, under ledger ownership."""
from __future__ import annotations

import os
import sys
import copy
import json
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import ledger
import dsgo
import proteus_model
import rootcpk
import sgo
import modfiles

OWNER = 'proteus'
MANIFEST = '.edf6vc_proteus.json'
BACKUP = '.edf6vc_proteus_backup'


def redirect(data: bytes) -> tuple[bytes, tuple[str, ...]]:
    """Redirect a stock or generated Proteus SGO, retaining every other field.

    Shared by build() and the test range: reading Root.cpk again must not silently
    select the stock model. Returned dependencies are paths under Mods.
    """
    doc = dsgo.parse(data) if data[:4] == b'DSGO' else None
    version, values = sgo.read(data) if doc is None else (None, None)
    klass = doc.root.get('xgs_scene_object_class') if doc else values.get('xgs_scene_object_class')
    if klass != 'VehicleBigBegaruta':
        return data, ()
    animation_model = doc.root.get('animation_model').items if doc else values['animation_model']
    resource = animation_model[0].items if doc else animation_model[0]
    host = next((h for h in proteus_model.HOSTS if resource[0].lower() in
                 (f'app:/object/{h.lower()}.mrab', f'app:/object/edf6vc_{h.lower()}.mrab')), None)
    if host is None:
        return data, ()  # a third-party model is its author's contract
    stem = 'EDF6VC_' + host
    resource[0] = f'app:/object/{stem.lower()}.mrab'
    animation_model[1] = f'app:/object/{stem.lower()}.cas'
    return (dsgo.write(doc) if doc else sgo.write(version, values),
            (f'OBJECT/{stem}.MRAB', f'OBJECT/{stem}.CAS'))


def build(root: str) -> dict[str, bytes]:
    """Retain all four engine seats and stock weapon/bone indices."""
    game = rootcpk.Game(root)
    files: dict[str, bytes] = {}
    for host in proteus_model.HOSTS:
        model, animation = proteus_model.build_model(game, host)
        stem = 'EDF6VC_' + host
        files[f'OBJECT/{stem}.MRAB'] = model
        files[f'OBJECT/{stem}.CAS'] = animation
        seen = 0
        for name in game.names('OBJECT'):
            if not name.upper().endswith('.SGO') or not name.upper().startswith(host):
                continue
            source = game.read('OBJECT', name)
            doc = dsgo.parse(source) if source[:4] == b'DSGO' else None
            version, values = sgo.read(source) if doc is None else (None, None)
            klass = doc.root.get('xgs_scene_object_class') if doc else values.get('xgs_scene_object_class')
            if klass != 'VehicleBigBegaruta':
                continue
            made, needs = redirect(source)
            if not needs:
                raise ValueError(f'{name}: unexpected Proteus model')
            files[f'OBJECT/{name}'] = made
            seen += 1
        if seen < 4:
            raise ValueError(f'{host}: only {seen} vehicle variants')
    return files


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Write-ahead transaction; originals survive both overwrite and interruption."""
    led, state = ledger.Ledger(root), _load(root)
    outputs = {ledger.key(rel): data for rel, data in files.items()}
    if any(not rel.startswith('OBJECT/') or '..' in rel.split('/') for rel in outputs):
        raise ValueError('Proteus outputs must be relative OBJECT paths')
    # Models first, then consumers; never publish an SGO before its resources.
    paths = []
    for rel in sorted(outputs, key=lambda r: (r.endswith('.SGO'), r)):
        data, path = outputs[rel], led.disk(rel)
        entry = state['files'].get(rel)
        if entry and entry.get('restoring'):
            _restore(root, led, state, rel)
            entry = state['files'].get(rel)
        current = modfiles.sha256_file(path)
        if entry and current not in (entry.get('written'), entry.get('pending'), entry.get('original')):
            continue  # somebody changed this file after installation; retain it
        if rel.endswith('.SGO'):
            # Apply only the model redirect to a pre-existing loose SGO. Rebuilds
            # use that original again, rather than resetting its other changes.
            base = (modfiles.read(_backup(root, rel)) if entry and entry.get('original')
                    else modfiles.read(path) if not entry else None)
            if base is not None:
                data, needs = redirect(base)
                if not needs:
                    continue
        if not entry:
            original = modfiles.read(path)
            if original is not None:
                modfiles.atomic_write(_backup(root, rel), original)
            entry = {'original': modfiles.sha256(original) if original is not None else None,
                     'ledger': copy.deepcopy(led.files.get(rel))}
            state['files'][rel] = entry
        entry['pending'] = modfiles.sha256(data)
        _save(root, state)  # durable before Ledger.put touches the target
        paths.append(led.put(OWNER, rel, data))
        entry['written'] = entry.pop('pending')
        _save(root, state)
    _remove(root, led, state, set(state['files']) - set(outputs))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Restore consumers first; retain resources referenced by surviving SGOs."""
    led, state = ledger.Ledger(root), _load(root)
    return _remove(root, led, state, set(state['files']))


def _load(root: str) -> dict:
    path = Path(root, 'Mods', MANIFEST)
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {'version': 1, 'files': {}}


def _save(root: str, state: dict) -> None:
    if not state['files']:
        Path(root, 'Mods', MANIFEST).unlink(missing_ok=True)
        return
    modfiles.atomic_write(os.path.join(root, 'Mods', MANIFEST), json.dumps(state, sort_keys=True).encode('utf-8'))


def _backup(root: str, rel: str) -> str:
    return os.path.join(root, 'Mods', BACKUP, *rel.split('/'))


def _restore(root: str, led: ledger.Ledger, state: dict, rel: str) -> bool:
    entry, path = state['files'][rel], led.disk(rel)
    others = set(led.owners(rel)) - {OWNER, ledger.LEGACY}
    if others:
        return False
    current = modfiles.sha256_file(path)
    accepted = {entry.get('written'), entry.get('pending'), entry.get('original')}
    if current not in accepted:
        return False
    # Validate the recovery material before announcing a restore or deleting.
    original = modfiles.read(_backup(root, rel)) if entry.get('original') else None
    if entry.get('original') and (original is None or modfiles.sha256(original) != entry['original']):
        raise ValueError(f'Proteus original backup missing or corrupt: {rel}')
    entry['restoring'] = True
    _save(root, state)
    if original is None:
        if os.path.isfile(path):
            os.remove(path)
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


def _references(root: str) -> set[str]:
    """Loose SGO consumers, including changed originals and generated range SGOs."""
    kept = set()
    for path in Path(root, 'Mods', 'OBJECT').glob('*'):
        if path.suffix.upper() != '.SGO':
            continue
        raw = path.read_bytes().lower()
        for host in proteus_model.HOSTS:
            stem = 'EDF6VC_' + host
            for ext in ('MRAB', 'CAS'):
                needle = f'app:/object/{stem.lower()}.{ext.lower()}'
                if needle.encode('utf-16le') in raw or needle.encode() in raw:
                    # A model and animation are one schema; preserve both even
                    # if an edited consumer names just one of the pair.
                    kept.update(f'OBJECT/{stem}.{e}' for e in ('MRAB', 'CAS'))
    return kept


def range_vehicle(led: ledger.Ledger, game, data: bytes, owner: str = 'testrange') -> tuple[bytes, tuple[str, ...]]:
    """Redirect and hold a generated mission SGO's actual resource dependencies.

    A standalone range can make the same pair directly without rewriting stock
    SGOs. A full install's pair is shared through `need`, not overwritten.
    """
    made, needs = redirect(data)
    if not needs:
        return made, ()
    if any(not os.path.isfile(led.disk(rel)) for rel in needs):
        host = Path(needs[0]).stem.removeprefix('EDF6VC_')
        model, animation = proteus_model.build_model(game, host)
        for rel, content in zip(needs, (model, animation)):
            if not os.path.isfile(led.disk(rel)):
                led.put(owner, rel, content)
    for rel in needs:
        led.need(owner, rel)
    return made, needs


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
