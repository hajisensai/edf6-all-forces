"""ini FrameRateCap=0 is the game's own 59.94 Hz: InstallFrameRate (src/frame_rate.cpp) returns before it installs
anything, and every piece of the frames above it (the input move, the flip swap chain and the Present's statistics, the
pacing guards, the frame gate, the camera / root interpolation) is installed from InstallFrameRate only, after that test.
The developer switch FrameRateDev of stages 2-3 is gone: no source reads it, the shipped ini and the tools do not write
it. Source only, no game."""
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


def code(text: str) -> str:
    return re.sub(r'//[^\n]*', '', text)   # comments may name anything


ini = read('EDF6VehicleCrew.ini')
check(not re.search(r'(?im)^\s*;?\s*FrameRateDev\s*=', ini), 'the shipped ini has no FrameRateDev key')
check(re.search(r'(?m)^FrameRateCap=-?\d+$', ini), 'the shipped ini has FrameRateCap')
for tool in ('installer.py', 'build_release.py'):
    path = os.path.join(ROOT, 'tools', tool)
    if os.path.exists(path):
        check('FrameRateDev' not in read('tools', tool), f'tools/{tool} does not write FrameRateDev')

src_dir = os.path.join(ROOT, 'src')
names = sorted(n for n in os.listdir(src_dir) if n.endswith(('.cpp', '.h')))
for name in names:
    check(not re.search(r'frameRateDev|FrameRateDev', code(read('src', name))), f'src/{name} reads no FrameRateDev')
check(re.search(r'\bint frameRateCap=0;', read('src', 'crew.h')), 'Config::frameRateCap defaults to 0 (the game\'s rate)')

fr = code(read('src', 'frame_rate.cpp'))
m = re.search(r'void InstallFrameRate\(\) noexcept \{\n(.*?)\n\}\n', fr, re.S)
check(m, 'InstallFrameRate found')
body = m.group(1)
off = body.find('if(c.frameRateCap==framerate::kOff)return;')
check(off >= 0, 'InstallFrameRate returns at FrameRateCap=0')
first = min(i for i in (body.find('Install'), body.find('Swap'), body.find('framerateview::')) if i >= 0)
check(off < first, 'nothing is installed before the FrameRateCap=0 return')

callers: dict[str, list[str]] = {'InstallSwapFlip()': [], 'framerateview::Install()': [], 'InstallGate()': [],
                                 'InstallInputMove()': [], 'SwapFlipUseWaitable(': []}
for name in names:
    text = code(read('src', name))
    for call in callers:
        if call in text:
            callers[call].append(name)
check(callers['InstallSwapFlip()'] == ['frame_rate.cpp', 'swap_flip.cpp', 'swap_flip.h'], f"InstallSwapFlip only from frame_rate.cpp: {callers['InstallSwapFlip()']}")
check(callers['framerateview::Install()'] == ['frame_rate.cpp'], f"framerateview::Install only from frame_rate.cpp: {callers['framerateview::Install()']}")
check(callers['InstallGate()'] == ['frame_rate.cpp'], f"InstallGate only in frame_rate.cpp: {callers['InstallGate()']}")
check(callers['InstallInputMove()'] == ['frame_rate.cpp'], f"InstallInputMove only in frame_rate.cpp: {callers['InstallInputMove()']}")
check(set(callers['SwapFlipUseWaitable(']) <= {'frame_rate.cpp', 'swap_flip.cpp', 'swap_flip.h'}, 'the waitable object only with the frame gate')
check(re.search(r'if\(gateOn && flip && c\.maxFrameLatency>0\)SwapFlipUseWaitable\(', body), 'the waitable object only with the gate on')
plugin = code(read('src', 'plugin.cpp'))
check(plugin.count('InstallFrameRate();') == 1, 'plugin.cpp installs the frame rate once')
print(f'frame rate off switch: {checks} checks ok')
