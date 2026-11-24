"""Rendering above the game's 59.94 Hz is a developer stage (docs/unlimited-fps-re.md §5): everything of it is behind
ini FrameRateDev, which is off by default and not in the shipped ini, so a player never runs it. Source only, no game.

- The shipped EDF6VehicleCrew.ini has no FrameRateDev key (not even commented out: a player would turn it on), and the
  installer / release tools do not write one.
- The setting defaults to false and is read once, as FrameRateDev.
- InstallFrameRate returns on FrameRateDev=0 before it installs anything, and the stage's pieces (the swap chain's flip
  model, the camera / root interpolation, the frame gate) are installed from InstallFrameRate only."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
checks = 0


def check(ok: object, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def read(*parts: str) -> str:
    return open(os.path.join(ROOT, *parts), encoding='utf-8').read()


ini = read('EDF6VehicleCrew.ini')
check(not re.search(r'(?im)^\s*;?\s*FrameRateDev\s*=', ini), 'the shipped ini has no FrameRateDev key')
for tool in ('installer.py', 'build_release.py'):
    path = os.path.join(ROOT, 'tools', tool)
    if os.path.exists(path):
        check('FrameRateDev' not in read('tools', tool), f'tools/{tool} does not write FrameRateDev')

crew_h = read('src', 'crew.h')
check(re.search(r'\bbool frameRateDev=false;', crew_h), 'Config::frameRateDev defaults to false')
plugin = read('src', 'plugin.cpp')
check(len(re.findall(r'frameRateDev=', plugin)) == 1 and re.search(r'n\.frameRateDev=ReadBool\(L"FrameRateDev",n\.frameRateDev\);', plugin),
      'FrameRateDev read once, with the default kept')

fr = re.sub(r'//[^\n]*', '', read('src', 'frame_rate.cpp'))
m = re.search(r'void InstallFrameRate\(\) noexcept \{\n(.*?)\n\}\n', fr, re.S)
check(m, 'InstallFrameRate found')
body = m.group(1)
off = body.find('if(!c.frameRateDev)')
check(off >= 0, 'InstallFrameRate tests FrameRateDev')
first_install = min(i for i in (body.find('Install'), ) if i >= 0)
check(off < first_install, 'nothing is installed before the FrameRateDev test')
check(re.search(r'if\(!c\.frameRateDev\) \{.*?return;\n    \}', body, re.S), 'FrameRateDev=0 returns')

src_dir = os.path.join(ROOT, 'src')
callers: dict[str, list[str]] = {'InstallSwapFlip()': [], 'framerateview::Install()': [], 'InstallGate()': [], 'SwapFlipUseWaitable(': []}
for name in sorted(os.listdir(src_dir)):
    if not name.endswith(('.cpp', '.h')):
        continue
    text = re.sub(r'//[^\n]*', '', read('src', name))
    for call in callers:
        if call in text:
            callers[call].append(name)
check(callers['InstallSwapFlip()'] == ['frame_rate.cpp', 'swap_flip.cpp', 'swap_flip.h'], f"InstallSwapFlip only from frame_rate.cpp: {callers['InstallSwapFlip()']}")
check(callers['framerateview::Install()'] == ['frame_rate.cpp'], f"framerateview::Install only from frame_rate.cpp: {callers['framerateview::Install()']}")
check(callers['InstallGate()'] == ['frame_rate.cpp'], f"InstallGate only in frame_rate.cpp: {callers['InstallGate()']}")
check(set(callers['SwapFlipUseWaitable(']) <= {'frame_rate.cpp', 'swap_flip.cpp', 'swap_flip.h'}, 'the waitable object only with the frame gate')
check(re.search(r'if\(gateOn && flip && c\.maxFrameLatency>0\)SwapFlipUseWaitable\(', body), 'the waitable object only with the gate on')
print(f'frame rate developer switch: {checks} checks ok')
