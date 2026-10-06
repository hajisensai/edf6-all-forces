"""Turn the stock KG6 Kepler anti-air vehicles into flak vehicles and the DLC KG7 Bohr into a
self-aiming ground-attack launcher, built from the player's own Root.cpk into the game's Mods folder.

  python autoturret/tools/build.py install   [--mods DIR] [--no-text] [--force]
  python autoturret/tools/build.py uninstall [--mods DIR] [--force]
  python autoturret/tools/build.py check     [--mods DIR]
  python autoturret/tools/build.py build OUTDIR [--no-text]     (into a folder outside the game, to look at)
  python autoturret/tools/build.py --out DIR [--no-text]        (the old form: install into DIR)

--mods defaults to <game>/Mods (pylib/rootcpk.py finds the game). install and uninstall refuse while EDF6.exe
runs.

Overrides the Kepler / Bohr call SGOs and their gun pairs, and the Keplers missions place (NPC and
boardable) in OBJECT, and gives the NPC Titan its side cannons (titan_ai.py). The guns get flak rounds and
the EDF6AutoTurret plugin's mark; the calls get more durability and a faster turret. No weapon rows are
added. The vehicles' own WEAPONTEXT rows are rewritten to show the new numbers, on top of the tables
already in Mods, so other mods' rows are kept (see describe.py).

The data works without the plugin: the guns are ordinary no-lock guns (LockonType 0, LockonRange 0) that
the stock game fires as it fires any; the plugin's mark sits in LockonTargetType, which only the lock query
reads, and a range-0 gun never makes one. Without the plugin (disabled, refused or deleted) the flak is
unaimed and bursts at its full range; nothing goes silent.

install records what it did in Mods/.edf6at_data.json:
  files: every whole file it wrote, with the SHA-256 written and the Mods file it replaced (backed up into
         Mods/.edf6at_backup/ the first time, or the latest foreign edit explicitly accepted with --force).
         A Mods file that is not ours (another mod's, or ours
         changed since) is not overwritten without --force. While installing, pending_sha records the
         replacement before it lands; sha still identifies the file before that write.
  texts: for each WEAPONTEXT table, whether install created it and every row it rewrote, as it was
         before and as written. Until the table lands, previous also identifies the pre-write row.
uninstall puts that back: a whole file still as we wrote it is restored from its backup or deleted (one
changed since is left alone, unless --force); each text row still as we wrote it gets its original
back, and a table we created that is then stock again is deleted. Other mods' rows and files stay.
Without a manifest (installed by the old build.py), uninstall removes what is byte-identical to this
build's output and puts the stock text back in rows identical to what it would write.
"""
from __future__ import annotations

import argparse
import copy
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'pylib'))
import dsgo  # noqa: E402
import describe  # noqa: E402
import modfiles  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
import titan_ai  # noqa: E402
import vehicle_setup  # noqa: E402
from dsgo import Node  # noqa: E402

# The five stock KG6 Kepler calls and the guns each one mounts (GUN02 / GUN03 are shared by two tiers).
CALLS = {
    'AWEAPON346.SGO': '01',   # KG6 Kepler
    'AWEAPON349.SGO': '02',   # KG6 Kepler E
    'AWEAPON352.SGO': '02',   # KG6 Kepler F
    'AWEAPON359.SGO': '03',   # KG6 Kepler YE
    'AWEAPON361.SGO': '03',   # KG6 Kepler YF
}
SIDES = ('L', 'R')
SOURCE_GUN = 'V_409HELI_GATLING01.SGO'   # EF31 Nereid auto-capture cannon
# Stock Kepler durability is under half a same-level tank's (YF 8750 vs Barrias TZ3 24000); the DLC
# Kepler YF-HV already runs 2.4x the YF's. Doubling keeps it the fragile one.
DURABILITY_SCALE = 2.0
TURRET = [65.0, 0.3, 0.3]  # gun-L turret params, the DLC Kepler YF-HV's: the stock 45-65/0.1 can't keep up
# Each gun fires every 6 frames instead of 3 at twice the damage per round: the same damage per
# second on paper, half the bursts on screen. The real gain is the blast and the proximity fuse.
FIRE_SLOWDOWN = 2.0

