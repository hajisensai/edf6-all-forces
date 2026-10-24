"""Read-only Deiroi accepted-action/native-network contract audit.

Pass --dll PATH (or PATH directly). No DLL means CTest skip code 77.
The file is parsed as a PE image; it is never loaded or executed.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import struct
import sys

SKIP = 77
CHECKS = 0


def check(condition: bool, message: str) -> None:
    global CHECKS
    CHECKS += 1
    if not condition:
        raise AssertionError(message)


def arguments() -> Path | None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path)
    parser.add_argument("--dll", type=Path)
    args = parser.parse_args()
    if args.path and args.dll and args.path.resolve() != args.dll.resolve():
        parser.error("positional DLL and --dll must name the same file")
    candidate = args.dll or args.path
    if candidate is None:
        configured = os.environ.get("EDF6_NATIVE_DLL", "")
        candidate = Path(configured) if configured else None
    return candidate


def run(path: Path) -> None:
    import capstone
    import capstone.x86 as x86
    import pefile

    pe = pefile.PE(str(path), fast_load=True)
    check(pe.FILE_HEADER.Machine == 0x8664, "supported x64 PE machine")
    check(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, "supported Steam EDF.dll timestamp")
    image = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    def instruction(rva: int):
        decoded = list(md.disasm(image[rva:rva + 16], rva, count=1))
        check(len(decoded) == 1, f"instruction exists at {rva:#x}")
        return decoded[0]

    def expect(rva: int, mnemonic: str, operands: str) -> None:
        ins = instruction(rva)
        check((ins.mnemonic, ins.op_str) == (mnemonic, operands),
              f"{rva:#x}: expected {mnemonic} {operands}, got {ins.mnemonic} {ins.op_str}")

    def branch(rva: int, target: int, mnemonic: str = "call") -> None:
        ins = instruction(rva)
        check(ins.mnemonic == mnemonic and len(ins.operands) == 1
              and ins.operands[0].type == x86.X86_OP_IMM
              and ins.operands[0].imm == target,
              f"{rva:#x}: expected {mnemonic} {target:#x}, got {ins.mnemonic} {ins.op_str}")

    def rip_target(rva: int, target: int) -> None:
        ins = instruction(rva)
        check(ins.mnemonic == "lea" and len(ins.operands) == 2
              and ins.operands[1].type == x86.X86_OP_MEM
              and ins.operands[1].mem.base == x86.X86_REG_RIP
              and ins.address + ins.size + ins.operands[1].mem.disp == target,
              f"{rva:#x}: expected state predicate address {target:#x}")

    def instructions(begin: int, end: int):
        decoded = list(md.disasm(image[begin:end], begin))
        check(bool(decoded) and decoded[-1].address + decoded[-1].size == end,
              f"complete native instruction range {begin:#x}..{end:#x}")
        return decoded

    module = Path(__file__).resolve().parents[1] / "src" / "enemy_control_deiroi_net.cpp"
    text = module.read_text(encoding="utf-8-sig")
    signatures = re.search(r"constexpr Signature signatures\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    check(signatures is not None, "module signature table is readable")
    records = re.findall(r"\{\s*(0x[\dA-Fa-f]+)\s*,\s*\{([^}]+)\}\s*\}", signatures.group(1))
    seen = set()
    for address, literals in records:
        rva = int(address, 16)
        expected = bytes(int(item, 0) for item in re.findall(r"0x[\dA-Fa-f]+|\d+", literals))
        check(len(expected) == 16, f"module preflight fingerprint length at {rva:#x}")
        check(rva not in seen, f"unique module fingerprint at {rva:#x}")
        check(image[rva:rva + 16] == expected, f"actual native bytes match module fingerprint at {rva:#x}")
        seen.add(rva)
    check({0x6D05C0, 0x32B790, 0x6CF6C0}.issubset(seen), "real-shot sender and stock replay fingerprints are present")

    def unsigned_table(name: str) -> list[int]:
        found = re.search(rf"constexpr unsigned {name}\[\]\s*=\s*\{{([^}}]+)\}}", text)
        check(found is not None, f"module {name} table is readable")
        return [int(value, 0) for value in re.findall(r"0x[\dA-Fa-f]+|\d+", found.group(1))]

    sites, targets = unsigned_table("sites"), unsigned_table("targets")
    expected_pairs = [
        (0x32BD98, 0x6CF6C0),
        (0x32BB2C, 0x32C180), (0x32BE9D, 0x32C180),
        (0x32CFF8, 0x32B460), (0x32C6D8, 0x32B790),
        (0x32C7A4, 0x32B790), (0x32C9C0, 0x32B790),
        (0x32D164, 0x32B790),
        (0x32C6E2, 0x32BA80), (0x32C7B2, 0x32BA80),
        (0x32C9D3, 0x32BA80), (0x32D172, 0x32BA80),
        (0x32C821, 0x32C310), (0x32D084, 0x32C310), (0x32C736, 0x32C310),
        (0x31F148, 0x3211C0), (0x31CD67, 0x3211C0), (0x31F051, 0x7748F0),
    ]
    check(len(sites) == len(targets) == 18, "all eighteen private lifecycle redirects have source and target")
    check(list(zip(sites, targets)) == expected_pairs, "redirect contract covers real emission and every stock action boundary")
    hooks = re.search(r"void\* const hooks\[\]\s*=\s*\{(.*?)\};", text, re.S)
    check(hooks is not None, "module hook table is readable")
    names = re.findall(r"reinterpret_cast<void\*>\(&([A-Za-z_]\w*)\)", hooks.group(1))
    check(names == ["StockShot", "StockState", "StockState", "StockSend",
                    "Close", "Charge", "Fire", "Idle", "Tick", "Tick", "Tick", "Tick",
                    "Transition", "Transition", "Transition", "MainTransition", "MainTransition", "MainNetwork"],
          "redirect callback order matches the lifecycle phases")
    tail_sites = {0x32C6D8, 0x32C7A4, 0x32D164, 0x31CD67}
    for site, target in expected_pairs:
        opcode = 0xE9 if site in tail_sites else 0xE8
        check(image[site] == opcode, f"{site:#x}: exact original E8/E9 redirect opcode")
        check(site + 5 + struct.unpack_from("<i", image, site + 1)[0] == target,
              f"{site:#x}: exact rel32 target")
        branch(site, target, "jmp" if site in tail_sites else "call")

    relay_opcodes = re.search(r"constexpr unsigned char opcodes\[\]\s*=\s*\{([^}]+)\}", text)
    check(relay_opcodes is not None, "private relay readiness pins original opcode type")
    values = [int(value, 0) for value in re.findall(r"0x[\dA-Fa-f]+|\d+", relay_opcodes.group(1))]
    check(values == [0xE9 if site in tail_sites else 0xE8 for site, _ in expected_pairs],
          "private late-hook checks preserve every original call/tail-jump opcode")
    factory_module = module.with_name("enemy_control_gun_factory.cpp")
    factory_text = factory_module.read_text(encoding="utf-8-sig")
    factory_signatures = re.search(r"constexpr Sig signatures\[\]\s*=\s*\{(.*?)\n\};", factory_text, re.S)
    check(factory_signatures is not None, "shared native factory signature table is readable")
    factory_seen = set()
    for address, literals in re.findall(r"\{\s*(0x[\dA-Fa-f]+)\s*,\s*\{([^}]+)\}\s*\}", factory_signatures.group(1)):
        rva = int(address, 16)
        expected = bytes(int(item, 0) for item in re.findall(r"0x[\dA-Fa-f]+|\d+", literals))
        check(len(expected) == 16 and image[rva:rva + 16] == expected,
              f"shared factory preflight matches actual native bytes at {rva:#x}")
        factory_seen.add(rva)
    check({0x6D05C0, 0x1194280}.issubset(factory_seen), "shared dispatcher validates both native factory entrypoints")
    factory_compact = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", factory_text))
    source_compact = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", text))
    check("kGunSite=0x6CFE2A,kGun=0x6D05C0,kUnit=0x1194280" in factory_compact,
          "shared dispatchers retain the actual native gun and RawObject factory targets")
    check("RegisterNativeGunFactoryProvider(gunProvider)" in source_compact
          and "InstallNativeGunFactoryDispatcher()" in source_compact,
          "Deiroi uses the shared gun provider instead of competing for its callsite")
    check("RegisterNativeUnitFactoryProvider(unitProvider)" in source_compact
          and "RegisterNativeUnitFactoryCallsite(0x84C5A)" in source_compact
          and "InstallNativeUnitFactoryDispatcher()" in source_compact,
          "Deiroi observes the inner actual RawObject return through the shared unit provider")
    check("NativeGunFactoryProviderReady(gunProvider)" in source_compact
          and "NativeUnitFactoryProviderReady(unitProvider)" in source_compact
          and "NativeUnitFactoryCallsiteReady(0x84C5A)" in source_compact,
          "late overwritten factory relays cannot be treated as an active accepted-action path")
    check("constNativeGunFactoryProvidergunProvider{0x31524744,&OwnsGun,&Shot,&FaultGun};" in source_compact
          and "constNativeUnitFactoryProviderunitProvider{0x31554744,&OwnsUnit,&InvokeUnit,&FaultUnit};" in source_compact,
          "provider tags and typed gun/RawObject callbacks remain distinct")
    branch(0x6CFE2A, 0x6D05C0)
    branch(0x84C5A, 0x1194280)

    # The four state1 poll hooks keep live guest state1 running. Narrow native
    # transition hooks prevent autonomous duplicate entry while preserving native
    # slot5 work, holder effects, elapsed state and dead/disabled cleanup.
    tick = re.search(r"bool __fastcall Tick\(void\* child\)\s*\{(.*?)\n\}", text, re.S)
    check(tick is not None, "module has the typed state1 poll wrapper")
    tick_body = re.sub(r"//[^\n]*", "", tick.group(1))
    compact_tick = re.sub(r"\s+", "", tick_body)
    check("auto*stream=Bound(child,&lease,&key,&bound);" in compact_tick,
          "state1 wrapper proves exact controlled part membership before suppression")
    check("if(bound&&stream&&Current(stream->root)&&Current(Reference(child))&&"
          "!static_cast<unsignedchar*>(child)[0x639]){" in compact_tick,
          "poll override requires a live retained root, live child and clear native disabled flag")
    check("if(!stream->authority)returnfalse;" in compact_tick,
          "live guests continue real native state1 effects and elapsed work")
    check("if(stream->quiescing||stream->faulted)returntrue;" in compact_tick,
          "quiescing or faulted authority freezes autonomous progression")
    check("returnFn<TickFn>(0x32BA80)(child);" in compact_tick,
          "unbound, normal authority and dead-child paths retain original state1 consumer")
    transition = re.search(r"void __fastcall Transition\([^\n]+\)\s*\{(.*?)\n\}", text, re.S)
    check(transition is not None, "three autonomous setters share a typed transition wrapper")
    transition_body = re.sub(r"//[^\n]*", "", transition.group(1))
    compact_transition = re.sub(r"\s+", "", transition_body)
    check("static_cast<unsignedchar*>(machine)-kMachine" in compact_transition,
          "transition recovers only the child owning this exact native machine")
    check("Bound(child,&lease,&key,&bound)" in compact_transition
          and "Current(stream->root)" in compact_transition
          and "Current(Reference(child))" in compact_transition,
          "transition filtering requires retained ownership and live native identities")
    check("!child[0x639]&&" in compact_transition,
          "fire disabled cleanup sharing 32D084 bypasses the autonomous transition filter")
    current = re.search(r"bool Current\([^\n]+\)\s*(?:noexcept\s*)?\{(.*?)\n\}", text, re.S)
    check(current is not None and
          "(!alive||(!(ref.object[0x18]&4)&&!ref.object[0x2E8]))"
          in re.sub(r"\s+", "", current.group(1)),
          "live predicate retains the real native dead flags before overriding a cleanup path")
    check("(!stream->authority||stream->quiescing||stream->faulted))return;" in compact_transition,
          "only accepted guests or inactive authority suppress autonomous state changes")
    check("Fn<NativeSetFn>(0x32C310)(machine,state,data);" in compact_transition,
          "unbound, normal authority and cleanup retain original native setter")
    for phase, poll, test, exit_branch, destination in [
            ("close", 0x32C6E2, 0x32C6E7, 0x32C6E9, 0x32C73B),
            ("charge", 0x32C7B2, 0x32C7B7, 0x32C7B9, 0x32C86B),
            ("fire", 0x32C9D3, 0x32C9D8, 0x32C9DA, 0x32D09C),
            ("idle", 0x32D172, 0x32D177, 0x32D179, 0x32D28B)]:
        branch(poll, 0x32BA80)
        expect(test, "test", "al, al")
        branch(exit_branch, destination, "jne")
        check(poll not in tail_sites, f"{phase}: state1 poll is a returning call, not a state-entry tail jump")

    # Native graph edges are independently identified by state descriptors.
    rip_target(0x32C7FB, 0x32C880)
    branch(0x32C821, 0x32C310)
    rip_target(0x32C710, 0x32D0C0)
    branch(0x32C736, 0x32C310)
    rip_target(0x32D05D, 0x32C6A0)
    rip_target(0x32D06D, 0x32D0C0)
    branch(0x32D084, 0x32C310)
    expect(0x32C826, "cmp", "byte ptr [rbx + 0x639], 0")
    branch(0x32C82D, 0x32C838, "jne")
    expect(0x32C82F, "cmp", "byte ptr [rbx + 0x2e8], 0")
    rip_target(0x32C840, 0x32D0C0)
    branch(0x32C866, 0x32C310)
    check(0x32C866 not in sites, "charge disabled/dead cleanup setter is deliberately unhooked")
    expect(0x32D02F, "cmp", "byte ptr [rsi + 0x639], al")
    branch(0x32D035, 0x32D03F, "jne")
    expect(0x32D037, "cmp", "byte ptr [rsi + 0x2e8], al")
    # Returning false from the guest poll retains these genuine native updates.
    expect(0x32C7BF, "mov", "rcx, qword ptr [rbx + 0xd10]")
    expect(0x32C7D9, "movss", "dword ptr [rcx + 0x10], xmm0")
    expect(0x32C7DE, "mov", "dword ptr [rcx + 0x14], eax")
    expect(0x6CFE3B, "lea", "rcx, [rsi + 0x160]")
    branch(0x6CFE42, 0x7A8A20)
    branch(0x6CFE56, 0x7A8C20)
    expect(0x32B27F, "call", "qword ptr [rax + 8]")
    expect(0x32B43E, "call", "qword ptr [rax + 0x10]")
    check(0x32B27F not in sites and 0x32B43E not in sites,
          "slot5 component ticks remain outside all action hooks")

    # Root main guns are native rows, not Battery objects. The same 6CFC60
    # emission loop reaches our sole 6CFE2A factory boundary for both families.
    expect(0x31F192, "mov", "rbp, qword ptr [rdi + 0x1638]")
    expect(0x31F199, "imul", "r15, qword ptr [rdi + 0x1648], 0x290")
    expect(0x31F22E, "add", "rbp, 0x290")
    expect(0x31F235, "add", "rbx, 0x290")
    expect(0x31F130, "mov", "rcx, qword ptr [rdi + 0x1638]")
    expect(0x31F137, "add", "rcx, 0xb0")
    rip_target(0x31F141, 0x31CB60)
    branch(0x31F148, 0x3211C0)
    expect(0x31CD4E, "lea", "rcx, [rdi + 0xb0]")
    rip_target(0x31CD58, 0x31CDB0)
    branch(0x31CD67, 0x3211C0, "jmp")
    expect(0x31CB93, "lea", "rcx, [rdi + 0x100]")
    branch(0x31CB9A, 0x6CFBC0)
    expect(0x31CC00, "mov", "rsi, qword ptr [rdi]")
    expect(0x31CD0A, "lea", "rcx, [rdi + 0x100]")
    branch(0x31CD29, 0x6CFC60)
    expect(0x31CD2E, "lea", "rcx, [rdi + 0x100]")
    branch(0x31CD35, 0x6D0210)
    expect(0x31CD71, "add", "rcx, 0x100")
    branch(0x31CD78, 0x6CFC00)
    expect(0x31CDB0, "ret", "0")
    expect(0x3211DB, "mov", "rsi, r8")
    expect(0x3211DE, "mov", "rbp, rdx")
    expect(0x3211FE, "call", "qword ptr [rax + 8]")
    expect(0x32121D, "mov", "qword ptr [rdi + 0x18], rbp")
    expect(0x321235, "call", "qword ptr [rax + 8]")

    # False at this network-active call bypasses the old type-5 packet and
    # D04/D05 writes in 31F05E..31F12E, while retaining the real main begin.
    # Earlier native acknowledgement copies in the same root tick are separate.
    expect(0x31F04A, "lea", "rcx, [rdi + 0x120]")
    branch(0x31F051, 0x7748F0)
    expect(0x31F056, "test", "al, al")
    branch(0x31F058, 0x31F130, "je")
    expect(0x31F05E, "lea", "rcx, [rdi + 0x1d05]")
    expect(0x31F065, "lea", "rdx, [rdi + 0x1d04]")
    expect(0x31F099, "inc", "byte ptr [rdi + 0x1d05]")
    expect(0x31F0A6, "mov", "byte ptr [rdi + 0x1d04], al")
    expect(0x31F0BF, "mov", "dl, 5")
    branch(0x31F0C9, 0x12B5790)
    expect(0x31F0FB, "call", "qword ptr [rax + 0x70]")
    expect(0x31F12B, "movzx", "eax, byte ptr [rcx]")
    expect(0x31F12E, "mov", "byte ptr [rdx], al")

    main_transition = re.search(r"void __fastcall MainTransition\([^\n]+\)\s*\{(.*?)\n\}", text, re.S)
    check(main_transition is not None, "root-main transition wrapper preserves the direct callback ABI")
    compact_main = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", main_transition.group(1)))
    check("auto*holder=static_cast<unsignedchar*>(machine)+0x50;" in compact_main
          and "BoundHolder(holder,&lease,&key,&bound,&actor)" in compact_main,
          "main machine+B0 maps to the exact trusted row+100 holder")
    check("if(!stream||(key>>16)!=32)return;" in compact_main,
          "main lifecycle cannot address a foot-part key")
    check("if(Current(stream->root)&&(!stream->authority||stream->quiescing||stream->faulted))return;" in compact_main,
          "only a live retained guest or inactive authority suppresses autonomous main state")
    check("callback==image+0x31CB60?6u:callback==image+0x31CDB0?7u:0u" in compact_main,
          "native root callbacks map exactly to MainFire and MainIdle")
    main_set = compact_main.rfind("Fn<MainSetFn>(0x3211C0)(machine,callback,data);")
    main_push = compact_main.find("Push(*stream,event);")
    check(0 <= main_set < main_push and
          "if(!stream->authority||stream->offline||!Current(stream->root)||stream->faulted)return;" in compact_main,
          "authority queues only after the original main state setter; dead cleanup remains native")
    main_network = re.search(r"bool __fastcall MainNetwork\([^\n]+\)\s*\{(.*?)\n\}", text, re.S)
    check(main_network is not None, "root-main network-active wrapper is present")
    compact_network = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", main_network.group(1)))
    check("static_cast<unsignedchar*>(netObject)-0x120" in compact_network
          and "if(NativeActionRole(root,&authority))returnfalse;" in compact_network,
          "only the authenticated controlled root bypasses old native main packets")
    check("if(stream.root.object==root&&Current(stream.root,false))returnfalse;" in compact_network,
          "retained restoration also suppresses late native main packets")
    check("returnFn<NetActiveFn>(0x7748F0)(netObject);" in compact_network,
          "unrelated network objects retain the original active test")
    compact_source = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", text))
    check("ReadDeiroiMainGunCount(lease,&mainCount)||mainCount>64" in compact_source
          and "constunsignedkey=(32u<<16)|index;" in compact_source,
          "main context bounds and group-32 identity are independently validated")
    check("ResolveDeiroiControlledHolderKey(*lease,*key,actor,&actualHolder)||actualHolder!=holder" in compact_source,
          "shared real-shot capture requires exact trusted holder identity")

    bridge_text = module.with_name("enemy_control_deiroi.cpp").read_text(encoding="utf-8-sig")
    bridge_compact = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", bridge_text))
    check("kMaxMainGuns=64,kMainGroup=32,kMainStride=0x290" in bridge_compact,
          "trusted main-row bridge uses the audited ordinal range and native stride")
    main_rows = re.search(r"bool MainRows\([^\n]+\)\s*(?:noexcept\s*)?\{(.*?)\n\}", bridge_text, re.S)
    check(main_rows is not None, "trusted root row-table reader is auditable")
    rows_compact = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", main_rows.group(1)))
    check("At<unsignedchar*>(owner.object,0x1638)" in rows_compact
          and "At<std::size_t>(owner.object,0x1648)" in rows_compact
          and "At<std::size_t>(owner.object,0x1640)" in rows_compact,
          "trusted row-table reader uses the actual native data/size/capacity fields")
    check("size>kMaxMainGuns||size>capacity||(size&&!Readable(table,size*kMainStride,writable))" in rows_compact,
          "trusted rows are bounded, inside capacity and fully readable")
    check("constauto*row=table+static_cast<std::size_t>(i)*kMainStride;" in rows_compact and "At<constvoid*>(row,0)!=owner.object" in rows_compact,
          "each plain row proves its owning retained root instead of inventing a child identity")
    check("*actor=root->ref.object;*holder=row+0x100;returntrue;" in bridge_compact,
          "main-row bridge returns the real root actor and exact native holder")
    check("reinterpret_cast<MainSetFn>(image+kMainSet)(row+0xB0,image+callback,nullptr);" in bridge_compact,
          "main state bridge passes a direct native callback rather than a foot member descriptor")
    check("phase>7||!acceptedSequence||(phase<2&&!seed)||!point||((phase>=6)!=((key>>16)==kMainGroup))" in bridge_compact,
          "accepted source bridge enforces the same foot/main phase domains as wire preflight")

    # 6D05C0 receives holder, complete world matrix and mutable seed. Its missing
    # owner path jumps past the only holder+34 increment. This is an ordinal,
    # not a real-object commit marker: a missing downstream factory can still
    # reach it. Real-object success requires observing the later dispatcher.
    expect(0x6D05F0, "mov", "rsi, r8")
    expect(0x6D05F3, "mov", "r15, rdx")
    expect(0x6D05F6, "mov", "rdi, rcx")
    expect(0x6D0638, "cmp", "qword ptr [rsp + 0x30], 0")
    branch(0x6D063E, 0x6D0942, "je")
    # Native first-shot FX/initialization is conditional on the actual source
    # holder+34 ordinal. Accepted replay must not manufacture a fresh burst.
    expect(0x6D0644, "cmp", "byte ptr [rdi + 0xd0], 0")
    branch(0x6D064B, 0x6D0653, "je")
    expect(0x6D064D, "cmp", "dword ptr [rdi + 0x34], 0")
    branch(0x6D0651, 0x6D066A, "jne")
    expect(0x6D0653, "cmp", "qword ptr [rdi + 0x70], 0")
    branch(0x6D0658, 0x6D066A, "je")
    expect(0x6D065A, "lea", "rcx, [rdi + 0x50]")
    expect(0x6D065E, "add", "rdx, 0x30")
    expect(0x6D0662, "xor", "r8d, r8d")
    branch(0x6D0665, 0x7B4510)
    compact_module = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", text))
    check("event.shotIndex=At<std::uint32_t>(holder,0x34);" in compact_module
          and "after!=before+1u||event.shotIndex!=before" in compact_module,
          "authority captures the pre-factory native shot index and verifies its single ordinal advance")
    shot_source = re.search(r"void __fastcall Shot\([^\n]+\)\s*\{(.*?)\n\}", text, re.S)
    check(shot_source is not None, "source capture has an auditable actual creation boundary")
    compact_shot = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", shot_source.group(1)))
    check("scope.captured=&event;" in compact_shot
          and "stream->count+stream->reserved>=kQueued" in compact_shot
          and "if(!stream->offline)++stream->reserved;" in compact_shot,
          "source reserves outbox capacity before any effectful native creation")
    check("if(scope.created&&(scope.calls!=1||after!=before+1u||event.shotIndex!=before||(!stream->offline&&!scope.queued)))stream->faulted=true;" in compact_shot,
          "source validates the native ordinal without discarding an already queued real creation")
    check("event.kind=FactoryKind(holder)==2?deiroi_wire::Kind::entity:deiroi_wire::Kind::shot;" in compact_shot,
          "the admitted native emitter type selects the corresponding wire schema")
    emitter = re.search(r"bool Emit\([^\n]+\)\s*(?:noexcept\s*)?\{(.*?)\n\}", text, re.S)
    check(emitter is not None, "accepted replay has an auditable native factory boundary")
    compact_emitter = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", emitter.group(1)))
    replay_steps = ["Put<std::uint32_t>(holder,0x34,event.shotIndex);",
                    "Fn<ShotFn>(0x6D05C0)(holder,matrix,&seed);",
                    "committed=scope.created&&scope.calls==1&&At<std::uint32_t>(holder,0x34)==event.shotIndex+1u;"]
    positions = [compact_emitter.find(step) for step in replay_steps]
    check(all(position >= 0 for position in positions) and positions == sorted(positions),
          "replay installs the source shot ordinal before the native factory and checks its native successor")
    check("__finally" in compact_emitter
          and "Put<std::uint32_t>(holder,0x34," not in compact_emitter.split("__finally", 1)[1],
          "replay retains the advanced native shot index instead of restoring a stale ordinal")
    expect(0x6D0935, "call", "qword ptr [rax + 8]")
    expect(0x6D0938, "inc", "dword ptr [r14 + 0x1a4]")
    expect(0x6D093F, "inc", "dword ptr [rdi + 0x34]")
    body = instructions(0x6D05C0, 0x6D09AA)
    ordinals = [ins.address for ins in body if ins.mnemonic == "inc" and ins.op_str == "dword ptr [rdi + 0x34]"]
    check(ordinals == [0x6D093F], "one native holder shot-ordinal increment per factory dispatch attempt")
    expect(0x6CFE20, "lea", "r8, [rbp + 0x30]")
    expect(0x6CFE24, "mov", "rdx, r13")
    expect(0x6CFE27, "mov", "rcx, rsi")
    branch(0x6CFE2A, 0x6D05C0)
    expect(0x6CFE2F, "inc", "rbx")
    expect(0x6CFE32, "movsxd", "rax, dword ptr [rsi + 0x44]")
    branch(0x6CFE39, 0x6CFE20, "jb")
    # Prove the admitted Bullet_TypeBullet -> adapter -> actual object path.
    # The outer adapter returns an out-weak address even on failure. Only the
    # inner 1194280 result is a registered, not-marked-for-delete RawObject.
    native_base = pe.OPTIONAL_HEADER.ImageBase
    check(struct.unpack_from("<Q", image, 0x17E6EE8)[0] - native_base == 0x6CF7D0,
          "admitted Bullet_TypeBullet Fire virtual slot has the audited implementation")
    check(struct.unpack_from("<Q", image, 0x17E6EF0)[0] - native_base == 0x6CFEF0,
          "admitted InitParam getter virtual slot has the audited implementation")
    expect(0x6CFEF0, "lea", "rax, [rcx + 0xa0]")
    # Getter returns InitParam at parameter-object+A0. Its real team/speed
    # and per-unit random/counter writes are separate from compatibility words
    # at the parameter object's own +90/+94.
    expect(0x6D066A, "mov", "rcx, qword ptr [rdi + 0x20]")
    expect(0x6D066E, "mov", "rax, qword ptr [rcx]")
    expect(0x6D0671, "call", "qword ptr [rax + 0x10]")
    expect(0x6D0674, "mov", "r14, rax")
    expect(0x6D067C, "mov", "edx, dword ptr [rcx + 0x314]")
    expect(0x6D0682, "mov", "dword ptr [rax + 0x90], edx")
    expect(0x6D06BE, "mov", "dword ptr [r14 + 0x1a0], eax")
    expect(0x6D084E, "movss", "xmm7, dword ptr [rax + 0x94]")
    expect(0x6D08A6, "movss", "dword ptr [rax + 0x94], xmm6")
    expect(0x6D08EB, "movss", "dword ptr [rax + 0x94], xmm7")
    expect(0x6D0938, "inc", "dword ptr [r14 + 0x1a4]")
    expect(0x6CF7EA, "lea", "rax, [rcx + 0xa0]")
    expect(0x6CF7F1, "mov", "qword ptr [rsp + 0x20], rax")
    expect(0x6CF7F6, "mov", "r9, qword ptr [rcx + 0x28]")
    branch(0x6CF806, 0x84C40)
    expect(0x84C46, "mov", "rax, r9")
    expect(0x84C49, "mov", "r10, r8")
    expect(0x84C4C, "mov", "r9, qword ptr [rsp + 0x50]")
    expect(0x84C54, "mov", "r8, rax")
    expect(0x84C57, "mov", "rdx, r10")
    branch(0x84C5A, 0x1194280)
    expect(0x84C64, "test", "rax, rax")
    branch(0x84C67, 0x84C79, "jne")
    expect(0x84C69, "mov", "qword ptr [rsi], rdx")
    expect(0x84C6C, "mov", "rax, rsi")
    expect(0x84C6F, "mov", "qword ptr [rsi + 8], rdx")
    expect(0x84CEC, "mov", "rax, rsi")
    expect(0x119428B, "mov", "rsi, r9")
    expect(0x119429E, "xor", "edi, edi")
    expect(0x11942B7, "mov", "rax, qword ptr [r8]")
    expect(0x11942C0, "call", "qword ptr [rax + 0x10]")
    expect(0x11942C9, "mov", "rcx, qword ptr [rsp + 0x20]")
    expect(0x11942DE, "mov", "qword ptr [rcx + 0x20], rax")
    expect(0x1194342, "mov", "qword ptr [rdx + 0x10], rcx")
    expect(0x119434F, "inc", "qword ptr [r14 + 8]")
    expect(0x11943B6, "test", "byte ptr [rcx + 0x18], 4")
    expect(0x11943BA, "cmove", "rdi, rcx")
    expect(0x11943F4, "mov", "rax, rdi")
    factory_path = re.search(r"unsigned FactoryKind\([^\n]+\)\s*(?:noexcept\s*)?\{(.*?)\n\}", text, re.S)
    check(factory_path is not None, "factory path admission is explicit")
    path_compact = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", factory_path.group(1)))
    check("table[1]==image+0x6CF7D0&&table[2]==image+0x6CFEF0" in path_compact
          and "table[1]==image+0x6CF910&&table[2]==image+0x6CFF00&&table[3]==image+0x6D0470&&Readable(parameter,0x4E0)" in path_compact,
          "only both proved native bullet/entity virtual layouts enter actual-result observation")
    check("boolOwnsUnit(constvoid*factory,constvoid*descriptor)noexcept{"
          "returnsample&&sample->factory==factory&&sample->descriptor==descriptor;}" in compact_module,
          "RawObject observation requires the exact accepted factory and actual InitParam identity")
    invocation = re.search(r"void\* InvokeUnit\([^\n]+\)\s*\{(.*?)\n\}", text, re.S)
    check(invocation is not None, "RawObject observer invokes the original factory")
    invoke_compact = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", invocation.group(1)))
    created_gate = invoke_compact.find("if(sample->created&&sample->captured&&!stream->offline){")
    queue_at = invoke_compact.find("sample->queued=Push(*stream,event,true);", created_gate)
    factory_at = invoke_compact.find("result=InvokeOriginalNativeUnitFactory(manager,matrix,factory,input);")
    check(0 <= factory_at < created_gate < queue_at,
          "actual RawObject return commits its reserved outbox entry before post-create native callbacks")
    snapshot = invoke_compact[created_gate:queue_at]
    check("std::memcpy(event.actual,sample->actual,sizeof(event.actual));" in snapshot
          and "event.entityTeam=sample->entityTeam;" in snapshot
          and "event.lockOrdinal=sample->lockOrdinal;" in snapshot
          and "std::memcpy(event.targetActorId,sample->targetActorId,32);" in snapshot,
          "the committed creation includes every actual and entity target word")
    check("if(!OwnsUnit(factory,descriptor))returnInvokeOriginalNativeUnitFactory(manager,matrix,factory,descriptor);" in invoke_compact
          and "if(sample->calls++){Fault(sample->stream);returnnullptr;}" in invoke_compact
          and "sample->created=result!=nullptr;" in invoke_compact,
          "outside-scope calls remain native; one actual non-null inner result is required")
    check("SampleScopescope=Scope(&stream,holder,&event,target);SampleScope*prior=sample;sample=&scope;" in compact_emitter
          and "__finally{sample=prior;}" in compact_emitter,
          "guest ACK scopes its received actual InitParam words and restores nesting state")
    offsets = re.search(r"constexpr unsigned actualOffsets\[\]\s*=\s*\{([^}]+)\}", text)
    check(offsets is not None and [int(value, 0) for value in re.findall(r"0x[\dA-Fa-f]+|\d+", offsets.group(1))]
          == [0x90, 0x94, 0x98, 0x9C, 0x1A0, 0x1A4],
          "actual wire words are the six proved InitParam offsets, not compatibility words")
    scope = re.search(r"SampleScope Scope\([^\n]+\)\s*(?:noexcept\s*)?\{(.*?)\n\}", text, re.S)
    check(scope is not None, "typed unit scope construction is auditable")
    scope_compact = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", scope.group(1)))
    check("At<unsignedchar*>(holder,0x20)" in scope_compact
          and "scope.factory=At<void*>(parameter,0x28);" in scope_compact
          and "scope.entity=FactoryKind(holder)==2;" in scope_compact
          and "scope.descriptor=parameter+(scope.entity?0x280:0xA0);scope.expected=expected;scope.target=target;" in scope_compact,
          "scope selects the exact native +A0 bullet or +280 entity descriptor and preserves target pin")
    check("SampleScopescope=Scope(stream,holder);" in compact_module,
          "authority samples real actual InitParam immediately at the actual unit boundary")
    check("auto*actual=static_cast<unsignedchar*>(descriptor)+(sample->entity?0x80:0);" in invoke_compact
          and "if(!Readable(actual,0x1A8,true))" in invoke_compact
          and "prior[i]=At<std::uint32_t>(actual,actualOffsets[i]);" in invoke_compact,
          "all actual source words are readable and guest temporary writes have saved native values")
    unit_steps = ["if(sample->expected)for(unsignedi=0;i<6;++i)Put<std::uint32_t>(actual,actualOffsets[i],sample->expected->actual[i]);",
                  "for(unsignedi=0;i<6;++i)sample->actual[i]=At<std::uint32_t>(actual,actualOffsets[i]);",
                  "result=InvokeOriginalNativeUnitFactory(manager,matrix,factory,input);"]
    unit_positions = [invoke_compact.find(step) for step in unit_steps]
    check(all(position >= 0 for position in unit_positions) and unit_positions == sorted(unit_positions),
          "guest actual words are installed and authority words observed before the same native RawObject factory")
    check("__finally{if(sample->expected&&!sample->entity)for(unsignedi=0;i<6;++i)"
          "Put<std::uint32_t>(actual,actualOffsets[i],prior[i]);}" in invoke_compact,
          "ordinary guest bullet words are restored; entity writes only modify the borrowed Raw clone")
    check("std::memcpy(rawDescriptor,descriptor,sizeof(rawDescriptors[0]));input=rawDescriptor;actual=rawDescriptor+0x80;" in invoke_compact,
          "entity replay passes a complete synchronous Raw clone with its embedded BulletIP")
    check("Put<void*>(input,0x50,target->object);Put<constvoid*>(input,0x58,target->control);" in invoke_compact
          and "Put<std::uint32_t>(input,0x38,sample->entityTeam);" in invoke_compact,
          "entity constructor receives the proved target lock weak and outer native team")
    check("CaptureNativeProjectileLock(static_cast<unsignedchar*>(descriptor)+0x50,sample->targetActorId,&sample->lockOrdinal)" in invoke_compact
          and "EnemyControlWeaponLockIdentity(stream->root.object,sample->targetActorId,&lock)" in invoke_compact,
          "native AI target and manual target are independently captured as full actor plus lock ordinal")
    check("std::memcpy(sample->targetActorId,sample->expected->targetActorId,32);sample->lockOrdinal=sample->expected->lockOrdinal;" in invoke_compact
          and "sample->expected?sample->expected->entityTeam:At<std::uint32_t>(descriptor,0x38)" in invoke_compact,
          "guest reconstructs the accepted entity target and outer team without reusing its old AI target")
    check("event.kind!=deiroi_wire::Kind::control&&(FactoryKind(*holder)==2)!=(event.kind==deiroi_wire::Kind::entity)" in compact_module,
          "entire-batch preflight proves wire kind matches the actual native emitter type")
    target_preflight = compact_module.find("if(events[i].kind==deiroi_wire::Kind::entity&&!Target(*stream,i,events[i].targetActorId,events[i].lockOrdinal))")
    apply_begin = compact_module.find("if(result==Result::applied)for(unsignedi=0;i<count;++i){")
    check(0 <= target_preflight < apply_begin
          and "accepted=Emit(*stream,holders[i],event,&stream->targets[i]);" in compact_module,
          "all entity targets are retained before the first effectful replay in the batch")


    check("sample&&sample->entity&&sample->parameter==nativeParameter&&sample->stream&&"
          "(sample->expected||(sample->manual&&!sample->stream->faulted&&!sample->stream->quiescing))" in compact_module,
          "entity target-source shim is limited to an exact replay or active manual emitter, preserving native AI targets")
    # Native subtype 1 is Bullet_TypeEntity: its Raw descriptor begins at
    # parameter+280 and embeds Bullet InitParam at +80 (getter parameter+300).
    expect(0x6CF49B, "mov", "edx, dword ptr [rcx + rax + 0x14]")
    expect(0x6CF4A7, "cmp", "edx, 1")
    branch(0x6CF4AA, 0x6CF6A2, "jne")
    rip_target(0x6CF4D7, 0x17E6F08)
    expect(0x6CF4DE, "mov", "qword ptr [rdi], rax")
    entity_virtuals = [struct.unpack_from("<Q", image, 0x17E6F08 + i * 8)[0] - native_base for i in range(4)]
    check(entity_virtuals == [0x6CF3D0, 0x6CF910, 0x6CFF00, 0x6D0470],
          "subtype-1 constructor installs the actual entity Fire/getter/registration virtuals")
    expect(0x6CFF00, "lea", "rax, [rcx + 0x300]")
    expect(0x6D0470, "mov", "al, 1")
    expect(0x6CF4E1, "lea", "rcx, [rdi + 0x280]")
    expect(0x6CF51B, "sub", "rcx, -0x80")
    expect(0x6CF5A8, "lea", "rcx, [rdi + 0x300]")
    expect(0x6CF93A, "mov", "r9d, dword ptr [rax + 0x314]")
    expect(0x6CF941, "mov", "dword ptr [rcx + 0x2b8], r9d")
    expect(0x6CFA71, "lea", "rax, [rbp + 0x280]")
    expect(0x6CFA78, "mov", "qword ptr [rsp + 0x20], rax")
    expect(0x6CFA7D, "mov", "r9, qword ptr [rbp + 0x28]")
    branch(0x6CFA90, 0x84C40)
    branch(0x84C5A, 0x1194280)

    # Entity Fire takes the target actor's real lockpoint zero and keeps the
    # weak in Raw+50/+58. The wire adapter can resolve another captured ordinal
    # but must preserve that weak contract, never substitute the actor pointer.
    expect(0x6CF9DE, "xor", "r8d, r8d")
    expect(0x6CF9E6, "mov", "rcx, r14")
    branch(0x6CF9E9, 0x548EF0)
    expect(0x548EF3, "cmp", "r9, qword ptr [rcx + 0x378]")
    expect(0x548F03, "shl", "r9, 4")
    expect(0x548F07, "add", "r9, qword ptr [rcx + 0x368]")
    expect(0x6CF9FC, "mov", "qword ptr [rbp + 0x2d0], rcx")
    expect(0x6CFA0A, "mov", "qword ptr [rbp + 0x2d8], rdx")
    expect(0x6CFA96, "mov", "qword ptr [rbp + 0x2d0], r12")
    expect(0x6CFAA4, "mov", "qword ptr [rbp + 0x2d8], r12")

    # Entity body construction copies both embedded BulletIP and the exact weak.
    # It immediately emits a nested bullet through a DIFFERENT descriptor/factory,
    # so a TLS-only Unit claim would incorrectly count or suppress that creation.
    branch(0x266239, 0x2667E0)
    branch(0x266241, 0x266610)
    expect(0x266801, "mov", "r14, rdx")
    expect(0x26680B, "sub", "rdx, -0x80")
    expect(0x26680F, "add", "rcx, 0x5d0")
    branch(0x266816, 0x247400)
    expect(0x2660CF, "mov", "edx, dword ptr [rsi + 0x38]")
    branch(0x2660D5, 0x54EE70)
    expect(0x26687E, "mov", "rax, qword ptr [r14 + 0x58]")
    expect(0x26688A, "mov", "rsi, qword ptr [r14 + 0x50]")
    expect(0x266895, "mov", "qword ptr [rbx + 0x7b0], rsi")
    expect(0x2668A3, "mov", "qword ptr [rbx + 0x7b8], rdx")
    expect(0x266637, "mov", "rax, qword ptr [rcx + 0x7b8]")
    expect(0x266649, "mov", "rcx, qword ptr [rbp + 0x7b0]")
    expect(0x266657, "mov", "qword ptr [rbp + 0x630], rcx")
    expect(0x266665, "mov", "qword ptr [rbp + 0x638], rdx")
    expect(0x26668A, "lea", "r9, [rbp + 0x5d0]")
    branch(0x2666A3, 0x1194280)
    check(0x630 == 0x5D0 + 0x60 and 0x2D0 == 0x280 + 0x50,
          "outer Raw lock and nested BulletIP weak use their distinct proved offsets")

    # 6D02B0's post-Fire registration is not a second spawn. Keep it on BOTH
    # peers: it assigns a type-4 parent-derived ID and inserts the existing body.
    branch(0x6CFB50, 0x6D02B0)
    expect(0x6D033D, "call", "qword ptr [rax + 0x18]")
    branch(0x6D03C2, 0x781630)
    branch(0x7816BF, 0x7766B0)
    branch(0x7816CC, 0x774BD0)
    branch(0x78172A, 0x782720)
    branch(0x782829, 0x782880)
    branch(0x782D6A, 0x774BD0)
    expect(0x7766D4, "mov", "dword ptr [rcx + 0xc], 4")
    branch(0x7766E2, 0x776AB0)
    expect(0x7766EA, "mov", "dword ptr [rsi + 4], eax")
    expect(0x7766ED, "mov", "word ptr [rsi], bx")
    expect(0x7766F0, "mov", "word ptr [rsi + 2], di")
    expect(0x774C2C, "movups", "xmm0, xmmword ptr [r14]")
    expect(0x774C30, "movups", "xmmword ptr [rbx + 8], xmm0")
    expect(0x774C38, "mov", "dword ptr [rbx + 0x18], eax")
    expect(0x774C3F, "mov", "qword ptr [rbx + 0x20], rax")
    for begin, end in [(0x6D02B0, 0x6D0453), (0x781630, 0x781945), (0x7766B0, 0x77670C),
                       (0x774BD0, 0x774D75), (0x782720, 0x782878), (0x782880, 0x782F6E)]:
        registration_body = instructions(begin, end)
        registration_calls = {ins.operands[0].imm for ins in registration_body if ins.mnemonic == "call"
                              and ins.operands[0].type == x86.X86_OP_IMM}
        check(not ({0x1194280, 0x84C40} & registration_calls),
              f"{begin:#x}: native identity registration has no actual RawObject spawn call")
    check(0x6D03C2 not in sites and 0x781630 not in sites,
          "accepted shot relays do not suppress native post-creation identity registration")
    legacy_net = module.with_name("edf41net.cpp").read_text(encoding="utf-8-sig")
    legacy_compact = re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", legacy_net))
    check("kRegisterChild=0x781630" in legacy_compact
          and "if(remote)Fn<GuestChildFn>(kGuestChild)(child);"
              "Fn<RegisterChildFn>(kRegisterChild)(manager,weakChild,weakParent,index,sub,remote);" in legacy_compact,
          "existing shared-model networking uses the same native identity registration on both peers")

    # The original stock receiver also reaches the exact factory, with temporary
    # parameter-word writes restored afterwards. The new action path replaces
    # that receiver boundary rather than duplicating its whole burst.
    expect(0x6CF6FD, "mov", "r14d, dword ptr [rcx + 0x90]")
    expect(0x6CF709, "mov", "r15d, dword ptr [rcx + 0x94]")
    expect(0x6CF710, "mov", "dword ptr [rcx + 0x90], eax")
    expect(0x6CF716, "mov", "dword ptr [rcx + 0x94], ebp")
    branch(0x6CF73B, 0x6D05C0)
    expect(0x6CF750, "mov", "dword ptr [rax + 0x90], r14d")
    expect(0x6CF75C, "mov", "dword ptr [rax + 0x94], r15d")

    # 32B790 is the sender only. For network mode 1 it sends only while the
    # charge callback is current; it does not start charge, fire or cancel.
    expect(0x32B8BE, "test", "byte ptr [rdi + 0x128], 1")
    branch(0x32B8C5, 0x32BA46, "jne")
    expect(0x32B8CB, "mov", "eax, dword ptr [rdi + 0xd00]")
    expect(0x32B8D1, "cmp", "eax, 1")
    branch(0x32B8D4, 0x32B90D, "jne")
    rip_target(0x32B8DD, 0x32C750)
    branch(0x32B8FE, 0x32A890)
    branch(0x32B905, 0x32BA46, "je")
    expect(0x32B90D, "cmp", "eax, 2")
    branch(0x32B910, 0x32BA46, "jl")
    expect(0x32B916, "inc", "byte ptr [rdi + 0xd21]")
    expect(0x32B923, "mov", "byte ptr [rdi + 0xd20], al")
    expect(0x32B9D7, "mov", "dl, 5")
    branch(0x32B9DE, 0x12B5790)
    branch(0x32BA11, 0x6D04F0)
    sender = instructions(0x32B790, 0x32BA71)
    direct_calls = {ins.operands[0].imm for ins in sender if ins.mnemonic == "call"
                    and ins.operands[0].type == x86.X86_OP_IMM}
    check(direct_calls == {0x12D85EC, 0x7748F0, 0x32A890, 0x30C250, 0x79A460,
                           0x12B5790, 0x6D04F0, 0x760180, 0x12D8770},
          "sender direct calls are only weak refs, state query, serializer and packet handling")
    root_writes = [(ins.address, operand.mem.disp) for ins in sender for operand in ins.operands
                   if operand.type == x86.X86_OP_MEM and operand.mem.base == x86.X86_REG_RDI
                   and operand.access & capstone.CS_AC_WRITE]
    check(root_writes == [(0x32B916, 0xD21), (0x32B923, 0xD20)],
          "sender mutates only native D21/D20 sequence counters on the child")

    indirect_calls = [(ins.address, ins.op_str) for ins in sender if ins.mnemonic == "call"
                      and ins.operands[0].type != x86.X86_OP_IMM]
    check(indirect_calls == [(0x32B883, "qword ptr [rax]"), (0x32B9AA, "qword ptr [rax]"),
                             (0x32B9BE, "qword ptr [rax + 8]"), (0x32BA2C, "qword ptr [rax + 0x70]")],
          "sender indirect calls are retained weak releases and native network send")

    # The remote mode-1 consumer waits for idle, compares sequence, applies the
    # requested state, then consumes sequence and the holder parameter update after
    # the intercepted setter returns. Mode >=2 takes the native early return.
    expect(0x32BAA1, "test", "byte ptr [rbx + 0x128], 1")
    branch(0x32BAA8, 0x32BB68, "je")
    expect(0x32BAAE, "mov", "eax, dword ptr [rbx + 0xd00]")
    expect(0x32BAB4, "cmp", "eax, 2")
    branch(0x32BAB7, 0x32BB5B, "jge")
    expect(0x32BABD, "cmp", "eax, 1")
    branch(0x32BAC0, 0x32BB68, "jne")
    rip_target(0x32BACE, 0x32D0C0)
    branch(0x32BAF1, 0x32A890)
    branch(0x32BAF8, 0x32BB68, "je")
    expect(0x32BAFF, "lea", "rdx, [rbx + 0xd21]")
    expect(0x32BB06, "lea", "rcx, [rbx + 0xd20]")
    branch(0x32BB12, 0x31CDC0)
    branch(0x32BB19, 0x32BB51, "jns")
    expect(0x32BB1B, "movzx", "edx, byte ptr [rbx + 0xd22]")
    expect(0x32BB22, "lea", "rcx, [rbx + 0xad0]")
    branch(0x32BB2C, 0x32C180)
    expect(0x32BB31, "movzx", "eax, byte ptr [rbx + 0xd21]")
    expect(0x32BB3F, "mov", "byte ptr [rbx + 0xd20], al")
    expect(0x32BB45, "movsx", "edx, byte ptr [rbx + 0xd23]")
    branch(0x32BB4C, 0x6D04A0)

    consumer = instructions(0x32BA80, 0x32BB75)
    consumer_calls = [ins for ins in consumer if ins.mnemonic == "call"]
    check(all(ins.operands[0].type == x86.X86_OP_IMM for ins in consumer_calls),
          "mode-1 consumer has no hidden indirect call")
    check([ins.operands[0].imm for ins in consumer_calls]
          == [0x7748F0, 0x32A890, 0x31CDC0, 0x32C180, 0x6D04A0],
          "consumer only validates, compares sequence, applies state and updates holder parameter")
    consumer_writes = [(ins.address, operand.mem.disp) for ins in consumer for operand in ins.operands
                       if operand.type == x86.X86_OP_MEM and operand.mem.base == x86.X86_REG_RBX
                       and operand.access & capstone.CS_AC_WRITE]
    check(consumer_writes == [(0x32BB3F, 0xD20)], "mode-1 consumer directly acknowledges only D20")
    expect(0x6D04A0, "mov", "rax, qword ptr [rcx + 0x20]")
    expect(0x6D04A9, "mov", "dword ptr [rax + 0x90], edx")

    # Type 6 sends the full native matrix and parameter payload. 32BBC0 receives the
    # embedded child+120 network object, hence +A70==holder+B90 and
    # +9B0==machine+AD0; message 5 and 6 are separate interception boundaries.
    expect(0x32B4E4, "test", "byte ptr [rbx + 0x128], 1")
    branch(0x32B4EB, 0x32B729, "jne")
    expect(0x32B4FE, "cmp", "dword ptr [rbx + 0xd00], 2")
    branch(0x32B505, 0x32B729, "jl")
    expect(0x32B6C6, "mov", "dl, 6")
    branch(0x32B6CC, 0x12B5790)
    branch(0x32B6DB, 0x761330)
    branch(0x32B6E7, 0x12B5580)
    branch(0x32B6F7, 0x6D04B0)
    expect(0x32BC05, "cmp", "al, 5")
    branch(0x32BC07, 0x32BEBA, "jl")
    expect(0x32BC0D, "test", "byte ptr [rsi + 8], 1")
    branch(0x32BC11, 0x32BC1F, "jne")
    expect(0x32BC13, "lea", "ecx, [rax - 5]")
    expect(0x32BC16, "cmp", "cl, 1")
    branch(0x32BC19, 0x32BED8, "jbe")
    expect(0x32BC1F, "cmp", "byte ptr [rsi + 0x1c8], 0")
    branch(0x32BC26, 0x32BED8, "jne")
    expect(0x32BC2F, "sub", "ecx, 5")
    branch(0x32BC32, 0x32BDA2, "je")
    expect(0x32BC38, "cmp", "ecx, 1")
    branch(0x32BC3B, 0x32BED8, "jne")
    expect(0x32BD86, "lea", "rcx, [rsi + 0xa70]")
    branch(0x32BD98, 0x6CF6C0)
    branch(0x32BDA5, 0x12B4BB0)
    branch(0x32BDB0, 0x12B4BB0)
    branch(0x32BDD1, 0x6CF780)
    expect(0x32BDD6, "mov", "ecx, dword ptr [rsi + 0xbe0]")
    expect(0x32BDDC, "cmp", "ecx, 1")
    branch(0x32BDDF, 0x32BE46, "jne")
    expect(0x32BE29, "mov", "byte ptr [rsi + 0xc01], bl")
    expect(0x32BE2F, "mov", "byte ptr [rsi + 0xc02], dil")
    expect(0x32BE3B, "mov", "byte ptr [rsi + 0xc03], al")
    expect(0x32BE46, "cmp", "ecx, 2")
    branch(0x32BE49, 0x32BED8, "jl")
    expect(0x32BE8F, "lea", "rcx, [rsi + 0x9b0]")
    branch(0x32BE9D, 0x32C180)
    expect(0x32BEA2, "mov", "byte ptr [rsi + 0xc00], bl")
    branch(0x32BEB3, 0x6D04A0)
    branch(0x32BED2, 0x328890)

    # Four state-entry notification sites retain their real CAS setup and the
    # direct native weapon start/cancel instead of treating the sender as state.
    expect(0x32C6A9, "test", "edx, edx")
    branch(0x32C6CB, 0x116BFC0)
    expect(0x32C759, "test", "edx, edx")
    branch(0x32C797, 0x116BFC0)
    branch(0x32C9B8, 0x6CFBC0)
    branch(0x32D097, 0x6CFC00)
    expect(0x32D0C9, "test", "edx, edx")
    branch(0x32D10F, 0x116BFC0)

    print(f"Deiroi accepted-action native audit: {CHECKS} checks passed; PE bytes only")


def main() -> int:
    path = arguments()
    if path is None or not path.is_file():
        print("SKIP: provide supported EDF.dll with --dll PATH or a positional path")
        return SKIP
    try:
        run(path)
    except ImportError as error:
        print(f"SKIP: native audit dependency unavailable: {error}")
        return SKIP
    return 0


if __name__ == "__main__":
    sys.exit(main())
