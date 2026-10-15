"""Model instrument contract, independent of game files and third-party cockpit assets."""
from pathlib import Path
import sys
import math

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
import cockpit_instruments as ci
from cockpits import Screen
from cockpit_shapes import basis

for kind in ('status','weapons','flight','map'):
    s=Screen(kind,(.2,-.3,.7),(0,.18,-.984),.18,.12)
    parts, controls=ci.instrument(s,0)
    assert controls and len({c.part for c in controls})==len(controls)
    assert len({c.control for c in controls})==len(controls)
    by_name={p.name:p for p in parts}
    right,up,normal=basis(s.facing,s.up)
    for p in parts:
        assert p.material in ci.MATERIALS
        for v in p.verts:
            rel=tuple(v.pos[k]-s.centre[k] for k in range(3))
            dot=lambda a,b: sum(x*y for x,y in zip(a,b))
            assert all(math.isfinite(x) for x in v.pos)
            assert abs(dot(rel,right))<=s.width*.501,(kind,p.name,'outside display width')
            assert abs(dot(rel,up))<=s.height*.501,(kind,p.name,'outside display height')
            assert 0<=dot(rel,normal)+1e-9<=.005,(kind,p.name,'not on display surface')
    for c in controls:
        assert c.part in by_name and by_name[c.part].tris
    if kind=='flight':
        assert {f'n_{ci.SPEED}',f'n_{ci.ALT}'} <= {c.control for c in controls}
    if kind=='status':
        assert sum(c.control.startswith('d_') for c in controls)==35
        _,passenger=ci.instrument(s,0,numbers=False)
        assert len(passenger)==4,'passenger status bars keep the full vehicle under the ubyte skin-index budget'
    if kind=='map':
        assert sum(c.control.startswith('t_') for c in controls)==6
print('physical instrument geometry and controls passed')
