"""EDF6VehicleCrew installer / uninstaller: the program the release zip ships (built into an .exe by
tools/build_release.py), so players do not need Python.

What install does, with EDF6.exe closed:
  1. finds the game directory (pylib/gamedir.py: next to the exe, then the Steam libraries) or asks for it;
  2. makes everything first, in memory, from the player's own Root.cpk (only read): the call weapons stacked
     onto the shared weapon table (call_weapons.stack, which also checks the table and its texts line up and
     offers repair when they do not), the jets, helicopters and drones (make_jets.build) and the submarine
     carrier (make_sub.build), the ejection's parachute canopy (make_chute.build), the big map
     (make_bigmap.build: the test range's plain stitched 3 x 3, seamless).
     They cannot be shipped prebuilt: they are derived from the game's files, and the
     weapon table is shared with other mods. Nothing is written unless all of it could be made;
  3. writes them: the generated objects (each file atomically, recorded in the ownership ledger,
     pylib/ledger.py), then the weapon table, its texts and the call SGOs in one transaction (all or none,
     call_weapons.commit), then EDF6AutoTurret's vehicle data (autoturret/tools/build.py: the Kepler flak, the
     Bohr, the NPC Titan's side guns, their text rows; its own manifest Mods/.edf6at_data.json and backups), and
     last the plugins (PLUGINS: EDF6VehicleCrew and EDF6AutoTurret): each DLL, and its .ini (a new one when there
     is none; else the player's own, with only the settings this version adds appended: merge_ini); then the big
     map (it sets BigWorld in EDF6VehicleCrew.ini) and the test range's target-only mission (testrange/gen.py target_range, on
     its slot), so everyone in an online room has the same map and the same objects (the user, 2026-10-05: one pack
     to play with others). The test range's forced loadout is never written: everyone picks their own class.

With StockVehicleStores=1 (or the older StockHeliStores=1) in the player's ini (on by default) install also gives the
stock vehicles' requests the stores they should carry (tools/make_stock_stores.py: the tanks, the missile launcher, the
flak, the bikes, the helicopters; EDF6AutoTurret's flak requests get theirs through its own manifest); with it 0 it takes
back what an earlier install gave them.

Uninstall removes the plugins and, when asked, the call weapons (their rows become placeholders that keep the
row numbers saves use), EDF6AutoTurret's vehicle data and the generated objects no other tool still needs.

Menu 3 downloads the newest build from the test site, menu 4 sends the logs back to it (tools/testhub.py;
the site itself is testhub/), menu 5 checks what is installed (check: reads only, the game may be running).
"""
from __future__ import annotations

import os
import re
import sys
import traceback
import time
from types import ModuleType
from typing import Any

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (HERE, os.path.join(HERE, '..', 'pylib'), os.path.join(HERE, '..', 'testrange'),
           os.path.join(HERE, '..', 'autoturret', 'tools')):
    sys.path.insert(0, os.path.normpath(_p))

import gamedir  # noqa: E402
import modfiles  # noqa: E402

PLUGIN = 'EDF6VehicleCrew'
PROCESS = modfiles.PROCESS
SECTION = 'VehicleCrew'
# Every plugin the pack ships, (file name without .dll / .ini, the ini's section): CMake builds each into
# build/Mods/Plugins (CMakeLists.txt, autoturret/CMakeLists.txt), tools/build_release.py bundles each one's DLL and
# ini into the exe, install writes them, uninstall removes them, check compares them (selftest pack_ships_every_plugin).
PLUGINS = ((PLUGIN, SECTION), ('EDF6AutoTurret', 'AutoTurret'))
PLUGIN_FILES = ('.dll', '.ini')            # shipped
# What a plugin writes beside itself: its log, the log rotated away (src/plugin.cpp RotateLog), the Primers' trace
# (src/primer.cpp TraceFile, ini PrimerTrace).
PLUGIN_RUNTIME = ('.log', '.log.1', '.primer.csv')
ADDED_HEADER = '; ---- 新版本新增的设置（安装器补入，默认值）----'
# Settings whose shipped default became on (the user, 2026-10-07: "还有什么默认是关的，都打开，都装mod了，肯定要打开啊"):
# key -> (the old default, the new one). An existing ini still holding the old default gets the new one, once: the
# marker line it gets keeps a later install from turning back on what the player has turned off since. The others at 0
# are values, choices or debug logs, not features off (BigWorld is make_bigmap's to set with the big map).
NEW_DEFAULTS: dict[str, dict[str, tuple[str, str]]] = {
    SECTION: {'HeliLandMs': ('0', '5000'), 'RescueAutoBoard': ('0', '1'), 'SeatSwitchOnline': ('0', '1'),
              'StockVehicleStores': ('0', '1')},
}
DEFAULTS_MARK = '; edf6vc-defaults-2026-10-07：安装器已把仍是旧默认值 0 的 HeliLandMs、RescueAutoBoard、SeatSwitchOnline、' \
                'StockVehicleStores 改成开启（想关就改回，之后安装不会再改）'


