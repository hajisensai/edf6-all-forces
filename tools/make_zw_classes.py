"""Writes the new player classes' files (docs/plan-2026-11-28-new-classes-impl.md §1) into <game>/Mods:
  Mods/OBJECT/ZW_VEHICLER_0N.SGO, ZW_RUNEBLADE_0N.SGO (N=1..4)   the battle SGOs SoldierInit [2] names: the stock
        Ranger's four (CONFIG.SGO SoldierInit[0][2], in order) with xgs_scene_object_class set to the class's factory
        (ZwVehicler / ZwRuneBlade, src/soldier_factory*: both AssultSoldier-based) and its hand / backpack slots the
        class's (SoldierClass hands / subs)
  Mods/MENUOBJECT/ZW_VEHICLER_0N.SGO, ZW_RUNEBLADE_0N.SGO        the menu SGOs [5][1]: the Ranger's four with the slot
        list the preview hangs on the hand the class's (menu_hands; docs/newclass-re/preview-slots.md)
  Mods/DEFAULTPACKAGE/CUSTOMCOLORZWVEHICLER.SGO, CUSTOMCOLORZWRUNEBLADE.SGO   [5][0]: the Ranger's colour sets
  Mods/OBJECT/ZW_RUNE_*.MRAB / .SGO                              the Rune Blade's shared rune circle and light trail
        (tools/make_runeblade_fx.py; its motions and model source: tools/make_runeblade_look.py)
The battle SGO carries no movement numbers to change (docs/newclass-re/motion.md §3: the dash's and the walk's attributes are
the accessories' custom_parameter), so the Vehicler's lost dash is the plugin's (src/vehicler_foot*), not this file's; roll is the
stock one. Every model is the Ranger's for now: ASSETS marks each file 'placeholder' (plan §3: not a delivery). Built from the
player's own Root.cpk (only read), written atomically and recorded in the ledger as this tool's (pylib/ledger.py
'zwclass'); --remove releases them. The weapon rows are tools/zw_weapons.py's (call_weapons stacks them). The texts
(TEXTS: class, weapon category and slot names, the keys SoldierInit and SoldierWeaponCategory name) are this tool's own
layer on the game's text tables (TEXT_TABLES), whatever mission packs are installed (install_texts): the top one. The
tables' other writer, make_edf5_campaign (the packs' mode names), writes beneath it and reads its own tables through it
(beneath); the installer runs install_texts after every other step that writes a table, so the keys are there with the
packs installed, without them, refused or turned off; remove() takes the layer off again.
  python tools/make_zw_classes.py [game dir]            write / refresh
  python tools/make_zw_classes.py [game dir] --remove   release them
"""
from __future__ import annotations

import base64
import os
import struct
import sys
from dataclasses import dataclass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import ledger  # noqa: E402
import modfiles  # noqa: E402
from modfiles import sha256, sha256_file  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402
sys.path.insert(0, HERE)
import make_runeblade_fx as rune_fx  # noqa: E402

OWNER = 'zwclass'   # pylib/ledger.py
CONFIG = ('DEFAULTPACKAGE', 'CONFIG.SGO')
TEMPLATE_CLASS = 0   # SoldierInit's Ranger: the base of both (AssultSoldier)
APPEARANCES = 4


