"""The plugins read the keyboard, the foreground window and the pad only through common/edf/input_source.h (the user,
2026-10-14: "桌面键盘漏进后台"): a direct GetAsyncKeyState / GetForegroundWindow / XInputGetState call in a plugin's
source reads the desktop's real input, which a background host (tests/autopilot) can no longer keep out. Comments and
the string a module's export is looked up by do not count."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ('src', 'autoturret/src', 'common')
ALLOWED = {os.path.normpath('common/edf/input_source.h')}
CALL = re.compile(r'(?<![\w:"])(GetAsyncKeyState|GetForegroundWindow|XInputGetState)\s*\(')


def code_of(text: str) -> str:
    """`text` without its comments and string literals (kept as blanks: the line numbers stay)."""
    out = re.sub(r'//[^\n]*', '', text)
    out = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), out, flags=re.S)
    return re.sub(r'"(?:\.|[^"\\n])*"', '""', out)


def offenders() -> list[str]:
    found: list[str] = []
    for d in DIRS:
        for base, _, files in os.walk(os.path.join(ROOT, d)):
            for name in files:
                if not name.endswith(('.cpp', '.h', '.inc')):
                    continue
                path = os.path.join(base, name)
                rel = os.path.normpath(os.path.relpath(path, ROOT))
                if rel in ALLOWED:
                    continue
                with open(path, encoding='utf-8', errors='replace') as f:
                    code = code_of(f.read())
                for i, line in enumerate(code.splitlines(), 1):
                    m = CALL.search(line)
                    if m:
                        found.append(f'{rel}:{i}: {m.group(1)} (use edf::AsyncKey / edf::ForegroundWindow / edf::PadSource)')
    return found


def main() -> int:
    bad = offenders()
    for b in bad:
        print('FAIL', b)
    probe = code_of('x = GetAsyncKeyState(1); // GetForegroundWindow()\n"XInputGetState(";')
    if not CALL.search(probe) or CALL.search(probe.split(';')[1]):
        print('FAIL the scan itself: a call must be found, a comment or a string must not')
        return 1
    print('input source guard:', 'ok' if not bad else f'{len(bad)} direct reads')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
