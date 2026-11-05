"""Tests for workflow_agent_type_default.rewrite (run: python -I test_workflow_agent_type_default.py)."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from workflow_agent_type_default import HELPER, MARKER, rewrite  # noqa: E402

SCRIPT = """export const meta = {
  name: 'x', description: 'has } brace and agent( in string',
  phases: [{ title: 'A' }],
}
phase('A')
const r = await agent('say agent( hi', {schema: S})
const rs = await parallel(xs.map(x => () => agent(`p ${x}`, {label: x})))
const t = await agent("q", {agentType: 'reviewer'})
// comment mentioning agent( stays
const u = await agent(`tpl agent( text ${ok ? agent('inner') : 'x'} end`)
const v = obj.agent('not me')
return [r, rs, t]
"""


def test_rewrites_calls_not_strings_or_members() -> None:
    out = rewrite(SCRIPT)
    assert out.count(MARKER) == 1
    assert "await __wfAgent('say agent( hi'" in out
    assert "() => __wfAgent(`p ${x}`" in out
    assert "obj.agent('not me')" in out
    assert "// comment mentioning agent( stays" in out
    assert "__wfAgent(`tpl agent( text ${ok ? __wfAgent('inner') : 'x'} end`)" in out
    assert "description: 'has } brace and agent( in string'" in out
    # helper sits right after the meta literal, before the body
    assert out.index(HELPER) < out.index("phase('A')")
    assert out.index("phases: [{ title: 'A' }],\n}") < out.index(HELPER)


def test_idempotent() -> None:
    once = rewrite(SCRIPT)
    assert rewrite(once) == once


def test_no_meta_or_no_agent_untouched() -> None:
    assert rewrite("const a = 1") == "const a = 1"
    s = "export const meta = {name: 'n', description: 'd'}\nlog('x')\n"
    assert rewrite(s) == s


def test_helper_semantics_explicit_type_wins() -> None:
    # Object.assign({agentType: default}, o) -> o.agentType overrides; null opts ok.
    assert "Object.assign({agentType: 'general'}, o || {})" in HELPER
    assert '"scout": "sonnet"' in HELPER and '"general": "opus"' in HELPER


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
