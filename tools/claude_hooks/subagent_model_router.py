"""PreToolUse hook for the Agent tool: pin the subagent model by subagent_type.

Only subagent_type decides the model; the prompt text and any `model` passed
in the call are ignored, so conversation framing cannot change the routing.
Types not listed pass through untouched (fork, Explore, claude-code-guide...).
"""

import json
import sys
from typing import Any

SONNET_TYPES: frozenset[str] = frozenset({"scout", "worker"})
OPUS_TYPES: frozenset[str] = frozenset({"reviewer", "general", "general-purpose", "claude", "Plan"})
# Built-in general-purpose carries every tool schema, the skills list and MCP/session prompt
# sections (~54k baseline tokens re-read on every turn). .claude/agents/general.md is the same
# role with only the tools subagents actually use (2026-11-05 audit), so redirect to it.
REDIRECT_TYPES: dict[str, str] = {"general-purpose": "general"}


def pinned_model(subagent_type: str | None) -> str | None:
    # Omitted subagent_type means general-purpose.
    kind = subagent_type or "general-purpose"
    if kind in SONNET_TYPES:
        return "sonnet"
    if kind in OPUS_TYPES:
        return "opus"
    return None


def main() -> int:
    event: dict[str, Any] = json.load(sys.stdin)
    if event.get("tool_name") not in ("Agent", "Task"):
        return 0
    tool_input: dict[str, Any] = dict(event.get("tool_input") or {})
    kind = tool_input.get("subagent_type") or "general-purpose"
    redirected = REDIRECT_TYPES.get(kind)
    # Forks / named teammates keep their own semantics; only plain dispatches are redirected.
    if redirected and not tool_input.get("name") and not tool_input.get("team_name"):
        tool_input["subagent_type"] = redirected
    model = pinned_model(tool_input.get("subagent_type"))
    if model is None:
        return 0
    if tool_input.get("model") == model and tool_input.get("subagent_type") == (event.get("tool_input") or {}).get("subagent_type"):
        return 0
    tool_input["model"] = model
    json.dump(
        {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "allow",
                "permissionDecisionReason": f"subagent model pinned to {model} by subagent_type",
                "updatedInput": tool_input,
            }
        },
        sys.stdout,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