# The Keplers the missions place: NPC-crewed (_AI, _WEAK_AI) and boardable (_MISSION). The AI ones
# mount their own AI guns (600-round bursts of 1-damage shot); all three become the modded tier-1
# Kepler, so an NPC Kepler is the same flak vehicle the player calls. Their own mission multipliers
# (WEAK_AI's halved damage) stay.
FLAK_OBJECTS = ('V603_FLAK_AI.SGO', 'V603_FLAK_WEAK_AI.SGO', 'V603_FLAK_MISSION.SGO')
AI_GUN = re.compile(r'v603_flak_gun01_([lr])_ai\.sgo', re.IGNORECASE)
PLAYER_GUN = r'v603_flak_gun01_\1.sgo'

# The DLC Kepler YF-HV keeps its high-velocity solid shot, durability and fast turret (already the
# buffed Kepler); its guns only get the auto-aim mark.
HV_CALL = 'MPACK_A_WEAPON016.SGO'   # not rebuilt; only its description changes
HV_GUN = 'V603_FLAK_GUNH01_DLC_{side}.SGO'

# The KG7 Bohr (DLC 2) and Bohr B share one grenade-launcher pair. They keep their own rounds,
# damage and rate (already above the same-level Barrias TZ4); they get the auto-aim in ground mode
# (ground targets first, lobbed rounds, stock impact fuse), double durability
# (24500 / 29400 vs TZ4-R 60000), a wider blast for crowds, and the stock grenade launcher's round
# class, whose blast also wrecks buildings (the DLC gun's _MapNoDamage variant spares them).
BOHR_CALLS = ('MPACK_B_WEAPON025.SGO', 'MPACK_B_WEAPON028.SGO')
BOHR_GUN = 'V603_FLAK_GLGUN01_DLC_{side}.SGO'
BOHR_EXPLOSION = 6.0   # stock 4
BOHR_AMMO_CLASS = 'GrenadeBullet01'   # stock GrenadeBullet01_MapNoDamage; same params as AGRENADELAUNCHER01

# Ballistics, fire rate, tracer colour, sound and muzzle flash come from the Nereid gun;
# model / bone / animation fields stay the Kepler gun's so the turret still works.
GUN_FIELDS = (
    'AmmoCount', 'FireInterval', 'FireAccuracy', 'FireRecoil', 'FireSe',
    'AmmoClass', 'AmmoSpeed', 'AmmoAlive', 'AmmoDamage', 'AmmoDamageReduce', 'AmmoExplosion',
    'AmmoIsPenetration', 'AmmoSize', 'AmmoHitSizeAdjust', 'AmmoHitImpulseAdjust', 'AmmoColor',
    'MuzzleFlash', 'MuzzleFlash_CustomParameter',
)
GUN_GRAVITY = 0.25  # Nereid uses 2.0 (it fires downward); anti-air wants a flat, fast arc
# Flak round: GrenadeBullet01 with custom type 1 bursts (blast damage + explosion effect) when its
# lifetime runs out; with bounce 0 it sticks to whatever it touches and bursts there at the same
# moment. AmmoAlive is the fuse at max range: the EDF6AutoTurret plugin shortens each round's own
# lifetime to the flight time to the tracked target, so rounds burst at the target's range.
GUN_AMMO = {
    'AmmoClass': 'GrenadeBullet01', 'AmmoModel': 'app:/WEAPON/bullet_grenade.rab',
    'AmmoSpeed': 8.0, 'AmmoAlive': 60.0, 'AmmoSize': 0.6, 'AmmoHitSizeAdjust': 1.0,
    'AmmoExplosion': 8.0, 'AmmoIsPenetration': 0.0,
    # The blast's push on what it hits, times the stock 1.0: the proximity fuse bursts nearly every round next to its
    # target, and at 1.0 the bursts threw monsters about (the user, 2026-10-05); the damage and the radius stay.
    'AmmoHitImpulseAdjust': 0.1,
    'AmmoColor': [3.0, 1.6, 0.6, 1.0],
    # [type 1 = burst on expiry, unused, unused, bounce 0 = stick, trail param, trail frames]
    'Ammo_CustomParameter': [1.0, -0.004, 1.0, 0.0, 0.05, 8.0],
    'AmmoHitSe': [0.0, 'common_damages_explode_S', 1.0, 1.0, 1.0, 200.0],
    'FireSe': [0.0, 'weapon_VHC_striker401_cannonTekkoRapid', 0.8, 1.0, 1.0, 40.0],
    'resource': ['app:/WEAPON/bullet_grenade.rab'],
}
# The plugin's mark (autoturret/src/turret.h kMarkAir / kMarkGround; keep them equal). The guns are no-lock
# guns, LockonType 0 and LockonRange 0, which stock fire-start fires with or without a lock; the plugin
# finds enemies itself (the game's lock-target registry). The mark is in LockonTargetType, which only the
# lock query reads (to pick a lock class), and a range-0 gun never locks. DistributionType 0: type 1 frees
# the lock list head on an empty-list shot.
MARK_AIR = 7301.0
MARK_GROUND = 7302.0


