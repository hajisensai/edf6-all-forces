"""The weapons of earlier games that EDF6 lacks (EDF5's, EDF4.1's), as rows of the shared weapon table: what
tools/call_weapons.py stacks after its call weapons, in the same transaction and manifest, kept where they are by id and
turned into placeholders on uninstall like those (see its doc).

The registries (REGISTRIES, in the order they were released: edf5port/weapons.json and edf5port/models.json from
tools/make_edf5_weapons.py, edf41port/weapons.json from tools/make_edf41_weapons.py; docs/edf5-weapons-plan.md): per
weapon its id (EDF6VC_E5_* / EDF6VC_E41_*), category, level, star caps, the EDF6 template row, its texts in the five
languages, the sound cues it needs swapped and the files it needs that EDF6 lacks ('assets': Mods path -> the file in
its game, converted by pylib/legacy_assets.py: a model only EDF5 has). Its SGO comes from one of two places:
  'edf6'  EDF6's own copy of it (in Root.cpk, never named by EDF6's table): copied under our id.
  <game>  converted by pylib/edf5port.py when the registry was made (EDF5's, EDF4.1's), kept in it ('weapon'), so
          the install needs neither game; a registry entry without one is converted from the player's own install
          (gamedir.find_other: $EDF41_DIR, next to EDF6, the Steam libraries). Without the game the row is still taken
          (tools/call_weapons.py plan_rows: every install of a release has the same rows, with or without it): a
          placeholder, its template's stock weapon named as waiting for the game (pending_*), until an install finds
          it; a row an earlier install built is kept as it is (its SGO is in Mods already).
A weapon is obtained as any other: it drops in missions by its level (acquire 0). The plugin grants only the call
weapons (calls.ID_PREFIX), never these.
"""
from __future__ import annotations

import collections
import copy
import json
import os
import struct
import sys
from dataclasses import dataclass, field
from functools import lru_cache
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import calls  # noqa: E402
import dsgo  # noqa: E402
import edf5port  # noqa: E402
import gamedir  # noqa: E402
import mab_legacy  # noqa: E402
import mdb  # noqa: E402
import rootcpk  # noqa: E402
import sgo  # noqa: E402
from dsgo import Node  # noqa: E402

# The registries: bundled next to the frozen installer, else the source tree's.
DATA = sys._MEIPASS if getattr(sys, 'frozen', False) else os.path.join(HERE, '..')  # type: ignore[attr-defined]
ACQUIRE = 0.0     # WEAPONTABLE column 5: dropped in missions like any weapon
PACK = 0.0        # column 8: no EDF6 DLC pack (the earlier games' packs are not EDF6's)
SOURCE = 1.0      # column 3: 1 every non-pack weapon, 3 a pack's


@dataclass(frozen=True)
class Game:
    key: str                          # the registry's source value for 'converted from this game'
    name: str                         # as the texts name it
    install: tuple[str, str, str]     # gamedir.find_other's (Steam folder, program, $VAR)
    prefix: str                       # our row ids
    convert: Callable[[dict, dict], dsgo.Document]   # its weapon SGO's members, names -> the EDF6 weapon


GAMES: tuple[Game, ...] = (
    Game('edf5', 'EDF5', gamedir.EDF5, 'EDF6VC_E5_', edf5port.weapon),
    Game('edf41', 'EDF4.1', gamedir.EDF41, 'EDF6VC_E41_', edf5port.weapon41),
)
# (game, registry under DATA) in release order: IDS follows it, and a release only adds a registry at its end (RELEASED).
REGISTRIES: tuple[tuple[str, str], ...] = (
    ('edf5', 'edf5port/weapons.json'),
    ('edf41', 'edf41port/weapons.json'),
    ('edf5', 'edf5port/models.json'),
)


class Unavailable(Exception):
    """What a weapon is built from is not on this machine (the game is not installed, or its Root.cpk lacks the
    file)."""


# A weapon waiting for its game: (name suffix, description); the row is its template's stock weapon until then.
PENDING_NOTE: dict[str, tuple[str, str]] = {
    'SC': ('（需要 {game}）', '本机没有找到 {game}，这一行暂时是原版的{stock}。装上 {game} 后重新运行安装器选「安装」，这里就会变成这把 {game} 武器。'),
    'CN': ('（需要 {game}）', '本機沒有找到 {game}，這一行暫時是原版的{stock}。裝上 {game} 後重新執行安裝器選「安裝」，這裡就會變成這把 {game} 武器。'),
    'JA': ('（{game} が必要）', 'この PC に {game} が見つからないため、この行は当面原版の{stock}です。{game} をインストールしてから'
                            'インストーラーで「インストール」を選び直すと、この {game} の武器になります。'),
    'EN': (' (needs {game})', '{game} was not found on this PC, so this row is the stock {stock} for now. Install {game} and '
                              "run the installer's install again to get this {game} weapon here."),
}


