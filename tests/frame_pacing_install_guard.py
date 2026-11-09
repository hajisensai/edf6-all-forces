"""InstallFramePacing (src/frame_pacing.cpp) gates each wait on what that wait reads: PumpWait (the window thread's
wait) reads no tick clock, so the frame step's signature kTickArg, the tick clock's kTickCountSig and tickPeriod must
not keep it from being installed (another MOD changing 0x11DAE60 used to take the window thread's fix with it).
TickWait is installed only after kTickArg and kTickCountSig match and tickPeriod is not 0. Source only, no game."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, 'src', 'frame_pacing.cpp'), encoding='utf-8').read()
checks = 0


def check(ok: object, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


m = re.search(r'bool InstallFramePacing\(\) noexcept \{\n(.*?)\n\}\n', src, re.S)
check(m, 'InstallFramePacing found')
body = re.sub(r'//[^\n]*', '', m.group(1))   # comments may name anything
pump = body.find('reinterpret_cast<void*>(&PumpWait)')
tick = body.find('reinterpret_cast<void*>(&TickWait)')
check(pump >= 0 and tick >= 0, 'both waits redirected in InstallFramePacing')
before_pump = body[:pump]
for name in ('kTickArg', 'kTickCountSig', 'kTickCount', 'tickPeriod', 'TickPeriod', 'qpcFreq'):
    check(not re.search(rf'\b{name}\b', before_pump), f'{name} is read before PumpWait is installed: it gates it')
check(re.search(r'kPumpArg', before_pump), 'PumpWait is gated by its own argument load kPumpArg')
# The only early return before PumpWait is the high-resolution timer probe both waits need.
lines = [ln.strip() for ln in before_pump.splitlines() if re.search(r'return', ln)]
check(all(ln.startswith(('if(!image)', 'if(!probe)')) for ln in lines), f'early returns before PumpWait: {lines}')
# After PumpWait nothing returns before the end: a failed tick check leaves PumpWait in place and skips TickWait only.
check(len(re.findall(r'\breturn\b', body[pump:])) == 1 and body.rstrip().endswith('return pump && tick;'),
      'nothing after PumpWait returns early')
before_tick = body[:tick]
for name in ('kTickArg', 'kTickCountSig', 'tickPeriod'):
    check(re.search(rf'\b{name}\b', before_tick[pump:]), f'TickWait is gated by {name}')
print(f'frame pacing install gates: {checks} checks ok')
