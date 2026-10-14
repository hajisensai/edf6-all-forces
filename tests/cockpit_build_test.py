"""The cockpits (pylib/cockpits.py) against the real models: every fitted seat of every stock model in pylib/cockpit_fits*.py
lays its kit out inside its hull (cockpits.fitted: the room measured from the model, nothing of an inside part seen from
outside), its screens are of a known kind; one model (the Blacker) is built whole the installer's way
(vehicle_optics.build_model): its archive has its textures where the game binds them (obj_model.texture_problems), and a
vc_eye_<seat> marker and the screens' vc_scr_ markers for every fitted seat, under the bone the fit names.

The plugin's own vehicles (FITS stems their generators make) are left to their generators' builds, but for the ones that
wear a stock model under a name of their own (tools/make_jets.py: the gunship, the transport plane, the transport
helicopter), whose fits are laid out on that model as make_jets makes it. A kit no seat takes yet (ALTERNATES) is laid
out in the seats of the kit it stands beside.
A kit that promises a closed tub (an aircraft cockpit: Kit.sill) is closed under its sill: every line of sight from the
eye further down meets a front face of the kit (cockpits.leaks; the user, 2026-10-14: "固定翼的底部是空白，完全能看过去"),
in every seat fitted above and, for the kits cut from real models (cockpit_kits_plugin.MODELS: the plugin's jets, whose
models their generators make), in every room from the least to the most the kit takes.
Exit 77 (skipped) without Root.cpk or without the cockpits' assets (the model folder's cockpits/)."""
from __future__ import annotations

import os
import sys
import time
from dataclasses import replace

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import cockpit_fits  # noqa: E402
import cockpit_kits  # noqa: E402
import cockpit_kits_plugin  # noqa: E402
import cockpit_room  # noqa: E402
import cockpits  # noqa: E402
import jet_models  # noqa: E402
import make_jets  # noqa: E402
import obj_model as om  # noqa: E402
import rootcpk  # noqa: E402
import vehicle_optics  # noqa: E402
from mdb import mdb_read, rab_read  # noqa: E402

failures = 0
# Kits no stock seat takes yet, laid out where a seat of the kit named takes its place: the AH-64A's rear (pilot's)
# station in the gunships' one seat (cockpit_kits_air heli_gunship_pilot).
ALTERNATES: dict[str, tuple[str, ...]] = {'heli_gunship': ('heli_gunship_pilot',)}


def expect(ok: bool, what: str) -> None:
    global failures
    print(('ok    ' if ok else 'FAIL  ') + what, flush=True)
    failures += 0 if ok else 1


def model_of(game, stem: str):  # noqa: ANN001, ANN201
    from pathlib import Path
    loose = Path(game.root, 'Mods', 'OBJECT', stem + '.MRAB')
    arc = rab_read(loose.read_bytes() if loose.is_file() else game.read('OBJECT', stem + '.MRAB'))
    return mdb_read(next(f for f in arc.files if f.name.lower() == stem.lower() + '.mdb').data)


def only_model(data: bytes):  # noqa: ANN201 - mdb.Mdb
    """The one model of an archive (what cockpits.graft_files grafts when none is named after its stem)."""
    mdbs = [f for f in rab_read(data).files if f.name.lower().endswith('.mdb')]
    if len(mdbs) != 1:
        raise ValueError(f'{len(mdbs)} models in the archive')
    return mdb_read(mdbs[0].data)


def stock_copies(game) -> dict:  # noqa: ANN001 - rootcpk.Game
    """{stem: model} of the plugin's vehicles that wear a stock model under a name of their own, as tools/make_jets.py
    makes them (jet_models.stock_copy, make_jets.heli_transport_model)."""
    out = {file.removesuffix('.MRAB'): only_model(jet_models.stock_copy(game, file)) for file in jet_models.STOCK_COPIES}
    out[make_jets.TRANSPORT_HELI_MODEL_FILE.removesuffix('.MRAB')] = only_model(make_jets.heli_transport_model(game))
    return out


def closed(what: str, kit) -> None:  # noqa: ANN001 - cockpits.Kit
    if kit.sill is None:
        return
    open_lines = cockpits.leaks(kit)
    shown = ', '.join(f'({y:+.0f},{p:+.0f})' for y, p in open_lines[:6])
    expect(not open_lines, f'{what}: closed under its sill ({kit.sill:+.0f} deg)'
           + (f': {len(open_lines)} lines of sight (yaw, pitch) see through, e.g. {shown}' if open_lines else ''))


