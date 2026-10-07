"""Run production catch/landing fixtures and optional mutation controls, without a game.

First build EDF6VehicleCrew to obtain edf6common.lib. This runner only uses the
source checkout, that build directory, and temporary files; it never installs or
loads the plugin. MSVC and the Windows SDK are required.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def compiler_environment() -> dict[str, str]:
    if shutil.which('cl'):
        return dict(os.environ)
    scripts = sorted(Path('C:/Program Files/Microsoft Visual Studio').glob(
        '*/Community/Common7/Tools/VsDevCmd.bat'))
    if not scripts:
        raise RuntimeError('Run from an x64 MSVC developer shell')
    result = subprocess.run(f'cmd /d /s /c ""{scripts[-1]}" -arch=x64 >nul && set"',
                            check=True, capture_output=True, text=True)
    return dict(line.split('=', 1) for line in result.stdout.splitlines()
                if '=' in line and not line.startswith('='))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--negative-controls', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    libraries = list((build / 'common').rglob('edf6common.lib'))
    if len(libraries) != 1:
        raise RuntimeError('Expected one built edf6common.lib in the build directory')
    env = compiler_environment()
    compiler = shutil.which('cl', path=next(value for key, value in env.items() if key.lower() == 'path'))
    if not compiler:
        raise RuntimeError('MSVC environment did not provide cl.exe')
    controls = [
        ('one-second-player-lead', 'src/pjet_catch.h',
         'const float relative=playerVelocity[i]-bodyVelocity[i]-spin[i];',
         '(void)bodyVelocity;(void)dt;const float relative=playerVelocity[i]/dt;'),
        ('cruise-survives-touchdown', 'src/playerjet.cpp',
         'if(j.throttleIn<=0.0f)j.throttle=0.0f;', '(void)j.throttleIn;'),
        ('idle-flare-takeoff', 'src/playerjet.cpp',
         'if(!belly && j.throttle>0.02f && speed>=k.rotate &&',
         'if(!belly && speed>=k.rotate &&'),
        ('origin-clearance', 'src/playerjet_board.inc',
         'return pjet::BottomClear(clear,RestOver(v,pos),kNoGround);',
         '(void)v;(void)pos;return clear;'),
        ('lost-supported-landing', 'src/playerjet.cpp',
         '&& (v[0x1580]&2)!=0 &&', '&& (v[0x1580]&2)==0 &&'),
        ('zero-error-stale-velocity', 'src/playerjet.cpp',
         'const float dist=Len(to);\n    if(Normalize(to))',
         'const float dist=Len(to);\n    if(dist<0.001f)return;\n    if(Normalize(to))'),
    ]
    with tempfile.TemporaryDirectory(prefix='edf6-recovery-') as temp:
        folder = Path(temp)
        shutil.copytree(root / 'src', folder / 'src')
        (folder / 'tests').mkdir()
        shutil.copy2(root / 'tests/playerjet_recovery_test.cpp', folder / 'tests')

        def run_case(name: str, mutation: tuple[str, str, str] | None = None) -> None:
            changed: Path | None = None
            original = ''
            if mutation:
                relative, old, new = mutation
                changed = folder / relative
                original = changed.read_text(encoding='utf-8')
                if original.count(old) != 1:
                    raise AssertionError(f'{name}: mutation no longer matches one production site')
                changed.write_text(original.replace(old, new), encoding='utf-8')
            binary = folder / 'recovery.exe'
            try:
                command = [compiler, '/nologo', '/std:c++17', '/O2', '/MT', '/W4', '/WX',
                           '/utf-8', '/permissive-', '/Gy', '/EHsc',
                           '/I' + str(root / 'common'), '/I' + str(folder / 'src'),
                           '/I' + str(root / 'third_party/EDFModLoader'),
                           '/I' + str(build / 'generated'),
                           str(folder / 'tests/playerjet_recovery_test.cpp'),
                           '/Fo' + str(folder / 'recovery.obj'), '/Fe' + str(binary),
                           '/link', '/OPT:REF', str(libraries[0]), 'user32.lib']
                compiled = subprocess.run(command, env=env, cwd=folder,
                                          text=True, capture_output=True)
                if compiled.returncode:
                    raise RuntimeError(compiled.stdout + compiled.stderr)
                result = subprocess.run([str(binary)], cwd=folder, capture_output=True, text=True)
                output = (result.stdout + result.stderr).strip()
                if mutation:
                    if result.returncode != 1 or not output.startswith('FAIL: '):
                        raise AssertionError(f'{name}: expected an assertion failure, got {result.returncode}: {output}')
                elif result.returncode:
                    raise AssertionError(f'{name}: {result.returncode}: {output}')
                print(f'{name}: {output}', flush=True)
            finally:
                if changed:
                    changed.write_text(original, encoding='utf-8')

        run_case('production')
        if args.negative_controls:
            for name, relative, old, new in controls:
                run_case(name, (relative, old, new))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