@dataclass(frozen=True)
class SoldierClass:
    type: int
    factory: str     # xgs_scene_object_class, the name src/soldier_factory* registers
    stem: str        # zw_vehicler -> OBJECT/zw_vehicler_0N.sgo, MENUOBJECT/zw_vehicler_0N.SGO
    npc_weapon: str  # the support soldier's weapon (tools/zw_weapons.py row id: WEAPON/<ID>.SGO)
    # Its second weapon in the same hand slot (the Ranger template's one slot already lists loads [0, 1]): the AI
    # (src/npcai.cpp ChooseArm) takes whichever reaches the target; '' none.
    npc_second: str = ''
    # Which of the class's slots (src/soldier_classes_logic.h kClasses, SoldierInit [4]) a soldier holds and which are
    # its backpack: the stock contract (docs/newclass-re/preview-slots.md) is hands = the support-0 slots, subs = the
    # support-1 ones, in both the battle SGO (soldier_weapon_slot [0][0] / soldier_sub_weapon_slot) and the menu's.
    hands: tuple[int, ...] = ()
    subs: tuple[int, ...] = ()
    # The menu SGO's soldier_weapon_slot [0][0]: the preview (0x940100) takes entry [edited slot % 3] as the slot whose
    # weapon it hangs on the hand, and hangs that row's animation_model without a test (a row with none throws on the
    # preview's thread). So every entry names a hand slot, and three of them: no edited slot leaves the hand empty.
    menu_hands: tuple[int, ...] = ()

    @property
    def name_key(self) -> str:
        return f'SoldierType_{self.factory}'

    def battle(self, n: int) -> str:
        return f'OBJECT/{self.stem.upper()}_0{n}.SGO'

    def menu(self, n: int) -> str:
        return f'MENUOBJECT/{self.stem.upper()}_0{n}.SGO'

    @property
    def custom_color(self) -> str:
        return f'DEFAULTPACKAGE/CUSTOMCOLOR{self.factory.upper()}.SGO'

    # the paths as SoldierInit writes them (plan §1)
    def battle_path(self, n: int) -> str:
        return f'app:/object/{self.stem}_0{n}.sgo'

    def menu_path(self, n: int) -> str:
        return f'APP:/MENUOBJECT/{self.stem}_0{n}.SGO'

    @property
    def custom_color_path(self) -> str:
        return f'App:/DefaultPackage/CustomColor{self.factory}.sgo'

    # The class's support soldier (src/support_soldier.cpp kBodies, support_call.h SupportWeapon::zwVehicler /
    # zwRuneBlade): the rifle Ranger NPC template with this factory and the class's own weapon.
    def npc(self, leader: bool) -> str:
        return f'OBJECT/EDF6VC_{self.stem.upper()}{"_LEADER" if leader else ""}.SGO'

    @property
    def npc_weapon_path(self) -> str:
        return f'app:/weapon/{self.npc_weapon}.sgo'

    @property
    def npc_weapon_paths(self) -> list[str]:
        return [self.npc_weapon_path] + ([f'app:/weapon/{self.npc_second}.sgo'] if self.npc_second else [])


# The Vehicler holds its pistol (slot 1); the vehicle slot (0, the Air Raider's like) is its backpack. The Rune Blade
# holds its melee and ranged weapons (runeblade_melee.cpp SlotInHand), its support slot (2) on the back; the menu shows
# the ranged one (the melee slot's default, ZW_MeleeNone, has no model).
CLASSES: tuple[SoldierClass, ...] = (SoldierClass(4, 'ZwVehicler', 'zw_vehicler', 'ZW_Gyrojet01',
                                                  hands=(1,), subs=(0,), menu_hands=(1, 1, 1)),
                                     SoldierClass(5, 'ZwRuneBlade', 'zw_runeblade', 'ZW_RuneCarbine01',
                                                  'ZW_RuneSaber01', hands=(0, 1), subs=(2,), menu_hands=(1, 1, 1)))
# The Ranger's (the template's), checked before they are rewritten.
RANGER_HANDS, RANGER_SUBS = (0, 1), (2, 3)
# The support soldiers' template (support_loadout.KINDS' rifle): an AI soldier, one weapon (AiSoldierRifle01) in one set.
NPC_TEMPLATE = 'N601_COMMON_RANGER_AF'


RUNEBLADE = CLASSES[1]


def files_of(c: SoldierClass) -> list[str]:
    return ([c.battle(n) for n in range(1, APPEARANCES + 1)] + [c.menu(n) for n in range(1, APPEARANCES + 1)]
            + [c.custom_color, c.npc(False), c.npc(True)] + (rune_fx.files() if c == RUNEBLADE else []))


