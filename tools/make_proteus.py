"""Build Proteus visual shield resources from Root.cpk, under ledger ownership."""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import ledger
import dsgo
import proteus_model
import rootcpk
import sgo

OWNER = 'proteus'


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
            animation_model = doc.root.get('animation_model').items if doc else values['animation_model']
            resource = animation_model[0].items if doc else animation_model[0]
            if resource[0].lower() != f'app:/object/{host.lower()}.mrab':
                raise ValueError(f'{name}: unexpected Proteus model')
            resource[0] = f'app:/object/{stem.lower()}.mrab'
            animation_model[1] = f'app:/object/{stem.lower()}.cas'
            files[f'OBJECT/{name}'] = dsgo.write(doc) if doc else sgo.write(version, values)
            seen += 1
        if seen < 4:
            raise ValueError(f'{host}: only {seen} vehicle variants')
    return files


def install(root: str, files: dict[str, bytes]) -> list[str]:
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(led.owned_by(OWNER)), writer=True)
