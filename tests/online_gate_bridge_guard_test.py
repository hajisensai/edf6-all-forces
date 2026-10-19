"""The exact stock-call exceptions never exempt new authority decisions."""
from pathlib import Path
import online_gate_guard as guard

root = Path(__file__).resolve().parents[1]
checks = 0
sources = [
    'src/enemy_control_command_actions.cpp',
    'src/enemy_control_deiroi_net.cpp',
    'src/enemy_control_target.cpp',
    'src/enemy_control_walking_guns.cpp',
    'src/enemy_control_weapon_aim.cpp',
]
for name in sources:
    code = guard.code_only((root / name).read_text(encoding='utf-8'))
    cleaned = guard.native_bridge_code(name, code).upper()
    assert all(rva.upper() not in cleaned for rva in guard.DECISION_RVAS), name
    checks += 1
    for rva in guard.DECISION_RVAS:
        bad = code + '\nbool BadAuthority(){ return NativeCall(image+' + rva + ')(nullptr); }\n'
        assert rva.upper() in guard.native_bridge_code(name, bad).upper(), (name, rva)
        checks += 1
# The call's surrounding contract is part of its exemption.
for name, was, changed in [
    ('src/enemy_control_deiroi_net.cpp', 'NativeActionRole(root,&authority)', 'UnboundRole(root,&authority)'),
    ('src/enemy_control_target.cpp', 'network[14]', 'network[15]'),
    ('src/enemy_control_weapon_aim.cpp', 'constexpr unsigned at=0x443A93;', 'constexpr unsigned at=0x443A94;'),
]:
    code = guard.code_only((root / name).read_text(encoding='utf-8'))
    assert was in code
    assert '0X7748F0' in guard.native_bridge_code(name, code.replace(was, changed)).upper(), name
    checks += 1
print(f'online_gate_bridge_guard: {checks} checks passed')
