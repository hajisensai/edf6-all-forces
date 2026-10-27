"""The split share package: a model pack (models/ + edf41/ + models/MODELS.json) and a main pack (installer +
models-required.json).

The model pack carries the large data that changes rarely: every RELEASE_MODELS folder (models/) and the EDF4.1 asset
pack (edf41/, pylib/edf41src.py: 4.1's maps, sounds, voices and subtitles; 2026-10-27 user: it goes with the models,
not in every main pack). Its version is a hash of its file names and contents, so the same files always give the same
version and the main pack can say which one it needs. The installer refuses an install whose models/ and edf41/ are
not that pack (a first install without the cockpit parts crashes); a plain CI / nightly zip has no models-required.json
and is not checked."""
from __future__ import annotations

import hashlib
import json
import os

MANIFEST = 'MODELS.json'            # in <models>/ of the model pack
REQUIRED = 'models-required.json'   # next to the installer in the main pack
PARTS = ('models', 'edf41')         # the model pack's folders, unpacked next to the installer


def part_dir(app_dir: str, part: str) -> str:
    """Where the installer reads `part` from: models/ as obj_model.model_roots() finds it first, edf41/ beside it."""
    if part == 'models':
        return os.environ.get('EDF6VC_MODELS') or os.path.join(app_dir, 'models')
    return os.path.join(app_dir, part)


def scan(app_dir: str) -> dict[str, int]:
    """{'<part>/<path>' ('/' separators): size} of every file of every part except the manifest."""
    out = {}
    for part in PARTS:
        root = part_dir(app_dir, part)
        for dirpath, dirnames, files in os.walk(root):
            dirnames.sort()
            for f in files:
                rel = part + '/' + os.path.relpath(os.path.join(dirpath, f), root).replace(os.sep, '/')
                if rel != f'models/{MANIFEST}':
                    out[rel] = os.path.getsize(os.path.join(dirpath, f))
    return dict(sorted(out.items()))


def path_of(app_dir: str, rel: str) -> str:
    part, _, rest = str(rel).partition('/')
    return os.path.join(part_dir(app_dir, part), *rest.split('/'))


def version(app_dir: str, files: dict[str, int]) -> str:
    """12 hex digits over every file's relative path and content."""
    h = hashlib.sha256()
    for rel in files:
        h.update(rel.encode('utf-8') + b'\0')
        with open(path_of(app_dir, rel), 'rb') as f:
            while chunk := f.read(1 << 20):
                h.update(chunk)
        h.update(b'\0')
    return h.hexdigest()[:12]


def manifest(app_dir: str) -> dict:
    """The manifest of the models/ and edf41/ next to `app_dir` (the folder the installer is in)."""
    files = scan(app_dir)
    return {'version': version(app_dir, files), 'files': files}


def problem(app_dir: str) -> str | None:
    """None when this installer needs no model pack or models/ + edf41/ are the pack it needs; else why not (Chinese,
    for the player). Compares the manifest's version and every file's presence and size, without reading the files."""
    try:
        with open(os.path.join(app_dir, REQUIRED), encoding='utf-8-sig') as f:
            want = json.load(f)['models_version']
    except OSError:
        return None
    except (ValueError, KeyError):
        return f'{REQUIRED} 损坏，请重新解压主包。'
    try:
        with open(os.path.join(part_dir(app_dir, 'models'), MANIFEST), encoding='utf-8-sig') as f:
            have = json.load(f)
    except (OSError, ValueError):
        return (f'这个安装器需要模型包 {want}，但安装器旁的 models 文件夹里没有它。'
                f'请把“模型包”zip 解压到安装器所在文件夹（合并进 models 和 edf41）后重试。')
    if not isinstance(have, dict) or have.get('version') != want:
        return (f'这个安装器需要模型包 {want}，但 models 文件夹是 {have.get("version") if isinstance(have, dict) else "?"}。'
                f'请解压对应版本的“模型包”zip 覆盖进 models 和 edf41 后重试。')
    files = have.get('files') if isinstance(have, dict) else None
    if not isinstance(files, dict) or any(str(rel).partition('/')[0] not in PARTS for rel in files):
        return f'模型包 {want} 的 {MANIFEST} 损坏，请重新解压“模型包”zip。'
    for rel, size in files.items():
        p = path_of(app_dir, rel)
        if not os.path.isfile(p) or os.path.getsize(p) != size:
            return f'模型包 {want} 不完整（{rel} 缺失或大小不对）。请重新解压“模型包”zip。'
    return None
