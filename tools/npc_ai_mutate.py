"""Mutation check of tools/npc_ai_check.cpp against src/npc_logic.h: every mutant (one decision broken) must turn the
offline check red. Builds each mutant in a scratch directory (argv[1], default %TEMP%/npc_ai_mutate) with MSVC:
    python -I tools/npc_ai_mutate.py"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('TEMP', '.'), 'npc_ai_mutate')
ENV = os.path.join(ROOT, 'tools', 'msvc-x64-env.cmd')
MUTANTS = [
    ('lane escape side', 'const float side=Dot(q,right)>=0.0f ? 1.0f : -1.0f;', 'const float side=1.0f;'),
    ('blast ignored', 'if(blast>0.0f && Dist(to,friends[i].pos)<blast*1.25f+friends[i].radius)return false;', ''),
    ('min range ignored', 'dist>a.reach || dist<MinRange(a))', 'dist>a.reach)'),
    ('no hysteresis', 'if(keep>0.0f && keep*1.25f>=bestScore)return current;', ''),
    ('air not preferred', 's*=a.homing || a.antiAir ? 3.0f : 0.5f;', ''),
    ('evade towards', 'for(int k=0;k<3;k+=2)push[k]+=d[k]*w;', 'for(int k=0;k<3;k+=2)push[k]-=d[k]*w;'),
    ('no roll', 'if(nearest<grab && rollReady){e.move=Move::roll;return e;}', ''),
    ('spot on the lane', 'a=std::fabs(Wrap(me-l))<=std::fabs(Wrap(me-r)) ? l : r;', ''),
    ('leader may be player', 'if(!m[i].alive || m[i].player)continue;', 'if(!m[i].alive)continue;'),
    ('join ignores size', 'if(sizes[i]+alive>maxSize)continue;', ''),
    ('cooldown never ends', 'if(c.squad==squad && c.until>now)return false;', 'if(c.squad==squad)return false;'),
    ('no reverse', 'if(std::fabs(s.bearing)>backAngle && s.dist<reverseMax) {', 'if(false) {'),
    ('reverse bearing unflipped', 's.bearing=Wrap(s.bearing+kPi);   // the tail onto the post', ''),
    ('steer sign', 'return -Clamp(s.bearing*10.0f,-1.0f,1.0f);', 'return Clamp(s.bearing*10.0f,-1.0f,1.0f);'),
    ('throttle sign', 'return -s.throttle;', 'return s.throttle;'),
    ('no turn on the spot', 's.throttle=std::fabs(s.bearing)>turnOnSpot ? 0.0f : ramp;', 's.throttle=ramp;'),
    ('aim pitch sign', 'const float pitch=-std::atan2(dir[1],h),yaw=h>1e-6f', 'const float pitch=std::atan2(dir[1],h),yaw=h>1e-6f'),
    ('local move transposed', 'float lx=dir[0]*c-dir[2]*s,lz=dir[0]*s+dir[2]*c;', 'float lx=dir[0]*c+dir[2]*s,lz=-dir[0]*s+dir[2]*c;'),
    ('roll side lost', 'if(ax<side)ax=side;', ''),
    ('route not script', 'if(f.route || f.rootRouted)return', 'if(f.rootRouted)return'),
    ('mark ignores move', 'return Dist(pos,mark)<=reach+moveRadius;', 'return Dist(pos,mark)<=reach;'),
]


def build(header: str) -> int:
    for d in ('src', 'tools'):  # the check includes ../src/npc_logic.h
        os.makedirs(os.path.join(WORK, d), exist_ok=True)
    with open(os.path.join(WORK, 'src', 'npc_logic.h'), 'w', encoding='utf-8', newline='') as f:
        f.write(header)
    shutil.copy(os.path.join(ROOT, 'tools', 'npc_ai_check.cpp'), os.path.join(WORK, 'tools', 'npc_ai_check.cpp'))
    exe = os.path.join(WORK, 'm.exe')
    bat = os.path.join(WORK, 'b.cmd')
    with open(bat, 'w') as f:
        f.write(f'@call "{ENV}" >nul 2>&1\r\n@cl /nologo /EHsc /std:c++17 /W4 /O2 /utf-8 tools\\npc_ai_check.cpp /Fem.exe >cl.log\r\n')
    if subprocess.run(['cmd', '/c', bat], cwd=WORK).returncode != 0:
        print(open(os.path.join(WORK, 'cl.log')).read()[-2000:])
        return -1
    return subprocess.run([exe], stdout=subprocess.DEVNULL).returncode


def main() -> int:
    base = open(os.path.join(ROOT, 'src', 'npc_logic.h'), encoding='utf-8').read()
    assert build(base) == 0, 'the unmutated check fails'
    bad = 0
    for name, old, new in MUTANTS:
        assert old in base, name
        rc = build(base.replace(old, new, 1))
        print(f'{"killed" if rc != 0 else "SURVIVED"}  {name} (rc={rc})')
        bad += rc == 0
    print(f'{len(MUTANTS) - bad}/{len(MUTANTS)} mutants killed')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
