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
import cockpit_instruments  # noqa: E402
import cockpit_kits  # noqa: E402
import cockpit_kits_plugin  # noqa: E402
import cockpit_room  # noqa: E402
import cockpits  # noqa: E402
import cockpit_shell  # noqa: E402
import cockpit_retrofit  # noqa: E402
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


# Real cabs (cockpit_kits_ground CABS) closed behind the driver: every line of sight back past his shoulders meets the
# cab's rear wall or seat. The stock hull round them is seen from inside there, where the game draws none of it: a gap
# in the cab shows the world (the user, 2026-10-19, of the rescue tank: 「救护车往后看会看到空气」; the ARHAN P880 cut
# had lost its rear wall to tools/prep_cockpit_ground.py's box). apc_cab (M-ATV) and tracked_cab (Tigr) are cut at
# their front seats' backs, their rear compartments left out: not closed behind yet, not checked.
CLOSED_BEHIND = frozenset(('truck_cab', 'trailer_cab'))
BEHIND = [(float(yaw), float(pitch)) for yaw in range(100, 261, 5) for pitch in range(-40, 21, 5)]


GENERATED_SKELETON = 32
ROOM = dict(left=1.5, right=1.5, up=1.0, down=1.0, ahead=2.0, back=2.0)


def generated_bones(fits) -> int:  # noqa: ANN001
    """The marker and instrument bones the fits' cockpits add (cockpits.graft's sharing), each kit laid out in ROOM."""
    def key(c):  # noqa: ANN001, ANN202
        return c.control if c.control[0] in 'dDplcuveZ' else (c.control, c.centre, c.facing, c.up, c.extent)
    total = 0
    for fit in fits:
        kit = cockpit_kits.KITS[fit.kit](ROOM)
        shared = {key(c) for c in kit.controls}
        for index, screen in enumerate(kit.screens):
            if screen.kind not in ('hud', 'sensor'):
                _parts, controls = cockpit_instruments.instrument(screen, index, numbers=fit.seat == 0,
                                                                   thumbnail=(('111', '111'), 1.), ranges=dict(kit.ranges))
                shared |= {key(c) for c in controls}
        total += 1 + len(kit.screens) + len(shared)
    return total


