"""Keep Sazabi's replay isolated from the local pilot, physics and damage decisions."""
from pathlib import Path
from online_gate_guard import body, code_only

root = Path(__file__).resolve().parents[1]
source = code_only((root / 'src/sazabi.cpp').read_text(encoding='utf-8'))
replay = code_only((root / 'src/sazabi_net.inc').read_text(encoding='utf-8'))
transport = code_only((root / 'src/sazabi_net.cpp').read_text(encoding='utf-8'))
drive = body(source, 'void Drive(')
assert drive.index('NetworkFrame(') < min(drive.index('Read('), drive.index('Pilot('), drive.index('ArmsStep('))
assert 'm->net.remote' in body(source, 'bool SazabiBodyStep(')
assert 'RemoteFresh(' in body(source, 'bool SazabiMessage(')
assert 'm->net.state.arms.guardShare' in body(source, 'bool SazabiMessage(')
presentation = body(replay, 'void ReplayNetwork(')
for forbidden in ('Read(', 'Pilot(', 'TakeButtons(', 'FlyFunnels(', 'ArmsFire(', 'DrillCharge(', 'Trigger(', 'VisitEnemies('):
    assert forbidden not in presentation, f'replay entered a decision/damage path: {forbidden}'
for line in presentation.splitlines():
    if 'EmcFire(' in line:
        assert ',0.0f)' in line, 'replayed special effects must carry zero damage'
assert 'm.net.state.controller==SazabiNetController(v)' in body(replay, 'bool RemoteFresh(')
assert '0x300' not in body(transport, 'std::int32_t SazabiNetController('), 'last driver cannot retain authority epoch'
assert '0x17DB4C8' in transport and 'sazabi_net::kMagic' in body(transport, 'bool Peek(')
assert 'f.target=-1' in body(replay, 'void AcceptNetwork('), 'wire ids must never become local target-slot indices'
print('sazabi_net_guard: replay, physics, shield, identity and zero-damage boundaries passed')
