"""The players' own loaded jets (src/player_vehicle.cpp, src/player_vehicle_logic.h): the wiring in the source, and, with
the game's EDF.dll (TimeDateStamp 0x678CCB46, read as a file), the code it rests on: the transporter's one configure call
(0x5E5687) and its arguments, the configure storing the requester and the path, the container making its vehicle from
that path (0x5E8FB9..0x5E8FDC). No game run.

    python tests/player_vehicle_native_audit.py
"""
from __future__ import annotations

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import gamedir  # noqa: E402

checks = failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


def read(rel: str) -> str:
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read()


def body(src: str, head: str) -> str:
    return src.split(head, 1)[1].split('\n}', 1)[0] if head in src else ''


def audit_source() -> None:
    pv = read('src/player_vehicle.cpp')
    air = read('src/airdrop.cpp')
    mission = read('src/mission.cpp')
    net = read('src/support_net.cpp')
    check('RedirectCall(image+kConfigureCall,image+kConfigure' in pv and 'kConfigureCall=0x5E5687' in pv and 'kConfigure=0x5E8B40' in pv,
          'the transporter configure call (0x5E5687 -> 0x5E8B40) is the one redirected')
    step = body(air, 'unsigned char* VehicleStep(')
    check('PlayerVehicleStepPath(sgo)' in step and step.index('PlayerVehicleStepPath(sgo)') < step.index('for(auto& d:drops)'),
          "the container's vehicle step asks for the path before its CreateObject; the airdrops still match the original")
    check(mission.index('PreloadSupportVariants();') < mission.index('PreloadPlayerVehicles();'),
          'the mission preloads the loaded jets after the support variants')
    check('plugin_net::RegisterChannel(PlayerVehicleNetChannel());' in net, 'the PVEH channel is registered')
    hook = body(pv, 'void ConfigureHook(')
    check(hook.index('reinterpret_cast<ConfigureFn>(image+kConfigure)') < hook.index('TwinOfPath'),
          'the stock configure runs first, whatever follows')
    check('if(InSession())SendMsg(m);' in hook and 'engine.Decide(' in hook and 'engine.Count(' in hook,
          'only this machine\'s request is decided (and told the room online); another\'s is counted')
    recv = body(pv, 'bool Receive(')
    check('SenderOwns(who,puid)' in recv and recv.index('SenderOwns') < recv.index('engine.Received(m)'),
          'a decision / sync is taken only from the requester\'s own machine')
    install = body(pv, 'bool InstallPlayerVehicles()')
    check('AirdropWatching()' in install and 'landing=watching && AirdropWatching();' in install,
          'without the vehicle step watched nothing is announced (a machine that cannot swap sends no have)')
    update = body(pv, 'void Update(')
    check('landing && origin' in update and 'SendHave()' in update, 'the have goes out only when this machine can swap')
    proto = read('src/support_protocol.h')
    m = re.search(r'kExtBuilt=([^;]*);', proto)
    check(m and 'kExtPlayerVehicle' in m.group(1) and 'kExtPlayerVehicle=1024u' in proto, 'the build announces kExtPlayerVehicle')
    logic = read('src/player_vehicle_logic.h')
    check('if(!peer_[i].feature || !peer_[i].heard || !PeerHas(peer_[i],want))want.bits=0;' in logic,
          'a requester loads its jet only when every peer announced the channel and has the file')


def audit_code(img: bytes) -> None:
    def calls(site: int, callee: int) -> bool:
        return img[site] == 0xE8 and site + 5 + struct.unpack_from('<i', img, site + 1)[0] == callee
    check(calls(0x5E5687, 0x5E8B40), '0x5E5687 calls the configure 0x5E8B40')
    check(img[0x5E5671:0x5E5687] == bytes.fromhex('f30f11742420' '4c8d4db0' '4c8b45a8' '488bd7' '488b4c2468'),
          'its arguments: level [rsp+20h], setup r9, the path r8, the requester rdx, the container rcx')
    sites = 0
    for i in range(0x1000, 0x1300000):
        if img[i] == 0xE8 and i + 5 + struct.unpack_from('<i', img, i + 1)[0] == 0x5E8B40:
            sites += 1
    check(sites == 1, f'the configure has one call site (found {sites}): the plugin\'s own airdrops call it directly')
    pv = read('src/player_vehicle.cpp')
    sig = re.search(r'kCallArgsSig\[\]=\{([^}]*)\}', pv).group(1)
    check(bytes(int(x, 16) for x in re.findall(r'0x([0-9A-F]{2})', sig)) == img[0x5E5671:0x5E5687],
          'player_vehicle.cpp kCallArgsSig is EDF.dll\'s')
    check(img[0x5E8B75:0x5E8B7C] == bytes.fromhex('48898e700b0000') and img[0x5E8BBD:0x5E8BC4] == bytes.fromhex('488d8e800b0000') and
          calls(0x5E8BC7, 0x3D450), 'the configure keeps the requester at +0xB70 and assigns the path to +0xB80')
    check(img[0x5E8FB9:0x5E8FC0] == bytes.fromhex('4c8d87800b0000') and img[0x5E8FC7:0x5E8FCA] == bytes.fromhex('4d8b00') and
          calls(0x5E8FDC, 0x11945E0), 'the container makes its vehicle from +0xB80 (CreateObject 0x11945E0 at 0x5E8FDC)')


def main() -> int:
    audit_source()
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if dll and os.path.isfile(dll):
        import room_preview_sigs
        _, img = room_preview_sigs.image(dll)
        audit_code(img)
    else:
        print('player_vehicle_native_audit: no game; EDF.dll checks skipped')
    print(f'player_vehicle_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