def bundle_dir() -> str:
    """Where the shipped plugin files are: inside the onefile exe, else the build output (build.cmd)."""
    if getattr(sys, 'frozen', False):
        return os.path.join(sys._MEIPASS, 'plugin')  # type: ignore[attr-defined]
    return os.path.normpath(os.path.join(HERE, '..', 'build', 'Mods', 'Plugins'))


def plugin_files() -> dict[str, tuple[bytes, bytes]]:
    """Each plugin's DLL and default ini (PLUGINS order), as shipped (or as build.cmd built them)."""
    src = bundle_dir()
    paths = [os.path.join(src, name + ext) for name, _ in PLUGINS for ext in PLUGIN_FILES]
    missing = [p for p in paths if not os.path.isfile(p)]
    if missing:
        raise SystemExit(f'找不到 {", ".join(missing)}：先运行 build.cmd 构建插件')
    out: dict[str, tuple[bytes, bytes]] = {}
    for name, _ in PLUGINS:
        dll, ini = (modfiles.read(os.path.join(src, name + ext)) for ext in PLUGIN_FILES)
        out[name] = (dll, ini)
    return out


def ask(prompt: str) -> str:
    try:
        return input(prompt).strip()
    except EOFError:
        return ''


def pick_game() -> str | None:
    found = gamedir.find()
    if found:
        print(f'游戏目录：{found}')
        return found
    print('没有自动找到游戏目录（需要同时有 EDF6.exe 和 Root.cpk）。')
    while True:
        path = ask('请把游戏目录粘贴到这里后回车（直接回车退出）：').strip('"')
        if not path:
            return None
        if gamedir.is_game(path):
            return os.path.normpath(path)
        print('这个目录里没有 EDF6.exe 和 Root.cpk，请重新输入。')


def check_loader(game: str) -> None:
    if not (os.path.isfile(os.path.join(game, 'winmm.dll')) or os.path.isfile(os.path.join(game, 'ModLoader.ini'))):
        print('！ 没有发现 EDFModLoader（游戏目录里的 winmm.dll / ModLoader.ini）。'
              '本插件靠它加载，请先装好 EDFModLoader，否则装了也不会生效。')


# ---------------------------------------------------------------- the ini

_KEY = re.compile(r'^\s*([A-Za-z0-9_]+)\s*=')
_SECTION = re.compile(r'^\s*\[([^\]]+)\]')


def _keys(lines: list[str], section_name: str = SECTION) -> dict[str, int]:
    """Key (lower case) -> its line, within [section_name]; section names are case-insensitive as in Win32."""
    out: dict[str, int] = {}
    section = ''
    for i, line in enumerate(lines):
        m = _SECTION.match(line)
        if m:
            section = m.group(1).strip()
            continue
        m = _KEY.match(line)
        if m and section.lower() == section_name.lower():
            out.setdefault(m.group(1).lower(), i)
    return out


def merge_ini(user: str, shipped: str, section: str = SECTION) -> tuple[str, list[str], list[str]]:
    """The player's ini with every setting of the shipped one it lacks appended to [section] (each with
    the comment lines above it in the shipped file), and nothing of theirs changed. Returns (text, keys added,
    keys of theirs the shipped ini no longer has: the plugin ignores them)."""
    nl = '\r\n' if '\r\n' in user else '\n'
    have = _keys(user.splitlines(), section)
    ship_lines = shipped.splitlines()
    ship = _keys(ship_lines, section)
    added: list[str] = []
    block: list[str] = []
    for key, i in ship.items():
        if key in have:
            continue
        j = i
        while j > 0 and ship_lines[j - 1].lstrip().startswith(';') and not ship_lines[j - 1].lstrip().startswith('; ----'):
            j -= 1
        block += ship_lines[j:i + 1]
        added.append(_KEY.match(ship_lines[i]).group(1))
    gone = [k for k in (_KEY.match(user.splitlines()[i]).group(1) for i in have.values()) if k.lower() not in ship]
    if not added:
        return user, added, gone
    lines = user.splitlines()
    heads = [(i, m.group(1).strip()) for i, m in ((i, _SECTION.match(x)) for i, x in enumerate(lines)) if m]
    start = next((i for i, name in heads if name.lower() == section.lower()), None)
    if start is None:
        lines.append(f'[{section}]')
        start = len(lines) - 1
    end = next((i for i, _ in heads if i > start), len(lines))   # the section runs to the next one
    while end > start + 1 and not lines[end - 1].strip():
        end -= 1
    insert = ['', ADDED_HEADER, *block]
    merged = lines[:end] + insert + lines[end:]
    return nl.join(merged) + nl, added, gone


