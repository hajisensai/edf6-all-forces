"""The test site (testhub/, https://edf6.fushi.moe): the installer's menu items 3 (download the newest test build)
and 4 (send the logs back), and the developer's commands. Standard library only: the installer exe bundles it.

Player side (tools/installer.py calls these):
  download_latest()   the newest build from the site into a folder next to the exe, checked against its SHA-256;
  send_logs(game)     the plugins' logs, ini files, versions and a recent crash dump, zipped, with a test case,
                      a result and a note.
The account (given by the developer) is asked once and kept in %APPDATA%/EDF6AllForces/testhub.json.

Developer side:
  python tools/testhub.py state                          builds, cases, the last reports
  python tools/testhub.py pull [--since N] [--out DIR]   reports after #N with their files, into DIR (default tmp/testhub)
  python tools/testhub.py reply ID TEXT                  answer a report (shown under it on the page)
  python tools/testhub.py case add TITLE [--steps T] [--status S] [--note T] [--sort N]
  python tools/testhub.py case set ID [--title T] [--steps T] [--status S] [--note T] [--sort N]
  python tools/testhub.py publish ZIP [--channel test] [--notes TEXT] [--commit SHA]
  status: todo / verify (fixed, to be checked) / pass / fail / closed.
The developer's account comes from $EDF6_TESTHUB_AUTH (user:password) or the same json file.
"""
from __future__ import annotations

import argparse
import base64
import glob
import hashlib
import io
import json
import os
import platform
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import zipfile

SITE = os.environ.get('EDF6_TESTHUB_URL', 'https://edf6.fushi.moe').rstrip('/')
CONFIG = os.path.join(os.environ.get('APPDATA') or os.path.expanduser('~'), 'EDF6AllForces', 'testhub.json')
LOG_TAIL = 12 << 20           # the last 12 MB of each log: the plugin's log grows to 32 MB before it rotates
DUMP_MAX = 60 << 20           # crash dumps bigger than this stay home
DUMP_AGE = 3 * 86400
UPLOAD_MAX = 95 << 20
RESULTS = {'1': ('pass', '修好了'), '2': ('fail', '还有问题'), '3': ('new', '发现新问题'), '4': ('info', '只是补充信息')}


class HubError(Exception):
    pass


# ---------------------------------------------------------------- the account

