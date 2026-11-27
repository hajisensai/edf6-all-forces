"""Offline A/B WAVs of the jets' engine sound (2026-11-27): the same scenes (another jet's distance, its angle, its rpm,
the player's own jet with its wind and airframe, flybys with the passby) through an older src/sound_engine.h ("A",
taken from git) and the current one with jet_air.h ("B"). Builds tools/jetsound_ab.cpp + jetsound_ab_old.cpp with cl
(tools/msvc-x64-env.cmd) and writes build/jetsound_ab/*.wav and README.txt (every file at one shared gain, so the
levels compare). No game, no install.

    python tools/jetsound_ab.py [--old <git rev>] [--out <dir>]
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The round before the WT air layers and the PC exclusions (docs/feature-wt-sound.md).
DEFAULT_OLD = '64ddb7be'


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--old', default=DEFAULT_OLD, help='git rev of the "A" sound_engine.h')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'jetsound_ab'))
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    old_dir = os.path.join(out, 'old')
    os.makedirs(old_dir, exist_ok=True)
    header = subprocess.run(['git', '-C', ROOT, 'show', f'{a.old}:src/sound_engine.h'], check=True,
                            capture_output=True).stdout
    with open(os.path.join(old_dir, 'sound_engine.h'), 'wb') as f:
        f.write(header)
    exe = os.path.join(out, 'jetsound_ab.exe')
    src = os.path.join(ROOT, 'src')
    tools = os.path.join(ROOT, 'tools')
    flags = '/nologo /O2 /EHsc /std:c++17 /W4 /WX /utf-8 /permissive-'
    lines = [
        '@echo off',
        f'call "{os.path.join(tools, "msvc-x64-env.cmd")}" >nul || exit /b 1',
        f'cd /d "{out}" || exit /b 1',
        f'cl {flags} /c /I"{old_dir}" /I"{src}" "{os.path.join(tools, "jetsound_ab_old.cpp")}" || exit /b 1',
        f'cl {flags} /I"{src}" "{os.path.join(tools, "jetsound_ab.cpp")}" jetsound_ab_old.obj /Fe"{exe}"',
    ]
    script = os.path.join(out, 'build.cmd')   # cmd's own quoting: a script, not an argument
    with open(script, 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
    r = subprocess.run(['cmd', '/c', script], capture_output=True, text=True, errors='replace')
    if r.returncode:
        sys.stdout.write(r.stdout + r.stderr)
        return r.returncode
    return subprocess.run([exe, os.path.join(ROOT, 'assets', 'sound', 'wt'), out]).returncode


if __name__ == '__main__':
    sys.exit(main())
