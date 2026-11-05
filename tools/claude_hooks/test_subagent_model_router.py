"""Tests for subagent_model_router (run: python -I tools/claude_hooks/test_subagent_model_router.py)."""

import io
import json
import os
import sys
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import subagent_model_router as router  # noqa: E402


def run(tool_input: dict[str, Any], tool_name: str = "Agent") -> dict[str, Any] | None:
    sys.stdin = io.StringIO(json.dumps({"tool_name": tool_name, "tool_input": tool_input}))
    out = io.StringIO()
    sys.stdout, real = out, sys.stdout
    try:
        router.main()
    finally:
        sys.stdout = real
    text = out.getvalue()
    return json.loads(text)["hookSpecificOutput"]["updatedInput"] if text else None


def test_routing() -> None:
    assert run({"prompt": "x"}) == {"prompt": "x", "subagent_type": "general", "model": "opus"}
    assert run({"subagent_type": "general-purpose"}) == {"subagent_type": "general", "model": "opus"}
    assert run({"subagent_type": "worker", "model": "opus"}) == {"subagent_type": "worker", "model": "sonnet"}
    assert run({"subagent_type": "scout"}) == {"subagent_type": "scout", "model": "sonnet"}
    assert run({"subagent_type": "reviewer", "model": "opus"}) is None
    assert run({"subagent_type": "fork"}) is None
    assert run({"subagent_type": "general-purpose", "name": "a"}) == {
        "subagent_type": "general-purpose", "name": "a", "model": "opus"}
    assert run({"subagent_type": "worker"}, tool_name="Bash") is None


if __name__ == "__main__":
    test_routing()
    print("ok test_routing")
