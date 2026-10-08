"""Supported EDF.dll constructor contract; no game process/DllMain is executed.

The native initializer's exact mark-zeroing instruction executes on a private
fixture. The audited movement loader and call order are fingerprinted separately.
This is not a complete constructor or Havok-world integration test.
"""
import ctypes as C
import hashlib
from pathlib import Path
import sys

if sys.platform != 'win32' or C.sizeof(C.c_void_p) != 8 or len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass the supported EDF.dll on Windows x64')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('Native safety assertions must be enabled')
import pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46 and pe.FILE_HEADER.Machine == 0x8664
assert pe.get_data(0x64E420, 11) == bytes.fromhex('488d056913190149890424')  # vtable = 0x17DF790
assert pe.get_data(0x64E476, 8) == bytes.fromhex('4d8db42480150000')  # movement = vehicle+0x1580
zero_registers = pe.get_data(0x64E4FC, 5)
assert zero_registers == bytes.fromhex('0f57c033c0')  # xorps xmm0,xmm0; xor eax,eax
zero = pe.get_data(0x64E501, 8)
assert zero == bytes.fromhex('410f1186ac000000')  # movups [r14+AC],xmm0
# The constructor first loads heli_movement, then constructs the flight shape.
assert pe.get_data(0x64E99B, 32) == bytes.fromhex('e8c01b0000498bd7498bcee8458b00004d8d442460498bd7498bcee8d5840000')
# Audited loader 0x6574F0: its scalar movement writes are +90,+94,+98,+9C,
# +A0,+A4,+A8. It does not load mission_setup or write the mark at +AC.
assert hashlib.sha256(pe.get_data(0x6574F0, 0x580)).hexdigest() == '9bc450abb24e9d3eed4675bf5a1dc179d1c311c17be4492f9c76ee1a07114fa7'
k = C.WinDLL('kernel32', use_last_error=True)
k.VirtualAlloc.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.c_uint]
k.VirtualAlloc.restype = C.c_void_p
k.VirtualFree.argtypes = [C.c_void_p, C.c_size_t, C.c_uint]
# Preserve R14; put the supplied movement fixture in R14 and execute exactly
# the native zeroing store, with XMM0 zero as it is at constructor 0x64E4FC.
code = bytes.fromhex('41564989ce') + zero_registers + zero + bytes.fromhex('415ec3')
page = k.VirtualAlloc(None, len(code), 0x3000, 0x40)
assert page
try:
    C.memmove(page, code, len(code))
    init = C.WINFUNCTYPE(None, C.c_void_p)(page)
    fixture = C.create_string_buffer(0x200)
    for value in (0.0, 7005.0, 7201.0, 7401.0):
        C.c_float.from_buffer(fixture, 0xAC).value = value
        init(C.addressof(fixture))
        assert C.c_float.from_buffer(fixture, 0xAC).value == 0.0
finally:
    assert k.VirtualFree(page, 0, 0x8000)
print('PASS: native constructor clears all 4 mark fixtures before flight shape creation')