def gun_lockon(mark: float) -> dict[str, float]:
    return {'LockonType': 0.0, 'LockonTargetType': mark, 'LockonRange': 0.0, 'Lockon_DistributionType': 0.0}


# What build.py wrote before 0.3.0 (LockonType 4, needing the plugin's fire-gate patch to fire), field for
# field and in the same order: only so an install made then is recognized as this mod's (byte for byte) by
# install and uninstall. Drop once no such install is left.
LEGACY_LOCKON = {
    'LockonType': 4.0, 'LockonTargetType': 0.0, 'Lockon_DistributionType': 0.0,
    'Lockon_FireEndToClear': 0.0, 'Lockon_AutoTimeOut': 1.0,
    'LockonAngle': [3.14, 1.57], 'LockonTime': 0.0, 'LockonFailedTime': 0.0, 'LockonHoldTime': 30.0,
}


MANIFEST = '.edf6at_data.json'
BACKUP = '.edf6at_backup'
MANIFEST_VERSION = 1


def py(v: object) -> object:
    if isinstance(v, (list, tuple)):
        return Node([py(x) for x in v])
    if isinstance(v, int) and not isinstance(v, bool):
        return float(v)
    return v


def load(d: str, n: str) -> dsgo.Document:
    return dsgo.parse(rootcpk.default().read(d, n))


def set_lockon(r: Node, mark: float, legacy: bool) -> None:
    if not legacy:
        for k, v in gun_lockon(mark).items():
            r.set(k, py(v))
        return
    for k, v in LEGACY_LOCKON.items():
        r.set(k, py(v))
    if mark == MARK_GROUND:
        r.set('LockonTargetType', 1.0)
    r.set('LockonRange', 0.0)


def build_gun(tier: str, side: str, legacy: bool = False) -> bytes:
    doc = load('WEAPON', f'V603_FLAK_GUN{tier}_{side}.SGO')
    src = load('WEAPON', SOURCE_GUN).root
    r = doc.root
    damage, interval = r.get('AmmoDamage'), r.get('FireInterval')
    for k in GUN_FIELDS:
        r.set(k, copy.deepcopy(src.get(k)))
    r.set('AmmoGravityFactor', GUN_GRAVITY)
    for k, v in GUN_AMMO.items():
        if k != 'resource':
            r.set(k, py(v))
    r.set('AmmoDamage', damage * FIRE_SLOWDOWN)
    r.set('FireInterval', interval * FIRE_SLOWDOWN)
    res = r.get('resource')
    res.items += [x for x in GUN_AMMO['resource'] if x not in res.items]
    set_lockon(r, MARK_AIR, legacy)
    return dsgo.write(doc)