# Every file this tool writes -> (what it is made from, its state). 'placeholder': the Ranger's model / colours stand in
# (plan §3 stage 1); a class's own art replaces the entry when it is made.
ASSETS: dict[str, tuple[str, str]] = {
    **{c.battle(n): (f'SoldierInit[0][2][{n - 1}]', 'placeholder') for c in CLASSES for n in range(1, APPEARANCES + 1)},
    **{c.menu(n): (f'SoldierInit[0][5][1][{n - 1}]', 'placeholder') for c in CLASSES for n in range(1, APPEARANCES + 1)},
    **{c.custom_color: ('SoldierInit[0][5][0]', 'placeholder') for c in CLASSES},
    **{c.npc(leader): (NPC_TEMPLATE + ('_LEADER' if leader else ''), 'placeholder') for c in CLASSES for leader in (False, True)},
    **{rel: ('tools/make_runeblade_fx.py', 'generated') for rel in rune_fx.files()},   # drawn here: the class's own
}

# The text keys (TEXTTABLE_STEAM.<LANG>.TXT_SGO; LANG as the game names them: CN traditional, SC simplified).
TEXTS: dict[str, dict[str, str]] = {
    'SoldierType_ZwVehicler': {'SC': '载具手', 'CN': '載具手', 'JA': 'ヴィークラー', 'EN': 'Vehicler', 'KR': '비클러'},
    'SoldierType_ZwRuneBlade': {'SC': '刻印剑士', 'CN': '刻印劍士', 'JA': 'ルーンブレイド', 'EN': 'Rune Blade',
                                'KR': '룬 블레이드'},
    'WeaponCategory_ZwVehicle': {'SC': '载具', 'CN': '載具', 'JA': 'ビークル', 'EN': 'Vehicles', 'KR': '탈것'},
    'WeaponCategory_ZwPistol': {'SC': '手枪', 'CN': '手槍', 'JA': 'ピストル', 'EN': 'Pistols', 'KR': '권총'},
    'WeaponCategory_ZwMelee': {'SC': '近战武器', 'CN': '近戰武器', 'JA': '近接武器', 'EN': 'Melee Weapons',
                               'KR': '근접 무기'},
    'WeaponCategory_ZwRanged': {'SC': '远程武器', 'CN': '遠程武器', 'JA': '遠距離武器', 'EN': 'Ranged Weapons',
                                'KR': '원거리 무기'},
    'WeaponCategory_ZwSupport': {'SC': '支援装备', 'CN': '支援裝備', 'JA': 'サポート装備', 'EN': 'Support Equipment',
                                 'KR': '지원 장비'},
    'ZW_SlotVehicle': {'SC': '载具', 'CN': '載具', 'JA': 'ビークル', 'EN': 'Vehicle', 'KR': '탈것'},
    'ZW_SlotPistol': {'SC': '手枪', 'CN': '手槍', 'JA': 'ピストル', 'EN': 'Pistol', 'KR': '권총'},
    'ZW_SlotMelee': {'SC': '近战', 'CN': '近戰', 'JA': '近接', 'EN': 'Melee', 'KR': '근접'},
    'ZW_SlotRanged': {'SC': '远程', 'CN': '遠程', 'JA': '遠距離', 'EN': 'Ranged', 'KR': '원거리'},
    'ZW_SlotSupport': {'SC': '支援装备', 'CN': '支援裝備', 'JA': 'サポート装備', 'EN': 'Support Equipment',
                       'KR': '지원 장비'},
}
TEXT_LANGS = ('CN', 'EN', 'JA', 'KR', 'SC')
# The tables the keys go into, under Mods (app:/etc/TextTable_steam.%LOCALE%.txt_sgo; make_edf5_campaign.TEXTS too).
TEXT_TABLES = {lang: f'ETC/TEXTTABLE_STEAM.{lang}.TXT_SGO' for lang in TEXT_LANGS}
# This tool's layer on them: per table the sha of what it wrote ('also': the one it is replacing, while an install
# writes) and the bytes beneath it ('under', base64; None: there was no table in Mods, the game's own was the base).
TEXT_RECORD = '.edf6vc_zw_texts.json'
UNREADABLE = (ValueError, KeyError, IndexError, TypeError, AssertionError, struct.error, UnicodeDecodeError)


