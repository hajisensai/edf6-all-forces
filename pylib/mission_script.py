"""Static mission-script selection and metadata audits. No engine execution or script translation.

The selection contract is EDF.dll 0x678CCB46, selector 0x1F68D0 and BEFORE gate 0x70DAD2.
A readable AC takes priority over BVM even if the source is invalid; compilation has no BVM fallback.
"""
from __future__ import annotations

from collections import Counter
from collections.abc import Callable
from dataclasses import dataclass
import hashlib
import re
import struct

import bvm41


@dataclass(frozen=True)
class File:
    data: bytes
    source: str


Reader = Callable[[str], File | None]


def mission_key(key: str) -> str:
    key = key.replace('\\', '/').strip('/')
    if key.lower().startswith('app:/mission/'):
        key = key[len('app:/mission/'):]
    elif key.lower().startswith('mission/'):
        key = key[len('mission/'):]
    if not key or any(p in ('', '.', '..') for p in key.split('/')) or ':' in key:
        raise ValueError(f'invalid mission key: {key!r}')
    return key


def select(reader: Reader, key: str, stem: str = 'MISSION') -> dict:
    """What the stock selector would open through this reader; never says it compiled or ran."""
    key = mission_key(key)
    if stem not in ('MISSION', 'BEFORE'):
        raise ValueError('script stem must be MISSION or BEFORE')
    base = f'MISSION/{key}/{stem}'
    if stem == 'BEFORE' and reader(base + '.BVM') is None:
        ac = reader(base + '.AC')
        return {'state': 'blocked_before_gate' if ac else 'absent',
                'ignored_ac': ac.source if ac else None, 'path': base}
    ac = reader(base + '.AC')
    bvm = reader(base + '.BVM')
    chosen, extension = (ac, 'AC') if ac is not None else (bvm, 'BVM')
    if chosen is None:
        return {'state': 'missing', 'path': base}
    return {'state': 'selected', 'path': base + '.' + extension, 'format': extension,
            'source': chosen.source, 'sha256': hashlib.sha256(chosen.data).hexdigest(),
            'bvm_fallback': False, 'shadowed_bvm': bvm.source if ac is not None and bvm is not None else None}


def bvm_metadata(data: bytes) -> dict:
    program = bvm41.parse(data)
    header = struct.unpack_from('<20I', data)
    decoded = bvm41._decode(data, header[12], header[14])
    calls, at = [], 0
    for name, code in [('<init>', program.init), *((f.name, f.code) for f in program.funcs)]:
        for index, instruction in enumerate(code):
            if instruction.op in bvm41.OP_NATIVE:
                native = (instruction.op, instruction.v)
                signature = bvm41.SIG6.get(native)
                calls.append({'function': name, 'instruction': index, 'offset': decoded[at + index][0],
                              'table': instruction.op - bvm41.OP_NAT0, 'id': instruction.v,
                              'signature': list(signature) if signature is not None else None,
                              'stack_effect': bvm41.NET6.get(native)})
        at += len(code)
    strings = {x.v for code in [program.init, *(f.code for f in program.funcs)]
               for x in code if x.op == bvm41.OP_PUSHS}
    natives = {(c['table'], c['id']) for c in calls}
    diagnostics = {}
    for name, check in (('stack_issues', bvm41.stack_check), ('type_issues', bvm41.type_check)):
        try:
            diagnostics[name] = check(program)
        except ValueError as error:
            diagnostics[name] = [f'analysis stopped: {error}']
    return {'functions': len(program.funcs), 'native_calls': calls,
            'native_ids': len(natives), 'missing_signatures': sorted({f"{c['table']}/{c['id']}" for c in calls
                                                                      if c['signature'] is None}),
            'level_multiplier_calls': [c for c in calls if c['table'] == 0 and c['id'] == 20],
            'fixed_multiplier_calls': [c for c in calls if c['table'] == 0 and c['id'] == 22],
            **diagnostics,
            'direct_references': sorted(s for s in strings if s.lower().startswith('app:/')),
            'voice_ids': sorted(s for s in strings if re.fullmatch(r'A[0-9A-Za-z_]+', s))}


_TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.DOTALL)


def ac_metadata(data: bytes) -> dict:
    source = data.decode('utf-8-sig')
    strings: set[str] = set()

    def strip(match: re.Match) -> str:
        token = match.group()
        if token.startswith('"'):
            strings.add(token[1:-1])
            return '""'
        return '\n' * token.count('\n')

    code = _TOKENS.sub(strip, source)
    calls = Counter(re.findall(r'\b(SetMissionLevelFix|SetMissionLevel|internal_CreateEvent)\s*\(', code))
    return {'encoding': 'utf-8', 'bom': data.startswith(b'\xef\xbb\xbf'),
            'api_call_sites': dict(sorted(calls.items())),
            'direct_references': sorted(s for s in strings if s.lower().startswith('app:/')),
            'voice_ids': sorted(s for s in strings if re.fullmatch(r'A[0-9A-Za-z_]+', s)),
            'compile_status': 'not_verified', 'gameplay_status': 'not_verified'}


def audit(reader: Reader, key: str) -> dict:
    result = {'key': mission_key(key), 'evidence': 'static_selection_only', 'scripts': {}}
    for stem in ('MISSION', 'BEFORE'):
        selection = select(reader, key, stem)
        result['scripts'][stem] = selection
        if selection['state'] != 'selected':
            continue
        file = reader(selection['path'])
        assert file is not None
        try:
            selection['metadata'] = ac_metadata(file.data) if selection['format'] == 'AC' else bvm_metadata(file.data)
        except (ValueError, IndexError, KeyError, struct.error, UnicodeError) as error:
            selection['parse_error'] = f'{type(error).__name__}: {error}'
    return result


def compare(old: File, new: File, old_rmpa: File | None = None, new_rmpa: File | None = None) -> dict:
    """Conservative AC-candidate differences; equal static sets do not mean behavior equivalence."""
    bvm, ac = bvm_metadata(old.data), ac_metadata(new.data)
    before = set(bvm['voice_ids'])
    after = set(ac['voice_ids'])
    old_refs, new_refs = set(bvm['direct_references']), set(ac['direct_references'])
    return {'bvm_source': old.source, 'ac_source': new.source,
            'bvm_sha256': hashlib.sha256(old.data).hexdigest(), 'ac_sha256': hashlib.sha256(new.data).hexdigest(),
            'rmpa_equal': old_rmpa.data == new_rmpa.data if old_rmpa and new_rmpa else None,
            'direct_references_removed': sorted(old_refs - new_refs),
            'direct_references_added': sorted(new_refs - old_refs),
            'voice_ids_removed': sorted(before - after), 'voice_ids_added': sorted(after - before),
            'event_sites_bvm': sum(c['table'] == 0 and c['id'] == 2 for c in bvm['native_calls']),
            'event_sites_ac': ac['api_call_sites'].get('internal_CreateEvent', 0),
            'bvm_level_calls': bvm['level_multiplier_calls'],
            'bvm_fixed_level_calls': bvm['fixed_multiplier_calls'],
            'ac_fixed_level_call_sites': ac['api_call_sites'].get('SetMissionLevelFix', 0),
            'fixed_level_review': 'different_call_site_count'
            if len(bvm['fixed_multiplier_calls']) != ac['api_call_sites'].get('SetMissionLevelFix', 0)
            else 'same_call_site_count_only',
            'ac_level_call_sites': ac['api_call_sites'].get('SetMissionLevel', 0),
            'level_semantics': 'requires_team_scope_review'
            if bvm['level_multiplier_calls'] or ac['api_call_sites'].get('SetMissionLevel', 0)
            else 'no_level_multiplier_call',
            'missing_bvm_signatures': bvm['missing_signatures'],
            'compile_status': 'not_verified', 'single_player_status': 'not_verified',
            'multiplayer_status': 'not_verified', 'automatic_switch_allowed': False}