@dataclass(frozen=True)
class Port:
    id: str
    game: str          # Game.key of the registry it is in
    sgo: str           # the SGO file in WEAPON (the game's, or EDF6's own copy for 'edf6')
    source: str        # the game's key, or 'edf6'
    category: int
    cls: str           # the class the installed weapon has (Weapon_Sub: converted)
    level: float
    stars: tuple[int, ...]
    template: str      # the EDF6 weapon whose row fills the other columns and is the placeholder
    text: dict         # lang -> [name, description, stats]
    damage_attribute: dict | None = None   # AmmoDamageAttribute a converted weapon takes (its EDF6 family's)
    weapon: dict | None = None             # converted when the registry was made (dsgo.dump): built with no game
    cues: dict = field(default_factory=dict)   # sound cue EDF6 lacks -> the one played instead
    assets: dict = field(default_factory=dict)   # Mods path it needs -> the file in its game it is converted from


def _load(game: str, registry: str) -> tuple[Port, ...]:
    """A registry. A missing one is an error, not an empty list: an install without it would have fewer rows than
    every other (the rows are the same everywhere, call_weapons.plan_rows)."""
    path = os.path.join(DATA, *registry.split('/'))
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    return tuple(Port(w['id'], game, w['sgo'], w['source'], int(w['category']), w['class'], float(w['level']),
                      tuple(w['stars']), w['template'], w['text'], w.get('damage_attribute'), w.get('weapon'),
                      w.get('cues', {}), w.get('assets', {}))
                 for w in data['weapons'])


PORTS: tuple[Port, ...] = tuple(p for game, registry in REGISTRIES for p in _load(game, registry))
IDS: tuple[str, ...] = tuple(p.id for p in PORTS)
BY_ID = {p.id: p for p in PORTS}
BY_GAME = {g.key: g for g in GAMES}
# Every order of IDS a release installed: (how many, sha256 of those ids joined by '\n'), frozen. Rows stay where an
# install put them (by id), so a later registry may only add weapons at the end of IDS (a new game's registry after
# the others): a removed id would leave its row nobody's (never retired), a reordered one would append in another
# order than other installs (tools/selftest.py).
RELEASED: dict[str, tuple[int, str]] = {
    'EDF5 weapons (2026-10-10)': (61, '1547668c15e2211bf19f9ff54483855df5903ff4a481dc80858ee69e9d6a7578'),
    'EDF4.1 weapons (2026-10-10)': (345, '169d12e069abf6eb1cbeb4106de7acf2f5c9c073be96a03a4a7f49a432b5569e'),
    'EDF5 weapons with converted models (2026-10-10)': (348, 'cb28e972fcb1b3c78c6f195c927b9dc42cdbdafd0da1bd525fe8f5b754b073a2'),
}


def retired_id(port_id: str) -> str:
    return calls.RETIRED_PREFIX + port_id[len('EDF6VC_'):]


@lru_cache(maxsize=None)
def _slots() -> dict[str, str]:
    return {k.upper(): p.id for p in PORTS for k in (p.id, retired_id(p.id))}


def slot_of(row_id: str) -> str | None:
    """The port a weapon table row belongs to (its own row, or the placeholder an uninstall left), else None."""
    return _slots().get(row_id.upper())


def sgo_file(p: Port) -> str:
    return f'WEAPON/{p.id}.SGO'


def files(p: Port) -> list[str]:
    """Every Mods file a built port writes: its SGO and its assets."""
    return [sgo_file(p)] + list(p.assets)


def game_root(game: Game, edf6_root: str) -> str | None:
    """Where this machine has `game` installed, if it does."""
    return gamedir.find_other(game.install, near=edf6_root)


@lru_cache(maxsize=None)
def _archive(root: str) -> rootcpk.Game:
    return rootcpk.Game(root)


def _names(p: Port) -> dict[str, str]:
    return {lang.lower(): t[0] for lang, t in p.text.items()}


def _swap_cues(v: dsgo.Value, cues: dict[str, str]) -> dsgo.Value:
    if isinstance(v, str):
        return cues.get(v, v)
    if isinstance(v, Node):
        v.items = [_swap_cues(c, cues) for c in v.items]
    return v