def apply_new_defaults(text: str, section: str = SECTION) -> tuple[str, list[str]]:
    """`text` (an ini) with each NEW_DEFAULTS key of [section] that still holds its old default given the new one, and
    DEFAULTS_MARK after the section's head; nothing when the mark is there already. Returns (text, keys changed)."""
    flips = NEW_DEFAULTS.get(section, {})
    if not flips or DEFAULTS_MARK.split('：', 1)[0] in text:
        return text, []
    nl = '\r\n' if '\r\n' in text else '\n'
    lines = text.splitlines()
    have = _keys(lines, section)
    changed: list[str] = []
    for key, (old, new) in flips.items():
        i = have.get(key.lower())
        if i is None:
            continue
        name, _, rest = lines[i].partition('=')
        value, sep, comment = rest.partition(';')
        if value.strip() != old:
            continue
        lines[i] = f'{name}={new}' + (f' {sep}{comment}' if sep else '')
        changed.append(key)
    head = next((i for i, x in enumerate(lines) if (m := _SECTION.match(x)) and m.group(1).strip().lower() == section.lower()), None)
    if head is None:
        return text, []
    lines.insert(head + 1, DEFAULTS_MARK)
    return nl.join(lines) + nl, changed


def planned_ini(user: str, shipped: str, section: str = SECTION) -> tuple[str, list[str], list[str], list[str]]:
    """The player's ini as install_plugin writes it: merge_ini, then apply_new_defaults. (text, added, gone, turned on)."""
    text, added, gone = merge_ini(user, shipped, section)
    text, flipped = apply_new_defaults(text, section)
    return text, added, gone, flipped


def player_ini_text(game: str, shipped_ini: bytes) -> str:
    """The ini the plugin will read after this install: the player's own as install_plugin leaves it (planned_ini),
    else the shipped one."""
    path = os.path.join(game, 'Mods', 'Plugins', PLUGIN + '.ini')
    shipped = (shipped_ini[3:] if shipped_ini.startswith(b'\xef\xbb\xbf') else shipped_ini).decode('utf-8', errors='replace')
    if not os.path.isfile(path):
        return shipped
    with open(path, 'rb') as f:
        raw = f.read()
    raw = raw[3:] if raw.startswith(b'\xef\xbb\xbf') else raw
    return planned_ini(raw.decode('utf-8', errors='replace'), shipped)[0]