def _load_config() -> dict:
    try:
        with open(CONFIG, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def _save_config(cfg: dict) -> None:
    os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
    with open(CONFIG, 'w', encoding='utf-8') as f:
        json.dump(cfg, f, ensure_ascii=False, indent=1)


def _auth_header(user: str, password: str) -> str:
    return 'Basic ' + base64.b64encode(f'{user}:{password}'.encode('utf-8')).decode('ascii')


class Hub:
    def __init__(self, user: str, password: str, site: str = SITE) -> None:
        self.site = site
        self.user = user
        self.auth = _auth_header(user, password)

    def request(self, method: str, path: str, data: bytes | None = None, headers: dict | None = None,
                timeout: float = 60) -> urllib.request.addinfourl:
        req = urllib.request.Request(self.site + path, data=data, method=method,
                                     headers={'Authorization': self.auth, 'User-Agent': 'edf6-testhub', **(headers or {})})
        try:
            return urllib.request.urlopen(req, timeout=timeout)
        except urllib.error.HTTPError as e:
            try:
                msg = json.loads(e.read().decode('utf-8')).get('error', '')
            except (ValueError, OSError):
                msg = ''
            if e.code == 401:
                raise HubError('账号或密码不对') from None
            raise HubError(f'测试站返回 {e.code} {msg}'.strip()) from None
        except urllib.error.URLError as e:
            raise HubError(f'连不上测试站 {self.site}：{e.reason}') from None

    def get_json(self, path: str):
        with self.request('GET', path) as r:
            return json.loads(r.read().decode('utf-8'))

    def post_json(self, path: str, body: dict):
        data = json.dumps(body, ensure_ascii=False).encode('utf-8')
        with self.request('POST', path, data, {'Content-Type': 'application/json'}) as r:
            return json.loads(r.read().decode('utf-8'))


def hub_for_player(ask) -> Hub:
    """The saved account, or the one the player types (checked against the site before it is saved)."""
    cfg = _load_config()
    if cfg.get('user') and cfg.get('password'):
        return Hub(cfg['user'], cfg['password'])
    print(f'第一次使用测试站（{SITE}）：输入开发者给你的账号和密码，之后会记住。')
    while True:
        user = ask('账号（直接回车取消）：')
        if not user:
            raise HubError('已取消')
        password = ask('密码：')
        hub = Hub(user, password)
        try:
            hub.get_json('/api/state')
        except HubError as e:
            print(f'  {e}，请重新输入。')
            continue
        _save_config({'user': user, 'password': password})
        print(f'已保存到 {CONFIG}')
        return hub


def hub_for_dev() -> Hub:
    auth = os.environ.get('EDF6_TESTHUB_AUTH', '')
    if ':' in auth:
        user, password = auth.split(':', 1)
        return Hub(user, password)
    cfg = _load_config()
    if cfg.get('user') and cfg.get('password'):
        return Hub(cfg['user'], cfg['password'])
    raise SystemExit(f'set EDF6_TESTHUB_AUTH=user:password (or write {CONFIG})')


# ---------------------------------------------------------------- download

def _progress(done: int, total: int) -> None:
    if total:
        print(f'\r  {done / total * 100:5.1f}%  {done >> 20} / {total >> 20} MB', end='', flush=True)


def download_latest(hub: Hub, into: str, current: str = '') -> str | None:
    """The newest build, unpacked into <into>/<zip name>/. Returns the installer exe's path, or None when the
    newest is the one running (current: this installer's build name)."""
    b = hub.get_json('/api/latest')
    name = b['name']
    stem = name[:-4] if name.lower().endswith('.zip') else name
    print(f'最新测试版：{name}（{b["size"] >> 20} MB，{time.strftime("%Y-%m-%d %H:%M", time.localtime(b["at"] / 1000))}）')
    if b.get('notes'):
        print('更新说明：\n  ' + b['notes'].strip().replace('\n', '\n  '))
    if current and stem == current:
        print('你现在运行的就是这一版，不用下载。')
        return None
    target = os.path.join(into, stem)
    exe = next(iter(glob.glob(os.path.join(target, '*.exe'))), None)
    if exe:
        print(f'已经下载过：{target}')
        return exe
    buf = io.BytesIO()
    digest = hashlib.sha256()
    with hub.request('GET', '/dl/' + urllib.parse.quote(name), timeout=120) as r:
        total = int(r.headers.get('Content-Length') or b['size'])
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            buf.write(chunk)
            digest.update(chunk)
            _progress(buf.tell(), total)
    print()
    if digest.hexdigest() != b['sha256']:
        raise HubError('下载的文件校验不对（可能下载中断了），请重试')
    with zipfile.ZipFile(buf) as z:
        z.extractall(target)
    print(f'已解压到 {target}')
    return next(iter(glob.glob(os.path.join(target, '*.exe'))), None)


# ---------------------------------------------------------------- logs

def _tail(path: str, limit: int) -> bytes:
    with open(path, 'rb') as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        f.seek(max(0, size - limit))
        data = f.read()
    if size > limit:
        data = f'(前面 {(size - limit) >> 20} MB 省略，只留最后 {limit >> 20} MB)\r\n'.encode('utf-8') + data
    return data


def _sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def _pe_stamp(path: str) -> str:
    """The PE header's TimeDateStamp (the plugins only run against EDF.dll 0x678CCB46)."""
    try:
        with open(path, 'rb') as f:
            head = f.read(4096)
        off = int.from_bytes(head[0x3C:0x40], 'little')
        return '0x%08X' % int.from_bytes(head[off + 8:off + 12], 'little')
    except (OSError, ValueError):
        return '?'


def collect(game: str, build: str = '') -> tuple[bytes, list[str]]:
    """The logs zipped: Mods/Plugins' logs (tails), ini files and DLL versions, the mod loader's log, the
    install ledgers (Mods/.edf6*.json), and the newest EDF6 crash dump of the last three days. Returns the zip
    and the names it holds."""
    mem = io.BytesIO()
    names: list[str] = []
    plugins = os.path.join(game, 'Mods', 'Plugins')
    with zipfile.ZipFile(mem, 'w', zipfile.ZIP_DEFLATED) as z:
        def add(path: str, arc: str, data: bytes | None = None) -> None:
            z.writestr(arc, data if data is not None else _tail(path, LOG_TAIL))
            names.append(arc)

        for path in sorted(glob.glob(os.path.join(plugins, '*.log')) + glob.glob(os.path.join(plugins, '*.log.1'))):
            add(path, 'Plugins/' + os.path.basename(path))
        for path in sorted(glob.glob(os.path.join(plugins, '*.ini'))):
            add(path, 'Plugins/' + os.path.basename(path))
        for path in sorted(glob.glob(os.path.join(game, '*.log')) + glob.glob(os.path.join(game, 'ModLoader.ini'))):
            add(path, 'game/' + os.path.basename(path))
        for path in sorted(glob.glob(os.path.join(game, 'Mods', '.edf6*.json'))):
            if os.path.getsize(path) < (4 << 20):
                add(path, 'Mods/' + os.path.basename(path))

        lines = [f'installer build: {build or "?"}', f'game: {game}', f'os: {platform.platform()}',
                 f'time: {time.strftime("%Y-%m-%d %H:%M:%S")}', '']
        for path in [os.path.join(game, 'EDF6.exe'), os.path.join(game, 'EDF.dll')] + \
                sorted(glob.glob(os.path.join(plugins, '*.dll'))) + sorted(glob.glob(os.path.join(game, '*.dll'))):
            if os.path.isfile(path):
                st = os.stat(path)
                lines.append(f'{os.path.relpath(path, game)}  {st.st_size}  '
                             f'{time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(st.st_mtime))}  pe {_pe_stamp(path)}  sha256 {_sha256(path)}')
        add('', 'versions.txt', '\r\n'.join(lines).encode('utf-8'))

        dumps = [p for p in glob.glob(os.path.join(os.environ.get('LOCALAPPDATA', ''), 'CrashDumps', 'EDF6*.dmp'))
                 if time.time() - os.path.getmtime(p) < DUMP_AGE and os.path.getsize(p) <= DUMP_MAX]
        if dumps:
            newest = max(dumps, key=os.path.getmtime)
            with open(newest, 'rb') as f:
                add(newest, 'crash/' + os.path.basename(newest), f.read())
    return mem.getvalue(), names


def _multipart(fields: dict[str, str], files: list[tuple[str, bytes]]) -> tuple[bytes, str]:
    boundary = uuid.uuid4().hex
    out = io.BytesIO()
    for k, v in fields.items():
        out.write(f'--{boundary}\r\nContent-Disposition: form-data; name="{k}"\r\n\r\n'.encode('utf-8'))
        out.write(v.encode('utf-8') + b'\r\n')
    for name, data in files:
        out.write(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{name}"\r\n'
                  'Content-Type: application/octet-stream\r\n\r\n'.encode('utf-8'))
        out.write(data + b'\r\n')
    out.write(f'--{boundary}--\r\n'.encode('utf-8'))
    return out.getvalue(), f'multipart/form-data; boundary={boundary}'


def post_report(hub: Hub, case_id: int | None, result: str, note: str, version: str,
                files: list[tuple[str, bytes]], source: str = 'exe') -> int:
    body, ctype = _multipart({'case_id': str(case_id or ''), 'result': result, 'note': note,
                              'version': version, 'source': source}, files)
    if len(body) > UPLOAD_MAX:
        raise HubError(f'要上传的内容有 {len(body) >> 20} MB，超过 {UPLOAD_MAX >> 20} MB')
    with hub.request('POST', '/api/report', body, {'Content-Type': ctype}, timeout=600) as r:
        return json.loads(r.read().decode('utf-8'))['id']


def send_logs(hub: Hub, game: str, ask, build: str = '') -> None:
    """Menu 4: pick the case, the result and a note; the logs go along."""
    st = hub.get_json('/api/state')
    cases = [c for c in st['cases'] if c['status'] != 'closed']
    print('\n现在要测的：')
    for c in cases:
        print(f'  {c["id"]:>3}  {c["title"]}')
    print('    0  其它 / 新问题')
    pick = ask('测的是哪一项？输入编号：')
    case_id = int(pick) if pick.isdigit() and any(c['id'] == int(pick) for c in cases) else None
    print('结果：1 修好了  2 还有问题  3 发现新问题  4 只是补充信息')
    result = RESULTS.get(ask('输入 1-4（直接回车 = 4）：') or '4', RESULTS['4'])[0]
    note = ask('一句话说说怎么测的、看到了什么（可以留空）：')
    print('收集日志……')
    data, names = collect(game, build)
    print('  ' + '、'.join(names))
    stamp = time.strftime('%Y%m%d-%H%M%S')
    rid = post_report(hub, case_id, result, note, build, [(f'logs-{stamp}.zip', data)])
    print(f'已上传（#{rid}，{len(data) >> 10} KB）。录屏、截图可以到 {SITE} 的反馈里补充。')


# ---------------------------------------------------------------- developer commands

def _print_state(st: dict) -> None:
    for b in st['builds'][:5]:
        print(f'build  {b["name"]}  {b["channel"]}  {time.strftime("%m-%d %H:%M", time.localtime(b["at"] / 1000))}')
    for c in st['cases']:
        print(f'case   #{c["id"]} [{c["status"]}] {c["title"]}')
    for r in st['reports'][:10]:
        files = ', '.join(f['name'] for f in r['files'])
        print(f'report #{r["id"]} case {r["case_id"]} {r["result"]} by {r["author"]} {r["version"]}'
              f'{" reply✓" if r["reply"] else ""}  {r["note"][:80]!r} {files}')


def _pull(hub: Hub, since: int, out: str) -> None:
    rs = hub.get_json(f'/api/reports?since={since}')['reports']
    for r in rs:
        d = os.path.join(out, f'report-{r["id"]}')
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'report.json'), 'w', encoding='utf-8') as f:
            json.dump(r, f, ensure_ascii=False, indent=1)
        for fl in r['files']:
            path = os.path.join(d, fl['name'])
            if not os.path.isfile(path):
                with hub.request('GET', f'/file/{fl["id"]}', timeout=600) as resp, open(path, 'wb') as f:
                    f.write(resp.read())
                if path.lower().endswith('.zip'):
                    with zipfile.ZipFile(path) as z:
                        z.extractall(path[:-4])
        print(f'#{r["id"]} {r["result"]} case {r["case_id"]}: {r["note"][:100]!r} -> {d}')
    print(f'{len(rs)} report(s); next: --since {max([r["id"] for r in rs], default=since)}')