def rooms(model) -> list[tuple[str, dict[str, float]]]:  # noqa: ANN001 - cockpit_kits_plugin.Model
    """The rooms (metres to the hull) `model` takes, named: the most it uses, the least it accepts, half way, and each
    side at its least with the others at their most."""
    most = {k: v + cockpit_room.MARGIN for k, v in model.want.items()}
    least = {k: v + cockpit_room.MARGIN + 1e-3 for k, v in model.least.items()}
    out = [('most', most), ('least', least), ('half', {k: (most[k] + least[k]) / 2 for k in most})]
    out += [(f'{k} least', {**most, k: least[k]}) for k in most]
    return out


def main() -> int:
    root = rootcpk.DEFAULT_GAME
    if not os.path.isfile(os.path.join(root, 'Root.cpk')) or not cockpits.assets_present():
        print('SKIP: Root.cpk and the cockpits\' assets (the model folder\'s cockpits/) are needed')
        return 77
    game = rootcpk.Game(root)
    for name, (model, _) in sorted(cockpit_kits_plugin.MODELS.items()):
        if model.sill is None:
            continue
        for label, room in rooms(model):
            closed(f'{name} in the room {label}', cockpit_kits.KITS[name](room))
    stock = {m.stem: m for m in vehicle_optics.MODELS if m.cockpits}
    copies = stock_copies(game)
    expect(set(copies) <= set(cockpit_fits.FITS), f'the stock copies {sorted(copies)} are fitted')
    for stem, fits in sorted(cockpit_fits.FITS.items()):
        if stem not in stock and stem not in copies:
            print(f'      {stem}: a generated model (its generator builds it)')
            continue
        started = time.perf_counter()
        md = copies[stem] if stem in copies else model_of(game, stem)
        hull = cockpits.hull_triangles(md)
        for fit in fits:
            try:
                kit = cockpits.fitted(hull, fit, cockpit_kits.KITS[fit.kit])
                kinds = {s.kind for s in kit.screens}
                expect(kinds <= set(cockpits.SCREEN_KINDS),
                       f'{stem} seat {fit.seat} ({fit.kit} on {fit.bone}): laid out inside the hull, screens {sorted(kinds)}')
                closed(f'{stem} seat {fit.seat} ({fit.kit})', kit)
            except Exception as e:  # noqa: BLE001 - every seat reported, then the count fails the test
                expect(False, f'{stem} seat {fit.seat} ({fit.kit}): {e}')
            for alt in ALTERNATES.get(fit.kit, ()):
                try:
                    kit = cockpits.fitted(hull, replace(fit, kit=alt), cockpit_kits.KITS[alt])
                    expect({s.kind for s in kit.screens} <= set(cockpits.SCREEN_KINDS),
                           f'{stem} seat {fit.seat} as {alt} (no seat of its own yet): laid out inside the hull')
                    closed(f'{stem} seat {fit.seat} as {alt}', kit)
                except Exception as e:  # noqa: BLE001
                    expect(False, f'{stem} seat {fit.seat} as {alt}: {e}')
        print(f'      {stem}: {time.perf_counter() - started:.1f} s', flush=True)

    spec = stock['VEHICLE403_TANK']
    data = vehicle_optics.build_model(game, spec)
    arc = rab_read(data)
    member = spec.stem.lower() + '.mdb'
    md = mdb_read(next(f for f in arc.files if f.name.lower() == member).data)
    expect(not om.texture_problems(arc, md, member), f'{spec.stem}: built, its textures where the game binds them')
    names = [md.name_of(b.name) or '' for b in md.bones]
    for fit in spec.cockpits:
        marker = cockpits.MARKER.format(fit.seat)
        at = names.index(marker) if marker in names else -1
        expect(at >= 0 and md.bones[at].parent == md.bone_index(fit.bone),
               f'{spec.stem} seat {fit.seat}: {marker} under {fit.bone}')
        screens = [n for n in names if n.startswith(f'vc_scr_{fit.seat}_')]
        expect(bool(screens), f'{spec.stem} seat {fit.seat}: its screens {screens}')
    print(f'{failures} FAILED' if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
