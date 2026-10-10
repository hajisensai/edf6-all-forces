"""EDF5's weapons that EDF6 lacks, as rows of the shared weapon table: what tools/call_weapons.py stacks after its call
weapons, in the same transaction and manifest, kept where they are by id and turned into placeholders on uninstall like
those (see its doc).

The registry is edf5port/weapons.json (tools/make_edf5_weapons.py, docs/edf5-weapons-plan.md): per weapon its id
(EDF6VC_E5_<EDF5 SGO>), category, level, star caps, the EDF6 template row and its texts in the five languages. Its SGO
comes from one of two places:
  'edf6'  EDF6's own copy of it (in Root.cpk, never named by EDF6's table): copied under our id.
  'edf5'  EDF5's file converted by pylib/edf5port.py when the registry was made, kept in it ('weapon'): the install
          needs no EDF5. (One without 'weapon' would be read from the player's EDF5 Root.cpk, gamedir.find_other:
          $EDF5_DIR, next to EDF6, the Steam libraries.) Without what it is built from the row is still taken (tools/call_weapons.py plan_rows: every install
          of a release has the same rows, with or without EDF5): a placeholder, its template's stock weapon named as
          waiting for EDF5 (pending_*), until an install finds EDF5; a row an earlier install built is kept as it is
          (its SGO is in Mods already).
A weapon is obtained as any other: it drops in missions by its level (acquire 0). The plugin grants only the call
weapons (calls.ID_PREFIX), never these.
"""
from __future__ import annotations

import copy
import json
import os
import sys
from dataclasses import dataclass
from functools import lru_cache

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import calls  # noqa: E402
import dsgo  # noqa: E402
import edf5port  # noqa: E402
import gamedir  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
from dsgo import Node  # noqa: E402

if getattr(sys, 'frozen', False):
    LIST = os.path.join(sys._MEIPASS, 'edf5port', 'weapons.json')   # type: ignore[attr-defined]
else:
    LIST = os.path.join(HERE, '..', 'edf5port', 'weapons.json')
ID_PREFIX = 'EDF6VC_E5_'
ACQUIRE = 0.0     # WEAPONTABLE column 5: dropped in missions like any weapon
PACK = 0.0        # column 8: no EDF6 DLC pack (EDF5's EX packs are not EDF6's)
SOURCE = 1.0      # column 3: 1 every non-pack weapon, 3 a pack's


class Unavailable(Exception):
    """What a weapon is built from is not on this machine (no EDF5, or its file is not in EDF5's Root.cpk)."""


# A weapon waiting for EDF5: (name suffix, description); the row is its template's stock weapon until then.
PENDING_NOTE: dict[str, tuple[str, str]] = {
    'SC': ('（需要 EDF5）', '本机没有找到 EDF5，这一行暂时是原版的{stock}。装上 EDF5 后重新运行安装器选「安装」，这里就会变成这把 EDF5 武器。'),
    'CN': ('（需要 EDF5）', '本機沒有找到 EDF5，這一行暫時是原版的{stock}。裝上 EDF5 後重新執行安裝器選「安裝」，這裡就會變成這把 EDF5 武器。'),
    'JA': ('（EDF5 が必要）', 'この PC に EDF5 が見つからないため、この行は当面原版の{stock}です。EDF5 をインストールしてから'
                          'インストーラーで「インストール」を選び直すと、この EDF5 の武器になります。'),
    'EN': (' (needs EDF5)', 'EDF5 was not found on this PC, so this row is the stock {stock} for now. Install EDF5 and run the '
                            "installer's install again to get this EDF5 weapon here."),
}


@dataclass(frozen=True)
class Port:
    id: str
    sgo: str           # the SGO file in WEAPON (EDF5's, or EDF6's own copy for 'edf6')
    source: str        # 'edf5' or 'edf6'
    category: int
    cls: str           # the class the installed weapon has (Weapon_Sub: converted)
    level: float
    stars: tuple[int, ...]
    template: str      # the EDF6 weapon whose row fills the other columns and is the placeholder
    text: dict         # lang -> [name, description, stats]
    damage_attribute: dict | None = None   # AmmoDamageAttribute a converted weapon takes (its EDF6 family's)
    weapon: dict | None = None             # an 'edf5' weapon converted (dsgo.dump of edf5port.weapon's root)


def _load() -> tuple[Port, ...]:
    with open(LIST, encoding='utf-8') as f:
        data = json.load(f)
    return tuple(Port(w['id'], w['sgo'], w['source'], int(w['category']), w['class'], float(w['level']),
                      tuple(w['stars']), w['template'], w['text'], w.get('damage_attribute'), w.get('weapon'))
                 for w in data['weapons'])


PORTS: tuple[Port, ...] = _load()
IDS: tuple[str, ...] = tuple(p.id for p in PORTS)
BY_ID = {p.id: p for p in PORTS}
# Every order of IDS a release installed: (how many, sha256 of those ids joined by '\n'), frozen. Rows stay where an
# install put them (by id), so a later registry may only add weapons at its end: a removed id would leave its row
# nobody's (never retired), a reordered one would append in another order than saves expect (tools/selftest.py).
RELEASED: dict[str, tuple[int, str]] = {
    'EDF5 weapons (2026-10-10)': (61, '1547668c15e2211bf19f9ff54483855df5903ff4a481dc80858ee69e9d6a7578'),
}