def build_hv_gun(side: str, legacy: bool = False) -> bytes:
    doc = load('WEAPON', HV_GUN.format(side=side))
    set_lockon(doc.root, MARK_AIR, legacy)
    return dsgo.write(doc)


def build_bohr_gun(side: str, legacy: bool = False) -> bytes:
    doc = load('WEAPON', BOHR_GUN.format(side=side))
    r = doc.root
    set_lockon(r, MARK_GROUND, legacy)
    r.set('AmmoExplosion', BOHR_EXPLOSION)
    r.set('AmmoClass', BOHR_AMMO_CLASS)
    return dsgo.write(doc)


def build_call(name: str, turret: list[float] | None, resources: list[str]) -> bytes:
    doc = load('WEAPON', name)
    r = doc.root
    setup = vehicle_setup.of_call(r, name)
    mul = setup.items[0]
    mul.items[0] = mul.items[0] * DURABILITY_SCALE
    if turret:
        vehicle_setup.guns(setup)[0].items[2] = py(turret)
    res = r.get('resource')
    res.items += [x for x in resources if x not in res.items]
    return dsgo.write(doc)


def build_object(name: str) -> bytes:
    """An NPC / mission Kepler (OBJECT/V603_FLAK_*.SGO) made the modded tier-1 Kepler: the flak guns
    in place of the AI guns, the same durability scale and fast turret as the calls."""
    doc = load('OBJECT', name)
    r = doc.root
    setup = vehicle_setup.check(r.get('mission_setup'), name)
    mul = setup.items[0]
    mul.items[0] = mul.items[0] * DURABILITY_SCALE
    for gun in vehicle_setup.guns(setup):
        if isinstance(gun, Node) and gun.items and isinstance(gun.items[0], str):
            gun.items[0] = AI_GUN.sub(PLAYER_GUN, gun.items[0])
    vehicle_setup.guns(setup)[0].items[2] = py(TURRET)
    res = r.get('resource')
    res.items = [AI_GUN.sub(PLAYER_GUN, x) for x in res.items]
    res.items += [x for x in GUN_AMMO['resource'] if x not in res.items]
    return dsgo.write(doc)


def build_files(legacy: bool = False) -> dict[str, bytes]:
    """Every whole file this mod writes, Mods-relative path -> bytes; each read back once. `legacy`: the
    guns as build.py wrote them before 0.3.0 (to recognize such an install)."""
    files: dict[str, bytes] = {}
    for name in CALLS:
        files[f'WEAPON/{name}'] = build_call(name, TURRET, GUN_AMMO['resource'])
    for tier in sorted(set(CALLS.values())):
        for side in SIDES:
            files[f'WEAPON/V603_FLAK_GUN{tier}_{side}.SGO'] = build_gun(tier, side, legacy)
    for side in SIDES:
        files[f'WEAPON/{HV_GUN.format(side=side)}'] = build_hv_gun(side, legacy)
    for name in FLAK_OBJECTS:
        files[f'OBJECT/{name}'] = build_object(name)
    files[f'OBJECT/{titan_ai.NAME}'] = titan_ai.build()
    for name in BOHR_CALLS:
        files[f'WEAPON/{name}'] = build_call(name, None, [])   # its turret is already the fast DLC one
    for side in SIDES:
        files[f'WEAPON/{BOHR_GUN.format(side=side)}'] = build_bohr_gun(side, legacy)
    for data in files.values():
        # the stock NPC Titan is a classic SGO, everything else DSGO; each must read back
        (sgo.read if data[:4] == b'SGO\0' else dsgo.parse)(data)
    return files


def build_texts(files: dict[str, bytes], mods: str) -> describe.Texts:
    vehicles = [describe.Vehicle(c, f'V603_FLAK_GUN{t}_L.SGO', 'flak') for c, t in CALLS.items()]
    vehicles.append(describe.Vehicle(HV_CALL, HV_GUN.format(side='L'), 'air'))
    vehicles += [describe.Vehicle(c, BOHR_GUN.format(side='L'), 'ground') for c in BOHR_CALLS]
    return describe.build_texts(vehicles, files, mods)


