"""The slim generator (tools/build_release.py build_generator) is frozen without GENERATOR_EXCLUDES. Every module its
in-game run needs must therefore stay outside that list: run the generator's own planning step (loadout_generator
generate_variants -> support_loadout.missing_names) on the shipped ini with each excluded module made unimportable.
Regression: support_config was excluded while support_loadout.crew_files imports it, so every sortie in the game said
'ModuleNotFoundError: No module named support_config' (2026-11-27). Pure Python; no game files."""
from __future__ import annotations

import ast
import importlib.abc
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for p in ('tools', 'pylib'):
    sys.path.insert(0, os.path.join(ROOT, p))


def excludes() -> tuple[str, ...]:
    tree = ast.parse(open(os.path.join(ROOT, 'tools', 'build_release.py'), encoding='utf-8').read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'GENERATOR_EXCLUDES' for t in node.targets):
            return tuple(ast.literal_eval(node.value))
    raise SystemExit('FAIL: tools/build_release.py has no GENERATOR_EXCLUDES')


class Block(importlib.abc.MetaPathFinder):
    def __init__(self, names: tuple[str, ...]) -> None:
        self.names = set(names)

    def find_spec(self, fullname, path=None, target=None):
        if fullname.split('.')[0] in self.names:
            raise ModuleNotFoundError(f"No module named '{fullname}' (excluded from the slim generator)", name=fullname)
        return None


blocked = excludes()
for name in blocked:
    sys.modules.pop(name, None)
sys.meta_path.insert(0, Block(blocked))

import loadout_generator  # noqa: E402,F401
import support_loadout  # noqa: E402

raw = open(os.path.join(ROOT, 'EDF6VehicleCrew.ini'), 'rb').read()
text = (raw[3:] if raw.startswith(b'\xef\xbb\xbf') else raw).decode('utf-8')
# Crew colours set, so crew_files walks every crew (the path that imported support_config).
text += '\r\nSupportCrewColour=1E3A8A\r\nSupportVehicleCrewWeapon=rifle\r\n'
with tempfile.TemporaryDirectory() as game:
    os.makedirs(os.path.join(game, 'Mods', 'Plugins'))
    names = support_loadout.missing_names(game, text, [])
print(f'generator_excludes_test: planning ran with {len(blocked)} modules excluded ({len(names)} files wanted)')
