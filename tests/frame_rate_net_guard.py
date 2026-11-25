"""The plugin's online traffic keeps the rate of the ticks with the frame gate on (docs/unlimited-fps-re.md §2.1, §2.2,
§4.2, §4.4). A frame may run with 0 ticks (src/frame_rate.cpp): System::Update (0x1198DA0) and Application::Update
(0x705530) still run, but the scene step count System+0xCE0 is written only inside the two per-tick loops (0x1198EEA,
0x1198FDA), and every object stage (0x1199131..0x119950B, the soldiers' pre-update 0x572DF0 that drives map.cpp
MapHumanFrame, the vehicles' input that drives crew.cpp InputHook) runs inside them.

So:
  - the four network pieces of the gate stand: Steam callbacks 0x705633, 0x788460 at 0x705693, EOS_Platform_Tick
    through 0x12ADB30 at 0x7056AE (EDF6Coop's per-frame work) run on frames with ticks only, and the gate goes in only
    when all 8 guards did (all or nothing, its own 3 writes all or none);
  - the plugin's own channels (plugin_net) are pumped once per logic frame: RunSupportNetTick returns at once unless
    GameFrame() moved, GameFrame only moves when SeeFrame sees +0xCE0 move, and SeeFrame is called from per-tick hooks
    only; plugin_net::Update / Receive and the extension poll live in that pump alone;
  - every file that sends (plugin_net::Send / Broadcast / SendHost, seat_net::Send, edf41net::Send) or pumps
    (SupportNetTick) is a known one, reviewed as tick driven; the per-presented-frame hooks (render, HUD, Present, UI,
    window procedure, fps-9) send nothing and pump nothing;
  - periodic sends are paced by time (roster kRosterGapMs) or by logic frames (enemy control kSampleFrames), never by
    calls.
A new sender or pumper fails this guard on purpose: check what drives it (a tick hook, or every presented frame) and
add it to the list. Source only, no game.

Usage: python frame_rate_net_guard.py [repository root]   (default: the parent of this file's directory)"""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
checks = 0