# ---------------------------------------------------------------- Mods folder


def _path(mods: str, rel: str) -> str:
    return os.path.join(mods, *rel.split('/'))


def _manifest_path(mods: str) -> str:
    return os.path.join(mods, MANIFEST)


def _load_manifest(mods: str) -> dict | None:
    m = modfiles.load_json(_manifest_path(mods), {})
    if not m:
        return None
    if m.get('version') != MANIFEST_VERSION:
        raise SystemExit(f'{MANIFEST}: version {m.get("version")}, this tool writes {MANIFEST_VERSION}')
    return m


def _stock_row(rel: str, row_id: str):
    """The stock text row of `row_id` in the WEAPONTEXT table `rel`, dumped."""
    rows = dsgo.parse(rootcpk.default().read('WEAPON', rel.split('/')[1])).root.get('text_table').items
    return dsgo.dump(rows[describe.stock_ids().index(row_id)])


class Built:
    """What this build and the old build.py (before 0.3.0) write, each built only when a Mods file has to be
    told apart from another mod's (the same game data gives the same bytes)."""

    def __init__(self, files: dict[str, bytes] | None = None) -> None:
        self._built: dict[bool, dict[str, bytes] | None] = {False: files, True: None}

    def _files(self, legacy: bool) -> dict[str, bytes]:
        if self._built[legacy] is None:
            self._built[legacy] = build_files(legacy=legacy)
        return self._built[legacy]

    def legacy_owns(self, mods: str, rel: str) -> bool:
        """The Mods file reads as the old build.py wrote it."""
        data = modfiles.read(_path(mods, rel))
        return data is not None and data == self._files(True).get(rel)

    def owns_unsaved(self, mods: str, rel: str, entry: dict) -> bool:
        """Compatibility with manifests predating pending_sha: an entry without a sha is one an install
        recorded and then died before saving the sha of the file it wrote. The file is still ours if it is absent, reads as
        this build or the old build.py writes it, or is still the Mods file the entry backed up (not written
        yet)."""
        data = modfiles.read(_path(mods, rel))
        if data is None or data == self._files(False).get(rel) or self.legacy_owns(mods, rel):
            return True
        return bool(entry['backup']) and data == modfiles.read(_path(mods, f'{BACKUP}/{entry["backup"]}'))


def _foreign(mods: str, rel: str, manifest: dict, built: Built) -> str | None:
    """Why the Mods file at `rel` is not ours to overwrite, or None (absent, ours, or the old build.py's)."""
    sha = modfiles.sha256_file(_path(mods, rel))
    if sha is None:
        return None
    entry = manifest['files'].get(rel)
    if entry is None:
        return None if built.legacy_owns(mods, rel) else f'{rel}: already in Mods and not written by this tool (another mod?)'
    if sha == entry.get('pending_sha'):
        return None   # the file landed before its final manifest update
    if 'pending_sha' in entry:
        return None if sha == entry['sha'] else f'{rel}: changed since this tool wrote it (another mod?)'
    if entry['sha'] is None:
        return None if built.owns_unsaved(mods, rel, entry) else f'{rel}: changed since this tool wrote it (another mod?)'
    if sha != entry['sha']:
        return f'{rel}: changed since this tool wrote it (another mod?)'
    return None


def _in_game(path: str) -> bool:
    return os.path.normcase(os.path.abspath(path)).startswith(os.path.normcase(os.path.abspath(rootcpk.DEFAULT_GAME)))


def _refuse_while_running(mods: str) -> None:
    """The game holds the tables it loaded and reads the files it needs while it runs: no writes into its
    folder then (a folder outside it, a dev build, is not the game's)."""
    if _in_game(mods):
        modfiles.refuse_while_running()


