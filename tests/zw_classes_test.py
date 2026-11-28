"""The new player classes' resources and weapon rows (docs/plan-2026-11-28-new-classes-impl.md; tools/zw_weapons.py,
tools/make_zw_classes.py): the released row order, placeholders, slots and categories against the plugin's rule table
(src/weapon_series.inc), the text keys in every language, the files' ledger lifecycle, and, when Root.cpk is at hand,
the SGOs built from the Ranger's. Plain asserts: python tests/zw_classes_test.py"""
from pathlib import Path
import hashlib
import os
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pylib'))
sys.path.insert(0, str(ROOT / 'tools'))
import calls  # noqa: E402
import call_weapons as cw  # noqa: E402
import gamedir  # noqa: E402
import ported_weapons as pw  # noqa: E402
import make_runeblade_fx as rune_fx  # noqa: E402
import make_runeblade_look as rl  # noqa: E402
import make_zw_classes as zc  # noqa: E402
import sgo  # noqa: E402
import vcobjects as vc  # noqa: E402
import zw_weapons as zw  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    assert ok, why


# rows: released prefixes never move, ids unique and apart from every other registry's
check(len(set(zw.IDS)) == len(zw.IDS) == len(zw.WEAPONS), 'zw ids unique')
for name, (n, digest) in zw.RELEASED.items():
    check(len(zw.IDS) >= n and hashlib.sha256('\n'.join(zw.IDS[:n]).encode()).hexdigest() == digest,
          f'{name}: the released rows moved')
check(max(n for n, _ in zw.RELEASED.values()) == len(zw.IDS), 'a new row is released')
check(zw.IDS[:5] == ('ZW_VehicleNone', 'ZW_Gyrojet01', 'ZW_MeleeNone', 'ZW_RuneCarbine01', 'ZW_SupportNone'),
      'stage 1 rows where they were (vehicler stage 2 appends after them)')
counts = sorted(n for n, _ in zw.RELEASED.values())
check(counts == sorted(set(counts)), 'each release a longer prefix')
# vehicle rows (vehicler stage 2): inert vehicle-slot tags; their vehicles are pylib/zw_vehicles.py's (tests/zw_vehicles_test)
check(all(w.category == 400 and w.template == zw.NONE_TEMPLATE for w in zw.WEAPONS if w.kind == 'vehicle'),
      'vehicle rows: category 400 from aSupportNone')
others = set(calls.IDS) | set(pw.IDS) | {pw.retired_id(x) for x in pw.IDS}
check(not (set(zw.IDS) | {zw.retired_id(x) for x in zw.IDS}) & others, 'zw rows apart from the calls / ported rows')
check(all(zw.retired_id(x).startswith(calls.RETIRED_PREFIX) and zw.retired_id(x) != x for x in zw.IDS),
      'placeholders carry the retired prefix')
plan = cw.plan_rows([f'stock{i}' for i in range(10)])
check(plan.appended == list(calls.IDS) + list(pw.IDS) + list(zw.IDS), 'zw rows after every other registry')

