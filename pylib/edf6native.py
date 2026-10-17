"""The installer's native helpers (native/*.cpp, one DLL: edf6cmpl.dll), loaded with ctypes. Every function there is a
port of a pure Python reference that stays the fallback (a dev checkout without build.cmd) and the definition of the
bytes: the native one gives the same bytes on every input it accepts, and hands back what it does not (tests/
test_native_*.py compare the two).

  cmpl.cpp         mdb.cmpl_compress / cmpl_decompress        (models, map parts)
  crilayla.cpp     crilayla.decompress                        (every compressed file read from a game's CPK)
  lzss_greedy.cpp  fmb_build.cmpl_fast                        (the SK maps' terrain leaves)
  aabb_tree.cpp    hkcms_build._section_tree, simd_tree       (the SK maps' collision meshes)

lib() -> the CDLL, or None. Found beside the frozen installer's modules (build_release bundles it) or in a dev
checkout's build/pylib. A DLL there that does not load raises, every time: never half native, half Python.
"""
from __future__ import annotations

import ctypes
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = 'edf6cmpl.dll'
_LIB: object = None   # the CDLL once loaded, False when there is none

_PROTOTYPES = {
    'edf6_cmpl_bound': (ctypes.c_size_t, (ctypes.c_size_t,)),
    'edf6_cmpl_compress': (ctypes.c_size_t, (ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p)),
    'edf6_cmpl_decompress': (ctypes.c_size_t, (ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t)),
    'edf6_crilayla_decompress': (ctypes.c_int, (ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t)),
    'edf6_lzss_greedy_bound': (ctypes.c_size_t, (ctypes.c_size_t,)),
    'edf6_lzss_greedy': (ctypes.c_size_t, (ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_void_p)),
    'edf6_section_tree': (ctypes.c_int, (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p)),
    'edf6_simd_tree_nodes': (ctypes.c_size_t, (ctypes.c_size_t,)),
    'edf6_simd_tree': (ctypes.c_int, (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p)),
}


def path() -> str:
    base = getattr(sys, '_MEIPASS', None)
    return os.path.join(base, NAME) if base else os.path.join(HERE, '..', 'build', 'pylib', NAME)


def lib() -> ctypes.CDLL | None:
    global _LIB
    if _LIB is None:
        where = path()
        if not (os.name == 'nt' and os.path.isfile(where)):
            _LIB = False
        else:
            loaded = ctypes.CDLL(os.path.abspath(where))
            for name, (restype, argtypes) in _PROTOTYPES.items():
                fn = getattr(loaded, name)   # an older DLL without one of them: AttributeError, not a silent fallback
                fn.restype, fn.argtypes = restype, argtypes
            _LIB = loaded
    return _LIB or None
