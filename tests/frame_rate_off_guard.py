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
check(re.search(r'\bint frameRateCap=1;', read('src', 'crew.h')), 'Config::frameRateCap defaults to 1 (the display\'s refresh rate, as the shipped ini)')

fr = code(read('src', 'frame_rate.cpp'))
m = re.search(r'void InstallFrameRate\(\) noexcept \{\n(.*?)\n\}\n', fr, re.S)
check(m, 'InstallFrameRate found')
body = m.group(1)
off = body.find('if(c.frameRateCap==framerate::kOff)return;')
check(off >= 0, 'InstallFrameRate returns at FrameRateCap=0')
first = min(i for i in (body.find('Install'), body.find('Swap'), body.find('framerateview::')) if i >= 0)
check(off < first, 'nothing is installed before the FrameRateCap=0 return')

callers: dict[str, list[str]] = {'InstallSwapFlip(': [], 'framerateview::Install()': [], 'InstallGate()': [],
                                 'InstallInputMove()': [], 'SwapFlipUseWaitable(': []}
for name in names:
    text = code(read('src', name))
    for call in callers:
        if call in text:
            callers[call].append(name)
check(callers['InstallSwapFlip('] == ['frame_rate.cpp', 'swap_flip.cpp', 'swap_flip.h'], f"InstallSwapFlip only from frame_rate.cpp: {callers['InstallSwapFlip(']}")
# The flip model is a developer switch: the shipped ini has no FrameRateFlip, the Config defaults it off, and the
# creation's hook goes in only when it is asked for (the review of #386, 1 and 2).
check(not re.search(r'(?im)^\s*;?\s*FrameRateFlip\s*=', ini), 'the shipped ini has no FrameRateFlip key')
check(re.search(r'\bbool frameRateFlip=false;', read('src', 'crew.h')), 'Config::frameRateFlip defaults to off')
check(re.search(r'InstallSwapFlip\(c\.frameRateFlip\)', body), 'InstallFrameRate passes the developer switch to InstallSwapFlip')
sf = code(read('src', 'swap_flip.cpp'))
check(re.search(r'if\(!flipModel\)return true;\s*return callTo\(kCreateCall', sf), 'the creation hook only with flipModel, after the Present')
check(callers['framerateview::Install()'] == ['frame_rate.cpp'], f"framerateview::Install only from frame_rate.cpp: {callers['framerateview::Install()']}")
# Stage 4 (frame_rate_objects.cpp): installed from framerateview::Install only (after its hooks are in, so with the
# frame gate), and every entry point does nothing before it.
objs_install = [n for n in names if re.search(r'frameobjects::Install\(\);', code(read('src', n)))]
check(objs_install == ['frame_rate_view.cpp'], f'frameobjects::Install only from frame_rate_view.cpp: {objs_install}')
view_install = re.search(r'bool Install\(\) noexcept \{\n(.*?)\n\}\n', code(read('src', 'frame_rate_view.cpp')), re.S)
check(view_install and view_install.group(1).find('frameobjects::Install();') > view_install.group(1).rfind('return false;'),
      'frameobjects::Install after every failing return of framerateview::Install')
objs = code(read('src', 'frame_rate_objects.cpp'))
for entry, guard in (('void Sample() noexcept {', 'if(on)'), ('void Pose(float alpha,', 'if(on '),
                     ('void Restore() noexcept {', 'if(on '), ('void NoteRootRefusal(', 'if(on ')):
    at = objs.find(entry)
    check(at >= 0 and guard in objs[at:objs.find('\n}', at)], f'frameobjects {entry.split("(")[0].split()[-1]} does nothing before Install')
check(callers['InstallGate()'] == ['frame_rate.cpp'], f"InstallGate only in frame_rate.cpp: {callers['InstallGate()']}")
check(callers['InstallInputMove()'] == ['frame_rate.cpp'], f"InstallInputMove only in frame_rate.cpp: {callers['InstallInputMove()']}")
check(set(callers['SwapFlipUseWaitable(']) <= {'frame_rate.cpp', 'swap_flip.cpp', 'swap_flip.h'}, 'the waitable object only with the frame gate')
check(re.search(r'if\(gateOn && flip && c\.maxFrameLatency>0\)SwapFlipUseWaitable\(', body), 'the waitable object only with the gate on')
plugin = code(read('src', 'plugin.cpp'))
check(plugin.count('InstallFrameRate();') == 1, 'plugin.cpp installs the frame rate once')
print(f'frame rate off switch: {checks} checks ok')
