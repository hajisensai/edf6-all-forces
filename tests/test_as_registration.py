"""AngelScript registration dataflow fixtures; no EDF.dll or third-party packages required."""
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pylib'))
from as_registration import registration_impl


def ins(address: int, mnemonic: str, operands: str, size: int = 7) -> SimpleNamespace:
    return SimpleNamespace(address=address, mnemonic=mnemonic, op_str=operands, size=size)


def code(rva: int) -> bool:
    return 0x1000 <= rva < 0x2000


class RegistrationTests(unittest.TestCase):
    def test_inline_funcptr_not_the_declaration_string(self) -> None:
        instructions = [ins(0x100, 'lea', 'rcx, [rip + 0x1000]'),
                        ins(0x107, 'mov', 'qword ptr [rsp + 0x40], rcx'),
                        ins(0x10e, 'lea', 'rdx, [rip + 0x3000]'),
                        ins(0x115, 'call', 'r10', 3)]
        self.assertEqual(registration_impl(instructions, 0x10e, code), [0x1107])

    def test_pointer_helper_layout_is_retained(self) -> None:
        instructions = [ins(0x100, 'lea', 'rdx, [rip + 0x1000]'),
                        ins(0x107, 'call', '0x1800', 5),
                        ins(0x10c, 'lea', 'r8, [rip + 0x3000]'),
                        ins(0x113, 'call', '0x1900', 5)]
        self.assertEqual(registration_impl(instructions, 0x10c, code), [0x1107])

    def test_other_data_lea_is_not_an_implementation(self) -> None:
        instructions = [ins(0x100, 'lea', 'rdx, [rip + 0x3000]'),
                        ins(0x107, 'call', 'r10', 3)]
        self.assertEqual(registration_impl(instructions, 0x100, code), [])

    def test_function_pointer_cannot_cross_a_registration_call(self) -> None:
        instructions = [ins(0x100, 'lea', 'rcx, [rip + 0x1000]'),
                        ins(0x107, 'mov', 'qword ptr [rsp + 0x40], rcx'),
                        ins(0x10e, 'call', 'r10', 3),
                        ins(0x111, 'lea', 'rdx, [rip + 0x3000]'),
                        ins(0x118, 'call', 'r10', 3)]
        self.assertEqual(registration_impl(instructions, 0x111, code), [])

    def test_subregister_write_invalidates_old_address(self) -> None:
        instructions = [ins(0x100, 'lea', 'rcx, [rip + 0x1000]'),
                        ins(0x107, 'xor', 'ecx, ecx', 2),
                        ins(0x109, 'mov', 'qword ptr [rsp + 0x40], rcx'),
                        ins(0x110, 'lea', 'rdx, [rip + 0x3000]'),
                        ins(0x117, 'call', 'r10', 3)]
        self.assertEqual(registration_impl(instructions, 0x110, code), [])


if __name__ == '__main__':
    unittest.main()
