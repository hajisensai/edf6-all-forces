"""Source guard of the sea rescue (2026-10-09, the user: 「给救援加一个支援目录项，走正规的呼叫支援流程」):
python tests/rescue_support_guard.py [--root DIR], exit 1 on a failure. What it holds in place:
  - nothing makes a helicopter with an empty or dummy seat for the rescue: the old spawner (HeliLaunch, a 410 made 60 m
    over the carrier deck with SpawnJet's seat path, its seats empty since the real crews came in) is gone, and the
    rescue's code (src/heli.cpp) makes no object at all;
  - the rescue's trigger asks for the support catalog's rescue entry (SupportRescueAt) and its heli is only ever the one
    the support deployment hands over (RescueHeliDeployed), after the deployment seated its real pilot;
  - the rescue entry is the catalog's last (older indices keep their wire meaning), carries its pilot and one door gunner
    (the other door is the swimmer's), and is planned only when every peer announced it (the hello's extension word,
    kExtSeaRescue: the capability bits 1|2|4|8 are #100's and full);
  - 2026-10-10: it is held to no mission support rule, never queued behind, cooled down by or written over the map's
    support; it takes off from a carrier's deck before the edge; its heli is the requester's (no nearest-swimmer search).
Run against the code before this change it fails (StartRescue called HeliLaunch).
"""
from __future__ import annotations

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from online_gate_guard import body, code_only, before  # noqa: E402

failures: list[str] = []


def fail(message: str) -> None:
    failures.append(message)


def read(root: str, rel: str) -> str:
    with open(os.path.join(root, rel), encoding='utf-8') as f:
        return f.read()


# Calls that make an object or seat a rider on their own: none belongs in the rescue (src/heli.cpp).
MAKERS = ('HeliLaunch(', 'SpawnJet(', 'CreateJet(', 'PrepareSupportAircraft(', 'SeatNpcRider(', 'RideAi(', 'kCopyHere')


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    root = parser.parse_args().root
    checks = 0
    for folder in ('src',):
        for name in sorted(os.listdir(os.path.join(root, folder))):
            if name.endswith(('.cpp', '.h', '.inc')) and 'HeliLaunch' in code_only(read(root, f'{folder}/{name}')):
                fail(f'{folder}/{name}: HeliLaunch is back (a heli made with an empty seat for the rescue)')
    checks += 1
    heli = code_only(read(root, 'src/heli.cpp'))
    for maker in MAKERS:
        checks += 1
        if maker in heli:
            fail(f'src/heli.cpp: {maker} in the heli module (the rescue must not make or seat anything itself)')
    start = body(heli, 'void StartRescue(')
    checks += 1
    if 'SupportRescueAt(' not in start:
        fail('src/heli.cpp StartRescue: the trigger does not ask for the support catalog\'s rescue entry (SupportRescueAt)')
    # The rescue's heli is the deployment's: its vehicle set only where the dispatcher hands it over.
    handover = body(heli, 'void RescueHeliDeployed(')
    checks += 1
    if not handover or 'r->vehicle=vehicle' not in handover or 'call.vehicle=vehicle' not in handover:
        fail('src/heli.cpp RescueHeliDeployed: the hand-over no longer takes the deployment\'s heli')
    outside = heli.replace(handover, '') if handover else heli
    checks += 1
    if re.search(r'(?:\.|->)vehicle=(?!=)', outside.split('// ---- Sea rescue')[-1] if '// ---- Sea rescue' in outside else outside):
        fail('src/heli.cpp: a rescue heli taken from somewhere other than the deployment\'s hand-over')
    dispatch = code_only(read(root, 'src/support_dispatch.cpp'))
    spawn = body(dispatch, 'bool SpawnDeployment(')
    checks += 1
    if not before(spawn, 'BoardAirborne(', 'RescueHeliDeployed('):
        fail('src/support_dispatch.cpp SpawnDeployment: the rescue gets its heli before the real pilot is seated')
    keys = re.search(r'static const wchar_t\* keys\[\]=\{([^}]*)\}', dispatch)
    checks += 1
    if not keys or re.findall(r'L"(\w+)"', keys.group(1))[-1:] != ['RESCUE']:
        fail('src/support_dispatch.cpp SupportCallKey: RESCUE is not the catalog\'s last entry (older indices would move)')
    crew = body(dispatch, 'unsigned AirCrew(')
    checks += 1
    if 'if(IsRescue(catalog))return 2u;' not in crew:
        fail('src/support_dispatch.cpp AirCrew: the rescue heli is not its pilot and one door gunner (one door is the swimmer\'s)')
    plan = body(dispatch, 'support_net::PlanResult Plan(')
    checks += 1
    if not before(plan, 'SupportPeersAcceptRescue()', 'PlanAirSupport('):
        fail('src/support_dispatch.cpp Plan: the rescue is planned without every peer\'s kExtSeaRescue')
    checks += 1
    if '!IsRescue(catalog) && !support::Allowed(' not in plan or '!IsRescue(plan.catalogId) && !support::Allowed(' not in body(dispatch, 'bool Validate('):
        fail('src/support_dispatch.cpp Plan / Validate: the rescue is held to the mission\'s support rules')
    checks += 1
    if not before(plan, 'PlanTakeoffSupport(', 'PlanAirSupport('):
        fail('src/support_dispatch.cpp Plan: the rescue does not try its takeoff point before the edge')
    own = body(dispatch, 'bool RescueRequest(') + body(dispatch, 'bool RescueHere(')
    checks += 1
    if not own or any(word in own for word in ('offlinePending', 'callAt', 'Status(', 'localRequest')):
        fail('src/support_dispatch.cpp RescueRequest: the rescue shares the map\'s queue, cooldown, status or request notice')
    checks += 1
    if 'SwimmerNear' in heli or 'kSwimmerClaim' in heli or 'r->swimmer=requester' not in handover:
        fail('src/heli.cpp: the rescue heli looks for the nearest swimmer instead of its requester')
    protocol = code_only(read(root, 'src/support_protocol.h'))
    checks += 1
    if not re.search(r'kExtensions=[^;]*kExtSeaRescue', protocol) or 'kCapSeaRescue' in protocol:
        fail('src/support_protocol.h: the sea rescue is not an extension bit of this build (kExtSeaRescue in kExtensions), '
             'or a capability bit claims it (the capability bits are #100\'s: kCapTransports 4, kCapLoadout 8)')
    hello = body(code_only(read(root, 'src/support_protocol.cpp')), 'void Session::Tick(')
    checks += 1
    if not before(hello, 'backend_.hooks.variants(', 'm.unit.resourceId|=kExtSeaRescue;'):
        fail('src/support_protocol.cpp Tick: the hello does not announce kExtSeaRescue after the variants hook wrote its word')
    net = code_only(read(root, 'src/support_net.cpp'))
    checks += 1
    if 'PeersHaveExtension(support_net::kExtSeaRescue)' not in body(net, 'bool SupportPeersAcceptRescue('):
        fail('src/support_net.cpp SupportPeersAcceptRescue: not the peers\' kExtSeaRescue')
    checks += 1
    if 'kFeatures' not in body(code_only(read(root, 'src/support_protocol.cpp')), 'std::uint32_t Session::PeersBehind(') or \
            's.mine=support_net::kFeatures' not in net:
        fail('the version notice compares capabilities only: an older guest without the rescue extension would look the same')
    for message in failures:
        print('FAIL', message)
    print(f'rescue_support_guard: {checks} checks, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
