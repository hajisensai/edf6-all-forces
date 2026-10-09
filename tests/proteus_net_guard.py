"""Keep the Proteus's control, the shield owner's HP count and the replicas' presentation separated in production source.

Run by ctest (proteus_net_guard). It holds in place what tests/proteus_net_runtime_test.cpp checks dynamically, so a
refactor cannot quietly move a decision to the wrong machine:
  - a replica's frame (NetworkFrame) never reads the driver's keys nor runs the stance rules or the legs;
  - pilot control never writes the shield's HP / quiet / broken lock: only a defense packet or its owner's barrier does;
  - the barrier's HP is read as the shield's only on the registered owner, a copy writes the owner's count into it;
  - a borrowed mount is pulled only on its operator's own machine, and its fire-step user is the operator or nobody;
  - the field writes only what this machine owns and keeps one contribution per source.
"""
from pathlib import Path
from online_gate_guard import body, code_only

root = Path(__file__).resolve().parents[1]
source = code_only((root / 'src/proteus.cpp').read_text(encoding='utf-8'))
network = code_only((root / 'src/proteus_net.inc').read_text(encoding='utf-8'))
field = code_only((root / 'src/proteus_field.inc').read_text(encoding='utf-8'))
shield = code_only((root / 'src/proteus_shield.inc').read_text(encoding='utf-8'))
weapons = code_only((root / 'src/proteus_weapons.inc').read_text(encoding='utf-8'))

frame = body(source, 'void Frame(')
assert frame.index('NetworkFrame(') < min(frame.index('Pressed('), frame.index('proteus::Step('), frame.index('Legs('))
assert 'DefenseOwner(v)' in frame, 'a driver that is not the owner keeps the owner HP count through its own Step'
remote = body(network, 'bool NetworkFrame(')
for forbidden in ('Pressed(', 'Legs(', 'proteus::Step('):
    assert forbidden not in remote, forbidden
assert 'FieldFrame(u,v,dt)' in remote and 'BarrierFrame(u,v)' in remote
assert remote.index('DefenseOwner(v)') < remote.index('proteus::Refill('), 'only the owner refills the shield'
apply = body(network, 'void ApplyControl(')
for field_name in ('.shield=', '.quiet=', '.broken='):
    assert field_name not in apply, f'pilot state cannot write the owner pool ({field_name})'
receive = body(network, 'void ReceiveNetwork(')
assert receive.index('Kind::defense') < receive.index('ApplyControl('), 'a defense packet is never applied as control'

step = body(shield, 'void BarrierStep(')
assert step.index('DefenseOwner(v)') < step.index('proteus::Sense('), 'the barrier HP is the shield HP only on its owner'
assert 'Put<float>(b,kBarrierHp,u->st.shield*u->barrier.full)' in step, 'a copy shows the owner count'
assert 'kBarrierNoEcho' in body(shield, 'void KillBarrier('), 'a dropped local barrier is never broadcast'

empty = body(weapons, 'void __fastcall ProteusEmptyWeapon(')
assert empty.index('OperatorLocal(') < empty.index('PullFn'), 'a borrowed mount is pulled only on its operator machine'
user = body(weapons, 'const void* __fastcall UserHook(')
assert user.index('EmptyMount(') < user.index('nextUser(iface,weapon)'), 'an empty mount never takes the stale native LAST rider'

assert 'FieldOwnedHere(o)' in body(source, 'void __fastcall FieldVisit(')
assert 'std::fmin(taken,1-p.defense)' in field and 'extra-t.extraGiven' in field and 'energy-t.energyGiven' in field
assert 'RestoreFieldMultiplier' in body(field, 'void RefreshFieldWrites(')
assert 'contribution.source.Is(v)' in field, 'reused source slots must not retain old field identity'

vhud = code_only((root / 'src/vhud.cpp').read_text(encoding='utf-8'))
assert 'ProteusBorrowedWeapons(v,r.seat,' in vhud, 'the seat HUD lists the stock mounts the seat borrows'
print('proteus_net_guard: control / owner HP / replica presentation, borrowed fire ownership and field dedup passed')
