"""InstallFramePacing (src/frame_pacing.cpp) gates each wait on what that wait reads: PumpWait (the window thread's
wait) reads no tick clock, so the frame step's signature kTickArg, the tick clock's kTickCountSig and tickPeriod must
not keep it from being installed (another MOD changing 0x11DAE60 used to take the window thread's fix with it).
TickWait is installed only after kTickArg and kTickCountSig match and tickPeriod is not 0. Source only, no game."""
from __future__ import annotations

import os
import re
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'pylib'))
import srcscan  # noqa: E402  (the one C++ reader: comments, literals, bodies, calls)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, 'src', 'frame_pacing.cpp'), encoding='utf-8').read()
checks = 0


def check(ok: object, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


body = srcscan.body(src, 'bool InstallFramePacing() noexcept', braces=False).strip('\n')   # comments out: they may name anything
check(body, 'InstallFramePacing found')
pump = body.find('reinterpret_cast<void*>(&PumpWait)')
tick = body.find('reinterpret_cast<void*>(&TickWait)')
check(pump >= 0 and tick >= 0, 'both waits redirected in InstallFramePacing')
before_pump = body[:pump]
for name in ('kTickArg', 'kTickCountSig', 'kTickCount', 'tickPeriod', 'TickPeriod', 'qpcFreq'):
    check(not re.search(rf'\b{name}\b', before_pump), f'{name} is read before PumpWait is installed: it gates it')
check(re.search(r'kPumpArg', before_pump), 'PumpWait is gated by its own argument load kPumpArg')
# The only early return before PumpWait is the high-resolution timer probe both waits need.
lines = [ln.strip() for ln in before_pump.splitlines() if re.search(r'\breturn\b', ln)]
check(all(ln.startswith(('if(!image)', 'if(!probe)')) for ln in lines), f'early returns before PumpWait: {lines}')
# After PumpWait nothing returns before the end: a failed tick check leaves PumpWait in place and skips TickWait only.
check(len(re.findall(r'\breturn\b', body[pump:])) == 1 and body.rstrip().endswith('return pump && tick;'),
      'nothing after PumpWait returns early')
before_tick = body[:tick]
# Another frame limiter on the frame step's wait (EDF6VR, which patches it later, from its DLL; an FPS unlimiter patch,
# from the bytes): asked before TickWait, and TickWait only in the branch where there is none.
foreign = before_tick.find('pacing::ForeignTickWait(image+kTickSleep,kTickSig,image+kTickBranch,kTickBranchSig,EdfVrInstalled())')
check(foreign >= 0, 'InstallFramePacing asks ForeignTickWait (with EdfVrInstalled) before TickWait')
check(re.search(r'foreign==pacing::Foreign::kVr\)\s*Log\(', before_tick[foreign:]) and
      re.search(r'else if\(foreign!=pacing::Foreign::kNone\)\s*Log\(', before_tick[foreign:]),
      'EDF6VR and a foreign patch each log a line and leave the wait')
vr = srcscan.body(src, 'bool EdfVrInstalled() noexcept', braces=False).strip('\n')
check('GetModuleHandleW(L"EDF6VR.dll")' in vr and vr.count('L"EDF6VR.dll"') == 2
      and 'GetFileAttributesW' in vr, 'EdfVrInstalled: loaded already, or its DLL beside this one (it loads after)')
# The frame rate (frame_rate.cpp): with the wait not ours, nothing of it goes in (no input move, guards, sync or Present
# leaf without the gate): GateSitesAsRead, which asks FramePacingTickWaitInstalled, before the first install.
rate = open(os.path.join(ROOT, 'src', 'frame_rate.cpp'), encoding='utf-8').read()
gs = srcscan.body(rate, 'bool GateSitesAsRead() noexcept', braces=False).strip('\n')
check(srcscan.calls(gs, 'FramePacingTickWaitInstalled'), 'GateSitesAsRead asks whose the wait is')
frb = srcscan.body(rate, 'void InstallFrameRate() noexcept', braces=False).strip('\n')
check(frb, 'InstallFrameRate found')
first = min(i for i in (frb.find('InstallInputMove()'), frb.find('InstallSwapFlip('), frb.find('InstallGuards()')) if i >= 0)
check(0 <= frb.find('if(!GateSitesAsRead())return;') < first, 'InstallFrameRate asks GateSitesAsRead before it installs anything')
for name in ('kTickArg', 'kTickCountSig', 'tickPeriod'):
    check(re.search(rf'\b{name}\b', before_tick[pump:]), f'TickWait is gated by {name}')

# InstallRenderFence: the fence wait redirected only after the loop and its poll match (another hook leaves it).
fence = srcscan.body(src, 'bool InstallRenderFence() noexcept', braces=False).strip('\n')
check(fence, 'InstallRenderFence found')
redirect = fence.find('RedirectCall(image+kFenceCall,image+kFencePoll,reinterpret_cast<void*>(&FenceWait)')
check(redirect >= 0, 'the fence poll redirected to FenceWait')
check(re.search(r'if\(!edf::Matches\(image,kFenceLoop,kFenceLoopSig,sizeof\(kFenceLoopSig\)\) \|\|\s*!edf::Matches\(image,kFencePoll,kFencePollSig,sizeof\(kFencePollSig\)\)\)',
                fence[:redirect]) and re.search(r'else ok=edf::RedirectCall', fence), 'the fence redirect is in the else of both signatures')
check(fence.startswith('    if(!image)return false;'), 'no image: nothing')

# InstallFrameLatency: MaxFrameLatency=0 installs nothing; the device init call is redirected only there.
lat = srcscan.body(src, 'bool InstallFrameLatency() noexcept', braces=False).strip('\n')
check(lat, 'InstallFrameLatency found')
redirect = lat.find('RedirectCall(image+kDeviceInitCall,image+kDeviceInit,reinterpret_cast<void*>(&DeviceInitHook)')
check(redirect >= 0, 'the device init redirected to DeviceInitHook')
check(re.search(r'if\(!Cfg\(\)\.maxFrameLatency\)\{[^}]*return false;\}', lat[:redirect]), 'MaxFrameLatency=0 returns before the redirect')
check(sum(len(re.findall(rf'(?<![\w]){n}\s*\(', srcscan.masked(src))) for n in ('RedirectCall', 'RedirectImportCall', 'PatchCode', 'PatchVtableSlot')) == 4, 'frame_pacing.cpp patches only its four sites')

# Both are installed at load, before the application's init (the fence loop and the device init run in it).
# The load's installs are the module registry's table (src/module_table.h kInstalls), run in its order.
table = open(os.path.join(ROOT, 'src', 'module_table.h'), encoding='utf-8').read()
table = table[table.index('constexpr InstallEntry kInstalls[]={'):]
table = table[:table.index('\n};')]
at = {f: [i for i, line in enumerate(table.split('\n')) if f'&Install<&{f}>' in line]
      for f in ('InstallFramePacing', 'InstallRenderFence', 'InstallFrameLatency', 'InstallFrameRate')}
check(all(len(v) == 1 for v in at.values()), f'the registry installs each once: {at}')
check(at['InstallFramePacing'] < at['InstallRenderFence'] < at['InstallFrameLatency'] < at['InstallFrameRate'],
      'the pacing, the fence, the latency, then the frame rate (whose gate needs TickWait)')
check(all('{IP::kBeforeAppInit,' in table.split('\n')[v[0]] for v in at.values()),
      'all four before the application\'s init (InstallPhase::kBeforeAppInit)')
print(f'frame pacing install gates: {checks} checks ok')