def install_plugin(game: str, dll: bytes, shipped_ini: bytes, name: str = PLUGIN, section: str = SECTION) -> None:
    dst = os.path.join(game, 'Mods', 'Plugins')
    modfiles.atomic_write(os.path.join(dst, name + '.dll'), dll)
    print(f'写入 {os.path.join(dst, name + ".dll")}')
    ini = os.path.join(dst, name + '.ini')
    if not os.path.isfile(ini):
        # A fresh install has already adopted this release's defaults. Record that
        # now, before the player changes one, so a later update keeps their choice.
        bom = shipped_ini.startswith(b'\xef\xbb\xbf')
        text, _ = apply_new_defaults(shipped_ini.decode('utf-8-sig'), section)
        modfiles.atomic_write(ini, (b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))
        print(f'写入 {ini}')
        return
    with open(ini, 'rb') as f:
        raw = f.read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    user = raw[3:].decode('utf-8') if bom else raw.decode('utf-8', errors='surrogateescape')
    text, added, gone, flipped = planned_ini(user, shipped_ini.decode('utf-8'), section)
    if text != user:   # the migration marker itself must persist even when every default was already current
        modfiles.atomic_write(ini, (b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8', errors='surrogateescape'))
    if added:
        print(f'保留你的 {ini}，补入新版本新增的设置：{", ".join(added)}')
    elif not flipped:
        print(f'保留你的 {ini}（没有需要补的新设置）')
    if flipped:
        print(f'  这些设置的默认值已改为开启，你的 ini 里还是旧默认值，已改成开启：{", ".join(flipped)}（想关就改回，之后安装不会再改）')
    if gone:
        print(f'  其中 {", ".join(gone)} 新版本已不再使用，可以手动删掉')


def remove_plugin(game: str) -> None:
    """Every plugin of the pack (PLUGINS), its ini and the logs it wrote beside itself."""
    for name, _ in PLUGINS:
        for ext in PLUGIN_FILES + PLUGIN_RUNTIME:
            path = os.path.join(game, 'Mods', 'Plugins', name + ext)
            if os.path.isfile(path):
                os.remove(path)
                print(f'删除 {path}')


# ---------------------------------------------------------------- install / uninstall


def misaligned(game: str, error: Exception) -> bool:
    """Explains call_weapons.Misaligned (the weapon texts no longer line up with the table). True when
    call_weapons.repair can fix it (a first install kept the files as they were), after warning what a repair
    loses."""
    import call_weapons
    print(f'\n！ {error}')
    if not os.path.isfile(os.path.join(game, 'Mods', call_weapons.MANIFEST)):
        return False
    later = call_weapons.changed_since(game, call_weapons.load_manifest(game), call_weapons.SHARED)
    if later:
        print('  注意：这些文件在本插件上次安装之后被别的工具改过，恢复会丢掉那些改动（之后重新运行那个 MOD 的安装即可补回）：'
              + '、'.join(later))
    return True


def repair_weapons(game: str) -> None:
    import call_weapons
    for rel in call_weapons.repair(game):
        print(f'恢复 {rel}')


def stack_weapons(game: str) -> dict[str, bytes] | None:
    """The call weapons stacked onto the weapon table; when its texts no longer line up, offers to put the
    files back as they were before the first install (call_weapons.repair) and stacks again. None: cancelled."""
    import call_weapons
    try:
        return call_weapons.stack(game)
    except call_weapons.Misaligned as e:
        if not misaligned(game, e):
            return None
        if ask('输入 y 把武器表和武器说明恢复到第一次安装本插件之前的样子，然后继续安装；其它 = 取消：').lower() != 'y':
            return None
        repair_weapons(game)
        return call_weapons.stack(game)


def retire_weapons(game: str) -> bool:
    """Turns the call weapons into placeholders (call_weapons.uninstall). When the weapon texts no longer line
    up with the table, offers what install offers (repair: the files back to before the first install, which
    takes the call weapons out too), or to leave the table alone and remove only the plugin and the generated
    objects. False: cancelled, nothing changed."""
    import call_weapons
    try:
        call_weapons.uninstall(game)
        return True
    except call_weapons.Misaligned as e:
        repairable = misaligned(game, e)
    print('  也可以跳过武器表：武器表和武器说明一个字节都不动，只删插件和生成的模型（呼叫武器的行留着，照原版 KM6 呼叫；'
          '玩家喷气机请求要用的模型仍被这些行登记着，会保留）。')
    prompt = ('输入 y 把武器表和武器说明恢复到第一次安装本插件之前的样子（呼叫武器随之去掉），然后继续卸载；'
              if repairable else '') + '输入 s 跳过武器表继续卸载；其它 = 取消：'
    choice = ask(prompt).lower()
    if choice == 'y' and repairable:
        repair_weapons(game)
        return True
    return choice == 's'


def build_autoturret(game: str) -> tuple[dict[str, bytes], bool] | None:
    """EDF6AutoTurret's vehicle data made in memory (autoturret/tools/build.py build_files, from Root.cpk), and
    whether install_autoturret may back up and overwrite the Mods files it replaces that are not its own (another
    mod's, or its own changed since: build.py --force, here asked). None: cancelled, nothing written."""
    import build as at_build
    import make_optics
    files = {rel: make_optics.redirect(data)[0] if rel.upper().startswith('OBJECT/') and rel.upper().endswith('.SGO') else data
             for rel, data in at_build.build_files().items()}
    problems = at_build.foreign(os.path.join(game, 'Mods'), files)
    if not problems:
        return files, False
    print('\n！ EDF6AutoTurret 的车辆数据要替换下面这些不是它写的文件（别的 MOD 的，或它写之后被改过）：')
    for p in problems:
        print('  ', p)
    if ask('输入 y 先备份这些文件再替换（卸载时按备份恢复）；其它 = 取消：').lower() != 'y':
        return None
    return files, True


def install_autoturret(game: str, files: dict[str, bytes], force: bool) -> None:
    """Writes EDF6AutoTurret's vehicle data and their WEAPONTEXT rows (after the call weapons: the rows go into the
    tables as call_weapons left them), recorded in its own manifest with backups (autoturret/tools/build.py)."""
    import build as at_build
    print('写入 EDF6AutoTurret 的车辆数据（防空车、玻尔斯、关卡防空车、NPC 泰坦副炮和它们的武器说明行；'
          '记录在 Mods/.edf6at_data.json）……')
    import make_optics
    import ledger
    import rootcpk
    led, source = ledger.Ledger(game), None
    final = {}
    for rel, data in files.items():
        if rel.upper().startswith('OBJECT/') and rel.upper().endswith('.SGO'):
            data, needs = make_optics.redirect(data)
            if needs:
                if source is None:
                    source = rootcpk.Game(game)
                data, _ = make_optics.range_vehicle(led, source, data, make_optics.OWNER)
        final[rel] = data
    at_build.install(os.path.join(game, 'Mods'), text=True, force=force, files=final, proteus=True)


def remove_autoturret(game: str) -> None:
    """Puts back what install_autoturret (or autoturret/tools/build.py install) replaced, by its manifest; a file
    or row changed since by someone else stays. Without the manifest there is nothing recorded to put back."""
    import build as at_build
    mods = os.path.join(game, 'Mods')
    if not at_build.installed(mods):
        return
    print('恢复 EDF6AutoTurret 改过的车辆数据（按 Mods/.edf6at_data.json；之后被别的 MOD 改过的文件和行保持原样）……')
    at_build.uninstall(mods, force=False)


def build_asset(cache: Any, module: ModuleType, label: str, builder: Any = None) -> Any:
    """Only regenerate an asset group when its recipe, inputs or installed outputs changed."""
    started = time.perf_counter()
    if cache.current(module.OWNER):
        print(f'{label}：校验通过，复用已有资源（{time.perf_counter() - started:.1f} 秒）', flush=True)
        return None
    print(f'{label}：生成中（首次安装或输入/输出发生变化）……', flush=True)
    built = (builder or module.build)(cache.game)
    print(f'{label}：生成完成（{time.perf_counter() - started:.1f} 秒）', flush=True)
    return built


def install(game: str, campaign_requested: bool = False) -> None:
    import buildcache
    import call_weapons
    import gen
    import make_artillery
    import make_bigmap
    import make_drill
    import make_edf5_campaign
    import make_emc
    import make_chute
    import make_jets
    import make_katyusha
    import make_sazabi
    import make_proteus
    import make_optics
    import make_stock_stores
    import make_sidecar
    import make_sub
    import rootcpk
    rootcpk.use(game)
    check_loader(game)
    plugins = plugin_files()
    stock_stores = make_stock_stores.wanted(player_ini_text(game, plugins[PLUGIN][1]))
    if call_weapons.recover(game):
        print('上次运行没有完成：已把武器表相关文件恢复到那次运行之前。')
    print('检查武器表并生成呼叫武器（只读 Root.cpk 与现有武器表）……')
    weapons = stack_weapons(game)
    if weapons is None:
        print('已取消，没有写入任何文件。')
        return
    print('生成 EDF6AutoTurret 的车辆数据（只读 Root.cpk）……')
    turret = build_autoturret(game)
    if turret is None:
        print('已取消，没有写入任何文件。')
        return
    cache = buildcache.Cache(game)
    optics = build_asset(cache, make_optics, '载具实体瞄具模型', make_optics.build_models)
    jets = build_asset(cache, make_jets, '战机、直升机、无人机')
    sub = build_asset(cache, make_sub, '潜水母舰')
    katyusha = build_asset(cache, make_katyusha, '喀秋莎火箭炮车')
    artillery = build_asset(cache, make_artillery, '自行榴弹炮')
    chute = build_asset(cache, make_chute, '降落伞')
    drill = build_asset(cache, make_drill, '钻头战车')
    emc = build_asset(cache, make_emc, 'EMC 蓄力光束')
    stock = None
    if stock_stores:
        print('给原版载具的请求加上应有的挂载（坦克、导弹车、防空车、摩托、直升机；ini StockVehicleStores=1；读取 Root.cpk，不修改它）……')
        # EDF6AutoTurret's flak / Bohr requests are its own: built on from its bytes and written back through its manifest
        files, skipped, handed = make_stock_stores.build(game, overlay={rel: data for rel, data in turret[0].items()
                                                                       if rel.upper().startswith('WEAPON/')})
        turret = ({**turret[0], **handed}, turret[1])
        stock = files, skipped
    sidecar = build_asset(cache, make_sidecar, '边三轮摩托')
    sazabi = build_asset(cache, make_sazabi, '沙扎比（模型生成约 1.5 分钟）')
    proteus = build_asset(cache, make_proteus, '普罗透斯支撑桩和护盾模型')
    bigmap = build_asset(cache, make_bigmap, '大地图（3 x 3 无缝平原，只读 Chunk02.cpk）')
    campaign = None
    if campaign_requested or make_edf5_campaign.wanted(game):
        print('生成可选实验 EDF5 战役（本篇、DLC1、DLC2 三个独立任务包；原始 BVM 脚本尚未逐关验证，缺少资源的 4 关不会安装）……')
        try:
            campaign = make_edf5_campaign.build(game)
        except make_edf5_campaign.Refused as e:
            print('！ 不安装 EDF5 战役：', e)
    print('\n全部生成完毕，开始写入。')
    if campaign is None and not make_edf5_campaign.wanted(game):
        # Finish an interrupted opt-out before updating the rest of the installation.
        make_edf5_campaign.remove(game)
        if make_edf5_campaign.enabled(game):
            raise make_edf5_campaign.Refused('无法完成 EDF5 战役停用：任务列表已被其他工具修改，已保留依赖文件。')
    for path in (make_optics.install_models(game, optics) if optics is not None else []):
        print('写入', path)
    for path in (make_jets.install(game, jets) if jets is not None else []) + \
            (make_sub.install(game, sub) if sub is not None else []) + \
            (make_katyusha.install(game, katyusha) if katyusha is not None else []) + \
            (make_artillery.install(game, artillery) if artillery is not None else []) + \
            (make_chute.install(game, chute) if chute is not None else []) + \
            (make_drill.install(game, drill) if drill is not None else []) + \
            (make_emc.install(game, emc) if emc is not None else []) + \
            (make_sidecar.install(game, sidecar) if sidecar is not None else []) + \
            (make_sazabi.install(game, sazabi) if sazabi is not None else []) + \
            (make_proteus.install(game, proteus) if proteus is not None else []):
        print('写入', path)
    if stock is not None:   # after make_jets: the stores' weapon files are its
        files, skipped = stock
        for path in make_stock_stores.install(game, files):
            print('写入', path)
        for rel in skipped:
            print('跳过（别的 mod 已经放了自己的请求文件，保持原样）', rel)
    else:
        for path in make_stock_stores.remove(game)[0]:
            print('删除（StockVehicleStores=0：原版载具的请求恢复原样）', path)
    print('写入呼叫武器（武器表只动本插件的行，其它行不动；全部写完或全部不写）……')
    call_weapons.install(game, weapons)
    install_autoturret(game, *turret)
    # Mutable stock SGOs are read after every primary writer, outside the expensive model cache.
    # AutoTurret's SGOs were redirected before its own manifest write; never layer another hash over them.
    stock_optics = {rel: data for rel, data in make_optics.build_stock_redirects(game).items()
                    if rel.upper() not in {p.upper() for p in turret[0]}}
    for path in make_optics.install_stock_redirects(game, stock_optics):
        print('写入', path)
    for name, section in PLUGINS:
        install_plugin(game, *plugins[name], name, section)
    if campaign is not None:   # after the plugin's ini: it sets EDF5CampaignContent there
        for path in make_edf5_campaign.install(game, campaign):
            print('写入', path)
        print(make_edf5_campaign.summary(campaign))
        for group, path, why in campaign[2]['skipped']:
            print('  跳过', group, path, why)
    if bigmap is not None:
        for path in make_bigmap.install(game, built=bigmap):
            print('写入', path)
    else:
        make_bigmap.set_big_world(game, make_bigmap.world_half(1))
    for group, files in (('jets', jets), ('sub', sub), ('katyusha', katyusha), ('artillery', artillery),
                         ('chute', chute), ('drill', drill), ('emc', emc), ('sidecar', sidecar), ('sazabi', sazabi), ('proteus', proteus), ('optics', optics)):
        if files is not None:
            cache.record(group, files)
    if bigmap is not None:
        mac, pieces = bigmap
        cache.record('bigmap', {f'MAP/{make_bigmap.MAP_FILE}': mac,
                                **{f'MAP/{name}': data for name, data in pieces.items()}})
    cache.save()  # assets succeeded: a later mission failure must not force expensive regeneration
    print('写入测试场关卡（只有靶子，没有敌人；联机时大家要有同样的关卡和物体）……')
    for line in gen.install(game, gen.target_range(gen.Plan())):
        print('  ', line)
    print('\n安装完成。启动游戏即可。')
    print('联机请同时更新配套 EDF Coop：全军出击房间仅对兼容的 MOD 玩家开放。')
    print('本次模型、挂载、实体瞄具和测试场资源已校验并安装；更新时请运行安装器，不要只替换 DLL。')


def uninstall_stock_stores(game: str) -> None:
    """The stock vehicles' stores taken back (make_stock_stores) while EDF6AutoTurret's data stays: its flak requests,
    which bring this tool's vehicle with the stores, rewritten first as it builds them alone (through its manifest), so
    no request is left bringing a vehicle that is gone."""
    import make_stock_stores
    import build as at_build
    if os.path.isfile(os.path.join(game, 'Mods', at_build.MANIFEST)):
        try:
            install_autoturret(game, at_build.build_files(), False)
        except SystemExit as e:   # a file of its changed by someone else since: it refuses, and the vehicle stays
            print('！ 没能重写 EDF6AutoTurret 的防空车请求：', e)
    deleted, kept = make_stock_stores.remove(game)
    for path in deleted:
        print('删除（原版载具的挂载要插件才能用，随插件一起删）', path)
    for path in kept:
        print('保留（之后被别的工具改过，或别的工具的请求还在用它）', path)


def uninstall(game: str) -> None:
    import gen
    import make_artillery
    import make_bigmap
    import make_drill
    import make_emc
    import make_chute
    import make_jets
    import make_katyusha
    import make_sazabi
    import make_proteus
    import make_optics
    import make_stock_stores
    import make_sidecar
    import make_sub
    import buildcache
    import rootcpk
    rootcpk.use(game)
    print('卸载会删掉插件（EDF6VehicleCrew、EDF6AutoTurret）。呼叫武器可以一起删：武器表里它们的行会变成「已卸载」的占位行，')
    print('效果和原版 KM6 轰炸机呼叫（玩家喷气机请求则是原版 N9 Eros）相同，行号保住，存档装备着也不会崩溃。')
    choice = ask('输入 1 = 插件和呼叫武器、生成的模型、AutoTurret 的车辆数据一起删；'
                 '输入 2 = 只删插件（武器、生成的模型和车辆数据留着，照原版 KM6 呼叫、炮照原版开火；'
                 '原版载具的额外挂载要插件才能用，照样删掉）；其它 = 取消：')
    if choice not in ('1', '2'):
        print('已取消。')
        return
    import make_edf5_campaign
    if make_edf5_campaign.removal_blocked(game):
        print('已取消卸载：EDF5 战役的模式表（CONFIG.SGO）或旧版任务列表被其他工具改过，不能安全撤回；保留任务文件和插件以免读档崩溃。')
        return
    if choice == '1':   # the call weapons point at the generated SGOs: those go only with the rows
        if not retire_weapons(game):
            print('已取消，没有删除任何文件。')
            return
        # First detach stock paths, preserving other writers' fields; consumers keep shared optic models.
        make_optics.remove(game)
        remove_autoturret(game)
        for remove in (make_stock_stores.remove, make_proteus.remove, make_sazabi.remove, make_sidecar.remove, make_emc.remove, make_drill.remove, make_chute.remove, make_artillery.remove, make_katyusha.remove,
                       make_sub.remove, make_jets.remove):
            deleted, kept = remove(game)
            for path in deleted:
                print('删除', path)
            for path in kept:
                print('保留（之后被别的工具改过）', path)
    if choice == '2':   # the stock vehicles' stores need the plugin to fire: they go with it (with 1 they went above)
        uninstall_stock_stores(game)
    if gen.uninstall(game):
        print('删除测试场关卡')
    # with 2 too: without the plugin the packs would stay listed as content the player does not own
    import make_edf5_campaign
    if make_edf5_campaign.installed(game):
        print('EDF5 战役：模式表、文本表还原成安装前的样子，删除 3 个任务包的任务列表（各任务包的存档 DEFP_E5*.MST 留着，重新安装后照旧显示）。')
        done, kept = make_edf5_campaign.remove(game)
        for path in done:
            print('还原', path)
        for path in kept:
            print('保留（之后被别的工具改过）', path)
    for path in make_bigmap.remove(game)[0]:
        print('删除', path)
    if choice == '1':
        for path in make_optics.remove(game)[0]:
            print('还原/删除瞄具资源', path)
    cache = os.path.join(game, 'Mods', buildcache.MANIFEST)
    if choice == '1' and os.path.isfile(cache):   # what it describes is gone; with 2 the models stay and it holds
        os.remove(cache)
        print('删除', cache)
    remove_plugin(game)
    print('\n卸载完成。')


def _text(path: str) -> str | None:
    raw = modfiles.read(path)
    if raw is None:
        return None
    return (raw[3:] if raw.startswith(b'\xef\xbb\xbf') else raw).decode('utf-8', errors='replace')


def check_range(game: str) -> bool:
    """The installer always writes the default slot; mission files are outside the asset ledger."""
    import gen
    out = gen.mission_dir(game, gen.DEFAULT_SLOT)
    missing = [name for name in ('MISSION.AC', 'MISSION.RMPA', gen.MARKER)
               if not modfiles.read(os.path.join(out, name))]
    print('\n测试场关卡：' + (f'缺失或为空：{", ".join(missing)}' if missing else '文件齐全'))
    return not missing


def check(game: str) -> bool:
    """Menu 5: what is installed against this pack, reading only (the game may be running): each plugin's DLL
    (this pack's or another build) and ini (settings this version adds still missing), EDF6AutoTurret's vehicle data
    (autoturret/tools/build.py check), the call weapons (call_weapons.check) and the generated files (the ledger:
    present and as written). True when everything is this pack's, complete."""
    import build as at_build
    import call_weapons
    import ledger
    import rootcpk
    rootcpk.use(game)
    ok = True
    shipped = plugin_files()
    for name, section in PLUGINS:
        have = modfiles.read(os.path.join(game, 'Mods', 'Plugins', name + '.dll'))
        same = have == shipped[name][0]
        ok &= same
        print(f'{name}.dll：' + ('与本安装包相同' if same else '缺失' if have is None else
                                 '与本安装包不同（旧版本或别的构建：退出游戏后运行安装器选 1 更新）'))
        text = _text(os.path.join(game, 'Mods', 'Plugins', name + '.ini'))
        lacking = merge_ini(text, shipped[name][1].decode('utf-8'), section)[1] if text is not None else []
        ok &= text is not None and not lacking
        print(f'{name}.ini：' + ('缺失' if text is None else f'缺少新版本的设置 {", ".join(lacking)}（选 1 会补上）'
                                 if lacking else '完整'))
    print('\nEDF6AutoTurret 车辆数据（Mods/.edf6at_data.json）：')
    ok &= at_build.check(os.path.join(game, 'Mods'))
    print('\n呼叫武器：')
    ok &= call_weapons.check(game)
    led = ledger.Ledger(game)
    gone = [k for k in sorted(led.files) if not os.path.isfile(led.disk(k))]
    changed = [k for k in sorted(led.files) if led.changed(k)]
    ok &= bool(led.files) and not gone and not changed
    print(f'\n生成的文件（Mods/{ledger.MANIFEST}）：{len(led.files)} 个，缺失 {len(gone)}，被改过 {len(changed)}')
    for k in gone:
        print('  缺失', k)
    for k in changed:
        print('  被改过', k)
    ok &= check_range(game)
    import make_edf5_campaign
    print()
    ok &= make_edf5_campaign.check(game)
    print('\n检查结果：' + ('全部是本安装包的，完整。' if ok else '有缺失或不一致（见上），退出游戏后运行安装器选 1 即可修复。'))
    return ok


def build_name() -> str:
    """This installer's build: build_release.py bundles build_info.json (the zip's name without .zip)."""
    import json
    try:
        with open(os.path.join(bundle_dir(), 'build_info.json'), encoding='utf-8') as f:
            return json.load(f).get('name', '')
    except (OSError, ValueError):
        return ''


def download_latest() -> int:
    """Menu 3: the newest build from the test site, unpacked next to this exe; offers to start its installer."""
    import testhub
    hub = testhub.hub_for_player(ask)
    here = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__))
    exe = testhub.download_latest(hub, here, build_name())
    if exe and ask('输入 y 现在打开新版安装器；其它 = 不打开：').lower() == 'y':
        os.startfile(exe)  # type: ignore[attr-defined]
        print('新版安装器已在另一个窗口打开，这个窗口可以关掉了。')
    return 0


