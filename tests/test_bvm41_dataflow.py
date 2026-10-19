"""BVM native argument analysis follows every predecessor of a control-flow join."""
from __future__ import annotations

from pathlib import Path
import sys
import struct
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "pylib"))
import bvm41 as b


def program(body: list[b.Ins]) -> b.Program:
    epilogue = [b.Ins(b.OP_PUSHL), b.Ins(b.OP_FPADD), b.Ins(b.OP_RET)]
    return b.Program("Mission", [], [], [b.Function("Main", b"", epilogue + body)])


def diamond(first: b.Ins, second: b.Ins) -> b.Program:
    return program([
        b.Ins(b.OP_PUSH1), b.Ins(b.OP_JZ, 7), first, b.Ins(b.OP_JMP, 8), second,
        b.Ins(b.OP_NAT0, 30), b.Ins(b.OP_END),
    ])


class NativeDataflowTests(unittest.TestCase):
    def test_type_check_reports_the_other_predecessor(self) -> None:
        prog = diamond(b.Ins(b.OP_PUSHS, "valid"), b.Ins(b.OP_PUSH, 3, 1))
        self.assertEqual(b.type_check(prog), [
            "Main 8: native 0/30 argument 1 of 1: reads p, the script passes i"])

    def test_native_call_keeps_all_literals_without_claiming_one_value(self) -> None:
        prog = diamond(b.Ins(b.OP_PUSHS, "first"), b.Ins(b.OP_PUSHS, "second"))
        calls = b.native_calls(prog, {(b.OP_NAT0, 30)})
        self.assertEqual(len(calls), 1)
        arg = calls[0][3][0]
        self.assertEqual(arg.t, "s")
        self.assertIsNone(arg.s)
        self.assertEqual(arg.literals, frozenset({"first", "second"}))
        self.assertEqual(b.type_check(prog), [])

    def test_same_literal_stays_exact(self) -> None:
        prog = diamond(b.Ins(b.OP_PUSHS, "same"), b.Ins(b.OP_PUSHS, "same"))
        arg = b.native_calls(prog, {(b.OP_NAT0, 30)})[0][3][0]
        self.assertEqual((arg.t, arg.s), ("s", "same"))
        self.assertEqual(arg.literals, frozenset({"same"}))

    def test_unknown_join_keeps_known_literal_as_a_possibility(self) -> None:
        prog = diamond(b.Ins(b.OP_PUSHS, "known"), b.Ins(b.OP_PUSHG, 0))
        arg = b.native_calls(prog, {(b.OP_NAT0, 30)})[0][3][0]
        self.assertEqual((arg.t, arg.s), ("?", None))
        self.assertEqual(arg.literals, frozenset({"known"}))
        self.assertEqual(b.type_check(prog), [])

    def test_loop_reaches_a_finite_literal_union(self) -> None:
        prog = program([
            b.Ins(b.OP_PUSHS, "initial"), b.Ins(b.OP_PUSH1), b.Ins(b.OP_JZ, 10),
            b.Ins(b.OP_POP), b.Ins(b.OP_PUSHS, "loop"), b.Ins(b.OP_JMP, 4),
            b.Ins(b.OP_END), b.Ins(b.OP_NAT0, 30), b.Ins(b.OP_END),
        ])
        calls = b.native_calls(prog, {(b.OP_NAT0, 30)})
        self.assertEqual(len(calls), 1)
        self.assertIsNone(calls[0][3][0].s)
        self.assertEqual(calls[0][3][0].literals, frozenset({"initial", "loop"}))

    def test_inconsistent_stack_depth_is_rejected(self) -> None:
        prog = program([b.Ins(b.OP_PUSHS, "growing"), b.Ins(b.OP_JMP, 3)])
        with self.assertRaisesRegex(ValueError, "Main.*stack depths"):
            b.native_calls(prog, {(b.OP_NAT0, 30)})

    def test_underflow_is_reported_without_index_error(self) -> None:
        prog = program([b.Ins(b.OP_POP), b.Ins(0x03), b.Ins(b.OP_END)])
        with self.assertRaisesRegex(ValueError, "Main.*stack underflow"):
            b.type_check(prog)


