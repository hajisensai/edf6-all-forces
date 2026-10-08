"""Keep control, target-owned defense and replica support separated in production."""
from pathlib import Path
from online_gate_guard import body, code_only

root = Path(__file__).resolve().parents[1]
source = code_only((root / 'src/proteus.cpp').read_text(encoding='utf-8'))
network = code_only((root / 'src/proteus_net.inc').read_text(encoding='utf-8'))
field = code_only((root / 'src/proteus_field.inc').read_text(encoding='utf-8'))
frame = body(source, 'void Frame(')
assert frame.index('NetworkFrame(') < min(frame.index('Pressed('), frame.index('proteus::Step('), frame.index('Legs('), frame.index('QueueWeapons('))
remote = body(network, 'bool NetworkFrame(')
for forbidden in ('Pressed(', 'Mark(', 'Legs(', 'DriverGun(', 'Salvo(', 'proteus::Step('):
    assert forbidden not in remote, forbidden
assert 'FieldFrame(u,v,dt,Cfg())' in remote
shield = body(source, 'float* Shield(')
assert shield.index('DefenseOwner(') < shield.index('proteus::Absorb(')
assert shield.index('proteus_damage_gate::Eligible(') < shield.index('proteus::Absorb('), 'native-rejected damage cannot consume the barrier'
assert 'SendDefense(' in shield
apply = body(network, 'void ApplyControl(')
assert '.barrier=' not in apply and '.quiet=' not in apply, 'pilot state cannot reset the damage owner pool'
assert 'DefenseOwner(v)' in body(network, 'void DefenseTick(')
assert 'FieldOwnedHere(o)' in body(source, 'void __fastcall FieldVisit(')
assert 'std::fmin(taken,1-p.defense)' in field and 'extra-t.extraGiven' in field and 'energy-t.energyGiven' in field
assert 'RestoreFieldMultiplier' in body(field, 'void RefreshFieldWrites(')
assert 'contribution.source.Is(v)' in field, 'reused source slots must not retain old field identity'
print('proteus_net_guard: control, pose/defense ownership, replica fire isolation and field dedup passed')

weapons = code_only((root / 'src/proteus_weapons.inc').read_text(encoding='utf-8'))
flush = body(weapons, 'void FlushWeapons(')
assert flush.index('IsOnlineAuthority(v)') < flush.index('DriverGun(')
assert 'u->net.remote' in flush and 'u->queuedDriver.Is(' in flush and 'u->queuedFireFrame!=GameFrame()' in flush
post = body(weapons, 'void __fastcall ProteusWeaponPost(')
assert post.index('nextWeaponPost)(object,step)') < post.index('FlushWeapons(v)')

vhud = code_only((root / 'src/vhud.cpp').read_text(encoding='utf-8'))
assert 'ProteusDriverSight(v,&r.arm[0])' in vhud and 'ProteusSightWeapon(v,r.seat)' in vhud
assert 'a.physicalOnly=true;a.coFired=true' in vhud, 'paired physical cannon must have a displayed firing path'