def send_logs() -> int:
    """Menu 4: the logs and a note back to the developer (the game may be running: the logs are opened shared)."""
    import testhub
    game = pick_game()
    if not game:
        return 1
    testhub.send_logs(testhub.hub_for_player(ask), game, ask, build_name())
    return 0


def manage_campaign(game: str) -> int:
    """Menu 6: explicit experimental campaign opt-in or removal, without uninstalling the plugins."""
    import make_edf5_campaign
    print('EDF5 战役默认开启：普通安装 / 更新会加上 EDF5 本篇、DLC1、DLC2 三个独立任务包（离线模式的「任务包」里选）；手动停用后保留停用选择。')
    print('原始 BVM 脚本尚未逐关验证，不能保证所有任务可以正常游玩；缺少资源的 4 关不会安装。')
    print('当前状态：' + ('已启用' if make_edf5_campaign.enabled(game) else '未启用'))
    choice = ask('输入 1 启用 / 更新实验战役（同时更新插件），2 停用战役（保留插件），其它 = 取消：')
    if choice == '1':
        install(game, campaign_requested=True)
        return 0 if make_edf5_campaign.enabled(game) and make_edf5_campaign.check(game) else 1
    if choice == '2':
        done, kept = make_edf5_campaign.remove(game, remember_disabled=True)
        for path in done:
            print('还原', path)
        for path in kept:
            print('保留（之后被别的工具改过）', path)
        if make_edf5_campaign.enabled(game):
            print('未能停用：模式表或旧版任务列表被其他工具改过，已保留它依赖的任务文件和插件。')
            return 1
        print('EDF5 战役已停用，后续普通更新不会重新启用。')
    return 0


