"""pylib/srcscan.py: the source guards' one C++ reader (comments out, literals whole, definitions and their bodies,
calls and callers), on synthetic sources, so a guard's answer depends on the code, not on its formatting."""
from __future__ import annotations

import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import srcscan  # noqa: E402

SAMPLE = r'''
#include "x.h"
// Foo() in a comment is no call
namespace crew {
namespace {
constexpr float kSpeed=660.0f;   // a speed
constexpr int kTable[]={1,2,3};
enum class Kind:int { kA=0, kB, kC=7 };
const char* kUrl="http://example/{";   // a brace and a // in a literal
char kQuote='"';
void Helper(int a);   // a declaration
}  // namespace

/* Bar() in a block
   comment */
void Helper(int a) {
    if(a){ Bar(a); }
    auto f=[&](int b){ return Baz(b); };
    f(a);
}

struct Thing : Base {
    int Value() const noexcept { return Qux(); }
};

std::uintptr_t __fastcall PreloadHook(void* self) noexcept {
    const char* s=R"(Fake(){)";
    Helper(1);
    Other::Run<int>(2);
    return 0;
}
}  // namespace crew
'''


class SrcScan(unittest.TestCase):
    def test_code_drops_comments_keeps_literals(self) -> None:
        c = srcscan.code(SAMPLE)
        self.assertNotIn('in a comment', c)
        self.assertNotIn('in a block', c)
        self.assertIn('"http://example/{"', c)
        self.assertIn("'\"'", c)
        self.assertEqual(c.count('\n'), SAMPLE.count('\n') - 0)   # line breaks kept (block comment's too)

    def test_body_skips_declarations_and_literal_braces(self) -> None:
        b = srcscan.body(SAMPLE, 'void Helper(int a)')
        self.assertTrue(b.startswith('{') and b.endswith('}'), b)
        self.assertIn('Bar(a);', b)
        self.assertIn('f(a);', b)
        inner = srcscan.body(SAMPLE, 'void Helper(int a)', braces=False)
        self.assertEqual('{' + inner + '}', b)
        p = srcscan.body(SAMPLE, r'__fastcall\s+PreloadHook\(', regex=True)
        self.assertIn('Helper(1);', p)
        self.assertIn('return 0;', p)   # the raw string's brace is not counted
        self.assertEqual(srcscan.body(SAMPLE, 'void Missing('), '')

    def test_definitions(self) -> None:
        names = [d.name for d in srcscan.definitions(SAMPLE)]
        self.assertEqual(names, ['Helper', 'Value', 'PreloadHook'])   # not the lambda, not the declaration
        self.assertIn('Qux()', srcscan.function(SAMPLE, 'Value'))

    def test_calls_and_callers(self) -> None:
        helper = srcscan.function(SAMPLE, 'Helper')
        self.assertTrue(srcscan.calls(helper, 'Bar'))
        self.assertTrue(srcscan.calls(helper, 'Baz'))   # in its lambda: still its body's
        self.assertFalse(srcscan.calls(SAMPLE, 'Foo'))   # only in a comment
        self.assertFalse(srcscan.calls(SAMPLE, 'Fake'))   # only in a literal
        self.assertTrue(srcscan.calls(SAMPLE, 'Run'))     # Other::Run<int>(
        self.assertEqual(srcscan.callers(SAMPLE, 'Helper'), {'PreloadHook'})
        self.assertEqual(srcscan.callers(SAMPLE, 'Qux'), {'Value'})
        self.assertTrue(srcscan.order(srcscan.function(SAMPLE, 'PreloadHook'), 'Helper', 'Run'))
        self.assertFalse(srcscan.order(srcscan.function(SAMPLE, 'PreloadHook'), 'Run', 'Helper'))

    def test_defines_and_values(self) -> None:
        for n in ('kSpeed', 'kTable', 'Kind', 'kB', 'kC', 'Helper', 'Thing'):
            self.assertTrue(srcscan.defines(SAMPLE, n), n)
        self.assertFalse(srcscan.defines(SAMPLE, 'Bar'))
        self.assertEqual(srcscan.value(SAMPLE, 'kSpeed'), '660.0f')
        self.assertEqual(srcscan.value(SAMPLE, 'kC'), '7')
        self.assertIsNone(srcscan.value(SAMPLE, 'kNope'))

    def test_reads_the_real_sources(self) -> None:
        text = srcscan.read('src/live_set.cpp')
        self.assertIn('HasAt', srcscan.functions(text))
        self.assertTrue(srcscan.calls(srcscan.function(text, 'Has'), 'HasAt'))


if __name__ == '__main__':
    unittest.main()