def check(ok: object, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def read(*parts: str) -> str:
    return open(os.path.join(ROOT, *parts), encoding='utf-8').read()


def code(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)   # comments may name anything


def body(text: str, head: str) -> str:
    """The braces' content of the first definition whose text starts with `head` (a regex)."""
    m = re.search(head, text)
    if not m:
        return ''
    i = text.find('{', m.end() - 1)
    if i < 0:
        return ''
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
    return ''


src_dir = os.path.join(ROOT, 'src')
check(os.path.isdir(src_dir), f'{src_dir} is a repository src directory')
names = sorted(n for n in os.listdir(src_dir) if n.endswith(('.cpp', '.h', '.inc')))
src = {n: code(read('src', n)) for n in names}

# --- 1. The gate's network guards (src/frame_rate.cpp) ---
fr = src['frame_rate.cpp']
check(re.search(r'\bkSteamCall=0x705633\b', fr), 'kSteamCall is 0x705633 (SteamAPI_RunCallbacks)')
check(re.search(r'\bkNetCall=0x705693\b', fr) and re.search(r'\bkNet=0x788460\b', fr), 'kNetCall 0x705693 calls 0x788460')
check(re.search(r'\bkEosCall=0x7056AE\b', fr) and re.search(r'\bkEos=0x12ADB30\b', fr), 'kEosCall 0x7056AE calls 0x12ADB30 (EOS_Platform_Tick)')
guards = body(fr, r'int InstallGuards\(\) noexcept \{')
check(guards, 'InstallGuards found')
check(re.search(r'RedirectImportCall\(image\+kSteamCall,kSteamSig,reinterpret_cast<void\*>\(&SteamGuard\)', guards),
      'the Steam callbacks call goes through SteamGuard')
check(re.search(r'RedirectCall\(image\+kNetCall,image\+kNet,reinterpret_cast<void\*>\(&NetGuard\)', guards),
      '0x705693 goes through NetGuard')
check(re.search(r'RedirectCall\(image\+kEosCall,image\+kEos,reinterpret_cast<void\*>\(&EosGuard\)', guards),
      '0x7056AE goes through EosGuard')
check(len(re.findall(r'\+\+done|done\+=', guards)) == 8, 'InstallGuards counts 8 guards')
check(re.search(r'constexpr int kGuards=8;', fr), 'kGuards is 8')
for g in ('SteamGuard() noexcept', r'NetGuard\(void\* self\)', r'EosGuard\(void\* self\)'):
    b = body(fr, r'void ' + g.replace('()', r'\(\)') + r'\s*\{')
    check(b.strip().startswith('if(Steps())'), f'{g.split("(")[0]} calls through only on a frame with ticks')
check(re.search(r'bool Steps\(\) noexcept \{ return !gateOn \|\| finalTicks; \}', fr),
      'Steps(): the gate off, or the frame\'s final tick count above 0')
install = body(fr, r'void InstallFrameRate\(\) noexcept \{')
check(install, 'InstallFrameRate found')
miss = install.find('if(guards!=kGuards || takes!=2 || !sync)')
gate = install.find('gateOn=viewOn && InstallGate();')
check(miss >= 0 and gate > miss, 'the gate is tried only after every guard went in')
check(re.search(r'if\(guards!=kGuards \|\| takes!=2 \|\| !sync\) \{[^}]*return;\s*\}', install, re.S),
      'a missing guard returns before the gate')
check(re.findall(r'\bgateOn=(?!false;)[^;]*;', fr) == ['gateOn=viewOn && InstallGate();'], 'gateOn is set in one place (from InstallGate, with the view on)')
ig = body(fr, r'bool InstallGate\(\) noexcept \{')
check(re.search(r'while\(written<3 && edf::PatchCode\(', ig) and re.search(r'if\(written<3\) \{', ig),
      'the gate\'s 3 writes are all or none')
check(re.search(r'for\(int i=written-1;i>=0;--i\)back=edf::PatchCode\(', ig), 'a failed gate write puts back the ones before it')
check(ig.find('SetFramePacingGate(') > ig.find('if(written<3)'), 'the pacing follows the gate only once it is in')
check('&finalTicks' in ig, 'the final n (after the clamp and the [rdi+9] reset) is what the guards read')

# --- 2. The pump: once per logic frame, GameFrame tick-locked ---
sn = src['support_net.cpp']
run = body(sn, r'void RunSupportNetTick\(\) noexcept \{')
check(run, 'RunSupportNetTick found')
check(re.match(r'\s*const ULONGLONG frame=GameFrame\(\);\s*if\(lastFrame==frame\)return;\s*lastFrame=frame;', run),
      'RunSupportNetTick returns at once unless the logic frame moved (no GetTickCount64 dedupe)')
check('pollBatch(' in run, 'the extension poll is in RunSupportNetTick')
check(sn.count('pollBatch(') == 1, 'the extension poll is nowhere else')
check(re.search(r'void SupportNetTick\(\) noexcept \{.*?__try \{RunSupportNetTick\(\);\}', sn, re.S),
      'SupportNetTick runs the pump through RunSupportNetTick')
check(len(re.findall(r'\bRunSupportNetTick\(\)', sn)) == 2, 'RunSupportNetTick is called from SupportNetTick only')
for n, t in src.items():
    if n.startswith('plugin_net'):
        continue
    if 'plugin_net::Receive(' in t:
        check(n == 'support_net.cpp' and t.count('plugin_net::Receive(') == 1 and 'plugin_net::Receive(' in run,
              f'plugin_net::Receive only inside RunSupportNetTick (src/{n})')
    if 'plugin_net::Update(' in t:
        check(n == 'support_net.cpp' and t.count('plugin_net::Update(') == 1, f'plugin_net::Update only in support_net.cpp (src/{n})')
    if 'plugin_net::RegisterChannel(' in t:
        check(n == 'support_net.cpp', f'channels are registered by support_net.cpp only (src/{n})')
ctx = body(sn, r'void CommandContext\(ULONGLONG now\) noexcept \{')
check('plugin_net::Update(' in ctx, 'plugin_net::Update is in CommandContext')

crew = src['crew.cpp']
check(re.search(r'ULONGLONG GameFrame\(\) noexcept \{ return frameClock\.logical; \}', crew),
      'GameFrame() only reads the clock (it observes nothing itself)')
seen = body(crew, r'void SeeFrame\(const void\*\) noexcept \{')
frame_h = open(os.path.join(ROOT, 'common', 'edf', 'game_frame.h'), encoding='utf-8').read()
check(re.search(r'gameframe::ReadStep\(image,s\)', seen) and re.search(r'frameClock\.Observe\(s\.system,s\.scene,s\.count\)', seen)
      and re.search(r'kSystemScene=0x68,kSystemStep=0xCE0;', frame_h)
      and re.search(r'edf::At<std::uint32_t>\(system,kSystemStep\)', frame_h),
      'SeeFrame steps the clock by System+0xCE0 (written in the tick loops only), read by gameframe::ReadStep')
observers = [n for n, t in src.items() if 'frameClock.Observe(' in t]
check(observers == ['crew.cpp'] and crew.count('frameClock.Observe(') == 1, f'the frame clock is observed by SeeFrame only: {observers}')
SEE_FRAME = {'carrierlaser.cpp', 'crew.cpp', 'npcai.cpp', 'pvp.cpp', 'support_dispatch.cpp'}   # ship AI, vehicle input, Think, PvpFrame, SupportActorFrame
see = {n for n, t in src.items() if re.search(r'(?<![\w:])SeeFrame\((?!const void\*\))', t) and not n.endswith('.h')}
check(see <= SEE_FRAME, f'SeeFrame is called from tick hooks only (new caller: check its driver): {sorted(see - SEE_FRAME)}')
for n, head in (('crew.cpp', r'void SharedFrameTick\(\) noexcept \{'), ('support_dispatch.cpp', r'void SupportDispatchTick\(\) noexcept \{'),
                ('pvp.cpp', r'void PvpFrame\(\) noexcept \{')):
    b = body(src[n], head)
    check(re.search(r'GameFrame\(\);?\s*\n?\s*if\(.*==\s*frame\)return;|if\(\w+==GameFrame\(\)\)return;', b),
          f'{head.split("(")[0].split()[-1]} runs once a logic frame')

# --- 3. Who sends, who pumps ---
PER_FRAME_FILES = ['dmgnum.cpp', 'firstperson.cpp', 'frame_pacing.cpp', 'frame_rate.cpp', 'frame_rate_view.cpp', 'frame_rate_objects.cpp', 'render_record.cpp', 'hud.cpp',
                   'infantryscope.cpp', 'map.cpp', 'perf_probe.cpp', 'room_preview.cpp', 'room_ui.cpp', 'scope_lens.cpp',
                   'sightzoom.cpp', 'swap_flip.cpp', 'thermal.cpp', 'thermal_gpu.cpp', 'thermal_models.cpp',
                   'worldmap.cpp', 'worldmap_gpu.cpp', 'worldmap_text.cpp', 'worldmap_tiles.cpp']
NET = re.compile(r'plugin_net::|#include "plugin_net\.h"|\bSupportNetTick\(|seat_net::Send\(|edf41net::Send\(|'
                 r'\bSubmit(SupportRequest|MapCommand|PreparedSupportPlan)\(|\bMapCommandNetworkReady\(|'
                 r'\bSupport(StrikeAt|CallAt|CallUnpaidAt|CallComposedAt)\(|\bUltimateStart\(|\bBarrageStart\(|'
                 r'\bSupportPoints(KeepHeld|Refund)\(')
for n in PER_FRAME_FILES:
    if n in src:
        hit = NET.search(src[n])
        check(not hit, f'src/{n} (runs on every presented frame) neither sends nor pumps: {hit.group(0) if hit else ""}')
check(sum(n in src for n in PER_FRAME_FILES) >= 15, 'the per-frame files are where this guard looks')

PLUGIN_SENDERS = {'barrage.cpp', 'floatgun.cpp', 'gravity_net.cpp', 'player_vehicle.cpp', 'support_points.cpp', 'ultimate.cpp'}
senders = {n for n, t in src.items() if re.search(r'plugin_net::(Send|Broadcast|SendHost)\(', t)} - {'plugin_net.cpp'}
check(senders == PLUGIN_SENDERS, f'plugin_net senders are the reviewed ones (new: check its driver; gone: drop it): '
      f'new {sorted(senders - PLUGIN_SENDERS)}, gone {sorted(PLUGIN_SENDERS - senders)}')
PUMPERS = {'command_net.cpp', 'support_dispatch.cpp', 'support_net.cpp'}
pumpers = {n for n, t in src.items() if re.search(r'(?<![\w:])SupportNetTick\(\);', t)}
check(pumpers == PUMPERS, f'SupportNetTick callers are the reviewed ones: {sorted(pumpers)}')
NATIVE_SENDERS = {'andryusha.cpp', 'dreadnought_turrets.inc', 'drill_net.cpp', 'edf41mother.cpp', 'edf41mother401.cpp', 'edf41robo.cpp', 'payload.cpp',
                  'proteus_net.cpp', 'sazabi_net.cpp', 'seat_turret.cpp'}   # vehicle input / object update hooks
native = {n for n, t in src.items() if re.search(r'(seat_net|edf41net)::Send\(', t)}
check(native <= NATIVE_SENDERS, f'native object senders are the reviewed ones: new {sorted(native - NATIVE_SENDERS)}')

# --- 4. Periodic traffic is paced by time or by logic frames, not by calls ---
pn = src['plugin_net.cpp']
# No roster any more (net-6, #390): joins are told when the room's peers change (peers.Joins), not every call.
check('rosterSum' not in pn and 'kRosterGapMs' not in pn, 'plugin_net sends no periodic roster (net-6)')
check(re.search(r'if\(ready && room\.peersReady\)peers\.Joins\(registry,c\.peerCount,', pn), 'joins are told through peers.Joins')
check(re.search(r'for\(std::size_t i=0;i<registry\.Count\(\);\+\+i\)if\(registry\.At\(i\)\.update\)registry\.At\(i\)\.update\(c,now\);',
                pn), 'each channel\'s update runs once a plugin_net::Update')
ec = src.get('enemy_control_net.cpp', '')
check(re.search(r'constexpr std::uint64_t kSampleFrames=2;', ec), 'enemy control samples every 2 logic frames')
check(len(re.findall(r'GameFrame\(\)-sample(->|\.)(lastFrame|sentFrame)>=kSampleFrames', ec)) >= 2,
      'enemy control pacing reads GameFrame, not a call count')
ecf = body(src.get('enemy_control.cpp', ''), r'void EnemyControlFrame\(\) noexcept \{')
check(re.search(r'if\(s\.frame==GameFrame\(\)\)continue;', ecf), 'the enemy control input is sent from EnemyControlFrame once a logic frame')

print(f'frame rate net guard: {checks} checks ok')
