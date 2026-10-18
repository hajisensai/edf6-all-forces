"""Exports an EDF4.1 asset pack (pylib/edf41src.py) from an EDF4.1 install: exactly the EDF4.1 files the install-time
converters read (tools/edf41_maps.py sources: every map's MAC / MAD / RAB, unpacked from 4.1's archives;
tools/edf41_sounds.py sources41: the weapon sound bank, the two voice banks and their lists, the subtitle plugin's text
when that 4.1 has it; tools/edf41_menu_bg.py SOURCES: the HQ background, unpacked from Root.cpk), laid out as EDF4.1 lays them out, and the manifest with each file's size, SHA-256 and EDF4.1
fingerprint. A player without EDF4.1 then gets the 4.1 maps and sounds from it: the share package carries it as the
folder `edf41` next to the installer (or $EDF41_ASSETS names it).

Developer tool; reads EDF4.1 only. Run again into the same folder, it rewrites only files that changed in EDF4.1 (a
file kept is hashed again first). The manifest is removed before anything is written and written last: an export cut
short is no pack (the installer finds none there) until it is run again. Files under the pack's folders that the new
manifest does not list are deleted (they would make the pack refused).

Usage:
  python tools/export_edf41_assets.py OUT [--edf41 DIR]     export (DIR: an EDF4.1 install; default: the one found)
  python tools/export_edf41_assets.py --check OUT [--full]  a pack's faults (--full: every SHA-256 too); exit 1 on any
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import edf41_maps  # noqa: E402
import edf41_menu_bg  # noqa: E402
import edf41_sounds  # noqa: E402
import edf41src  # noqa: E402
import gamedir  # noqa: E402

README = '说明.txt'
README_TEXT = """EDF4.1 素材包（EDF6 全军出击安装器用）

这是从 EDF4.1 里抽出的、安装器转换 4.1 任务地图、4.1 音效 / 语音 / 字幕和 4.1 本部背景所需的原始文件（{count} 个，{size}）。
放法：整个 {folder} 文件夹和「EDF6VehicleCrew安装器.exe」放在同一个文件夹里（不要改名，不要只拷一部分）。
没装 EDF4.1 时，安装器会从这里转换；装了 EDF4.1 时，安装器优先用已安装的 EDF4.1。
文件清单与 SHA-256 在 {manifest}；缺文件或文件被改过时，用到它的那张地图或那组声音不会安装，安装器会说明原因。
"""


def wanted() -> list[str]:
    """Every rel the pack holds (subtitle files only when the EDF4.1 has them: export decides)."""
    return [rel for name in edf41_maps.MAPS for rel in edf41_maps.sources(name)] + edf41_sounds.sources41() + \
        list(edf41_menu_bg.SOURCES)


def _sha_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 22), b''):
            h.update(block)
    return h.hexdigest()


def _copy(src: str, dst: str) -> str:
    """`src` copied to `dst` through a temporary file; its SHA-256 (of what was written)."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + '.tmp-export'
    h = hashlib.sha256()
    with open(src, 'rb') as i, open(tmp, 'wb') as o:
        for block in iter(lambda: i.read(1 << 22), b''):
            h.update(block)
            o.write(block)
    os.replace(tmp, dst)
    return h.hexdigest()


def _write(dst: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + '.tmp-export'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, dst)


