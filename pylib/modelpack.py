"""The split share package: a model pack (models/ + MODELS.json) and a main pack (installer + models-required.json).

The model pack's version is a hash of its file names and contents, so the same models always give the same version
and the main pack can say which one it needs. The installer refuses an install whose models/ is not that pack
(a first install without the cockpit parts crashes); a plain CI / nightly zip has no models-required.json and is
not checked."""
from __future__ import annotations

import hashlib
import json
import os

MANIFEST = 'MODELS.json'            # in <models>/ of the model pack
REQUIRED = 'models-required.json'   # next to the installer in the main pack


def scan(models: str) -> dict[str, int]:
    """{path relative to models/ ('/' separators): size} of every file except the manifest."""
    out = {}
    for dirpath, dirnames, files in os.walk(models):
        dirnames.sort()
        for f in files:
            rel = os.path.relpath(os.path.join(dirpath, f), models).replace(os.sep, '/')
            if rel != MANIFEST:
                out[rel] = os.path.getsize(os.path.join(dirpath, f))
    return dict(sorted(out.items()))


def version(models: str, files: dict[str, int]) -> str:
    """12 hex digits over every file's relative path and content."""
    h = hashlib.sha256()
    for rel in files:
        h.update(rel.encode('utf-8') + b'\0')
        with open(os.path.join(models, rel), 'rb') as f:
            while chunk := f.read(1 << 20):
                h.update(chunk)
        h.update(b'\0')
    return h.hexdigest()[:12]


def manifest(models: str) -> dict:
    files = scan(models)
    return {'version': version(models, files), 'files': files}


def problem(app_dir: str) -> str | None:
    """None when this installer needs no model pack or models/ is the pack it needs; else why not (Chinese, for the
    player). Compares the manifest's version and every file's presence and size, without reading the models."""
    try:
        with open(os.path.join(app_dir, REQUIRED), encoding='utf-8-sig') as f:
            want = json.load(f)['models_version']
    except OSError:
        return None
    except (ValueError, KeyError):
        return f'{REQUIRED} 损坏，请重新解压主包。'
    models = os.environ.get('EDF6VC_MODELS') or os.path.join(app_dir, 'models')   # obj_model.model_roots() order
    try:
        with open(os.path.join(models, MANIFEST), encoding='utf-8-sig') as f:
            have = json.load(f)
    except (OSError, ValueError):
        return (f'这个安装器需要模型包 {want}，但安装器旁的 models 文件夹里没有它。'
                f'请把“模型包”zip 解压到安装器所在文件夹（合并进 models）后重试。')
    if not isinstance(have, dict) or have.get('version') != want:
        return (f'这个安装器需要模型包 {want}，但 models 文件夹是 {have.get("version") if isinstance(have, dict) else "?"}。'
                f'请解压对应版本的“模型包”zip 覆盖进 models 后重试。')
    files = have.get('files') if isinstance(have, dict) else None
    if not isinstance(files, dict):
        return f'模型包 {want} 的 {MANIFEST} 损坏，请重新解压“模型包”zip。'
    for rel, size in files.items():
        p = os.path.join(models, *str(rel).split('/'))
        if not os.path.isfile(p) or os.path.getsize(p) != size:
            return f'模型包 {want} 不完整（{rel} 缺失或大小不对）。请重新解压“模型包”zip。'
    return None
