"""Run production catch/landing and hangar-recovery fixtures and optional mutation controls, without a game.

Fixtures: tests/playerjet_recovery_test.cpp (compiles src/playerjet.cpp) and tests/hangar_recover_test.cpp (the
Dreadnought's hangar flying a fighter home, src/hangar_recover.h). Each negative control puts a replaced or broken law
back into a copy of the source and must make its fixture fail.

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
    # Every case is a compile of its own (cl /O2 of the fixture and the plugin source it includes, ~5 s), so the 27 ran
    # one after another for minutes under one ctest: ctest's playerjet_recovery_<I> run the I-th of N shares side by side.
    parser.add_argument('--shard', default='0/1', help='I/N: every N-th case from the I-th (0 <= I < N)')
    args = parser.parse_args()
    index, count = (int(x) for x in args.shard.split('/'))
    if not 0 <= index < count:
        raise ValueError(f'--shard {args.shard}: want I/N with 0 <= I < N')
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
        ('cooling-enemies-starve-batch', 'src/playerjet.cpp',
         'if(ram::CooldownSlot(sc.hits,kEnemyRamHits,sc.ms,kEnemyRamGapMs,[object](const ObjRef& t){return t.Is(object);})<0)return;',
         '(void)sc.hits;(void)sc.ms;'),
        ('one-second-player-lead', 'src/pjet_catch.h',
         'const float relative=playerVelocity[i]-bodyVelocity[i]-spin[i];',
         '(void)bodyVelocity;(void)dt;const float relative=playerVelocity[i]/dt;'),
        ('cruise-survives-touchdown', 'src/playerjet.cpp',
         'if(!j.keys && j.throttleIn<=0.0f)j.throttle=0.0f;', '(void)j.throttleIn;'),
        ('idle-flare-takeoff', 'src/playerjet.cpp',
         'if(!belly && j.throttle>0.02f && speed>=k.rotate &&',
         'if(!belly && speed>=k.rotate &&'),
        ('origin-clearance', 'src/playerjet_board.inc',
         'return pjet::BottomClear(clear,RestOver(v,pos),kNoGround);',
         '(void)v;(void)pos;return clear;'),
        ('lost-supported-landing', 'src/playerjet.cpp',
         '&& (v[0x1580]&2)!=0 &&', '&& (v[0x1580]&2)==0 &&'),
        ('takeoff-own-wheels-land', 'src/playerjet.cpp',
         'if(handling::OwnLiftOffContact(j.liftedOff,ms-j.liftOffAt,kLiftOffGraceMs,j.vel[1]))return;',
         '(void)kLiftOffGraceMs;'),
        ('doorless-catch-unflown', 'src/playerjet.cpp',
         'j.clear=clear;j.climb=j.vel[1];\n    Air(j,v,s,pos,clear,water,dt,ms);',
         'j.clear=clear;j.climb=j.vel[1];\n    if(catchFlight.hasDoor)Air(j,v,s,pos,clear,water,dt,ms);'),
        # test hub #42: the formation that wrote the catch jet's velocity whole within kCatchHoming, back.
        ('catch-formation-velocity', 'src/playerjet.cpp',
         'j.clear=clear;j.climb=j.vel[1];\n    Air(j,v,s,pos,clear,water,dt,ms);',
         'j.clear=clear;j.climb=j.vel[1];\n    if(catchFlight.hasDoor && dist<kCatchHoming)'
         '{for(int i=0;i<3;++i)j.vel[i]=(catchFlight.target[i]-pos[i])*1.5f;}\n'
         '    else Air(j,v,s,pos,clear,water,dt,ms);'),
        ('autopilot-start-nose-speed', 'src/playerjet.cpp',
         'std::memcpy(j.vel,e->m.vel,12);\n        j.throttle=1.0f;',
         'for(int i=0;i<3;++i)j.vel[i]=reinterpret_cast<const float*>(v+kMatrix)[8+i]*150.0f;\n        j.throttle=1.0f;'),
        ('takeover-heading-speed', 'src/playerjet.cpp',
         'std::memcpy(j.vel,j.sent,12);\n        j.phase=Phase::air;j.throttle=1.0f;j.autopilot=false;',
         'j.vel[0]=0.0f;j.vel[1]=0.0f;j.vel[2]=150.0f;\n        j.phase=Phase::air;j.throttle=1.0f;j.autopilot=false;'),
        ('capture-never-boards', 'src/playerjet.cpp',
         'if(!BoardingUnderway() && !BoardingRequest(v)) {', 'if(false) {(void)v;'),
        # review 2026-11-21: a refused capture (or no boarding hook) waited out kCatchMostMs in silence
        ('catch-refusal-waits', 'src/playerjet.cpp',
         'if(!BoardingUnderway() && !BoardingRequest(v)) {', 'if(!BoardingUnderway() && !BoardingRequest(v) && false) {'),
        ('catch-sent-without-boarding', 'src/playerjet.cpp',
         'if(!BoardingReady()) {', 'if(false) {'),
        # review 2026-11-21: the catch jet pursuing straight at the player circled one inside its turn, looped over one
        # behind it, and at full throttle never came round
        ('catch-circles-inside-turn', 'src/pjet_catch.h',
         'if(*extending){}   // straight on', 'if(false){}'),
        ('catch-loops-over', 'src/pjet_catch.h',
         'else if(std::atan2(std::fabs(sine),cosine)>kCatchTurnIn) {', 'else if(cosine>2.0f) {'),
        ('catch-turns-at-full-throttle', 'src/playerjet.cpp',
         'j.throttle=onIt && dist>kCatchHoming ? 1.0f : corner;', 'j.throttle=dist>kCatchHoming ? 1.0f : 0.7f;(void)onIt;(void)corner;'),
        ('catch-floor-late', 'src/playerjet.cpp',
         'if(run>1e-4f && j.aim[1]/run<least)', 'if(clear<kCatchFloor && run>1e-4f && j.aim[1]<0.0f)'),
        # test hub #42: a hailed rotor craft's nose on the player whatever way it flies
        ('hail-nose-on-player', 'src/playerjet_board.inc',
         'if(std::hypot(vel[0],vel[2])>kHailFaceSpeed)', 'if(vel[0]>1e30f)'),
        ('zero-error-stale-velocity', 'src/playerjet.cpp',
         'const float dist=Len(to);\n    // Its pass planned',
         'const float dist=Len(to);\n    if(dist<0.001f)return;\n    // Its pass planned'),
    ]
    # test hub #42: the Dreadnought's hangar flying a fighter home (src/hangar_recover.h).
    hangar = 'src/hangar_recover.h'
    hangar_controls = [
        # its hold's any-way velocity from afar (the law until 2026-11-21): a fighter turned round slides and backs up
        ('hangar-hold-from-afar', hangar,
         'else wing::Step(rel,want,kRecoverAccel,kRecoverTurn,dt,lateral,kRecoverRate);',
         'else Close(rel,want,kRecoverAccel,dt);'),
        # the first fix's capture: within kApproachOut x 1.5 of the mouth, its whole last stretch
        ('hangar-capture-225-to-mouth', hangar,
         'if(off<kCaptureRange && Dot(rel,rel)<=kCaptureSpeed*kCaptureSpeed) {',
         'if(wing::Len3(to)<kApproachOut*1.5f) {(void)rel;(void)off;'),
        # a fighter taken into the hold at whatever speed it passes through it
        ('hangar-capture-any-speed', hangar,
         'if(off<kCaptureRange && Dot(rel,rel)<=kCaptureSpeed*kCaptureSpeed) {',
         'if(off<kCaptureRange) {(void)rel;'),
        # flying for its final line without slowing for it
        ('hangar-closing-unslowed', hangar,
         'const float closing=std::fmin(plan.most,std::sqrt(kRecoverAccel*out.dist));',
         'const float closing=plan.most;'),
        # its nose not on its path (the hold's nose, on its goal)
        ('hangar-nose-on-goal', hangar,
         'for(int i=0;i<3;++i){out.nose[i]=n[i];out.up[i]=u[i]/l;}',
         'for(int i=0;i<3;++i){out.nose[i]=out.dist>1e-3f ? d[i]/out.dist : n[i];out.up[i]=u[i]/l;}'),
    ]
    with tempfile.TemporaryDirectory(prefix='edf6-recovery-') as temp:
        folder = Path(temp)
        shutil.copytree(root / 'src', folder / 'src')
        (folder / 'tests').mkdir()
        shutil.copy2(root / 'tests/playerjet_recovery_test.cpp', folder / 'tests')
        shutil.copy2(root / 'tests/hangar_recover_test.cpp', folder / 'tests')

        def run_case(name: str, fixture: str, mutation: tuple[str, str, str] | None = None) -> None:
            changed: Path | None = None
            original = ''
            if mutation:
                relative, old, new = mutation
                changed = folder / relative
                original = changed.read_text(encoding='utf-8')
                if original.count(old) != 1:
                    raise AssertionError(f'{name}: mutation no longer matches one production site')
                changed.write_text(original.replace(old, new), encoding='utf-8')
            # One binary per case: a just-run exe can stay locked a moment (antivirus scan), and relinking the same
            # path failed with LNK1104 (2026-11-21).
            binary = folder / f'{fixture}-{name}.exe'
            try:
                command = [compiler, '/nologo', '/std:c++17', '/O2', '/MT', '/W4', '/WX',
                           '/utf-8', '/permissive-', '/Gy', '/EHsc',
                           '/I' + str(root / 'common'), '/I' + str(folder / 'src'),
                           '/I' + str(root / 'third_party/EDFModLoader'),
                           '/I' + str(build / 'generated'),
                           str(folder / f'tests/{fixture}.cpp'),
                           '/Fo' + str(folder / f'{fixture}-{name}.obj'), '/Fe' + str(binary),
                           '/link', '/OPT:REF', str(libraries[0]), 'user32.lib']
                compiled = subprocess.run(command, env=env, cwd=folder,
                                          text=True, capture_output=True)
                if compiled.returncode:
                    raise RuntimeError(compiled.stdout + compiled.stderr)
                result = subprocess.run([str(binary)], cwd=folder, capture_output=True, text=True)
                output = (result.stdout + result.stderr).strip()
                if mutation:
                    if result.returncode != 1 or not any(line.startswith('FAIL: ') for line in output.splitlines()):
                        raise AssertionError(f'{name}: expected an assertion failure, got {result.returncode}: {output}')
                elif result.returncode:
                    raise AssertionError(f'{name}: {result.returncode}: {output}')
                print(f'{name}: {output.splitlines()[-1] if output else output}', flush=True)
            finally:
                if changed:
                    changed.write_text(original, encoding='utf-8')

        cases = [('production', 'playerjet_recovery_test', None), ('production-hangar', 'hangar_recover_test', None)]
        if args.negative_controls:
            cases += [(name, 'playerjet_recovery_test', (relative, old, new)) for name, relative, old, new in controls]
            cases += [(name, 'hangar_recover_test', (relative, old, new)) for name, relative, old, new in hangar_controls]
        for name, fixture, mutation in cases[index::count]:
            run_case(name, fixture, mutation)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
