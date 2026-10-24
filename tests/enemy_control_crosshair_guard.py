"""The stock crosshair is not drawn for a player who controls an enemy (the user, 2026-10-21:
"操控怪物的时候没隐藏武器准星"). The crosshair's draw (HudPlayer_CrossHair slot 3, src/infantryscope.cpp
CrossDrawHook) must return before the original for the soldier it is for (+0x580) while
EnemyControlReplacesPlayerHud holds, the same predicate the hidden armor and weapon gauges use
(src/stockgauge.cpp, docs/hud-re.md 9.3.1)."""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main() -> int:
    source = (ROOT / "src" / "infantryscope.cpp").read_text(encoding="utf-8")
    hook = re.search(r"void __fastcall CrossDrawHook\(.*?\n}\n", source, re.S)
    failures = []
    if not hook:
        failures.append("CrossDrawHook not found")
    else:
        body = hook.group(0)
        guard = body.find("EnemyControlReplacesPlayerHud(soldier))return;")
        original = body.find("nextCrossDraw(self,a,b);")
        if guard < 0:
            failures.append("the crosshair's draw does not return for a player controlling an enemy")
        if original < 0 or (guard >= 0 and guard > original):
            failures.append("the enemy-control return must come before the original draw")
        if "At<const void*>(self,kCrossHuman)" not in body:
            failures.append("the soldier tested must be the crosshair's own (+0x580)")
    if '#include "enemy_control.h"' not in source:
        failures.append("infantryscope.cpp must include enemy_control.h")
    stock = (ROOT / "src" / "stockgauge.cpp").read_text(encoding="utf-8")
    if "EnemyControlReplacesPlayerHud(" not in stock:
        failures.append("the gauges no longer use EnemyControlReplacesPlayerHud: keep one predicate")
    for f in failures:
        print("FAIL", f)
    print(f"enemy control crosshair guard: {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