def closed(what: str, kit, name: str = '') -> None:  # noqa: ANN001 - cockpits.Kit
    if name in CLOSED_BEHIND:
        open_lines = cockpits.unmet(kit, BEHIND)
        shown = ', '.join(f'({y:+.0f},{p:+.0f})' for y, p in open_lines[:6])
        expect(not open_lines, f'{what}: closed behind the driver'
               + (f': {len(open_lines)} lines of sight (yaw, pitch) see through, e.g. {shown}' if open_lines else ''))
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
    # Measure the original C-130 circular bores, not the asymmetric outer plates
    # or an invented 2x2 grid. The native surface is a 16-sided ring at this depth.
    import numpy as np
    pos=cockpit_kits_plugin._cockpit(cockpit_kits_plugin.DECK.slug)[0]
    for site in cockpit_retrofit.C130[0].dials[:2]:
        distance=np.linalg.norm(pos[:,:2]-site.centre[:2],axis=1)
        ring=np.unique(np.round(pos[(abs(pos[:,2]-.84747)<1e-5)&(distance>.053)&(distance<.056),:2],5),axis=0)
        expect(len(ring)>=12 and np.allclose(ring.mean(0),site.centre[:2],atol=5e-5),
               f'C-130 standby: live case centred on the original bore {site.centre}')
        expect(len(ring)>0 and np.ptp(ring,axis=0).max()<site.diameter<.1236,
               'C-130 standby: case covers its bore without exceeding the original bezel')
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
            # Its model is its generator's; its cockpits' instrument bones laid out in a generous room must leave the
            # skeleton GENERATED_SKELETON bones (EV603_MARINE, the largest a generator starts from, has 19).
            need = generated_bones(fits)
            expect(need + GENERATED_SKELETON <= 256,
                   f'{stem}: a generated model; its cockpits need {need} bones, {256 - need} left for its skeleton')
            continue
        started = time.perf_counter()
        md = copies[stem] if stem in copies else model_of(game, stem)
        hull = cockpits.hull_triangles(md)
        separated=cockpit_shell.separate(md,fits,hull)
        expect(all(b.inv_bind==md.bones[b.parent].inv_bind for b in separated.bones[len(md.bones):]),
               f'{stem}: shell children preserve the exact native inverse binds')
        before=sum(len(me.indices) for o in md.objects for me in o.meshes)
        after=sum(len(me.indices) for o in separated.objects for me in o.meshes)
        expect(before==after,f'{stem}: all native triangles retained for outside views')
        envelopes=cockpit_shell.regions(md,fits,hull)
        expect(len(envelopes)==sum(f.kit in cockpit_shell.REPLACEMENTS for f in fits),
               f'{stem}: every replacement station has a bounded native cabin region')
        remaining=sum(int((cockpit_shell.triangle_masks(separated,o,me,envelopes)[0]!=0).sum())
                      for o in separated.objects for me in o.meshes)
        expect(remaining==0,f'{stem}: no unguarded native cabin triangles remain')
        required_bones=len(separated.bones)+len(fits)
        for fit in fits:
            try:
                kit = cockpits.fitted(hull, fit, cockpit_kits.KITS[fit.kit])
                required_bones+=len(kit.screens)
                shared=set()
                def key(control):   # as cockpits.graft shares a bone
                    return (control.control if control.control[0] in 'dDplcuveZ' else
                            (control.control,control.centre,control.facing,control.up,control.extent))
                for control in kit.controls:
                    shared.add(key(control))
                for index,screen in enumerate(kit.screens):
                    if screen.kind not in ('hud','sensor'):
                        _parts,controls=cockpit_instruments.instrument(screen,index,numbers=fit.seat==0,
                                                                     thumbnail=(("111","111"),1.))
                        for control in controls:
                            shared.add(key(control))
                required_bones+=len(shared)
                kinds = {s.kind for s in kit.screens}
                expect(kinds <= set(cockpits.SCREEN_KINDS),
                       f'{stem} seat {fit.seat} ({fit.kit} on {fit.bone}): laid out inside the hull, screens {sorted(kinds)}')
                closed(f'{stem} seat {fit.seat} ({fit.kit})', kit, fit.kit)
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
        expect(required_bones<=256,f'{stem}: all localized labels and vehicle thumbnails fit the skin palette ({required_bones}/256)')
        print(f'      {stem}: {time.perf_counter() - started:.1f} s', flush=True)

    spec = stock['VEHICLE403_TANK']
    data = vehicle_optics.build_model(game, spec)
    arc = rab_read(data)
    member = spec.stem.lower() + '.mdb'
    md = mdb_read(next(f for f in arc.files if f.name.lower() == member).data)
    expect(not om.texture_problems(arc, md, member), f'{spec.stem}: built, its textures where the game binds them')
    cockpit_meshes=[me for o in md.objects for me in o.meshes if md.name_of(md.materials[me.material].name).startswith('edf6vc_cpit_')]
    expect(bool(cockpit_meshes) and all(next(e for e in me.elems if e.name.lower()=='position').fmt==1 for me in cockpit_meshes),
           'built cockpit/display positions retain float32 precision, keeping glyphs off the backing')
    ink = [m for m in md.materials if 'instrument_ink' in md.name_of(m.name)]
    expect(bool(ink) and all(m.shader == 'snd_BRDF_Common_Light' for m in ink),
           'physical instrument labels use the native emissive material, not the dark hull shader')
    colours={name:next(p.value[:3] for p in next(m for m in md.materials if md.name_of(m.name).endswith('instrument_'+name)).params
                       if p.name=='light_color') for name in ('green','amber','red')}
    expect(colours['green'][1]>colours['green'][0] and colours['red'][0]>colours['red'][1]
           and len({tuple(v) for v in colours.values()})==3,
           'native emissive constants preserve distinct health colours instead of white bloom')
    interior = [m for m in md.materials if 'edf6vc_cpit_' in md.name_of(m.name)]
    expect(all(all(p.value[:3] == [1.0,1.0,1.0] for p in m.params if p.name == 'diffuse') for m in interior),
           'cockpit albedos keep their source colour instead of the tank template diffuse multiplier')
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
