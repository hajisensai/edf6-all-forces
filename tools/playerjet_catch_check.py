"""Compile the production Held gate and ObjRef against a tiny object/seat fixture.

The regression is the first frame of an empty, newly spawned NPC catch twin:
there is no PJet yet, and Held must allow PlayerJetFrame to reach Make. The
fixture also replaces a control block at the same address to check stale catches.
No game, game assets, or installed plugin is used. Requires MSVC (auto-detected
on Windows, or --compiler); the headerless fixture needs no SDK or CRT libraries.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def production_function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    compiler = args.compiler or shutil.which('cl')
    if not compiler:
        found = sorted(Path('C:/Program Files/Microsoft Visual Studio').glob(
            '*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
        compiler = found[-1] if found else None
    if not compiler:
        parser.error('MSVC cl.exe not found; pass --compiler')
    root = args.source_root
    held = production_function((root / 'src/playerjet_board.inc').read_text(encoding='utf-8'),
                               'bool Held(const unsigned char* v) noexcept')
    crew = (root / 'src/crew.h').read_text(encoding='utf-8')
    ref = re.search(r'struct ObjRef \{.*?\n\};', crew, re.S)
    if not ref:
        raise RuntimeError('production ObjRef not found')
    fixture = r'''
unsigned char vehicle=0, other=0;
char firstControl=0, nextControl=0;
const void* currentControl=&firstControl;
constexpr int kSelfCtrl=0;
template<class T> T At(const void*,int) noexcept { return static_cast<T>(currentControl); }
@REF@
enum class Rider { none, player, dummy };
Rider rider=Rider::none;
unsigned SeatCount(const unsigned char*) noexcept { return 1; }
unsigned char* SeatAt(unsigned char* v,unsigned) noexcept { return v; }
Rider SeatRider(const unsigned char*) noexcept { return rider; }
constexpr int kHailNone=0;
struct PJet {
    bool driven=false,autopilot=false,keep=false;
    struct { int phase=0; } hail;
    ObjRef ref;
};
PJet entry;
bool haveEntry=false;
const PJet* Find(const unsigned char* v) noexcept {
    return haveEntry && entry.ref.Is(v) ? &entry : nullptr;
}
struct { const unsigned char* v=nullptr; } catchFlight;
struct { ObjRef caught; } bail;
@HELD@
int main() {
    // A new same-kind catch: the original failure, before Make can allocate its PJet.
    catchFlight.v=&vehicle;bail.caught=ObjRef::Of(&vehicle);
    if(!Held(&vehicle))return 1;
    if(Held(&other))return 2;
    // A recycled vehicle address must not inherit the catch from the previous control block.
    currentControl=&nextControl;
    if(Held(&vehicle))return 3;
    currentControl=&firstControl;
    // An existing, otherwise unheld entry must not make a stale catch pointer valid either.
    haveEntry=true;entry.ref=ObjRef::Of(&vehicle);bail.caught=ObjRef{};
    if(Held(&vehicle))return 4;
    catchFlight.v=nullptr;
    entry.keep=true;if(!Held(&vehicle))return 5;entry.keep=false;
    entry.hail.phase=1;if(!Held(&vehicle))return 6;entry.hail.phase=0;
    entry.autopilot=true;if(!Held(&vehicle))return 7;entry.autopilot=false;
    entry.driven=true;if(!Held(&vehicle))return 8;entry.driven=false;
    if(Held(&vehicle))return 9;
    haveEntry=false;rider=Rider::player;
    if(!Held(&vehicle))return 10;
    return 0;
}
'''.replace('@REF@', ref.group()).replace('@HELD@', held)
    with tempfile.TemporaryDirectory(prefix='edf6-catch-check-') as temp:
        directory = Path(temp)
        source, binary = directory / 'check.cpp', directory / 'check.exe'
        source.write_text(fixture, encoding='utf-8')
        subprocess.run([str(compiler), '/nologo', '/std:c++17', '/O2', '/W4', '/WX', '/utf-8',
                        '/GS-', '/Zl', str(source), '/Fo' + str(directory / 'check.obj'),
                        '/Fe' + str(binary), '/link', '/nodefaultlib', '/entry:main',
                        '/subsystem:console'], check=True, cwd=directory)
        result = subprocess.run([str(binary)], check=False)
        if result.returncode:
            raise AssertionError(f'production Held regression case {result.returncode} failed')
    print('playerjet catch: 10 production ownership cases passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
