"""Installed generated assets are the cache: no duplicate game-derived blobs.

Recipes hash the generator and its transitive local imports (also function-local imports).
Release builds ship these hashes, since a onefile executable has no Python source files.
CPK identity uses path, size, mtime and ctime rather than hashing many GB on every update.
External model inputs and installed outputs are content-hashed. Shared mutable weapon
tables and the optional stock-helicopter requests deliberately stay outside this cache.
"""
from __future__ import annotations

import ast
import json
import os
from pathlib import Path
import sys
from typing import Any

import ledger
from modfiles import atomic_write, sha256, sha256_file

MANIFEST = '.edf6vc_builds.json'
RECIPES = 'asset_recipes.json'
GROUPS = ('jets', 'sub', 'katyusha', 'artillery', 'chute', 'drill', 'emc', 'sidecar', 'bigmap')
MODEL_INPUTS = {'artillery': 'twin_tank', 'drill': 'drill_tank'}


def source_recipes(root: str) -> dict[str, str]:
    """Fingerprint each generator's dependency closure, without importing or executing it."""
    modules = {p.stem: p for folder in ('pylib', 'tools', 'testrange')
               for p in (Path(root) / folder).glob('*.py')}
    dependencies: dict[str, set[str]] = {}
    digests: dict[str, str] = {}
    for name, path in modules.items():
        raw = path.read_bytes()
        digests[name] = sha256(raw)
        imports: set[str] = set()
        tree = ast.parse(raw)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imports.update(a.name.split('.')[0] for a in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                imports.add(node.module.split('.')[0])
            elif isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'import_module' and node.args:
                arg = node.args[0]
                if isinstance(arg, ast.Constant) and isinstance(arg.value, str):
                    imports.add(arg.value)
                elif isinstance(arg, ast.Subscript) and isinstance(arg.value, ast.Name):
                    for assignment in tree.body:
                        targets = assignment.targets if isinstance(assignment, ast.Assign) else [assignment.target] if isinstance(assignment, ast.AnnAssign) else []
                        if any(isinstance(t, ast.Name) and t.id == arg.value.id for t in targets):
                            imports.update(ast.literal_eval(assignment.value).values())
        dependencies[name] = imports & modules.keys()
    out = {}
    for group in GROUPS:
        seen: set[str] = set()
        pending = ['make_' + group]
        while pending:
            name = pending.pop()
            if name not in seen:
                seen.add(name)
                pending.extend(dependencies[name] - seen)
        out[group] = sha256(json.dumps({n: digests[n] for n in sorted(seen)}, sort_keys=True).encode())
    return out


def recipes() -> dict[str, str]:
    if getattr(sys, 'frozen', False):
        path = Path(sys._MEIPASS) / 'plugin' / RECIPES
        with path.open(encoding='utf-8') as f:
            return json.load(f)
    return source_recipes(str(Path(__file__).resolve().parent.parent))


def inputs(game: str, group: str) -> dict[str, Any]:
    """Cheap stock archive identity, plus content of the selected external model folder."""
    archive = Path(game) / ('Chunk02.cpk' if group == 'bigmap' else 'Root.cpk')
    st = archive.stat()
    result: dict[str, Any] = {'archive': [str(archive.resolve()), st.st_size, st.st_mtime_ns, st.st_ctime_ns]}
    if group in MODEL_INPUTS:
        import obj_model
        folder = obj_model.model_dir(MODEL_INPUTS[group])
        result['model'] = None if folder is None else {
            str(p.relative_to(folder)).replace('\\', '/'): sha256_file(str(p))
            for p in sorted(Path(folder).rglob('*')) if p.is_file()}
        if folder is not None:
            result['external'] = model_external_inputs(folder)
    return result


def model_external_inputs(folder: str) -> dict[str, str | None]:
    """Follow OBJ material libraries and MTL absolute/CWD texture references like obj_model.

    Missing references are included: a texture appearing later can take precedence over
    the same-named fallback next to the OBJ. Geometry need not be parsed to discover these.
    """
    import obj_model
    root = Path(folder).resolve()
    external: dict[str, str | None] = {}

    def record(path: Path) -> None:
        path = path.resolve()
        if not path.is_relative_to(root):
            external[str(path)] = sha256_file(str(path))

    for obj in root.rglob('*.obj'):
        with obj.open(encoding='utf-8', errors='replace') as f:
            for line in f:
                parts = line.split()
                if not parts or parts[0] != 'mtllib':
                    continue
                library = obj.parent / ' '.join(parts[1:])
                record(library)
                if library.is_file():
                    for material in obj_model.read_mtl(str(library)).values():
                        if material.diffuse_map:
                            record(Path(material.diffuse_map))
    return external


class Cache:
    def __init__(self, game: str) -> None:
        self.game = game
        self.path = os.path.join(game, 'Mods', MANIFEST)
        self.recipes = recipes()
        self.entries: dict[str, Any] = {}
        self.signatures: dict[str, dict] = {}
        try:
            with open(self.path, encoding='utf-8') as f:
                data = json.load(f)
            if isinstance(data, dict) and data.get('version') == 1 and isinstance(data.get('groups'), dict):
                self.entries = data['groups']
        except (OSError, ValueError):
            pass  # absent/corrupt cache is always a rebuild, never an install failure

    def current(self, group: str) -> bool:
        signature = {'recipe': self.recipes[group], 'inputs': inputs(self.game, group)}
        self.signatures[group] = signature
        entry = self.entries.get(group)
        if not isinstance(entry, dict) or entry.get('signature') != signature:
            return False
        outputs = entry.get('outputs')
        if not isinstance(outputs, dict) or not outputs:
            return False
        led = ledger.Ledger(self.game)
        if set(outputs) != set(led.owned_by(group)):
            return False
        for rel, digest in outputs.items():
            # Only ledger-owned relative paths are read, never arbitrary paths from the cache.
            if not isinstance(rel, str) or rel.startswith(('/', '\\')) or ':' in rel or '..' in rel.replace('\\', '/').split('/'):
                return False
            if led.files[rel].get('sha') != digest or sha256_file(led.disk(rel)) != digest:
                return False
        return True

    def record(self, group: str, files: dict[str, bytes]) -> None:
        """Remember expected generated bytes, not whatever a subsequent writer leaves on disk."""
        self.entries[group] = {'signature': self.signatures[group],
                               'outputs': {ledger.key(p): sha256(b) for p, b in files.items()}}

    def save(self) -> None:
        atomic_write(self.path, json.dumps({'version': 1, 'groups': self.entries}, sort_keys=True).encode())