# categories: each row's class and slot, the slot rule table the plugin reads (generated src/weapon_series.inc)
for w in zw.WEAPONS:
    check(w.category // 100 in (4, 5) and zw.class_of(w.category) == w.category // 100, f'{w.id}: class')
    check(zw.slot_of(w.id) == w.id == zw.slot_of(zw.retired_id(w.id)), f'{w.id}: own row and placeholder')
    check(cw.slot_of(w.id) == zw.slot_of(w.id), f'{w.id}: call_weapons knows it')
check({w.category for w in zw.WEAPONS} <= set(zw.CATEGORY_KEYS), 'every row category has a name key')
check({c for slots in zw.SLOTS for _, cats, _ in slots for c in cats} == set(zw.CATEGORY_KEYS), 'slots and categories')
inc = (ROOT / 'src' / 'weapon_series.inc').read_text(encoding='utf-8')
for w in zw.WEAPONS:
    check(f'"{w.id}"' in inc, f'{w.id}: in weapon_series.inc')
for k, slots in enumerate(zw.SLOTS):
    for i, (name, cats, two) in enumerate(slots):
        cells = ','.join([str(c) for c in cats] + ['-1'] * (8 - len(cats)))
        rule = '{%d,%d,%s,{%s}},   // %s' % (zw.FIRST_CLASS + k, i, 'true' if two else 'false', cells, name)
        check(rule in inc, f'{name}: slot rule generated')

# hands and backpack (docs/newclass-re/preview-slots.md): the stock contract, hand slots = the support-0 slots of the
# class's table (src/soldier_classes_logic.h kClasses, SoldierInit [4][i][4]), backpack = the support-1 ones; the menu
# preview hangs the row of slot menu_hands[edited % 3] on the hand by its animation_model, unchecked (0x940100 ->
# 0x93AA00: a row without one throws on the preview thread, the crash of 11-28 with ZW_V9_VZ9 in the vehicle slot)
import re  # noqa: E402
table = (ROOT / 'src' / 'soldier_classes_logic.h').read_text(encoding='utf-8')
for c in zc.CLASSES:
    entry = re.search(r'\{k\w+,L"%s",(.*?)\n\s*(\d)\},' % c.factory, table, re.S)
    check(entry is not None, f'{c.factory}: in kClasses')
    support = [int(f) for f in re.findall(r'\{L"ZW_Slot\w+",L"\w+",\d+,\d+,(\d)\}', entry.group(1))]
    check(len(support) == int(entry.group(2)) == len(zw.SLOTS[c.type - zw.FIRST_CLASS]), f'{c.factory}: slot count')
    check(c.hands == tuple(i for i, f in enumerate(support) if f == 0), f'{c.factory}: hands are the support-0 slots')
    check(c.subs == tuple(i for i, f in enumerate(support) if f == 1), f'{c.factory}: backpack the support-1 slots')
    check(len(c.menu_hands) == 3 and set(c.menu_hands) <= set(c.hands), f'{c.factory}: menu shows 3 hand entries')
    shown = {cat for i in c.menu_hands for cat in zw.SLOTS[c.type - zw.FIRST_CLASS][i][1]}
    empty = [w.id for w in zw.WEAPONS if w.category in shown and (w.kind in ('none', 'vehicle') or
                                                                   w.template == zw.NONE_TEMPLATE)]
    check(not empty, f'{c.factory}: rows without a model in a slot the menu hangs on the hand: {empty}')
check(zc.CLASSES[1].hands[0] == 0, 'the Rune Blade melee slot (runeblade_melee.cpp SlotInHand) stays a hand slot')

# texts: every key SoldierInit / SoldierWeaponCategory / the slots name, in every language the game has
needed = {c.name_key for c in zc.CLASSES} | set(zw.CATEGORY_KEYS.values())
needed |= {name for slots in zw.SLOTS for name, _, _ in slots}
check(needed <= set(zc.TEXTS), f'missing text keys {needed - set(zc.TEXTS)}')
for key, by_lang in zc.TEXTS.items():
    check(set(by_lang) == set(zc.TEXT_LANGS) and all(v.strip() for v in by_lang.values()), f'{key}: all 5 languages')
check(set(zc.TEXT_LANGS) == set(zw.LANGS), 'text and weapon languages agree')
for lang in zc.TEXT_LANGS:
    members: dict = {'Other': 'kept'}
    zc.add_texts(members, lang)
    check(members['Other'] == 'kept' and all(members[k] == zc.TEXTS[k][lang] for k in zc.TEXTS), f'{lang}: add_texts')

# files: every one registered as a placeholder, the paths SoldierInit writes map onto them
check(set(zc.ASSETS) == {p for c in zc.CLASSES for p in zc.files_of(c)}, 'ASSETS is every file written')
fx_files = set(rune_fx.files())
check(all(state == 'placeholder' for rel, (_, state) in zc.ASSETS.items() if rel not in fx_files), 'models: placeholders')
check(rl.bad_fx() == [], f'motions name unknown fx {rl.bad_fx()}')
check(fx_files <= set(zc.ASSETS) and all(zc.ASSETS[rel][1] == 'generated' for rel in fx_files), 'rune fx: generated')
for c in zc.CLASSES:
    for n in range(1, zc.APPEARANCES + 1):
        check('/'.join(zc._rel(c.battle_path(n))) == c.battle(n), f'{c.factory} battle {n}: path')
        check('/'.join(zc._rel(c.menu_path(n))) == c.menu(n), f'{c.factory} menu {n}: path')
    check('/'.join(zc._rel(c.custom_color_path)) == c.custom_color, f'{c.factory}: colour path')
# the support soldiers (support_call.h zwVehicler / zwRuneBlade): the bodies the plugin's kBodies names, a weapon of ours
import re  # noqa: E402
bodies = re.findall(r'L"app:/object/(EDF6VC_ZW_\w+)\.sgo",L"app:/object/(EDF6VC_ZW_\w+)\.sgo"',
                    (ROOT / 'src' / 'support_soldier.cpp').read_text(encoding='utf-8'))
check([(f'OBJECT/{a.upper()}.SGO', f'OBJECT/{b.upper()}.SGO') for a, b in bodies] ==
      [(c.npc(False), c.npc(True)) for c in zc.CLASSES], 'the support soldiers\' bodies are support_soldier.cpp\'s')
check([c.npc_weapon for c in zc.CLASSES] == ['ZW_Gyrojet01', 'ZW_RuneCarbine01'] and
      all(c.npc_weapon in zw.IDS for c in zc.CLASSES), 'each support soldier carries its class\'s own weapon row')
check([c.npc_second for c in zc.CLASSES] == ['', 'ZW_RuneSaber01'] and zc.CLASSES[1].npc_second in zw.IDS and
      zc.CLASSES[1].npc_weapon_paths == ['app:/weapon/ZW_RuneCarbine01.sgo', 'app:/weapon/ZW_RuneSaber01.sgo'] and
      zc.CLASSES[0].npc_weapon_paths == ['app:/weapon/ZW_Gyrojet01.sgo'],
      'the Rune Blade support soldier: the carbine and the saber in its one slot; the Vehicler its pistol only')

# ledger lifecycle in a scratch game dir: write, refresh, release
with tempfile.TemporaryDirectory(prefix='edf6vc-zw-') as game:
    files = {rel: f'fake {rel}'.encode() for rel in zc.ASSETS}
    written = zc.install(game, files)
    check(len(written) == len(files) and all(os.path.isfile(p) for p in written), 'install writes every file')
    deleted, kept = zc.remove(game)
    check(len(deleted) == len(files) and not kept and not any(os.path.exists(p) for p in written), 'remove releases all')

# with the game: the SGOs built from the Ranger's (CI has no game)
game_root = gamedir.find()
if game_root is None:
    print('zw_classes: no game here, Root.cpk part skipped')
else:
    import dsgo  # noqa: E402
    built = zc.build(game_root)
    check(set(built) == set(zc.ASSETS), 'build writes ASSETS')
    for key in rune_fx.VARIANTS:
        rune_fx.check(built[f'OBJECT/{rune_fx.out_arc(key)}'], key)
        check(dsgo.parse(built[f'OBJECT/{rune_fx.out_sgo(key)}']).root.get('xgs_scene_object_class') == 'FarEventObject',
              f'rune fx {key}: object class')
    gaps = rl.missing_motions(vc.Game(game_root).read)
    check(gaps == [], f'runeblade motions the stock CAS lacks: {gaps}')
    for c in zc.CLASSES:
        for n in range(1, zc.APPEARANCES + 1):
            check(dsgo.parse(built[c.battle(n)]).root.get('xgs_scene_object_class') == c.factory,
                  f'{c.factory} battle {n}: factory class')
        for leader in (False, True):
            npc = dsgo.parse(built[c.npc(leader)]).root
            check(npc.get('xgs_scene_object_class') == c.factory and
                  npc.get('soldier_load_weapon').items == c.npc_weapon_paths, f'{c.factory} support soldier: factory, weapons')
    # the stock contract itself, on the game's four classes: battle hand lists = support-0 slots, subs = support-1
    init = sgo.plain(sgo.read(vc.Game(game_root).read(*zc.CONFIG))[1])['SoldierInit']
    for entry in init[:4]:
        support = [s[4] for s in entry[4]]
        stock = dsgo.parse(vc.Game(game_root).read(*zc._rel(entry[2][0]))).root
        held = sorted(int(i) for hand in stock.get('soldier_weapon_slot').items for i in hand.items[0].items)
        backs = sorted(int(x.items[0]) for x in stock.get('soldier_sub_weapon_slot').items)
        check(held == [i for i, f in enumerate(support) if f == 0] and backs == [i for i, f in enumerate(support) if f == 1],
              f'stock {entry[1]}: hands the support-0 slots, backpack the support-1 ones')
    for c in zc.CLASSES:
        for n in range(1, zc.APPEARANCES + 1):
            root = dsgo.parse(built[c.battle(n)]).root
            check(root.get('soldier_weapon_slot').items[0].items[0].items == [float(i) for i in c.hands] and
                  [x.items for x in root.get('soldier_sub_weapon_slot').items] ==
                  [[float(i), 'weapon', 0.0] for i in c.subs], f'{c.factory} battle {n}: hands / backpack')
            menu = built[c.menu(n)]
            members = sgo.read(menu)[1]
            check(members['soldier_weapon_slot'] == [[list(c.menu_hands), 'weapon', 0, 0]], f'{c.factory} menu {n}: slots')
            stock_menu = sgo.read(vc.Game(game_root).read(*zc._rel(zc._config(vc.Game(game_root))[5][1][n - 1])))[1]
            check({k: v for k, v in members.items() if k != 'soldier_weapon_slot'} ==
                  {k: v for k, v in stock_menu.items() if k != 'soldier_weapon_slot'}, f'{c.factory} menu {n}: rest stock')
            mab = menu.find(b'MAB\0')
            check(mab < 0 or mab % 16 == 0, f'{c.factory} menu {n}: embedded MAB 16-aligned')
    sgos = zw.build(lambda rel: cw.stock(game_root, rel))
    for c in zc.CLASSES:
        shown = {cat for i in c.menu_hands for cat in zw.SLOTS[c.type - zw.FIRST_CLASS][i][1]}
        for w in zw.WEAPONS:
            if w.category in shown:
                model = sgo.load(data=sgos[zw.files(w)[0]]).get('animation_model')
                check(isinstance(model, list) and model, f'{w.id}: the menu hangs it on the hand, it has a model')
    check(set(sgos) == {rel for w in zw.WEAPONS for rel in zw.files(w)}, 'every zw weapon SGO built')
    check(all(data[:4] == b'DSGO' or data[:3] == b'SGO' for data in sgos.values()), 'zw weapon SGOs are SGOs')
print(f'zw_classes: {checks} checks passed')