def export(root41: str, out: str, say=print) -> dict:  # noqa: ANN001 - say(str)
    """The pack written to `out` from the EDF4.1 at `root41`; its manifest."""
    if edf41src.is_pack(root41) or not edf41src._is_edf41(root41):
        raise SystemExit(f'{root41} 不是已安装的 EDF4.1（要有 EDF41.exe 和 Root.cpk）')
    if os.path.normcase(os.path.abspath(out)).startswith(os.path.normcase(os.path.abspath(root41)) + os.sep):
        raise SystemExit('输出目录不能在 EDF4.1 目录里面')
    old: dict[str, dict] = {}
    manifest_path = os.path.join(out, edf41src.MANIFEST)
    if edf41src.is_pack(out):
        try:
            old = edf41src.Pack(out).files
        except ValueError:
            old = {}
        os.remove(manifest_path)   # an export cut short leaves no pack
    os.makedirs(out, exist_ok=True)
    archives = edf41_maps.Archives(root41)
    menu = edf41_menu_bg.Source(root41)
    files: dict[str, dict] = {}
    reused = 0
    for rel in wanted():
        dst = os.path.join(out, *rel.split('/'))
        prev = old.get(edf41src.key(rel))
        if rel.startswith('MAP/'):
            name = rel.split('/', 1)[1]
            source = archives.fingerprint(name)
        elif rel in edf41_menu_bg.SOURCES:
            source = menu.fingerprint(rel)
        else:
            src = os.path.join(root41, *rel.split('/'))
            if not os.path.isfile(src):
                if rel.split('/')[0] == edf41_sounds.PLUGINS[0]:
                    say(f'  没有 {rel}（字幕插件的文本，可选：没有时字幕用台词编号）')
                    continue
                raise SystemExit(f'EDF4.1 缺少 {rel}')
            st = os.stat(src)
            source = [st.st_size, st.st_mtime_ns]
        if prev and prev.get('source') == source and os.path.isfile(dst) and os.path.getsize(dst) == prev['size'] \
                and _sha_file(dst) == prev['sha256']:
            files[rel] = prev
            reused += 1
            continue
        say(f'  导出 {rel}')
        if rel.startswith('MAP/') or rel in edf41_menu_bg.SOURCES:
            data = archives.read(name) if rel.startswith('MAP/') else menu.read(rel)
            _write(dst, data)
            files[rel] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'source': source}
            del data
        else:
            sha = _copy(src, dst)
            files[rel] = {'size': os.path.getsize(dst), 'sha256': sha, 'source': source}
    listed = {edf41src.key(r) for r in files}
    for top in edf41src.TOP:
        for d, _dirs, names in os.walk(os.path.join(out, top)):
            for n in names:
                rel = os.path.relpath(os.path.join(d, n), out).replace(os.sep, '/')
                if edf41src.key(rel) not in listed:
                    say(f'  删除清单外的 {rel}')
                    os.remove(os.path.join(d, n))
    manifest = {'kind': edf41src.KIND, 'format': edf41src.FORMAT, 'files': files}
    total = sum(e['size'] for e in files.values())
    with open(os.path.join(out, README), 'w', encoding='utf-8') as f:
        f.write(README_TEXT.format(count=len(files), size=f'{total / 2**30:.2f} GB', folder=edf41src.FOLDER,
                                   manifest=edf41src.MANIFEST))
    tmp = manifest_path + '.tmp-export'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
    os.replace(tmp, manifest_path)
    say(f'素材包：{out}，{len(files)} 个文件（{reused} 个未变），{total} 字节（{total / 2**30:.2f} GB）')
    return manifest


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('out')
    ap.add_argument('--edf41', help='an EDF4.1 install (default: $EDF41_DIR, next to EDF6, the Steam libraries)')
    ap.add_argument('--check', action='store_true', help="report the pack's faults instead of exporting")
    ap.add_argument('--full', action='store_true', help='with --check: every SHA-256 too')
    args = ap.parse_args(argv)
    if args.check:
        faults = edf41src.check(args.out, full=args.full)
        for why in faults:
            print(why)
        print(f'{len(faults)} 个问题' if faults else '素材包完整' + ('（含 SHA-256）' if args.full else '（大小）'))
        return 1 if faults else 0
    root41 = args.edf41 or gamedir.find_other(gamedir.EDF41, near=gamedir.find())
    if not root41:
        print('没有找到 EDF4.1：用 --edf41 指定')
        return 1
    export(root41, args.out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