class Models:
    """What EDF6 holds about the models the ported weapons show, read once per install: each model's bones, and the
    nodes EDF6's own weapons on it give their locators (edf5port.fit_locators' `prefer`). `stock(rel)` reads EDF6's
    file; `weapons` names EDF6's weapon SGOs (WEAPON/<name>), () for none (then only the root is preferred)."""

    def __init__(self, stock, weapons: tuple[str, ...] = ()) -> None:  # noqa: ANN001 - see build_sgo
        self.stock = stock
        self.weapons = weapons
        self._bones: dict[str, list[tuple[str, int]]] = {}
        self._nodes: dict[str, dict[str, str]] | None = None
        self._peers: dict[str, list[Node]] = {}
        self.known: dict[str, set[str]] = {f: set() for f in edf5port.HOLD_NAMES}

    @staticmethod
    def model_of(doc: dsgo.Document) -> tuple[str, str] | None:
        if 'animation_model' not in doc.root.names.values():
            return None
        rab, name = doc.root.get('animation_model').items[0].items[:2]
        return rab, name

    def bones(self, doc: dsgo.Document) -> list[tuple[str, int]]:
        """(name, parent) of every bone of the EDF6 model the weapon `doc` shows (animation_model[0]: its RAB, the MDB
        in it); [] for a weapon without a model (Weapon_Accessory). edf5port.Unsupported when EDF6 lacks that model or
        it does not read."""
        model = self.model_of(doc)
        if model is None:
            return []
        rab, name = model
        key = f'{rab}|{name}'.lower()
        if key not in self._bones:
            folder, _, file = rab.split(':/', 1)[-1].rpartition('/')
            try:
                archive = mdb.rab_read(self.stock(f'{folder.upper()}/{file.upper()}'))
                found = mdb.mdb_read(next(f for f in archive.files if f.name.lower() == name.lower()).data)
            except (KeyError, ValueError, StopIteration, AssertionError, IndexError, struct.error) as e:
                raise edf5port.Unsupported(f'model {rab} {name} not in EDF6 or unreadable: {e!r}') from e
            self._bones[key] = [(found.name_of(b.name), b.parent) for b in found.bones]
        return self._bones[key]

    def _index(self) -> None:
        """EDF6's weapons read once: per model, the weapons on it and the node each locator name hangs on; per HOLD
        name field, the names they use."""
        if self._nodes is not None:
            return
        votes: dict[str, dict[str, collections.Counter]] = {}
        for name in self.weapons:
            try:
                root = dsgo.parse(self.stock(f'WEAPON/{name}')).root
            except (KeyError, ValueError, IndexError, struct.error):
                continue
            for f in edf5port.HOLD_NAMES:
                if f in root.names.values():
                    text = edf5port._first_text(root.get(f))
                    if text is not None:
                        self.known[f].add(text)
            try:
                am = root.get('animation_model')
                rab = am.items[0].items[0].lower()
                nodes = mab_legacy.mab_locator_nodes(am.items[2].data)
            except (KeyError, ValueError, AttributeError, IndexError, struct.error):
                continue   # not a weapon with a MAB block: nothing to learn from it
            self._peers.setdefault(rab, []).append(root)
            for loc, node in nodes:
                votes.setdefault(rab, {}).setdefault(loc, collections.Counter())[node] += 1
        self._nodes = {rab: {loc: c.most_common(1)[0][0] for loc, c in locs.items()} for rab, locs in votes.items()}

    def preferred(self, doc: dsgo.Document) -> dict[str, str]:
        """{locator name: the node EDF6's own weapons on this weapon's model hang it on} (the most of them)."""
        model = self.model_of(doc)
        if model is None:
            return {}
        self._index()
        return self._nodes.get(model[0].lower(), {})

    def peers(self, doc: dsgo.Document) -> list[Node]:
        """EDF6's own weapons on this weapon's model."""
        model = self.model_of(doc)
        if model is None:
            return []
        self._index()
        return self._peers.get(model[0].lower(), [])


