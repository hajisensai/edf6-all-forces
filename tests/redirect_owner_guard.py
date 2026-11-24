"""Every game call site has one owner (edf::RedirectCall / PatchCall).

edf::RedirectCall takes a call site only while it still calls the stock function: a second module redirecting the same
site is refused at load and its feature is silently left out (as the first person's culling was whenever the unlimited
frame rate was on, both at 0x1198FA4). This scans both plugins (src/, autoturret/, common/) for the call sites passed to
RedirectCall / PatchCall / RedirectImportCall (the k* constants in their first argument, scalars or arrays) and fails
when one RVA is redirected from two files. A feature that has to act at a taken site registers with its owner instead
(0x1198FA4: src/render_record.cpp, the frame rate's turn then the first person's). perf_probe.cpp's Chain stacks on
any owner by design (it accepts the owner's thunk) and passes no k* constant to RedirectCall.
"""
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CALLS = ("RedirectCall(", "PatchCall(", "RedirectImportCall(")


def strip(text):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)


def first_arg(text, start):
    depth, i = 0, start
    while i < len(text):
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return text[start:i]
            depth -= 1
        elif c == "," and depth == 0:
            return text[start:i]
        i += 1
    return text[start:]


def owners():
    sites = collections.defaultdict(set)
    for top in ("src", "autoturret", "common"):
        for dirpath, _, files in os.walk(os.path.join(ROOT, top)):
            for name in files:
                if not name.endswith((".cpp", ".h", ".inc")):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = strip(f.read())
                if not any(c in text for c in CALLS):
                    continue
                rel = os.path.relpath(path, ROOT).replace("\\", "/")
                values = collections.defaultdict(list)
                for n, v in re.findall(r"\b(k\w+)\s*=\s*(0x[0-9A-Fa-f]+)\b", text):
                    values[n].append(int(v, 16))
                for n, body in re.findall(r"\b(k\w+)\s*\[\s*\w*\s*\]\s*=\s*\{([^}]*)\}", text):
                    values[n].extend(int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", body))
                for call in CALLS:
                    for m in re.finditer(r"(?<![\w:])(?:edf::)?" + re.escape(call), text):
                        arg = first_arg(text, m.end())
                        for ident in re.findall(r"\bk\w+", arg):
                            for v in values.get(ident, ()):
                                sites[v].add(rel)
    return sites


def main():
    failures = []
    sites = owners()
    for rva, files in sorted(sites.items()):
        if len(files) > 1:
            failures.append(f"0x{rva:X} is redirected from {len(files)} files: {', '.join(sorted(files))}")
    if sites.get(0x1198FA4) != {"src/render_record.cpp"}:
        failures.append(f"0x1198FA4 (the render recording) must be redirected by src/render_record.cpp alone, "
                        f"found {sorted(sites.get(0x1198FA4, ()))}")
    for rel, slot in (("src/frame_rate_view.cpp", "kFrameRate"), ("src/firstperson.cpp", "kFirstPerson")):
        with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
            text = strip(f.read())
        if "renderrecord::Install()" not in text or f"renderrecord::Set(renderrecord::Slot::{slot}," not in text:
            failures.append(f"{rel} must install render_record and register its {slot} turn")
        if re.search(r"0x1198FA4|0x11978D0", text, re.I):
            failures.append(f"{rel} names the render recording's call itself (render_record.cpp owns it)")
    for f in failures:
        print("FAIL", f)
    print(f"redirect_owner_guard: {len(sites)} call sites, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