def add_texts(members: dict, lang: str) -> None:
    """The new classes' text keys into a text table's members (other keys untouched)."""
    for key, by_lang in TEXTS.items():
        members[key] = by_lang[lang]


def _record_path(root: str) -> str:
    return os.path.join(root, 'Mods', TEXT_RECORD)


def _table_path(root: str, rel: str) -> str:
    return os.path.join(root, 'Mods', *rel.split('/'))


def _on_top(entry: dict | None, data: bytes | None) -> bool:
    return bool(entry) and data is not None and sha256(data) in (entry['sha'], *entry.get('also', ()))


def _under(entry: dict) -> bytes | None:
    return None if entry['under'] is None else base64.b64decode(entry['under'])


def beneath(root: str, rel: str, data: bytes | None) -> bytes | None:
    """`data` (the file `rel` under Mods as it is on disk) without this tool's text layer: what was there before the
    layer while the layer is what is on disk (None: no table was), else `data` itself (no layer, or someone wrote the
    file since). make_edf5_campaign reads its own tables through this."""
    if rel not in TEXT_TABLES.values():
        return data
    entry = modfiles.load_json(_record_path(root), {}).get(rel)
    return _under(entry) if _on_top(entry, data) else data


def _has_keys(data: bytes, rel: str) -> bool:
    lang = next(lang for lang, r in TEXT_TABLES.items() if r == rel)
    try:
        members = sgo.read(data)[1]
    except UNREADABLE:
        return False
    return any(members.get(key) == by_lang[lang] for key, by_lang in TEXTS.items())


def texts_installed(root: str) -> bool:
    """The classes are installed (this tool owns files in the ledger): their names belong in the tables."""
    return bool(ledger.Ledger(root).owned_by(OWNER))


def install_texts(root: str) -> tuple[list[str], list[str]]:
    """Puts this tool's text layer on top of each table as it is now (the packs' or another mod's in Mods, else the
    game's own from Root.cpk, only read), or takes it off when the classes are not installed (remove_texts). A table
    already holding exactly that is not rewritten. Run after every other writer of the tables (the installer: after
    the mission packs). Returns (paths written or put back, notes: a table that does not read as one, left as it is)."""
    if not texts_installed(root):
        return remove_texts(root)[0], []
    path = _record_path(root)
    stored = modfiles.load_json(path, {})
    record = dict(stored)
    game = None   # opened only for a table that is nowhere in Mods
    files: dict[str, bytes] = {}
    entries: dict[str, dict] = {}
    notes = []
    for lang, rel in TEXT_TABLES.items():
        on_disk = modfiles.read(_table_path(root, rel))
        entry = record.get(rel)
        under = _under(entry) if _on_top(entry, on_disk) else on_disk
        if under is None:
            if game is None:
                game = vc.Game(root)
            base = game.read(*rel.split('/'))
        else:
            base = under
        try:
            ver, members = sgo.read(base)
            add_texts(members, lang)
            data = sgo.write_depth_first(ver, members)
        except UNREADABLE as e:
            notes.append(f'Mods/{rel} 读不了（{type(e).__name__}: {e}），可能是别的 MOD 用了别的格式：'
                         '不改动它，这个语言里新兵种没有名字。')
            if entry is not None and not _on_top(entry, on_disk):
                del record[rel]   # the layer is gone from it (someone wrote it since)
            continue
        new = {'sha': sha256(data), 'under': None if under is None else base64.b64encode(under).decode('ascii')}
        if data == on_disk:
            record[rel] = new
            continue
        files[rel] = data
        entries[rel] = {**new, 'also': [] if on_disk is None else [sha256(on_disk)]}
    if not files:
        if record != stored:
            modfiles.save_json(path, record)
        return [], notes
    # The record first, accepting the old bytes and the new: a run cut short leaves each table's layer recognised.
    modfiles.save_json(path, {**record, **entries})
    written = []
    for rel, data in files.items():
        written.append(_table_path(root, rel))
        modfiles.atomic_write(written[-1], data)
    for entry in entries.values():
        del entry['also']
    modfiles.save_json(path, {**record, **entries})
    return written, notes


