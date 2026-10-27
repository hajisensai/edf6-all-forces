"""X-Morph: Defense's packs (packs/**/*.zip, plain zip archives), read-only, and EXOR's text format.

The game mounts every pack into one tree; a pack whose name's leading number is higher replaces the same path in a
lower one (00_win32_data.zip < 01_win32_data.zip < 02_...). Maps are kept a second way: a map is a folder
maps[_N]/<map>/ and the highest N that has the map is its current revision (maps/story_japan, maps_6/story_japan,
maps_7/story_japan: maps_7 is the one the game plays).

    Packs(root)                          root: the game folder (or its packs/)
        .read(path) -> bytes             the current file at a path
        .exists(path), .paths(prefix)
        .map_dir(name) -> str            'maps_7/story_japan'
    parse(text) -> Node                  EXOR's '<Name> { key "value" <Child> { ... } }' (.ent, .entities, .game,
                                         .att, .terrain, ...) and Ogre's material scripts' blocks

The game is data: nothing in a pack runs (its Lua is never executed or imported).
"""
from __future__ import annotations

import os
import re
import zipfile
from dataclasses import dataclass, field

STEAM_DIR = r'D:\steam\steamapps\common\X-Morph Defense'   # the developer machine's install (tools/prep_xmorph.py)


class PackError(ValueError):
    pass


def _rank(name: str) -> tuple[int, str]:
    m = re.match(r'(\d+)_', os.path.basename(name))
    return (int(m.group(1)) if m else 0, name)


class Packs:
    def __init__(self, root: str = STEAM_DIR) -> None:
        packs = root if os.path.basename(os.path.normpath(root)) == 'packs' else os.path.join(root, 'packs')
        if not os.path.isdir(packs):
            raise PackError(f'{root}: no packs folder (X-Morph: Defense not installed there)')
        zips = []
        for d, _dirs, files in os.walk(packs):
            zips += [os.path.join(d, f) for f in files if f.lower().endswith('.zip')]
        self.root = packs
        self._where: dict[str, tuple[zipfile.ZipFile, str]] = {}
        self._zips = []
        for z in sorted(zips, key=_rank):          # higher ranks last: they replace
            zf = zipfile.ZipFile(z)
            self._zips.append(zf)
            for info in zf.infolist():
                if not info.is_dir():
                    self._where[info.filename.replace('\\', '/').lower()] = (zf, info.filename)

    def close(self) -> None:
        for z in self._zips:
            z.close()

    def exists(self, path: str) -> bool:
        return path.replace('\\', '/').lower() in self._where

    def read(self, path: str) -> bytes:
        key = path.replace('\\', '/').lower()
        if key not in self._where:
            raise PackError(f'{path}: not in the packs')
        zf, name = self._where[key]
        return zf.read(name)

    def text(self, path: str) -> str:
        return self.read(path).decode('utf-8-sig', errors='replace')

    def paths(self, prefix: str = '') -> list[str]:
        p = prefix.replace('\\', '/').lower()
        return sorted(k for k in self._where if k.startswith(p))

    def map_dir(self, name: str) -> str:
        """The current revision's folder of a map: maps_N/<name> with the highest N (bare maps/ is 0)."""
        best = None
        for k in self._where:
            m = re.match(r'(maps(?:_(\d+))?)/([^/]+)/game/', k)
            if m and m.group(3) == name.lower():
                n = int(m.group(2) or 0)
                if best is None or n > best[0]:
                    best = (n, f'{m.group(1)}/{m.group(3)}')
        if best is None:
            raise PackError(f'map {name}: not in the packs')
        return best[1]


# ------------------------------------------------------------------------------------------ text format

@dataclass
class Node:
    name: str
    values: dict[str, list[str]] = field(default_factory=dict)    # key -> every value written under it, in order
    children: list['Node'] = field(default_factory=list)

    def get(self, key: str, default: str | None = None) -> str | None:
        v = self.values.get(key)
        return v[-1] if v else default

    def child(self, name: str) -> 'Node | None':
        return next((c for c in self.children if c.name == name), None)

    def all(self, name: str) -> list['Node']:
        return [c for c in self.children if c.name == name]

    def find(self, name: str) -> list['Node']:
        """Every descendant named name, depth first."""
        out = []
        for c in self.children:
            if c.name == name:
                out.append(c)
            out += c.find(name)
        return out

    def floats(self, key: str) -> tuple[float, ...]:
        v = self.get(key)
        if v is None:
            raise PackError(f'{self.name}: no {key}')
        return tuple(float(x) for x in v.split())

    def vector(self) -> tuple[float, float, float]:
        """A '{ x "1" y "2" z "3" }' block (missing axes are 0: the game writes only non-zero ones)."""
        return tuple(float(self.get(a, '0')) for a in 'xyz')          # type: ignore[return-value]


_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|([{}])|(\n)|([^\s{}"]+)')
NL = 'nl'


def parse(text: str) -> Node:
    """EXOR's blocks. A line's head word and what follows it on that line:
        Name                     then '{' (here or on a later line): a child block
        key "value"              a value (written once per occurrence: a key may repeat)
        key "value" {            a child carrying a value (aim_volume "SphericalAimVolume" { ... }): its 'type'
        key word word ...        a value of the words (a material script's 'set_texture_alias diffuse a.dds'),
                                 or, when '{' follows, a child whose 'type' is the words ('material a : b')
    '//' comments run to the line's end."""
    toks = []
    for q, brace, nl, word in _TOKEN.findall(re.sub(r'//[^\n]*', '', text)):
        if brace:
            toks.append((brace, brace))
        elif nl:
            toks.append((NL, ''))
        elif word:
            toks.append(('w', word))
        else:
            toks.append(('q', q))
    root = Node('')
    stack = [root]
    i, n = 0, len(toks)

    def next_real(j: int) -> int:
        while j < n and toks[j][0] == NL:
            j += 1
        return j

    while i < n:
        kind, val = toks[i]
        if kind == NL:
            i += 1
            continue
        if kind == '}':
            if len(stack) == 1:
                raise PackError('unbalanced }')
            stack.pop()
            i += 1
            continue
        if kind == '{':
            raise PackError('a block without a name')
        j = i + 1
        rest = []
        while j < n and toks[j][0] in ('q', 'w'):
            rest.append(toks[j])
            j += 1
        k = next_real(j)
        if k < n and toks[k][0] == '{':
            child = Node(val)
            if rest:
                child.values['type'] = [' '.join(v for _t, v in rest)]
            stack[-1].children.append(child)
            stack.append(child)
            i = k + 1
            continue
        # A lone word is a flag ('#endif' in material scripts, an empty 'TowerComp'): kept with an empty value.
        stack[-1].values.setdefault(val, []).append(' '.join(v for _t, v in rest))
        i = j
    if len(stack) != 1:
        raise PackError(f'{len(stack) - 1} block(s) left open')
    return root