def foreign(mods: str, files: dict[str, bytes]) -> list[str]:
    """Why install would refuse without --force: the Mods files among `files` (build_files) that are not this tool's
    to overwrite (another mod's, or ours changed since). tools/installer.py asks before it writes anything."""
    manifest = _load_manifest(mods) or {'version': MANIFEST_VERSION, 'files': {}, 'texts': {}}
    built = Built(files)
    return [p for p in (_foreign(mods, rel, manifest, built) for rel in files) if p]


def installed(mods: str) -> bool:
    """A manifest is there: install (this tool's, or tools/installer.py's) wrote into `mods`."""
    return os.path.isfile(_manifest_path(mods))


def install(mods: str, text: bool, force: bool, files: dict[str, bytes] | None = None) -> None:
    """`files`: build_files() made earlier (tools/installer.py builds everything before it writes anything)."""
    _refuse_while_running(mods)
    manifest = _load_manifest(mods) or {'version': MANIFEST_VERSION, 'files': {}, 'texts': {}}
    files = build_files() if files is None else files
    texts = build_texts(files, mods) if text else describe.Texts({}, {})
    built = Built(files)
    problems = {rel: p for rel in files if (p := _foreign(mods, rel, manifest, built))}
    if problems and not force:
        raise SystemExit('not overwriting files this tool does not own:\n  ' + '\n  '.join(problems.values())
                         + '\nrerun with --force to back them up and overwrite them')
    # Back up what we replace the first time and record it, before any file is written. A file the old
    # build.py wrote is ours already; what it replaced back then is not known, so it has no backup.
    for rel in files:
        if rel in manifest['files']:
            if rel in problems:
                # A later mod's edit supersedes our previous restore point. Save it under a
                # fresh content-addressed name before changing the manifest; interruption
                # before that save must leave the previous restore point intact too.
                data = modfiles.read(_path(mods, rel))
                backup = f'overrides/{modfiles.sha256(data)}/{rel}'
                modfiles.atomic_write(_path(mods, f'{BACKUP}/{backup}'), data)
                manifest['files'][rel]['backup'] = backup
            continue
        backup = None
        if os.path.isfile(_path(mods, rel)) and not built.legacy_owns(mods, rel):
            backup = rel
            dst = _path(mods, f'{BACKUP}/{rel}')
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(_path(mods, rel), dst)
        manifest['files'][rel] = {'sha': None, 'backup': backup}
    for rel, rows in texts.rows.items():
        entry = manifest['texts'].setdefault(rel, {'created': not os.path.isfile(_path(mods, rel)), 'rows': {}})
        for row_id, (before, written) in rows.items():
            kept = entry['rows'].get(row_id)
            if kept:
                original = kept['original']   # a reinstall keeps the first original
            elif dsgo.dump(before) == dsgo.dump(written):
                original = _stock_row(rel, row_id)   # already ours (the old build.py): what it replaced is unknown
            else:
                original = dsgo.dump(before)
            # Uninstall must recognize the old row too if a whole-file write fails before this table lands.
            entry['rows'][row_id] = {'original': original, 'ours': dsgo.dump(written), 'previous': dsgo.dump(before)}
    # Write-ahead fingerprints: both sides of every atomic replacement belong to this install. This applies
    # to upgrades too, whose sha is already set. A later tool version can recognize an interrupted write
    # from these hashes without regenerating the failed version's bytes. Preserve the chosen restore point.
    for rel, data in files.items():
        entry = manifest['files'][rel]
        entry['sha'] = modfiles.sha256_file(_path(mods, rel))
        entry['pending_sha'] = modfiles.sha256(data)
    modfiles.save_json(_manifest_path(mods), manifest)
    # Clear each pending fingerprint only after the file has landed; either manifest state recognizes it.
    for rel, data in {**files, **texts.files}.items():
        modfiles.atomic_write(_path(mods, rel), data)
        if rel in manifest['files']:
            manifest['files'][rel]['sha'] = modfiles.sha256(data)
            manifest['files'][rel].pop('pending_sha', None)
            modfiles.save_json(_manifest_path(mods), manifest)
        if rel in texts.files:
            for row in manifest['texts'][rel]['rows'].values():
                row.pop('previous', None)
            modfiles.save_json(_manifest_path(mods), manifest)
        print(f'{rel:36s} {len(data):>10d}')