def remove_texts(root: str) -> tuple[list[str], list[str]]:
    """Takes this tool's text layer off: each table it is still on top of back to what was beneath (deleted when there
    was none). Returns (paths put back, paths kept: written by someone since, left as they are)."""
    path = _record_path(root)
    record = modfiles.load_json(path, {})
    done, kept = [], []
    for rel, entry in record.items():
        table = _table_path(root, rel)
        data = modfiles.read(table)
        if not _on_top(entry, data):
            if data is not None and _has_keys(data, rel):   # written over since, the keys still in it
                kept.append(table)
            continue
        under = _under(entry)
        if under is None:
            os.remove(table)
        else:
            modfiles.atomic_write(table, under)
        done.append(table)
    if os.path.isfile(path):
        os.remove(path)
    return done, kept


def _config(game: vc.Game) -> list:
    init = sgo.plain(sgo.read(game.read(*CONFIG))[1])['SoldierInit']
    ranger = init[TEMPLATE_CLASS]
    if ranger[0] != TEMPLATE_CLASS or ranger[1] != 'SoldierType_Ranger':
        raise ValueError(f'CONFIG.SGO SoldierInit[{TEMPLATE_CLASS}] is {ranger[:2]}, not the Ranger')
    return ranger


def _rel(path: str) -> tuple[str, str]:
    """'app:/object/p505_ranger.sgo' -> ('OBJECT', 'P505_RANGER.SGO')."""
    folder, name = path.split(':/', 1)[1].split('/')
    return folder.upper(), name.upper()


def battle_sgo(template: bytes, c: SoldierClass) -> bytes:
    if template[:4] != b'DSGO':
        raise ValueError('the Ranger battle SGO is not a DSGO')
    doc = dsgo.parse(template)
    if doc.root.get('xgs_scene_object_class') != 'AssultSoldier':
        raise ValueError(f'the Ranger battle SGO is a {doc.root.get("xgs_scene_object_class")}, not an AssultSoldier')
    doc.root.set('xgs_scene_object_class', c.factory)
    hand, subs = _battle_slots(doc.root)
    hand.items[0].items[0].items[:] = [float(i) for i in c.hands]
    subs.items[:] = [dsgo.Node([float(i), 'weapon', 0.0]) for i in c.subs]
    return dsgo.compact(doc)


def _battle_slots(root: dsgo.Node) -> tuple[dsgo.Node, dsgo.Node]:
    """The Ranger battle SGO's soldier_weapon_slot (one hand set [[0, 1], 'weapon', 0, 0]) and soldier_sub_weapon_slot
    ([2, 'weapon', 0], [3, 'weapon', 0]); any other layout is not the template this was written for."""
    hand, subs = root.get('soldier_weapon_slot'), root.get('soldier_sub_weapon_slot')
    if (not isinstance(hand, dsgo.Node) or len(hand.items) != 1 or not isinstance(hand.items[0], dsgo.Node)
            or not isinstance(hand.items[0].items[0], dsgo.Node)
            or hand.items[0].items != [hand.items[0].items[0], 'weapon', 0.0, 0.0]
            or hand.items[0].items[0].items != [float(i) for i in RANGER_HANDS]):
        raise ValueError("the Ranger battle SGO's soldier_weapon_slot is not [[[0, 1], 'weapon', 0, 0]]")
    if (not isinstance(subs, dsgo.Node) or
            [x.items if isinstance(x, dsgo.Node) else x for x in subs.items] !=
            [[float(i), 'weapon', 0.0] for i in RANGER_SUBS]):
        raise ValueError("the Ranger battle SGO's soldier_sub_weapon_slot is not [2, 'weapon', 0], [3, 'weapon', 0]")
    return hand, subs


