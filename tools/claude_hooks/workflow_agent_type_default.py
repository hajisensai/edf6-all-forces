"""PreToolUse hook (Workflow): default every agent() call to the trimmed `general` agent type.

A workflow agent() without opts.agentType runs as the built-in workflow-subagent, which carries
26 tool schemas (Artifact, SendUserFile, plugin/skill suggesters, ccd session tools, visualize...)
plus the skills list: ~55k baseline tokens re-read on every turn (2026-11-05 audit: workflow
subagents were 19% of all spend). .claude/agents/general.md is the same Opus role with only
the tools subagents actually use. An explicit opts.agentType in the script still wins.

Rewrite: every code-position call `agent(` becomes `__wfAgent(`, and a one-line helper is
inserted right after the `export const meta = {...}` literal. Idempotent via MARKER.
"""

from __future__ import annotations

import json
import os
import re
import sys
from typing import Any

DEFAULT_TYPE = "general"
MARKER = "/*wf-agent-default*/"
# Mirror subagent_model_router.py: the model is pinned by agent type (workflow agent() calls do not
# pass through the Agent-tool router, and agent .md `model:` is not applied to workflow agents).
TYPE_MODEL: dict[str, str] = {"scout": "sonnet", "worker": "sonnet", "general": "opus", "reviewer": "opus"}
HELPER = (
    f"{MARKER} const __wfAgent = (p, o) => {{ const q = Object.assign({{agentType: '{DEFAULT_TYPE}'}}, o || {{}}); "
    f"const m = {json.dumps(TYPE_MODEL)}[q.agentType]; if (m) q.model = m; return agent(p, q); }};\n"
)
def _rewrite_calls(body: str) -> str:
    """Replace `agent(` with `__wfAgent(` only in code: skips strings, template text, comments
    and member calls (`x.agent(`). Template `${...}` bodies are code and are rewritten."""
    out: list[str] = []
    i, n = 0, len(body)
    # Stack of contexts: "code" (with brace depth) or "tpl" (template literal text).
    stack: list[list[Any]] = [["code", 0]]
    while i < n:
        ctx = stack[-1]
        ch = body[i]
        if ctx[0] == "tpl":
            if ch == "\\":
                out.append(body[i:i + 2]); i += 2; continue
            if ch == "`":
                stack.pop(); out.append(ch); i += 1; continue
            if body.startswith("${", i):
                stack.append(["code", 0]); out.append("${"); i += 2; continue
            out.append(ch); i += 1; continue
        # code context
        if body.startswith("//", i):
            j = body.find("\n", i); j = n if j < 0 else j
            out.append(body[i:j]); i = j; continue
        if body.startswith("/*", i):
            j = body.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append(body[i:j]); i = j; continue
        if ch in "'\"":
            j = i + 1
            while j < n and body[j] != ch and body[j] != "\n":
                j += 2 if body[j] == "\\" else 1
            out.append(body[i:j + 1]); i = j + 1; continue
        if ch == "`":
            stack.append(["tpl"]); out.append(ch); i += 1; continue
        if ch == "{":
            ctx[1] += 1
        elif ch == "}":
            if ctx[1] == 0 and len(stack) > 1:
                stack.pop(); out.append(ch); i += 1; continue
            ctx[1] -= 1
        if body.startswith("agent(", i) and (i == 0 or not re.match(r"[\w.$]", body[i - 1])):
            out.append("__wfAgent("); i += len("agent("); continue
        out.append(ch); i += 1
    return "".join(out)


def _meta_end(script: str) -> int | None:
    """Index just past the closing brace of the `export const meta = {` literal."""
    m = re.search(r"export\s+const\s+meta\s*=\s*\{", script)
    if not m:
        return None
    depth = 0
    i = m.end() - 1
    quote: str | None = None
    while i < len(script):
        ch = script[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                quote = None
        elif ch in "'\"`":
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                j = i + 1
                while j < len(script) and script[j] in " \t;":
                    j += 1
                return j
        i += 1
    return None


def rewrite(script: str) -> str:
    if MARKER in script:
        return script
    end = _meta_end(script)
    if end is None:
        return script
    head, body = script[:end], script[end:]
    new_body = _rewrite_calls(body)
    if new_body == body:
        return script
    return head + "\n" + HELPER + new_body.lstrip("\n")


def main() -> None:
    try:
        payload: dict[str, Any] = json.load(sys.stdin)
    except json.JSONDecodeError:
        return
    if payload.get("tool_name") != "Workflow":
        return
    tool_input: dict[str, Any] = dict(payload.get("tool_input") or {})
    path = tool_input.get("scriptPath")
    if isinstance(path, str) and path:
        # Iteration path: rewrite the persisted script file in place (idempotent).
        try:
            with open(path, encoding="utf-8") as f:
                old = f.read()
        except OSError:
            return
        new = rewrite(old)
        if new != old:
            data = new.encode("utf-8")
            with open(path, "wb") as f:
                f.write(data)
        return
    script = tool_input.get("script")
    if not isinstance(script, str):
        return
    new_script = rewrite(script)
    if new_script == script:
        return
    tool_input["script"] = new_script
    json.dump(
        {"hookSpecificOutput": {"hookEventName": "PreToolUse", "updatedInput": tool_input}},
        sys.stdout,
        ensure_ascii=False,
    )


if __name__ == "__main__":
    main()
