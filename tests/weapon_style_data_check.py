"""Check the real stock energy-weapon contracts behind HUD classification (read-only Root.cpk)."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'pylib'))
import rootcpk,dsgo
if not __debug__: raise RuntimeError('Run without -O: contract checks must execute')
game=Path(sys.argv[1]) if len(sys.argv)>1 else Path(rootcpk.DEFAULT_GAME)
if not (game/'Root.cpk').is_file():
    print('SKIP: installed Root.cpk unavailable');raise SystemExit(77)
r=rootcpk.Game(str(game))
cases=[('V_510_MASER_THUNDER01.SGO','Weapon_VehicleMaser','SolidBullet01'),
       ('V_510_MASER_AI_THUNDER01.SGO','Weapon_VehicleMaser','SolidBullet01'),
       ('V_510_MASER_THUNDER_MISSION.SGO','Weapon_VehicleMaser','SolidBullet01'),
       ('V_612_A_LASERRIFLE_L.SGO',None,'EfsExposureBullet'),('V_612_A_LASERRIFLE_R.SGO',None,'EfsExposureBullet'),
       ('V_612_A_LASERRIFLE_DLC2_L.SGO',None,'LaserBullet01'),('V_612_A_LASERRIFLE_DLC2_R.SGO',None,'LaserBullet01'),
       ('V_612_LASER_CANNON01_R.SGO',None,'LaserBullet01'),('V_612_HOMING_LASER01_L.SGO',None,'HomingLaserBullet01')]
for name,weapon,ammo in cases:
    d=dsgo.parse(r.read('WEAPON',name)).root
    assert d.get('AmmoClass')==ammo,(name,ammo)
    if weapon: assert d.get('xgs_scene_object_class')==weapon,name
    assert d.get('AmmoGravityFactor')==0.0,name
    assert d.get('AmmoSpeed')>0 and d.get('AmmoAlive')>0,name
    print('PASS',name,weapon or '-',ammo)
dll=game/'EDF.dll'
if dll.is_file():
    import pefile,struct
    pe=pefile.PE(str(dll),fast_load=True)
    assert pe.FILE_HEADER.TimeDateStamp==0x678CCB46 and pe.FILE_HEADER.Machine==0x8664
    for vt,expected in [(0x17E5E40,b'.?AVWeapon_VehicleMaser@@'),(0x179ECA8,b'.?AVFactory@EfsExposureBullet@@'),(0x179FC60,b'.?AVFactory@LaserBullet01@@')]:
        col=struct.unpack('<Q',pe.get_data(vt-8,8))[0]-pe.OPTIONAL_HEADER.ImageBase
        td=struct.unpack('<I',pe.get_data(col+12,4))[0]
        assert pe.get_data(td+16,len(expected)+1)==expected+b'\0'
    print('PASS 3 native RTTI identities (file read only, no DLL entry point)')
print(f'{len(cases)} real energy weapon contracts passed; no game or installation writes')
