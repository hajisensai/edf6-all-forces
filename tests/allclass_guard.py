"""Source guard of every class in every seat (src/allclass.cpp, ini AllClassesRide): python tests/allclass_guard.py
[--root DIR], exit 1 on a failure. What it holds in place:
  - the seat masks are widened in one place only, the seat build's hook (SeatBuild, through seatclass::Widen, after
    the stock build and on the seat it appended), so a closed seat (0) is never opened and the Proteus rework's
    closing (proteus.cpp: save the mask, write 0, put the saved one back) keeps working over it;
  - no other source writes a seat's class mask but the Proteus rework (save / 0 / restore);
  - every seat test of the plugin's is the stock one (human +0x31C & seat +0x30 & seat +0x34), so the player's and the
    NPCs' boarding, the seat switch, the boarding hint and the rescue's door seat all see the widened masks the way
    the stock board button does; none tests a class by a fixed mask;
  - the two hooks are installed at load (plugin.cpp, before a mission makes a vehicle), behind their signatures;
  - the option: AllClassesRide=1 in the shipped ini with its Chinese comment (incl. online), read by plugin.cpp,
    Cfg's allClassesRide default on, logged, in the README; the hooks test it (On()).
"""
from __future__ import annotations

import argparse
import os
import re
import sys

failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)


def code_only(text: str) -> str:
    return '\n'.join(line.split('//', 1)[0] for line in text.splitlines())


def raw(root: str, rel: str) -> str:
    with open(os.path.join(root, rel), encoding='utf-8') as f:
        return f.read()