def main(argv: list[str]) -> int:
    print(f'== {PLUGIN} 安装程序 {build_name()} ==\n')
    mode = argv[0] if argv else ''
    if mode not in ('install', 'uninstall', 'update', 'logs', 'check', 'campaign'):
        pick = ask('输入 1 安装 / 更新，2 卸载，3 下载最新测试版，4 回传日志给开发者，5 检查安装状态，6 管理 EDF5 实验战役，回车退出：')
        mode = {'1': 'install', '2': 'uninstall', '3': 'update', '4': 'logs', '5': 'check', '6': 'campaign'}.get(pick, '')
        if not mode:
            return 0
    if mode == 'check':   # reads only: the game may be running
        game = pick_game()
        return 1 if not game else 0 if check(game) else 1
    if mode in ('update', 'logs'):
        import testhub
        try:
            return download_latest() if mode == 'update' else send_logs()
        except testhub.HubError as e:
            print(f'\n没有完成：{e}')
            return 1
    if modfiles.game_running():
        print(f'{PROCESS} 正在运行。请先退出游戏再运行本程序（本程序不会替你关游戏）。')
        return 1
    game = pick_game()
    if not game:
        return 1
    if mode == 'campaign':
        return manage_campaign(game)
    (install if mode == 'install' else uninstall)(game)
    return 0


