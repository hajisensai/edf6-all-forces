"""Source guard of the NPC driver's CarBase AI pass (src/npcai.cpp): python tests/npc_car_aim_guard.py [--root DIR], exit
1 on a failure. The 60X truck and the 607 RoboTruck have a slot 6 of their own that builds their turret aim table; their
slot 70 walks it unchecked. The plugin runs the AI pass of a vehicle a real soldier drives (not on the AI list, so no
slot 6 ran): a player's 60X a soldier then drove faulted at EDF+0x625A17 every frame once it had a target (testhub #16,
2026-10-17). What it holds in place:
  - DriveVehicleAi asks CarAimReady for a CarBase pass before the pass runs (LogDrivenVehicle, the slot-7 call);
  - CarAimReady tests the SGO key (0x528A0 on +0xA0) before it runs the class's slot 6, since a missing key throws after
    the action is registered, and gives false (driven only) for a slot 6 it does not know;
  - the two trucks' slot 6 and key pairs stay together (tests/npc_vehicle_ai_native_audit.py checks them on EDF.dll).
"""
from __future__ import annotations

import argparse
import os
import sys

failures: list[str] = []


def code_only(text: str) -> str:
    return '\n'.join(line.split('//', 1)[0] for line in text.splitlines())


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        failures.append(f'{signature!r} not found')
        return ''
    depth, i = 0, text.index('{', start)
    for j in range(i, len(text)):
        depth += {'{': 1, '}': -1}.get(text[j], 0)
        if depth == 0:
            return text[i:j + 1]
    return text[i:]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    root = ap.parse_args().root
    with open(os.path.join(root, 'src', 'npcai.cpp'), encoding='utf-8') as f:
        src = code_only(f.read())

    drive = body(src, 'void DriveVehicleAi(')
    ready = drive.find('if(kind==AiPass::car && !CarAimReady(v,vt))return;')
    pass_call = drive.find('reinterpret_cast<AiPassFn>(vt[kSlotAiPass])(v,dt);')
    if ready < 0 or pass_call < 0 or ready > pass_call:
        failures.append('DriveVehicleAi must return on !CarAimReady before it runs the CarBase pass')

    aim = body(src, 'bool CarAimReady(')
    key = aim.find('(image+kSgoKeyNode)(v+kObjectSgo,key)')
    six = aim.find('reinterpret_cast<SixFn>(vt[kSlotSix])(v);')
    if key < 0 or six < 0 or key > six:
        failures.append('CarAimReady must test the SGO key before it runs the class slot 6')
    if 'vt[kSlotSix]==image+kCarSix)return true;' not in aim:
        failures.append("CarAimReady must pass CarBase's own slot 6 through untouched")
    if ': nullptr;' not in aim or 'if(key && ' not in aim:
        failures.append('CarAimReady must leave an unknown own slot 6 unbuilt (key nullptr -> driven only)')
    for pair in ('vt[kSlotSix]==image+kTruckSix ? L"v60X_aim_info"', 'vt[kSlotSix]==image+kRoboTruckSix ? L"v607_aim_info"'):
        if pair not in aim:
            failures.append(f'CarAimReady lost the pair {pair!r}')
    if 'kTruckSix=0x6266B0' not in src or 'kRoboTruckSix=0x6234E0' not in src or 'kSgoKeyNode=0x528A0' not in src:
        failures.append('the trucks\' slot 6 / the key lookup addresses changed: re-run npc_vehicle_ai_native_audit.py')

    for f in failures:
        print('FAIL:', f)
    if failures:
        return 1
    print('PASS: the CarBase pass waits for the trucks\' own slot 6, run only when the SGO has their aim table')
    return 0


if __name__ == '__main__':
    sys.exit(main())
