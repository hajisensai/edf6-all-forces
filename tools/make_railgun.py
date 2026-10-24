"""Railgun turret emplacement (Sketchfab model) generator; --check is read-only, --out exports without installing."""
from __future__ import annotations
import sys
import make_tracked_combat as tracked

OWNER = 'railgun'

def build(root: str) -> dict[str, bytes]:
    return tracked.build(root, OWNER)

def install(root: str, files: dict[str, bytes]) -> list[str]:
    return tracked.install(root, files, OWNER)

def remove(root: str) -> tuple[list[str], list[str]]:
    return tracked.remove(root, OWNER)

def vehicle_sgo(game) -> bytes:
    return tracked.vehicle_sgo(game, OWNER)

def check(files: dict[str, bytes], game=None) -> None:
    tracked.check(files, OWNER, game)

if __name__ == '__main__':
    raise SystemExit(tracked.main(sys.argv[1:], OWNER))