def retired_id(port_id: str) -> str:
    return calls.RETIRED_PREFIX + port_id[len('EDF6VC_'):]


def slot_of(row_id: str) -> str | None:
    """The port a weapon table row belongs to (its own row, or the placeholder an uninstall left), else None."""
    upper = row_id.upper()
    for p in PORTS:
        if upper in (p.id.upper(), retired_id(p.id).upper()):
            return p.id
    return None


def sgo_file(p: Port) -> str:
    return f'WEAPON/{p.id}.SGO'


def edf5_root(game_root: str) -> str | None:
    return gamedir.find_other(gamedir.EDF5, near=game_root)


@lru_cache(maxsize=None)
def _edf5(root: str) -> rootcpk.Game:
    return rootcpk.Game(root)


def _names(p: Port) -> dict[str, str]:
    return {lang.lower(): t[0] for lang, t in p.text.items()}


def build_sgo(p: Port, stock, edf5: str | None) -> bytes:  # noqa: ANN001 - stock(rel) -> bytes, the EDF6 file
    """The installed SGO of `p`: EDF6's own copy, or EDF5's converted (edf5port). Unavailable: EDF5 is not there or
    lacks the file; edf5port.Unsupported: something in it this conversion does not carry."""
    if p.source == 'edf6':
        return stock(f'WEAPON/{p.sgo.upper()}')
    if p.weapon is not None:   # converted when the registry was made: no EDF5 needed
        doc = dsgo.Document(dsgo.load(p.weapon), [])
    else:
        if edf5 is None:
            raise Unavailable('EDF5 not found')
        try:
            data = _edf5(edf5).read('WEAPON', p.sgo)
        except KeyError as e:
            raise Unavailable(f"EDF5's Root.cpk has no WEAPON/{p.sgo}") from e
        doc = edf5port.weapon(sgo.read(data)[1], _names(p))
    if p.damage_attribute is not None:
        doc.root.set('AmmoDamageAttribute', Node([float(v) for v in p.damage_attribute.values()],
                                                 dict(enumerate(p.damage_attribute))))
    if p.cls == edf5port.SUB and doc.root.get('xgs_scene_object_class') != edf5port.SUB:
        template = dsgo.parse(stock(f'WEAPON/{p.template.upper()}.SGO')).root
        edf5port.to_sub(doc, template)
    if doc.root.get('xgs_scene_object_class') != p.cls:
        raise edf5port.Unsupported(f"class {doc.root.get('xgs_scene_object_class')}, the registry says {p.cls}")
    return dsgo.write(doc)


def build(game_root: str, stock) -> tuple[dict[str, bytes], dict[str, str]]:  # noqa: ANN001 - see build_sgo
    """({port id: its SGO} for every port this machine can build, {port id: why not} for the others)."""
    edf5 = edf5_root(game_root)
    out: dict[str, bytes] = {}
    why: dict[str, str] = {}
    for p in PORTS:
        try:
            out[p.id] = build_sgo(p, stock, edf5)
        except (Unavailable, edf5port.Unsupported) as e:
            why[p.id] = f'{type(e).__name__}: {e}'
    return out, why


def table_row(template: Node, p: Port) -> Node:
    row = copy.deepcopy(template)
    stock_path = template.items[1]
    prefix = stock_path[:stock_path.rfind('/') + 1]           # app:/weapon/
    suffix = stock_path[stock_path.rfind('.'):]                 # .sgo
    row.items[0] = p.id
    row.items[1] = f'{prefix}{p.id}{suffix}'
    row.items[2] = float(p.category)
    row.items[3] = SOURCE
    row.items[4] = float(p.level)
    row.items[5] = ACQUIRE
    row.items[6] = Node([float(x) for x in p.stars])
    row.items[8] = PACK
    return row


def text_row(p: Port, lang: str) -> Node:
    node = edf5port.json_node(p.text[lang])
    assert isinstance(node, Node)
    return node


def retired_table_row(template: Node, p: Port) -> Node:
    """The placeholder for an uninstalled port: its template's own stock row (stock SGO, level) under the placeholder
    id, obtained as the port itself is (ACQUIRE). Not the template's acquire: a template may be a new save's starting
    weapon (1, AssultRifle01: every new save would be handed the placeholder, and own the port once EDF5 is found) or
    a DLC / bonus item (3, granted with that DLC)."""
    row = copy.deepcopy(template)
    row.items[0] = retired_id(p.id)
    row.items[5] = ACQUIRE
    return row


def retired_text_row(template: Node, p: Port, lang: str) -> Node:
    row = copy.deepcopy(template)
    note = calls.RETIRED_NOTE[calls._lang(lang)]
    row.items[0] = p.text[lang][0] + note[0]
    row.items[1] = calls.retired_description(lang, str(template.items[0]))
    return row


def pending_table_row(template: Node, p: Port) -> Node:
    """The row of a weapon waiting for EDF5: the placeholder (the template's stock row under the placeholder id, which
    the install that finds EDF5 takes back like an uninstalled one's)."""
    return retired_table_row(template, p)


def pending_text_row(template: Node, p: Port, lang: str) -> Node:
    row = copy.deepcopy(template)
    note = PENDING_NOTE[calls._lang(lang)]
    row.items[0] = p.text[lang][0] + note[0]
    row.items[1] = note[1].format(stock=str(template.items[0]))
    return row
