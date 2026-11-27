"""Build the three aura trucks (docs/feature-aura-vehicles.md): the command truck, the repair truck and the ammo truck.

Each is the stock V512_KEITRUCK (Root.cpk, read only: its SGO, handling, physics, seats) under its own SGO and its own
model: the kei truck's model with one transform-only marker bone under `body` that tells the plugin which aura the
truck carries (src/aura_logic.h kMarkers, src/aura.cpp reads it with the bone lookup), the body's diffuse tinted in the
aura's colour (the same colours as its range ring on the HUD), its cockpits grafted (pylib/cockpit_fits_plugin.py FITS,
the kei truck's seats) and its LODs made (pylib/model_lod.py). The requests are tools/calls.py EDF6VC_CALL_AURA_*.
`--out DIR` only writes reviewable assets outside the game; installation uses the ledger.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, replace
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
import cockpits
import ledger
import model_lod
import rootcpk
import sgo
import vehicle_optics
from mdb import RabFile, cmpl_compress, mdb_read, mdb_write, rab_read, rab_write

OWNER = 'aura_vehicles'
HOST = 'V512_KEITRUCK'
DURABILITY = 1000.


@dataclass(frozen=True)
class Truck:
    key: str                 # tools/calls.py kind and pylib/vcobjects.py GROUND_VEHICLES key
    stem: str                # its Mods/OBJECT SGO and MRAB
    marker: str              # src/aura_logic.h kMarkers (the same index as aura::Kind)
    tint: tuple[float, float, float]   # the body's diffuse (the HUD ring's colour: hud.cpp AuraHud)

    @property
    def model(self) -> str:
        return self.stem.lower() + '.mdb'


# In aura::Kind order (command, repair, ammo): tests/aura_vehicles_guard.py holds them to src/aura_logic.h.
TRUCKS = (
    Truck('aura_cmd', 'EDF6VC_AURA_CMD', 'edf6vc_aura_cmd', (1.0, 0.78, 0.42)),
    Truck('aura_rep', 'EDF6VC_AURA_REP', 'edf6vc_aura_rep', (0.58, 1.0, 0.58)),
    Truck('aura_amo', 'EDF6VC_AURA_AMO', 'edf6vc_aura_amo', (0.55, 0.8, 1.0)),
)
# Where the marker sits in the body's frame (it is transform-only: it draws nothing; the plugin only asks it exists).
MARKER_AT = (0.0, 1.0, 0.0)


def model(game, truck: Truck) -> bytes:
    """The kei truck's archive with its model member renamed to the truck's, the marker added and the body tinted."""
    arc = rab_read(game.read('OBJECT', HOST + '.MRAB'))
    member = next(f for f in arc.files if f.name.lower() == HOST.lower() + '.mdb')
    md = mdb_read(member.data)
    frame = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., *MARKER_AT, 1.]
    md = vehicle_optics.preorder(vehicle_optics.add_marker(md, 'body', truck.marker, frame))
    opaque = [m for m in md.materials if 'Alpha' not in m.shader]
    if len(opaque) != 1:
        raise ValueError(f'{HOST}: {len(opaque)} opaque materials, the tint expects the one body material')
    body = opaque[0]
    params = [replace(p, value=[*truck.tint, p.value[3]]) if p.name == 'diffuse' else p for p in body.params]
    if params == list(body.params):
        raise ValueError(f'{HOST}: its body material has no diffuse to tint')
    md = replace(md, materials=[replace(m, params=params) if m is body else m for m in md.materials])
    files = [RabFile(truck.model, f.folder, f.flag, cmpl_compress(mdb_write(md)), f.unk) if f is member else f
             for f in arc.files]
    return rab_write(replace(arc, files=files))


def vehicle_sgo(game, truck: Truck) -> bytes:
    """The kei truck's SGO drawing the truck's model: every other entry (seats, handling, physics, no weapons) kept."""
    version, m = sgo.read(game.read('OBJECT', HOST + '.SGO'))
    if m['vehicle_weapon_setting'] != []:
        raise ValueError(f'{HOST}: the aura trucks expect an unarmed host')
    m['animation_model'][0] = [f'app:/object/{truck.stem.lower()}.mrab', truck.model]
    m['game_object_durability'] = DURABILITY
    return sgo.write(version, m)


def build(root):
    game = rootcpk.Game(root)
    files: dict[str, bytes] = {}
    for truck in TRUCKS:
        files[f'OBJECT/{truck.stem}.MRAB'] = model(game, truck)
        files[f'OBJECT/{truck.stem}.SGO'] = vehicle_sgo(game, truck)
    # each seat's cockpit (pylib/cockpit_fits_plugin.py FITS, the kei truck's fits), then the LODs (pylib/model_lod.py)
    files = model_lod.finish(cockpits.graft_files(game, files))
    assert not model_lod.check_files(files), model_lod.check_files(files)
    return files


def install(root, files):
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(r) for r in files}))
    return paths


def remove(root):
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', default=rootcpk.DEFAULT_GAME)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    out = Path(a.out).resolve()
    if out.is_relative_to(Path(a.game).resolve()):
        ap.error('--out must be outside the game directory')
    for name, data in build(a.game).items():
        p = out / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        print(p)


if __name__ == '__main__':
    main()
