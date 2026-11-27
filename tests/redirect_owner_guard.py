"""Every code write goes through the code owner registry, and the call sites named by constant keep one owner.

Two ways, side by side. The static scan (owners() below, kept from before the registry) reads the k* constants in the
first argument of RedirectCall / PatchCall / RedirectImportCall across both plugins and common/ and fails when one RVA
is redirected from two files: it sees across the two DLLs (each has its own registry) and fails CI, where the registry
only logs. It also keeps 0x1198FA4 (the render recording) with src/render_record.cpp alone, frame_rate_view.cpp and
firstperson.cpp registering their turns there. It misses literal addresses (image+0x...), loops over tables, PatchCode
writing an E8 and hand-written writes; the registry (common/code_owner.cpp, edf/code_owner.h) covers those at run
time: each patch primitive (edf::RedirectCall, PatchCode,
RedirectImportCall, PatchVtableSlot, ChainVtableSlot) claims its bytes for the calling file (__builtin_FILE() as a
default argument) before it looks at them, and a second file's claim is logged ("PATCH conflict: ..."; the first
person's culling was left out silently when the frame rate redirected 0x1198FA4 first, #395).
tests/code_owner_test.cpp covers the registry.

This guard keeps the registry seeing every write:
  - the primitives claim before they read or write, the import call and the vtable chain passing the caller on;
  - every hand-written write in the plugins (src/, autoturret/: a VirtualProtect making memory writable, PAGE_READWRITE
    or PAGE_EXECUTE_READWRITE, and a pointer swap, InterlockedCompareExchangePointer / InterlockedExchangePointer)
    claims its bytes with edf::ClaimCode within the few lines before;
  - no plugin file names an owner itself (__builtin_FILE / a file name passed), and only the files listed here chain
    (Claim::kLayered) over another's patch;
  - both plugins send conflicts to their log before the first patch and log the totals once loaded.
"""
import collections
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

WRITABLE = re.compile(r"VirtualProtect\([^;]*PAGE_(EXECUTE_)?READWRITE")
SWAP = re.compile(r"Interlocked(CompareExchange|Exchange)Pointer\(")
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
                if (WRITABLE.search(line) or SWAP.search(line)) and rel not in UNREGISTERED_OK:
                    writes += 1
                    before = "\n".join(lines[max(0, i - 4):i + 1])
                    check("edf::ClaimCode(" in before, f"{rel}:{i + 1}: a code write of its own claims its bytes first")
            if "__builtin_FILE" in text or re.search(r"(RedirectCall|PatchCode|RedirectImportCall|PatchVtableSlot|"
                                                     r"ChainVtableSlot)\([^;]*\"[^\"]*\.(cpp|h)\"", text):
                owners += 1
                check(False, f"{rel}: names an owner itself (the registry's owner is the calling file)")
            if "Claim::kLayered" in text:
                layered += 1
                check(rel in LAYERED_OK, f"{rel}: chains over another's patch (Claim::kLayered) only where listed")
check(writes >= 8, f"the hand-written code writes found ({writes})")
check(layered == len(LAYERED_OK), "perf_probe.cpp's Chain is layered")

for rel in ("src/plugin.cpp", "autoturret/src/plugin.cpp"):
    text = read(rel)
    sink = text.find("edf::SetClaimConflictSink(&LogClaimConflict)")
    # the first patch: the module registry's install pass (its first entry the crash dump's), the turret's own list
    first = min(i for i in (text.find("modules::InstallAll(", sink), text.find("InstallCrashDump();", sink),
                            text.find("HookFlak();", sink)) if i > 0)
    check(0 < sink < first, f"{rel}: conflicts go to the log from before the first patch")
    check("edf::GetClaimStats()" in text and "PATCH owners:" in text, f"{rel}: the totals logged once loaded")

table = read("src/module_table.h")
table = table[table.index("constexpr InstallEntry kInstalls[]={"):]
entries = [line.lstrip() for line in table.split("\n")[1:] if line.lstrip().startswith("{")]
check(entries[0].startswith("{IP::kCrashDump,Id::kCrashDump,"),
      "src/module_table.h: the crash dump the load's first install (a crash in any patch after it is dumped)")

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


def call_owners():
    """RVA -> the files redirecting it, from the k* constants in the calls' first argument (scalars or arrays)."""
    sites = collections.defaultdict(set)
    for top in ("src", "autoturret", "common"):
        for dirpath, _, files in os.walk(os.path.join(ROOT, top)):
            for name in files:
                if not name.endswith((".cpp", ".h", ".inc")):
                    continue
                rel = os.path.relpath(os.path.join(dirpath, name), ROOT).replace("\\", "/")
                text = strip(read(rel))
                if not any(c in text for c in CALLS):
                    continue
                values = collections.defaultdict(list)
                for n, v in re.findall(r"\b(k\w+)\s*=\s*(0x[0-9A-Fa-f]+)\b", text):
                    values[n].append(int(v, 16))
                for n, table in re.findall(r"\b(k\w+)\s*\[\s*\w*\s*\]\s*=\s*\{([^}]*)\}", text):
                    values[n].extend(int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", table))
                for call in CALLS:
                    for m in re.finditer(r"(?<![\w:])(?:edf::)?" + re.escape(call), text):
                        for ident in re.findall(r"\bk\w+", first_arg(text, m.end())):
                            for v in values.get(ident, ()):
                                sites[v].add(rel)
    return sites


sites = call_owners()
shared = {rva: files for rva, files in sites.items() if len(files) > 1}
for rva, files in sorted(shared.items()):
    check(False, f"0x{rva:X} is redirected from {len(files)} files: {', '.join(sorted(files))}")
check(not shared, f"each of the {len(sites)} call sites named by constant is redirected from one file")
check(sites.get(0x1198FA4) == {"src/render_record.cpp"},
      f"0x1198FA4 (the render recording) is redirected by src/render_record.cpp alone ({sorted(sites.get(0x1198FA4, ()))})")
for rel, slot in (("src/frame_rate_view.cpp", "kFrameRate"), ("src/firstperson.cpp", "kFirstPerson")):
    text = strip(read(rel))
    check("renderrecord::Install()" in text and
          re.search(r"renderrecord::Set(Project)?\(renderrecord::Slot::" + slot + ",", text) is not None,
          f"{rel} installs render_record and registers its {slot} turn")
    check("SetBeforeProject" not in text,
          f"{rel} takes no projection turn of its own (render_record.cpp shares 0x1197B50 with the lens view)")
    check(not re.search(r"0x1198FA4|0x11978D0", text, re.I),
          f"{rel} does not name the render recording's call itself (render_record.cpp owns it)")

print(f"redirect_owner_guard: {len(failures)} failed")
sys.exit(1 if failures else 0)
