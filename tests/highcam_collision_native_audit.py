"""Execute verified EDF.dll F6760 in a private PE image, without DllMain or imports.

Only the scene-query, filter-mask and security-cookie callees are substituted.
The installed DLL is read only; no game process is opened or started.
Usage: python tests/highcam_collision_native_audit.py [GAME_DIR_OR_EDF_DLL]
Missing DLL/platform/dependencies: exit 77. Unexpected native bytes: fail closed.
This audits camera response to inclusive t=0 plane contacts, not live Havok behavior.
"""
from __future__ import annotations

import ctypes as ct
import hashlib
import math
import os
from pathlib import Path
import struct
import sys

COLLISION = 0xF6760
COLLISION_SIZE = 0x2E8
QUERY, FILTER, COOKIE = 0x11A9270, 0x106100, 0x12D8770
SIGNATURES = {
    COLLISION: (COLLISION_SIZE, "ed88bf4903070d8b1c4b93efb8d55a3270d06d448ffbafb3ea514419b8a8ebef"),
    QUERY: (16, "0cd17aa86ab16bccf6c14b69ae2792d9703f3e44ebb8f9bb4da25b00321bd030"),
    FILTER: (16, "770966589e1c1361a9017b4287067c093363ff775a681b964e4eb542c92042df"),
    COOKIE: (16, "daca55000d172fa9c7dec43f332214fc502fc0943cb3860b8da677e164b2cad9"),
}


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def vector(address: int) -> tuple[float, float, float]:
    return tuple((ct.c_float * 3).from_address(address))


def write_vector(address: int, value: tuple[float, float, float]) -> None:
    ct.memmove(address, struct.pack("<4f", *value, 1.0), 16)


def distance(a: tuple, b: tuple) -> float:
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def direction(eye: tuple, look: tuple) -> tuple[float, float, float]:
    d = tuple(b - a for a, b in zip(eye, look))
    size = math.sqrt(sum(x * x for x in d))
    return tuple(x / size for x in d) if size else (0.0, 0.0, 1.0)  # 4E220's zero-vector identity