def build_sgo(p: Port, stock, root: str | None, models: Models | None = None) -> bytes:  # noqa: ANN001 - stock(rel) -> bytes, the EDF6 file
    """The installed SGO of `p`: EDF6's own copy, or its game's converted (edf5port) from that game's install at
    `root`, its MAB locators on bones of its EDF6 model (`models`: Models(stock) when not given, the root then the only
    bone a missing node moves to). Unavailable: the game is not there or lacks the file; edf5port.Unsupported:
    something in it this conversion does not carry."""
    if p.source == 'edf6':
        return stock(f'WEAPON/{p.sgo.upper()}')
    if p.weapon is not None:   # converted when the registry was made: the game is not needed
        doc = dsgo.Document(dsgo.load(p.weapon), [])
    else:
        name = BY_GAME[p.game].name
        if root is None:
            raise Unavailable(f'{name} not found')
        try:
            data = _archive(root).read('WEAPON', p.sgo)
        except KeyError as e:
            raise Unavailable(f"{name}'s Root.cpk has no WEAPON/{p.sgo}") from e
        doc = BY_GAME[p.game].convert(sgo.read(data)[1], _names(p))
    models = models or Models(stock)
    if p.damage_attribute is not None:
        doc.root.set('AmmoDamageAttribute', Node([float(v) for v in p.damage_attribute.values()],
                                                 dict(enumerate(p.damage_attribute))))
    if p.cues:
        _swap_cues(doc.root, p.cues)
    if p.cls == edf5port.SUB and doc.root.get('xgs_scene_object_class') != edf5port.SUB:
        template = dsgo.parse(stock(f'WEAPON/{p.template.upper()}.SGO')).root
        edf5port.to_sub(doc, template)
    if doc.root.get('xgs_scene_object_class') != p.cls:
        raise edf5port.Unsupported(f"class {doc.root.get('xgs_scene_object_class')}, the registry says {p.cls}")
    # Held as EDF6's own weapons on its model are (after to_sub: a vehicle call is held as a Weapon_Sub), then every
    # locator on a bone the model has.
    template = dsgo.parse(stock(f'WEAPON/{p.template.upper()}.SGO')).root
    edf5port.hold_as(doc, models.peers(doc), template, models.known)
    edf5port.fit_locators(doc, models.bones(doc), models.preferred(doc))
    return dsgo.write(doc)


def bundled(rel: str) -> str:
    """Where the converted copy of the asset at Mods path `rel` ships with the tools (tools/make_edf5_weapons.py
    writes it), so the install needs no earlier game for it (2026-10-10 user: make them ahead of time)."""
    return os.path.join(DATA, 'edf5port', 'assets', *rel.split('/'))


def build_assets(p: Port, root: str | None) -> dict[str, bytes]:
    """{Mods path: bytes} of the files `p` needs that EDF6 lacks: the converted copy shipped with the tools
    (bundled), else converted from its game at `root`."""
    import legacy_assets
    out: dict[str, bytes] = {}
    name = BY_GAME[p.game].name
    for rel, source in p.assets.items():
        if os.path.isfile(bundled(rel)):
            with open(bundled(rel), 'rb') as f:
                out[rel] = f.read()
            continue
        if root is None:
            raise Unavailable(f'{name} not found')
        folder, file = source.split('/', 1)
        try:
            data = _archive(root).read(folder, file)
        except KeyError as e:
            raise Unavailable(f'Root.cpk of {name} has no {source}') from e
        try:
            out[rel] = legacy_assets.convert(source, data)
        except ValueError as e:   # a converter refused it: the weapon waits as a placeholder
            raise edf5port.Unsupported(f'{source}: {e}') from e
    return out


def build_port(p: Port, stock, root: str | None, models: Models | None = None) -> dict[str, bytes]:  # noqa: ANN001 - see build_sgo
    """{Mods path: bytes} of every file `p` writes (files(p))."""
    return {sgo_file(p): build_sgo(p, stock, root, models), **build_assets(p, root)}


def build(edf6_root: str, stock) -> tuple[dict[str, dict[str, bytes]], dict[str, str]]:  # noqa: ANN001 - see build_sgo
    """({port id: its files} for every port this machine can build, {port id: why not} for the others)."""
    roots = {g.key: game_root(g, edf6_root) for g in GAMES}
    try:   # EDF6's weapons, for the nodes its own weapons hang locators on (Models.preferred)
        weapons = tuple(n for d, n in rootcpk.Game(edf6_root).cpk.index if d.upper() == 'WEAPON' and n.upper().endswith('.SGO'))
    except (OSError, ValueError, KeyError):   # a stand-in game without a Root.cpk: the root only
        weapons = ()
    models = Models(stock, weapons)
    out: dict[str, dict[str, bytes]] = {}
    why: dict[str, str] = {}
    for p in PORTS:
        try:
            out[p.id] = build_port(p, stock, roots[p.game], models)
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
    weapon (1, AssultRifle01: every new save would be handed the placeholder, and own the port once its game is found)
    or a DLC / bonus item (3, granted with that DLC)."""
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
    """The row of a weapon waiting for its game: the placeholder (the template's stock row under the placeholder id,
    which the install that finds the game takes back like an uninstalled one's)."""
    return retired_table_row(template, p)


def pending_text_row(template: Node, p: Port, lang: str) -> Node:
    row = copy.deepcopy(template)
    suffix, about = PENDING_NOTE[calls._lang(lang)]
    game = BY_GAME[p.game].name
    row.items[0] = p.text[lang][0] + suffix.format(game=game)
    row.items[1] = about.format(game=game, stock=str(template.items[0]))
    return row