def menu_sgo(template: bytes, c: SoldierClass) -> bytes:
    """The class's menu SGO (classic SGO): the Ranger's, its soldier_weapon_slot's slot list the class's menu_hands."""
    version, members = sgo.read(template)
    slots = members.get('soldier_weapon_slot')
    if slots != [[list(RANGER_HANDS), 'weapon', 0, 0]]:
        raise ValueError(f"the Ranger menu SGO's soldier_weapon_slot is {slots}, not [[[0, 1], 'weapon', 0, 0]]")
    slots[0][0] = list(c.menu_hands)
    # write(): its raw blocks 16-aligned as the stock files have them (the 3rd / 4th appearance embed a MAB, at 0x280);
    # write_depth_first() would pack it at 0x26C.
    return sgo.write(version, members)


def npc_sgo(template: bytes, c: SoldierClass) -> bytes:
    """The class's support soldier: the rifle Ranger NPC (an AssultSoldier, one weapon) made by the class's factory, its
    weapon the class's own (soldier_load_weapon [0]; its one weapon set keeps index 0). Every other value (AI, model,
    voice) as the template's."""
    if template[:4] != b'DSGO':
        raise ValueError(f'{NPC_TEMPLATE} is not a DSGO')
    doc = dsgo.parse(template)
    if doc.root.get('xgs_scene_object_class') != 'AssultSoldier':
        raise ValueError(f'{NPC_TEMPLATE} is a {doc.root.get("xgs_scene_object_class")}, not an AssultSoldier')
    loads = doc.root.get('soldier_load_weapon')
    if not isinstance(loads, dsgo.Node) or len(loads.items) != 1 or not isinstance(loads.items[0], str):
        raise ValueError(f'{NPC_TEMPLATE} does not load exactly one weapon')
    if c.npc_second:
        slots = doc.root.get('soldier_weapon_slot')
        if (not isinstance(slots, dsgo.Node) or len(slots.items) != 1 or not isinstance(slots.items[0], dsgo.Node)
                or not isinstance(slots.items[0].items[0], dsgo.Node) or slots.items[0].items[0].items != [0.0, 1.0]):
            raise ValueError(f'{NPC_TEMPLATE} has not one weapon slot of loads [0, 1]: no room for a second weapon')
    doc.root.set('xgs_scene_object_class', c.factory)
    loads.items[:] = c.npc_weapon_paths
    return dsgo.compact(doc)


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    ranger = _config(game)
    battles, (color, menus) = ranger[2], ranger[5][:2]
    if len(battles) != APPEARANCES or len(menus) != APPEARANCES:
        raise ValueError(f'the Ranger has {len(battles)} battle / {len(menus)} menu SGOs, want {APPEARANCES}')
    out: dict[str, bytes] = {}
    for c in CLASSES:
        for n in range(1, APPEARANCES + 1):
            out[c.battle(n)] = battle_sgo(game.read(*_rel(battles[n - 1])), c)
            out[c.menu(n)] = menu_sgo(game.read(*_rel(menus[n - 1])), c)
        out[c.custom_color] = game.read(*_rel(color))
        for leader in (False, True):
            out[c.npc(leader)] = npc_sgo(game.read('OBJECT', NPC_TEMPLATE + ('_LEADER' if leader else '') + '.SGO'), c)
    out.update(rune_fx.build(game))
    if set(out) != set(ASSETS):
        raise AssertionError('build and ASSETS disagree')
    return out


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's, each one only when it is not already exactly that (every install rebuilds
    them: an unchanged file is not rewritten, as support_loadout.install); what it wrote before and does not now is
    released. Returns the paths written."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = []
    for rel, data in files.items():
        if sha256_file(led.disk(rel)) == sha256(data) and (ledger.key(rel) in before or led.owners(rel)):
            if ledger.key(rel) not in before:
                led.need(OWNER, rel)
            continue
        paths.append(led.put(OWNER, rel, data))
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Takes the text layer off and releases this tool's files: (deleted or put back, kept changed)."""
    done, kept = remove_texts(root)
    led = ledger.Ledger(root)
    deleted, changed = led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)
    return done + deleted, kept + changed


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    for path in install(root, build(root)):
        print('写入', path)
    written, notes = install_texts(root)
    for path in written:
        print('写入', path)
    for note in notes:
        print('！', note)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