def _restore_texts(mods: str, texts: dict, force: bool) -> dict:
    """Puts the original rows back; returns the entries it had to leave (changed by someone else)."""
    left: dict = {}
    if not texts:
        return left   # nothing to put back (installed with --no-text): the game's own table is not needed
    ids = describe.table_ids(mods)
    for rel, entry in texts.items():
        path = _path(mods, rel)
        data = modfiles.read(path)
        if data is None:
            continue
        doc = dsgo.parse(data)
        rows = describe.text_rows(doc, ids, rel)
        for row_id, row in entry['rows'].items():
            if row_id not in ids:
                print(f'{rel}: row {row_id} is gone from WEAPONTABLE: left as is')
                continue
            at = ids.index(row_id)
            if dsgo.dump(rows[at]) not in (row['ours'], row.get('previous')) and not force:
                print(f'{rel}: row {row_id} changed since this tool wrote it: left as is (--force restores it)')
                left.setdefault(rel, {'created': entry['created'], 'rows': {}})['rows'][row_id] = row
                continue
            rows[at] = dsgo.load(row['original'])
        # Stock again (same values; the string pool's order may differ from the stock file's): we made the
        # file, so it goes.
        stock = dsgo.parse(rootcpk.default().read('WEAPON', rel.split('/')[1]))
        if entry['created'] and rel not in left and dsgo.dump(doc.root) == dsgo.dump(stock.root):
            os.remove(path)
            print(f'removed {rel} (back to stock)')
        else:
            modfiles.atomic_write(path, dsgo.compact(doc))
            print(f'restored our rows of {rel}')
    return left


def uninstall(mods: str, force: bool) -> None:
    _refuse_while_running(mods)
    manifest = _load_manifest(mods)
    if manifest is None:
        uninstall_unrecorded(mods)
        return
    left = {'version': MANIFEST_VERSION, 'files': {}, 'texts': {}}
    built = Built()
    for rel, entry in manifest['files'].items():
        path = _path(mods, rel)
        backup = _path(mods, f'{BACKUP}/{entry["backup"]}') if entry['backup'] else None
        sha = modfiles.sha256_file(path)
        ours = sha is None or sha in (entry['sha'], entry.get('pending_sha')) or (
            entry['sha'] is None and 'pending_sha' not in entry and built.owns_unsaved(mods, rel, entry))
        if not ours and not force:
            print(f'{rel}: changed since this tool wrote it (another mod?): left as is (--force restores it)')
            left['files'][rel] = entry
            continue
        if backup and os.path.isfile(backup):
            modfiles.atomic_write(path, modfiles.read(backup))
            print(f'restored {rel}')
        elif sha is not None:
            os.remove(path)
            print(f'removed {rel}')
    left['texts'] = _restore_texts(mods, manifest['texts'], force)
    if left['files'] or left['texts']:
        modfiles.save_json(_manifest_path(mods), left)
        print(f'{MANIFEST} keeps what was left; the backups stay in {BACKUP}')
        return
    shutil.rmtree(os.path.join(mods, BACKUP), ignore_errors=True)
    os.remove(_manifest_path(mods))


def uninstall_unrecorded(mods: str) -> None:
    """An install without a manifest (the old build.py): ours is what is byte-identical to its output or
    this build's (the same game data gives the same bytes); its text rows that read exactly as this build
    writes them get the stock text back. Anything else is left alone."""
    print(f'no {MANIFEST}: removing only what matches the build output byte for byte')
    files = build_files()
    legacy = build_files(legacy=True)
    for rel, data in files.items():
        current = modfiles.read(_path(mods, rel))
        if current is not None and current in (data, legacy[rel]):
            os.remove(_path(mods, rel))
            print(f'removed {rel}')
        elif current is not None:
            print(f'{rel}: differs from the build output (another mod, or other game data): left as is')
    recorded = {}
    for rel, rows in build_texts(files, mods).rows.items():
        mine = {row_id: {'original': _stock_row(rel, row_id), 'ours': dsgo.dump(before)}
                for row_id, (before, written) in rows.items() if dsgo.dump(before) == dsgo.dump(written)}
        if mine:
            recorded[rel] = {'created': False, 'rows': mine}
    _restore_texts(mods, recorded, force=False)


