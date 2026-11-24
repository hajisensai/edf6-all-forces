"""Every code write goes through the code owner registry (common/code_owner.cpp, edf/code_owner.h).

Two modules patching one site is found at run time, not here: each patch primitive (edf::RedirectCall, PatchCode,
RedirectImportCall, PatchVtableSlot, ChainVtableSlot) claims its bytes for the calling file (__builtin_FILE() as a
default argument) before it looks at them, and a second file's claim is logged ("PATCH conflict: ..."; the first
person's culling was left out silently when the frame rate redirected 0x1198FA4 first, #395). The static scan this
replaced read the k* constants in the primitives' first argument and missed literal addresses (image+0x...), loops over
tables, PatchCode writing an E8, and hand-written VirtualProtect writes; tests/code_owner_test.cpp covers the registry.

This guard keeps the registry seeing every write:
  - the primitives claim before they read or write, the import call and the vtable chain passing the caller on;
  - every hand-written code write in the plugins (VirtualProtect to PAGE_EXECUTE_READWRITE in src/, autoturret/)
    claims its bytes with edf::ClaimCode right before;
  - no plugin file names an owner itself (__builtin_FILE / a file name passed), and only the files listed here chain
    (Claim::kLayered) over another's patch;
  - both plugins send conflicts to their log before the first patch and log the totals once loaded.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAYERED_OK = {"src/perf_probe.cpp"}   # Chain: on top of whatever a site calls now, by design (perf_probe.h)
# Code writes outside the registry, each with why. crash_dump.cpp patches kernelbase!SetUnhandledExceptionFilter (not
# EDF.dll; it leaves a jump already there alone) and links no edf6common (crash_dump_test, mission_watch_test build it
# alone).
UNREGISTERED_OK = {"src/crash_dump.cpp"}
failures = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def body(text, signature):
    at = text.find(signature)
    if at < 0:
        return ""
    start = text.find("{", at)
    depth, i = 0, start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return text[start:]


memory = read("common/memory.cpp")
patch = read("common/patch.cpp")
redirect = body(memory, "bool RedirectCall(")
claim = redirect.find("ClaimCode(callSite,5,kind,owner)")
check(claim > 0 and claim < redirect.find("callSite[0]"), "RedirectCall claims its 5 bytes before it reads them")
code = body(patch, "bool PatchCode(")
check(0 < code.find("ClaimCode(at,size,kind,owner)") < code.find("memcmp"), "PatchCode claims before it compares")
vt = body(patch, "bool PatchVtableSlot(")
check(0 < vt.find("ClaimCode(slot,sizeof(void*),kind,owner)") < vt.find("VirtualProtect"), "PatchVtableSlot claims first")
check("PatchVtableSlot(slot,current,hook,Claim::kLayered,owner)" in body(patch, "bool ChainVtableSlot("),
      "ChainVtableSlot claims as layered for its caller")
check("PatchCode(site,sig,code,sizeof(code),Claim::kSole,owner)" in body(patch, "bool RedirectImportCall("),
      "RedirectImportCall's write is claimed for its caller")
for header, names in (("common/edf/memory.h", ("RedirectCall",)),
                      ("common/edf/patch.h", ("PatchVtableSlot", "ChainVtableSlot", "PatchCode", "RedirectImportCall"))):
    text = read(header)
    for name in names:
        decl = re.search(r"bool " + name + r"\(([^;]*)\)\s*noexcept;", text)
        check(bool(decl) and "const char* owner=__builtin_FILE()" in decl.group(1),
              f"{name}'s owner defaults to the calling file")

writes = owners = layered = 0
for top in ("src", "autoturret"):
    for dirpath, _, files in os.walk(os.path.join(ROOT, top)):
        for name in files:
            if not name.endswith((".cpp", ".h", ".inc")):
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), ROOT).replace("\\", "/")
            lines = read(rel).splitlines()
            text = "\n".join(lines)
            for i, line in enumerate(lines):
                if re.search(r"VirtualProtect\([^;]*PAGE_EXECUTE_READWRITE", line) and rel not in UNREGISTERED_OK:
                    writes += 1
                    before = "\n".join(lines[max(0, i - 3):i + 1])
                    check("edf::ClaimCode(" in before, f"{rel}:{i + 1}: a code write of its own claims its bytes first")
            if "__builtin_FILE" in text or re.search(r"(RedirectCall|PatchCode|RedirectImportCall|PatchVtableSlot|"
                                                     r"ChainVtableSlot)\([^;]*\"[^\"]*\.(cpp|h)\"", text):
                owners += 1
                check(False, f"{rel}: names an owner itself (the registry's owner is the calling file)")
            if "Claim::kLayered" in text:
                layered += 1
                check(rel in LAYERED_OK, f"{rel}: chains over another's patch (Claim::kLayered) only where listed")
check(writes >= 6, f"the hand-written code writes found ({writes})")
check(layered == len(LAYERED_OK), "perf_probe.cpp's Chain is layered")

for rel in ("src/plugin.cpp", "autoturret/src/plugin.cpp"):
    text = read(rel)
    sink = text.find("edf::SetClaimConflictSink(&LogClaimConflict)")
    first = min(i for i in (text.find("InstallCrashDump();", sink), text.find("HookFlak();", sink)) if i > 0)
    check(0 < sink < first, f"{rel}: conflicts go to the log from before the first patch")
    check("edf::GetClaimStats()" in text and "PATCH owners:" in text, f"{rel}: the totals logged once loaded")

print(f"redirect_owner_guard: {len(failures)} failed")
sys.exit(1 if failures else 0)