class FunctionSignatureTests(unittest.TestCase):
    @staticmethod
    def returning() -> b.Function:
        # Native EDF6 evidence: 0x10ACB0B/37 and 0x10ACC28/64 read the signed byte at function-table+0x0C.
        # M045's _SetupBarrier has the packed word 0x103, three type bytes (01 02 02), and ends pushl1/jmp0.
        # Its callers either POP or STOREIND the returned value. CALL 0x10AC1DB pushes only a return address;
        # RET 0x10AC1FD removes only that address, so the function's extra value survives as one result slot.
        return b.Function("_SetupBarrier", b"\x01\x02\x02", [
            b.Ins(b.OP_PUSHL), b.Ins(b.OP_FPADD, 4, 1), b.Ins(b.OP_RET),
            b.Ins(b.OP_FPSUB, 4, 1), b.Ins(b.OP_STOREL), b.Ins(b.OP_STOREL, 3, 1),
            b.Ins(b.OP_STOREL, 2, 1), b.Ins(b.OP_STOREL, 1, 1),
            b.Ins(b.OP_PUSHL, 1, 1), b.Ins(b.OP_JMP, 0, 1),
        ], signature_flags=0x100)

    def test_0103_signature_roundtrips_without_reading_259_types(self) -> None:
        prog = b.Program("Mission", [], [], [self.returning()])
        data = b.build(prog)
        table = struct.unpack_from("<I", data, 0x24)[0]
        self.assertEqual(struct.unpack_from("<I", data, table + 12)[0], 0x103)
        parsed = b.parse(data)
        self.assertEqual(parsed.funcs[0].types, b"\x01\x02\x02")
        self.assertEqual(parsed.funcs[0].signature_flags, 0x100)
        self.assertEqual(parsed.funcs[0].results, 1)
        self.assertEqual(b.build(parsed), data)

    def test_call_consumes_parameters_and_keeps_the_return_slot(self) -> None:
        caller = b.Function("Main", b"", [
            b.Ins(b.OP_PUSHL), b.Ins(b.OP_FPADD, 1, 1), b.Ins(b.OP_RET),
            b.Ins(b.OP_FPSUB, 1, 1), b.Ins(b.OP_STOREL), b.Ins(b.OP_PUSH1),
            b.Ins(b.OP_PUSH, 0x3F800000, 4), b.Ins(b.OP_PUSH, 0x3F800000, 4),
            b.Ins(b.OP_CALL, 1), b.Ins(b.OP_NAT0, 3000, 2), b.Ins(b.OP_JMP, 0, 1),
        ])
        prog = b.Program("Mission", [], [], [caller, self.returning()])
        self.assertEqual(b.stack_check(prog), [])
        self.assertEqual(b.type_check(prog), [])
        args = b.native_calls(prog, {(b.OP_NAT0, 3000)})[0][3]
        self.assertEqual(args, [b.Arg("?")])

    def test_missing_return_value_is_unbalanced(self) -> None:
        fn = self.returning()
        del fn.code[-2]
        self.assertTrue(any("expected 1" in problem for problem in b.stack_check(
            b.Program("Mission", [], [], [fn]))))

    def test_unknown_flags_are_preserved_but_not_guessed_by_analysis(self) -> None:
        fn = self.returning()
        fn.signature_flags = 0x200
        data = b.build(b.Program("Mission", [], [], [fn]))
        parsed = b.parse(data)
        self.assertEqual(parsed.funcs[0].signature_flags, 0x200)
        self.assertEqual(b.build(parsed), data)
        with self.assertRaisesRegex(ValueError, "unknown function signature flags 0x200"):
            b.type_check(parsed)
        with self.assertRaisesRegex(ValueError, "unknown function signature flags 0x200"):
            b.stack_check(parsed)

    def test_negative_signed_count_is_preserved_but_not_analysed(self) -> None:
        fn = self.returning()
        fn.types = b"\x01" * 128
        data = b.build(b.Program("Mission", [], [], [fn]))
        parsed = b.parse(data)
        self.assertEqual(len(parsed.funcs[0].types), 128)
        self.assertEqual(b.build(parsed), data)
        with self.assertRaisesRegex(ValueError, "unsupported signed parameter count 128"):
            b.type_check(parsed)


if __name__ == "__main__":
    unittest.main()