def check(mods: str) -> bool:
    manifest = _load_manifest(mods)
    if manifest is None:
        print(f'no {MANIFEST} in {mods}: not installed by this tool (or by the old build.py)')
        return False
    ok = True
    for rel, entry in manifest['files'].items():
        sha = modfiles.sha256_file(_path(mods, rel))
        complete = sha is not None and sha == entry['sha'] and 'pending_sha' not in entry
        state = 'ours' if complete else ('missing' if sha is None else
                'pending install' if sha in (entry['sha'], entry.get('pending_sha')) else 'CHANGED by someone else')
        ok &= complete
        print(f'{rel:36s} {state}{" (replaced a Mods file, backed up)" if entry["backup"] else ""}')
    ids = describe.table_ids(mods)
    for rel, entry in manifest['texts'].items():
        data = modfiles.read(_path(mods, rel))
        if data is None:
            print(f'{rel}: missing')
            ok = False
            continue
        rows = dsgo.parse(data).root.get('text_table').items
        aligned = len(rows) == len(ids)
        ok &= aligned
        print(f'{rel}: {len(rows)} rows, {"aligned" if aligned else "NOT ALIGNED"} with WEAPONTABLE ({len(ids)})')
        for row_id, row in entry['rows'].items():
            same = aligned and row_id in ids and dsgo.dump(rows[ids.index(row_id)]) == row['ours'] and 'previous' not in row
            ok &= same
            print(f'  {row_id:24s} {"ours" if same else "CHANGED or gone"}')
    return ok


def build(outdir: str, text: bool) -> None:
    outdir = os.path.abspath(outdir)
    if _in_game(outdir):
        raise SystemExit('build writes outside the game folder; use install for the game')
    files = build_files()
    if text:
        files.update(build_texts(files, outdir).files)
    for rel, data in files.items():
        modfiles.atomic_write(_path(outdir, rel), data)
        print(f'{rel:36s} {len(data):>10d}')


def main(argv: list[str]) -> int:
    default_mods = os.path.join(rootcpk.DEFAULT_GAME, 'Mods')
    if not argv or argv[0] in ('--out', '--no-text'):   # the old form: build.py [--out DIR] [--no-text]
        ap = argparse.ArgumentParser(description='the pre-0.3.0 form of: install --mods DIR')
        ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'build', 'Mods'))   # beside the plugins' build output (build/Mods/Plugins)
        ap.add_argument('--no-text', action='store_true')
        args = ap.parse_args(argv)
        install(os.path.abspath(args.out), not args.no_text, force=False)
        return 0
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    p = sub.add_parser('install')
    p.add_argument('--mods', default=default_mods)
    p.add_argument('--no-text', action='store_true', help='leave the WEAPONTEXT tables alone')
    p.add_argument('--force', action='store_true', help='back up and overwrite Mods files this tool does not own')
    p = sub.add_parser('uninstall')
    p.add_argument('--mods', default=default_mods)
    p.add_argument('--force', action='store_true', help='also restore files and rows changed since install')
    p = sub.add_parser('check')
    p.add_argument('--mods', default=default_mods)
    p = sub.add_parser('build')
    p.add_argument('outdir')
    p.add_argument('--no-text', action='store_true')
    args = ap.parse_args(argv)
    if args.command == 'install':
        install(os.path.abspath(args.mods), not args.no_text, args.force)
    elif args.command == 'uninstall':
        uninstall(os.path.abspath(args.mods), args.force)
    elif args.command == 'check':
        return 0 if check(os.path.abspath(args.mods)) else 1
    else:
        build(args.outdir, not args.no_text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
