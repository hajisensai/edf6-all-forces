"""Keep Sazabi's replay isolated from the local pilot, physics and damage decisions."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "pylib"))
from srcscan import body, code as code_only  # noqa: E402  (the one C++ reader)

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
assert 'object_net::Register(sazabi_net::kMagic,&Received)' in transport
assert 'object_net::ChainClass(object_net::Class::v506)' in transport
import re
_row = re.search(r'\{Class::v506,(0x[0-9A-F]+),kVehicleReceive,', (root / 'src/object_net.h').read_text(encoding='utf-8'))
assert _row and int(_row.group(1), 16) + 17 * 8 == 0x17DB4C8, '506 NetworkObject slot 17 is 0x17DB4C8'
assert 'f.target=-1' in body(replay, 'void AcceptNetwork('), 'wire ids must never become local target-slot indices'
print('sazabi_net_guard: replay, physics, shield, identity and zero-damage boundaries passed')