def run(path: Path, pefile, capstone) -> None:
    pe = pefile.PE(str(path), fast_load=True)
    require(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, "unsupported EDF.dll timestamp")
    require(pe.FILE_HEADER.Machine == 0x8664 and pe.OPTIONAL_HEADER.Magic == 0x20B, "requires AMD64 PE32+")
    require(pe.OPTIONAL_HEADER.SizeOfImage == 0x22CE000, "unexpected image size")
    for rva, (size, sha) in SIGNATURES.items():
        require(hashlib.sha256(pe.get_data(rva, size)).hexdigest() == sha, f"unexpected native code at {rva:#x}")
    require(pe.get_data(0xF5058, 10) == bytes.fromhex("c78728050000cdcccc3d"), "camera collision margin initialization changed")
    require(struct.unpack("<f", pe.get_data(0x1765A68, 4))[0] == 100.0, "look correction distance threshold changed")
    disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    instructions = list(disassembler.disasm(pe.get_data(COLLISION, COLLISION_SIZE), COLLISION))
    calls = [i.op_str for i in instructions if i.mnemonic == "call"]
    require(calls == [hex(FILTER), hex(QUERY), hex(QUERY), hex(COOKIE)], f"unexpected native call graph: {calls}")
    require(instructions[-1].mnemonic == "ret", "incomplete native routine")
    print("Verified native F6760 and its complete four-call graph; creating private image.", flush=True)

    kernel = ct.WinDLL("kernel32", use_last_error=True)
    kernel.VirtualAlloc.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_ulong, ct.c_ulong]
    kernel.VirtualAlloc.restype = ct.c_void_p
    kernel.VirtualFree.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_ulong]
    kernel.VirtualFree.restype = ct.c_int
    kernel.FlushInstructionCache.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_size_t]
    kernel.FlushInstructionCache.restype = ct.c_int
    mapped = pe.get_memory_mapped_image()  # bytes only: no Windows loader, relocations/imports/DllMain never run
    base = kernel.VirtualAlloc(None, pe.OPTIONAL_HEADER.SizeOfImage, 0x3000, 0x40)
    require(bool(base), f"VirtualAlloc failed: {ct.get_last_error()}")
    checks = 0
    handler_token = None
    try:
        ct.memmove(base, mapped, len(mapped))
        # Diagnose an unexpected native boundary without hiding a crash behind a successful Python exit code.
        @ct.WINFUNCTYPE(ct.c_long, ct.c_void_p)
        def native_exception(pointers):
            record = ct.c_void_p.from_address(pointers).value
            code = ct.c_uint32.from_address(record).value
            address = ct.c_void_p.from_address(record + 16).value
            if code == 0xC0000005:
                access = ct.c_size_t.from_address(record + 40).value
                print(f"NATIVE ACCESS VIOLATION: instruction RVA {address - base:#x}, address {access:#x}", flush=True)
            return 0

        kernel.AddVectoredExceptionHandler.argtypes = [ct.c_ulong, ct.c_void_p]
        kernel.AddVectoredExceptionHandler.restype = ct.c_void_p
        kernel.RemoveVectoredExceptionHandler.argtypes = [ct.c_void_p]
        kernel.RemoveVectoredExceptionHandler.restype = ct.c_ulong
        handler_token = kernel.AddVectoredExceptionHandler(1, ct.cast(native_exception, ct.c_void_p))
        scene = ct.create_string_buffer(128)
        ct.c_void_p.from_address(base + 0x20B2958).value = ct.addressof(scene)
        ct.c_void_p.from_address(base + 0x20B2970).value = ct.addressof(scene)
        plane_point = (0.0, 0.0, 0.0)
        plane_normal = (0.0, 1.0, 0.0)
        queries: list[tuple] = []
        callback_errors: list[str] = []
        counts = {"filter": 0, "cookie": 0}

        @ct.WINFUNCTYPE(None, ct.c_void_p, ct.c_void_p, ct.c_void_p)
        def query(_world, collector, request):
            try:
                start, end = vector(request + 0x40), vector(request + 0x50)
                ct.c_uint32.from_address(collector + 0xC).value = 0
                # A zero-length ray has no direction. A nonzero ray starting on the plane reports contact at t=0.
                signed = lambda p: sum((p[i] - plane_point[i]) * plane_normal[i] for i in range(3))
                a, b = signed(start), signed(end)
                fraction = None
                if distance(start, end) > 1e-7:
                    if abs(a) <= 1e-5:
                        fraction = 0.0
                    elif a * b <= 0 and a != b:
                        fraction = a / (a - b)
                queries.append((start, end, fraction))
                if fraction is not None:
                    hit = tuple(start[i] + (end[i] - start[i]) * fraction for i in range(3))
                    ct.c_uint32.from_address(collector + 0xC).value = 1
                    write_vector(collector + 0x30, hit)
                    write_vector(collector + 0x40, plane_normal)
            except Exception as exc:  # callbacks must not silently swallow an audit failure at the C boundary
                callback_errors.append(repr(exc))

        @ct.WINFUNCTYPE(ct.c_uint32, ct.c_void_p)
        def filter_mask(_context):
            counts["filter"] += 1
            return 0

        @ct.WINFUNCTYPE(None, ct.c_size_t)
        def cookie_check(_cookie):
            counts["cookie"] += 1

        for rva, callback in [(QUERY, query), (FILTER, filter_mask), (COOKIE, cookie_check)]:
            stub = b"\x48\xB8" + struct.pack("<Q", ct.cast(callback, ct.c_void_p).value) + b"\xFF\xE0"
            ct.memmove(base + rva, stub, len(stub))
        require(bool(kernel.FlushInstructionCache(ct.c_void_p(-1), base, pe.OPTIONAL_HEADER.SizeOfImage)), "instruction-cache flush failed")
        collide = ct.WINFUNCTYPE(None, ct.c_void_p, ct.c_void_p, ct.c_void_p, ct.c_void_p)(base + COLLISION)
        camera = ct.create_string_buffer(0x660 + 15)
        cam = (ct.addressof(camera) + 15) & ~15
        ct.c_float.from_address(cam + 0x528).value = 0.1

        def invoke(eye: tuple, look: tuple) -> tuple[tuple, tuple]:
            nonlocal checks
            queries.clear()
            # F687B uses legacy SSE SUBPS with a memory operand: the native stack target is 16-byte aligned.
            desired = ct.create_string_buffer(16 + 15)
            desired_address = (ct.addressof(desired) + 15) & ~15
            write_vector(desired_address, look)
            write_vector(cam + 0x630, eye)
            write_vector(cam + 0x640, look)
            if checks == 0:
                print("Entering original F6760 with query/filter/cookie substitutes.", flush=True)
            collide(cam, cam + 0x640, cam + 0x630, desired_address)
            require(not callback_errors, str(callback_errors))
            require(len(queries) == 2, "native collision must execute both queries for an equal desired/current look")
            checks += 1
            return vector(cam + 0x630), vector(cam + 0x640)

        for plane_point in [(0.0, 0.0, 0.0), (120.0, -25.0, -40.0)]:
            for plane_normal in [(0.0, 1.0, 0.0), (0.6, 0.8, 0.0)]:
                eye = tuple(plane_point[i] + (0.0, 45.0, -35.0)[i] for i in range(3))
                expected_direction = direction(eye, plane_point)
                collapsed_eye, actual_look = invoke(eye, plane_point)
                require(queries[0][0] == queries[0][1] and queries[0][2] is None, "first native query must be the zero-length look query")
                require(queries[1][2] == 0.0, "second native query must contact the surface at its start")
                expected_eye = tuple(plane_point[i] + plane_normal[i] * 0.1 for i in range(3))
                require(distance(collapsed_eye, expected_eye) < 2e-5, "native eye correction must be hit + normal * 0.1 m")
                require(distance(actual_look, plane_point) < 1e-6, "zero-length miss must leave look at impact")
                if plane_normal == (0.0, 1.0, 0.0):
                    require(direction(collapsed_eye, actual_look)[1] < -0.99999, "surface contact must reproduce -90 degree collapse")
                checks += 1
                # Keep the same optical ray through the impact, but put collision look in free space on that ray.
                # The final case is the production fix: retreat the pivot exactly one native camera margin (0.1 m)
                # from the impact toward the unchanged eye, rather than relying on an arbitrarily distant pivot.
                production_fraction = 1.0 - ct.c_float.from_address(cam + 0x528).value / distance(eye, plane_point)
                for fraction in (0.05, 0.25, 0.75, production_fraction):
                    pivot = tuple(eye[i] + (plane_point[i] - eye[i]) * fraction for i in range(3))
                    stable_eye, stable_look = invoke(eye, pivot)
                    require(all(hit is None for _a, _b, hit in queries), "free-space collision pivots must not hit the plane")
                    require(distance(stable_eye, eye) < 1e-5, "native collision must retain the overhead eye")
                    require(distance(direction(stable_eye, stable_look), expected_direction) < 1e-5, "collision pivot must preserve the exact impact-facing direction")
                    checks += 1
        require(counts == {"filter": 20, "cookie": 20}, f"unexpected native boundary counts: {counts}")
        print(f"highcam_collision_native_audit: {checks} checks passed; 20 original F6760 calls")
        print("Verified: inclusive plane t=0 contact collapses eye to hit + normal * 0.1 m; free-space optical-ray pivots remain stable.")
    finally:
        if handler_token:
            kernel.RemoveVectoredExceptionHandler(handler_token)
        require(bool(kernel.VirtualFree(base, 0, 0x8000)), "VirtualFree failed")
        pe.close()


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else os.environ.get("EDF6_DIR", r"D:\steam\steamapps\common\EARTH DEFENSE FORCE 6"))
    if path.is_dir():
        path /= "EDF.dll"
    if not path.is_file():
        print(f"SKIP: EDF.dll unavailable: {path}")
        return 77
    if os.name != "nt" or ct.sizeof(ct.c_void_p) != 8:
        print("SKIP: requires 64-bit Windows Python")
        return 77
    try:
        import pefile
        import capstone
    except ImportError as exc:
        print(f"SKIP: missing audit dependency: {exc.name}")
        return 77
    try:
        run(path, pefile, capstone)
        return 0
    except Exception as exc:
        print(f"FAIL: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
