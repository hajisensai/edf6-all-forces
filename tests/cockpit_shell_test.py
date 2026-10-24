"""Native cabin separation: exact exterior/animation retention and seat-scoped occlusion."""
from collections import Counter
from dataclasses import replace
from pathlib import Path
import struct
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'pylib'), str(ROOT/'tools')]
import cabin_view as cv
import cockpit_kits
import cockpit_shell as shell
import cockpits
import model_view as mv
import vehicle_optics as vo
from mdb import Bone, Mdb, Material, Mesh, Object, VElem, ident, inverse_affine, read_elem

assert set(cockpit_kits.KITS) == shell.REPLACEMENTS | shell.RETAINED, 'every kit needs an explicit shell policy'
assert not shell.REPLACEMENTS & shell.RETAINED


def bone(index, parent, name, matrix):
    return Bone(index,parent,-1,-1,name,0,3,0,1,0,0,matrix,inverse_affine(matrix),[1,1,1,1],[0,0,0,1])


def mesh(offset, owner, *, upper=False, mixed=False, half=False):
    # Deliberately retain an unrelated payload column and a large enough buffer
    # offset to catch uint16 multiplication overflow in a vertex subset operation.
    fmt, width = (7,8) if half else (1,16)
    elems=[VElem(fmt,0,0,'POSITION' if upper else 'position'), VElem(1,width,0,'BLENDWEIGHT'),
           VElem(21,width+16,0,'BLENDINDICES'), VElem(12,width+20,0,'TEXCOORD')]
    size=width+28
    points=[(-.1,-.1,0),(.1,-.1,0),(0,.1,0)]
    data=b''
    for i in range(1803):
        x,y,z=points[i%3];point=(x+offset[0],y+offset[1],z+offset[2],1)
        data+=struct.pack('<4e' if half else '<4f',*point)
        data+=struct.pack('<4f4B2f',*(.5,.5,0,0) if mixed else (1,0,0,0),
                          owner,2 if mixed else 0,0,0,.125,.875)
    return Mesh(bytes([0,1,2 if mixed else 1,0]),0,0,size,elems,0,data,struct.pack('<3H',1800,1801,1802))


body=ident();body[12:15]=[3,2,-4]
model=Mdb(0x14,['root','body','rotor','host','native','door_r'],
          [bone(0,-1,0,ident()),bone(1,0,1,body),bone(2,0,2,ident()),
           replace(bone(3,1,5,ident()),inv_bind=inverse_affine(body))],
          [Object(3,0,[mesh((3,2,-3),1,upper=True),mesh((3,2,8),1,half=True),
                       mesh((3,2,-3),2),mesh((3,2,-3),1,mixed=True),mesh((3,2,-3),3)])],
          [Material(0,0,0,4,'native',[],[],3)],[])
fits=(cockpits.Fit(0,'body',(3,2,-4),'heli_gunship'),cockpits.Fit(12,'body',(3.1,2,-4),'passenger'))
# Native exporters can store a slightly different inverse from a recomputation of
# the hierarchy. Skinning must inherit that exact stored value, not "correct" it.
model.bones[1].inv_bind[12] += .03125
model.bones[3].inv_bind[12] += .015625
# Closed one-metre cube round the eye, independent of the model's detail triangles.
p=np.array([(x,y,z) for x in (-1,1) for y in (-1,1) for z in (-1,1)],float)+[3,2,-4]
tri=np.array([(0,1,3),(0,3,2),(4,6,7),(4,7,5),(0,4,5),(0,5,1),
              (2,3,7),(2,7,6),(0,2,6),(0,6,4),(1,5,7),(1,7,3)])
hull=p[tri]
before=[me.vdata for o in model.objects for me in o.meshes]
result=vo.preorder(shell.separate(model,fits,hull))
assert before==[me.vdata for o in model.objects for me in o.meshes], 'source model was mutated'
names=[result.name_of(b.name) for b in result.bones]
clones=[b for b in result.bones if result.name_of(b.name).startswith(shell.PREFIX)]
assert len(clones)==2 and all(result.name_of(b.name).startswith('vc_shell_4097_') for b in clones)
assert {result.name_of(result.bones[b.parent].name) for b in clones}=={'body','door_r'}
assert all(np.allclose(b.local,ident()) for b in clones), 'visibility children inherit their own animated parents'
assert all(b.inv_bind==model.bones[b.parent if b.parent<len(model.bones) else 0].inv_bind
           for b in shell.separate(model,fits,hull).bones[len(model.bones):])


def triangles(md, pose=None, locals_=None):
    pos,triangles,_=mv.geometry(md,pose or {},[],locals_)
    return Counter(tuple(np.round(t.reshape(-1),6)) for t in pos[triangles])


for pose in ({},{'body':[('y',37),('x',-14)],'door_r':[('y',45)]}):
    assert triangles(model,pose)==triangles(result,pose), 'outside view or parent animation changed'
payloads=[(me.material,read_elem(me,'TEXCOORD',0)[-1]) for o in result.objects for me in o.meshes]
assert all(x==(0,(.125,.875)) for x in payloads), 'source material/UV payload changed'
collapsed=ident();collapsed[:12]=[0.]*12
local={result.name_of(b.name):collapsed for b in clones}
pos,triangles_,_=mv.geometry(result,{},[],local)
area=np.linalg.norm(np.cross(pos[triangles_[:,1]]-pos[triangles_[:,0]],pos[triangles_[:,2]]-pos[triangles_[:,0]]),axis=1)
assert sum(area>1e-8)==3, 'only the nearby native body face collapses; rotor, distant body and mixed skin remain'
kept=replace(model,objects=[replace(model.objects[0],meshes=model.objects[0].meshes[1:4])])
visible=Counter(tuple(np.round(t.reshape(-1),6)) for t in pos[triangles_[area>1e-8]])
assert visible==triangles(kept)
assert triangles(cv.first_person_shell(result,12))==triangles(result,locals_=local)
assert triangles(cv.first_person_shell(result,1))==triangles(result), 'preview must respect the same seat mask'
assert shell.separate(model,(replace(fits[0],kit='remote_display'),),hull) is model
assert shell.separate(model,fits,np.zeros((0,3,3))) is model, 'open/missing hull must never erase a model'
# Non-overlapping crew regions still share one replacement cabin. The retained
# remote operator on that body must not inherit either visibility bit.
env=[(0,1,np.eye(4),np.array([-1,-1,-1]),np.array([1,1,1])),
     (12,1,np.eye(4),np.array([2,1,-4]),np.array([4,3,-2]))]
mask,_=shell.triangle_masks(model,model.objects[0],model.objects[0].meshes[0],env)
assert list(mask)==[4097], 'looking into an adjacent crew region must not expose its old hull'
wing=mesh((3,2,-3),1)
payload=bytearray(wing.vdata)
struct.pack_into('<4f',payload,1801*wing.vsize,20,2,-3,1)
wing=replace(wing,vdata=bytes(payload))
assert not shell.triangle_masks(model,model.objects[0],wing,shell.regions(model,fits,hull))[0].any(), \
       'a wing crossing the cabin box remains intact instead of losing its external tip'
print('native shell payload, animated exterior, large/half/uppercase vertices, protected equipment and seat masks passed')