def body(text: str, signature: str) -> str:
    m = re.search(signature + r'[^;{]*\{', text)
    if not m:
        return ''
    depth, i = 1, m.end()
    while depth and i < len(text):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    root = ap.parse_args().root
    allclass = code_only(raw(root, 'src/allclass.cpp'))
    policy = code_only(raw(root, 'src/seat_class.h'))

    # The policy.
    if 'constexpr std::uint32_t kClassBits=0x0F;' not in policy:
        fail('seat_class.h: kClassBits is not R|WD|F|AR (0x0F)')
    if 'return (mask&kClassBits) ? (mask|kClassBits) : mask;' not in policy:
        fail('seat_class.h: Widen must add the class bits only to a seat some class may take (0 stays closed)')
    if 'kFallbackRide[]=L"vehicle_striker_rear"' not in policy:
        fail('seat_class.h: the bench pose is vehicle_striker_rear (the one riding state all four CAS have)')

    # The seat build hook: the stock build first, then the appended seat, through Widen, behind On().
    build = body(allclass, r'void __fastcall SeatBuild\(')
    if not build:
        fail('allclass.cpp: SeatBuild is gone')
    else:
        stock = build.find('(image+kSeatBuild)(vehicle,entry)')
        widen = build.find('seatclass::Widen(')
        put = build.find('Put<std::uint32_t>(seat,kSeatClassMask,wide)')
        if not 0 <= stock < widen < put:
            fail('allclass.cpp SeatBuild: the stock build, then seatclass::Widen, then the write (in that order)')
        if 'count!=before+1' not in build or '(count-1)*kSeatStride' not in build:
            fail('allclass.cpp SeatBuild: only the seat this build appended is widened')
        if '!On()' not in build:
            fail('allclass.cpp SeatBuild: AllClassesRide=0 must leave the stock mask')
    if len(re.findall(r'Put<[^>]*>\([^;]*kSeatClassMask', allclass)) != 1:
        fail('allclass.cpp: the class mask is written in SeatBuild alone')
    if 'bool On() noexcept { return Cfg().enabled && Cfg().allClassesRide; }' not in allclass:
        fail('allclass.cpp: On() is the plugin and the option')
    ride = body(allclass, r'bool __fastcall RideState\(')
    if not ride or 'if(stock(cas,casPlayer,name,flag))return true;' not in ride or 'seatclass::kFallbackRide' not in ride \
            or '!name || !On()' not in ride:
        fail('allclass.cpp RideState: the seat\'s own state first, the bench pose only after a refusal, with the option')
    install = body(allclass, r'bool InstallAllClassesRide\(\) noexcept')
    for site, target in (('kSeatBuildCall', 'kSeatBuild'), ('kRideStateCall', 'kSetCasState')):
        if f'RedirectCall(image+{site},image+{target},' not in install:
            fail(f'allclass.cpp: {site} is redirected only while it still calls {target}')
    if 'build &&' not in install or 'pose &&' not in install:
        fail('allclass.cpp: each hook only behind its signatures')

    # Nobody else writes a seat's class mask (+0x30) but the Proteus rework's save / close / restore.
    for dirpath, _dirs, files in os.walk(os.path.join(root, 'src')):
        for f in files:
            if not f.endswith(('.cpp', '.h', '.inc')) or f in ('allclass.cpp', 'proteus.cpp'):
                continue
            text = code_only(raw(root, os.path.relpath(os.path.join(dirpath, f), root)))
            for m in re.finditer(r'Put<[^>]*>\(\s*(?:seat|SeatAt\([^)]*\)|s)\s*,\s*(?:0x30|kSeatClassMask|kSeatClass)\b', text):
                fail(f'src/{f}: writes a seat class mask ({m.group(0)}): only allclass.cpp / proteus.cpp may')
    proteus = code_only(raw(root, 'src/proteus.cpp'))
    seats = body(proteus, r'void Seats\(Unit& u,unsigned char\* v,bool two,bool kick\) noexcept')
    opens = body(proteus, r'void OpenSeats\(Unit& u,unsigned char\* v\) noexcept')
    if 'u.seatMask[s]=At<std::int32_t>(SeatAt(v,s),kSeatClassMask);' not in seats \
            or 'Put<std::int32_t>(SeatAt(v,s),kSeatClassMask,0);' not in seats:
        fail('proteus.cpp Seats: closing saves the (widened) mask and writes 0')
    if 'Put<std::int32_t>(SeatAt(v,s),kSeatClassMask,u.seatMask[s]);' not in opens:
        fail('proteus.cpp OpenSeats: the saved mask is put back')

    # The plugin's seat tests are the stock one.
    tests = {
        'src/npcai.cpp': r'At<std::uint32_t>\(h,kHumanMask\)&At<std::uint32_t>\(seat,kSeatClass\)&At<std::uint32_t>\(seat,kSeatEnable\)',
        'src/seatswitch.cpp': r'At<std::uint32_t>\(human,kHumanClass\)&At<std::uint32_t>\(seat,kSeatClassOn\)&At<std::uint32_t>\(seat,kSeatClassMask\)',
        'src/crew.cpp': r'mask & At<std::uint32_t>\(seat,0x30\) & At<std::uint32_t>\(seat,0x34\)',
        'src/heli.cpp': r'mask&At<std::uint32_t>\(seat,kSeatClassMask\)&At<std::uint32_t>\(seat,kSeatMask\)',
    }
    for rel, pattern in tests.items():
        if not re.search(pattern, code_only(raw(root, rel))):
            fail(f'{rel}: its seat test is no longer the stock triple test (human +0x31C & seat +0x30 & +0x34)')
    for rel, name in (('src/npcai.cpp', 'kHumanMask=0x31C'), ('src/seatswitch.cpp', 'kHumanClass=0x31C'),
                      ('src/heli.cpp', 'kHumanRideMask=0x31C')):
        if name not in code_only(raw(root, rel)):
            fail(f'{rel}: {name}')
    if "At<std::uint32_t>(human,0x31C)" not in code_only(raw(root, 'src/crew.cpp')):
        fail('src/crew.cpp PlayerBoardingEntrance: the human mask is +0x31C')
    for dirpath, _dirs, files in os.walk(os.path.join(root, 'src')):
        for f in files:
            if f.endswith(('.cpp', '.h', '.inc')):
                text = code_only(raw(root, os.path.relpath(os.path.join(dirpath, f), root)))
                if re.search(r'(?:0x31C|kHumanMask|kHumanClass|kHumanRideMask)\)\s*==\s*(?:1|2|4|8)\b', text):
                    fail(f'src/{f}: tests a class by a fixed mask: the seat test is the stock one')

    # Installed at load, the option wired.
    plugin = code_only(raw(root, 'src/plugin.cpp'))
    if 'InstallAllClassesRide();' not in plugin:
        fail('plugin.cpp: InstallAllClassesRide is not called at load')
    if 'n.allClassesRide=ReadBool(L"AllClassesRide",n.allClassesRide);' not in plugin:
        fail('plugin.cpp: AllClassesRide is not read')
    if 'allClassesRide=%d' not in plugin:
        fail('plugin.cpp: AllClassesRide is not logged')
    if not re.search(r'bool allClassesRide=true;', raw(root, 'src/crew.h')):
        fail('crew.h: allClassesRide must default on')
    ini = raw(root, 'EDF6VehicleCrew.ini')
    m = re.search(r'((?:^;.*\n)+)AllClassesRide=1\s*$', ini, re.M)
    if not m:
        fail('EDF6VehicleCrew.ini: AllClassesRide=1 with its comment above it')
    elif not all(w in m.group(1) for w in ('职业', '联机', '普罗透斯')):
        fail('EDF6VehicleCrew.ini: the AllClassesRide comment tells the classes, the Proteus seats and online')
    if 'AllClassesRide' not in raw(root, 'README.md'):
        fail('README.md: AllClassesRide is not documented')
    if not os.path.isfile(os.path.join(root, 'docs', 'all-classes-ride-re.md')):
        fail('docs/all-classes-ride-re.md is missing')

    for f in failures:
        print('FAIL', f)
    if not failures:
        print('allclass guard: ok')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