def offer_error_report(text: str) -> None:
    """After an installer error: the error and the logs to the test site, if the player wants."""
    if not getattr(sys, 'frozen', False) or ask('输入 y 把这个错误和日志发给开发者；其它 = 不发：').lower() != 'y':
        return
    try:
        import testhub
        hub = testhub.hub_for_player(ask)
        files = [('installer-error.txt', text.encode('utf-8'))]
        game = gamedir.find()
        if game:
            files.append(('logs.zip', testhub.collect(game, build_name())[0]))
        rid = testhub.post_report(hub, None, 'new', '安装器出错（自动回传）', build_name(), files)
        print(f'已发送（#{rid}）。')
    except Exception as e:   # a courtesy: its failure must not hide the error above
        print(f'发送失败：{e}')


def run() -> int:
    try:
        code = main(sys.argv[1:])
    except SystemExit as e:  # the tools report refusals this way
        if e.code not in (None, 0):
            print(f'\n未完成：{e.code}')
        code = 0 if e.code in (None, 0) else 1
    except Exception:
        traceback.print_exc()
        print('\n出错了，上面是错误信息。武器表和武器说明要么全部写入、要么已恢复原样；'
              '生成的模型每个文件要么是新的要么是旧的。问题解决后重新运行安装即可。')
        offer_error_report(traceback.format_exc())
        code = 1
    if getattr(sys, 'frozen', False):
        ask('\n按回车关闭窗口……')
    return code


if __name__ == '__main__':
    sys.exit(run())