def _publish(hub: Hub, zpath: str, channel: str, notes: str, commit: str) -> None:
    name = os.path.basename(zpath)
    stem = name[:-4]
    version = stem.split('-', 1)[1] if '-' in stem else stem
    with open(zpath, 'rb') as f:
        data = f.read()
    q = urllib.parse.urlencode({'name': name, 'version': version, 'channel': channel, 'commit': commit})
    with hub.request('PUT', f'/api/build?{q}', data, {'Content-Type': 'application/zip',
                                                      'X-Notes': urllib.parse.quote(notes)}, timeout=600) as r:
        print(r.read().decode('utf-8'))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('state')
    p = sub.add_parser('pull')
    p.add_argument('--since', type=int, default=0)
    p.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '..', 'tmp', 'testhub'))
    p = sub.add_parser('reply')
    p.add_argument('id', type=int)
    p.add_argument('text')
    p = sub.add_parser('case')
    p.add_argument('action', choices=('add', 'set'))
    p.add_argument('target', help='title (add) or id (set)')
    for k in ('title', 'steps', 'status', 'note'):
        p.add_argument('--' + k)
    p.add_argument('--sort', type=int)
    p = sub.add_parser('publish')
    p.add_argument('zip')
    p.add_argument('--channel', default='test')
    p.add_argument('--notes', default='')
    p.add_argument('--commit', default='')
    a = ap.parse_args(argv)
    hub = hub_for_dev()
    if a.cmd == 'state':
        _print_state(hub.get_json('/api/state'))
    elif a.cmd == 'pull':
        _pull(hub, a.since, os.path.normpath(a.out))
    elif a.cmd == 'reply':
        print(hub.post_json('/api/reply', {'report_id': a.id, 'text': a.text}))
    elif a.cmd == 'case':
        body = {k: v for k, v in (('title', a.title), ('steps', a.steps), ('status', a.status),
                                  ('dev_note', a.note), ('sort', a.sort)) if v is not None}
        if a.action == 'add':
            body['title'] = a.target
        else:
            body['id'] = int(a.target)
        print(hub.post_json('/api/case', body))
    elif a.cmd == 'publish':
        _publish(hub, a.zip, a.channel, a.notes, a.commit)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except HubError as e:
        raise SystemExit(str(e))
