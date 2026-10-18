"""Recover supported AngelScript registration layouts without guessing nearby LEA targets.

The caller supplies decoded, instruction-aligned code and the executable RVA range predicate.
This module reads bytes/instructions only; it never loads a DLL or runs a registration function.
"""
from __future__ import annotations

import re
from collections.abc import Callable, Iterable
from typing import Any


_RIP = re.compile(r"(\w+), \[rip ([+-]) (0x[0-9a-f]+)\]")
_STORE = re.compile(r"qword ptr \[rsp \+ 0x40\], (\w+)")
_ALIASES = {alias: full for full, aliases in {
    'rax': ('rax', 'eax', 'ax', 'al', 'ah'), 'rbx': ('rbx', 'ebx', 'bx', 'bl', 'bh'),
    'rcx': ('rcx', 'ecx', 'cx', 'cl', 'ch'), 'rdx': ('rdx', 'edx', 'dx', 'dl', 'dh'),
    'rsi': ('rsi', 'esi', 'si', 'sil'), 'rdi': ('rdi', 'edi', 'di', 'dil'),
    **{f'r{i}': (f'r{i}', f'r{i}d', f'r{i}w', f'r{i}b') for i in range(8, 16)},
}.items() for alias in aliases}



def registration_impl(instructions: Iterable[Any], declaration_xref: int,
                      executable: Callable[[int], bool]) -> list[int]:
    """Function RVAs passed by this declaration's registration block, or [] for an unknown layout.

    EDF6 builds an asSFuncPtr inline: LEA reg,function; MOV [rsp+40h],reg; LEA rdx,declaration;
    CALL RegisterGlobalFunction. Older layouts use LEA rdx,function; CALL pointer helper;
    LEA r8,declaration; CALL registration. Calls delimit blocks and invalidate volatile constants.
    In particular a declaration/data address from an unrelated LEA is never a function candidate.
    """
    registers: dict[str, int] = {}
    packed: int | None = None
    helper: int | None = None
    selected: int | None = None
    result: set[int] = set()
    for ins in instructions:
        operands = ins.op_str
        if ins.mnemonic == 'call':
            if selected is not None:
                result.add(selected)
            # A pointer-building helper receives a code address in rdx. The next registration may use it.
            pointer = registers.get('rdx')
            helper = pointer if pointer is not None and executable(pointer) else None
            registers.clear()
            packed = selected = None
            if ins.address > declaration_xref:
                break
            continue
        match = _RIP.fullmatch(operands) if ins.mnemonic == 'lea' else None
        if match:
            register, sign, displacement = match.groups()
            target = ins.address + ins.size + int(displacement, 16) * (1 if sign == '+' else -1)
            registers[register] = target
            if ins.address == declaration_xref:
                if register == 'rdx':
                    selected = packed
                elif register == 'r8':
                    selected = helper
            continue
        store = _STORE.fullmatch(operands) if ins.mnemonic == 'mov' else None
        if store:
            value = registers.get(store.group(1))
            packed = value if value is not None and executable(value) else None
            continue
        # Only the exact constants above are trusted. Any other register write invalidates its old address.
        first = _ALIASES.get(operands.split(',', 1)[0], '')
        if first in registers and ins.mnemonic not in ('cmp', 'test', 'push'):
            registers.pop(first)
        if ins.mnemonic in ('ret', 'int3'):
            break
    return sorted(result)
