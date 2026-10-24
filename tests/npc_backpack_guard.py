"""Source guard of the NPCs' backpack weapons (docs/npc-backpack-re.md): python tests/npc_backpack_guard.py [--root DIR],
exit 1 on a failure. What it holds in place:
  - npc_backpack.cpp: ready only after every signature of npc_backpack_signatures.inc matched; NpcPackUse never on a player,
    its gates before its writes (the use going on never cut), the four writes in the game's order, the message only from
    this machine's registered soldier (InSession, +0x128 local), no session function of its own (online_authority.h);
  - npcai.cpp: InstallNpcAi installs it; Backpack runs from Drive alone (never for a script's unit), only with the held
    weapon known, behind NpcBackpack; a backpack weapon is out of ArmsOf's arms; while a use goes on the main trigger and the
    picks are off; Think still drives only the soldiers this machine runs (IsOnlineAuthority);
  - the host takes the pack off a soldier for a room with an older build (SupportPeersApplyBackpacks, kExtBackpack announced
    by every hello and named by the version notice);
  - the generator puts the weapon in a sub weapon slot, out of the weapon sets; the room menu's new field is a 4th, its first
    three as they were;
  - the ini key is read, shipped and documented; the offline check runs under CTest.
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
    return '\n'.join(re.sub(r'//.*', '', line) for line in text.splitlines())


def read(root: str, rel: str) -> str:
    with open(os.path.join(root, rel), encoding='utf-8') as f:
        return f.read()


def body(text: str, signature: str) -> str:
    at = text.find(signature)
    while at >= 0:
        open_at = text.find('{', at)
        if open_at < 0:
            return ''
        if ';' not in text[at:open_at]:
            break
        at = text.find(signature, at + 1)
    if at < 0:
        return ''
    depth, i = 0, text.find('{', at)
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return ''


def before(text: str, *parts: str) -> bool:
    at = -1
    for p in parts:
        nxt = text.find(p, at + 1)
        if nxt < 0 or nxt <= at:
            return False
        at = nxt
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    root = parser.parse_args().root
    checks = 0

    native = code_only(read(root, 'src/npc_backpack.cpp'))
    install = body(native, 'bool InstallNpcBackpack() noexcept')
    checks += 1
    if not before(install, 'ready=false;', 'for(const auto& s:kSignatures)', 'Matches(s.rva,s.bytes,s.size)', 'return false;', 'ready=true;'):
        fail('npc_backpack.cpp InstallNpcBackpack: ready before every signature matched')
    checks += 1
    if '#include "npc_backpack_signatures.inc"' not in native:
        fail('npc_backpack.cpp: the signatures are not the generated ones')
    use = body(native, 'bool NpcPackUse(unsigned char* h,int slot) noexcept')
    prepare = body(native, 'bool PrepareUse(unsigned char* h,int slot,UseState* out) noexcept')
    can_use = body(native, 'bool NpcPackCanUse(unsigned char* h,int slot) noexcept')
    checks += 1
    if 'if(!ready || !h || IsAnyPlayer(h))return false;' not in prepare or 'PrepareUse(h,slot,&use)' not in can_use:
        fail('NpcPackUse: a player (whose own input does this) or an unverified profile goes on')
    checks += 1
    gates = ('if(At<const void*>(set,kSetSub))return false;', 'if(h[kStateBits]&kStateNoPack)return false;',
             'if(At<std::uint32_t>(h,kHumanState)&kStateFree)return false;',
             'if(At<const void*>(h,kSubLock) && At<std::int32_t>(h,kSubLockCount)>=1)return false;',
             'if(!(At<std::uint32_t>(h,kControlMask)&MaskOf(slot)))return false;',
             'if(At<std::uint32_t>(weapon,kWeaponAmmo)==0)return false;')
    writes = ('Put<std::uint16_t>(*held,kMainBusy,0);', 'Put<void*>(set,kSetSub,use.sub);',
              'Fn<GoFn>(kMachineGo)(set+kSetMachine,image+kSubState,0);', 'h[kSubChain]=0;')
    if not before(prepare, *gates) or not before(use, 'if(!PrepareUse(h,slot,&use))return false;', *writes):
        fail('NpcPackUse: not every gate before the writes, or the writes not in the game\'s order (0x5A1FC1..0x5A1FF8)')
    checks += 1
    if not before(use, 'h[kSubChain]=0;', 'InSession() && (At<std::uint16_t>(h,kNetFlags)&(online::kNetRemote|online::kNetLocal))==online::kNetLocal',
                  'SendUse(h,slot)'):
        fail('NpcPackUse: the message 0x10 not only from this machine\'s registered soldier, or before the use')
    checks += 1
    if any(rva in native for rva in ('0x7748F0', '0x784210', '0x630F90', '0x630DF0')):
        fail('npc_backpack.cpp asks a session function itself (online_authority.h is the one place)')
    send = body(native, 'void SendUse(unsigned char* h,int slot)')
    checks += 1
    if not before(send, 'write(stream.bytes,kMessageSub)', 'write(stream.bytes,static_cast<std::int8_t>(slot))', '[kSlotSend])(net,stream.bytes,0)'):
        fail('SendUse: not (0x10, slot) through slot 14 as 0x5A2030..0x5A2060')

    npcai = code_only(read(root, 'src/npcai.cpp'))
    checks += 1
    if 'InstallNpcBackpack();' not in body(npcai, 'bool InstallNpcAi() noexcept'):
        fail('InstallNpcAi does not install the backpack')
    drive = body(npcai, 'Plan Drive(Soldier& s,unsigned char* h,const SoldierClass& c,const Arms& a,')
    scripted = body(npcai, 'Plan Scripted(Soldier& s,unsigned char* h,const Arms& a,const float* eye,const float* pos) noexcept')
    checks += 1
    if 'if(a.held[0]>=0){Backpack(s,h,t.e,eye,mask,ms);p.fire=p.fire && h[kTrigger]!=0;}' not in drive or 'Backpack(' in scripted:
        fail('Backpack runs outside Drive (a script\'s unit), or without the held weapon known')
    checks += 1
    if npcai.count('Backpack(s,h,') != 1:
        fail('Backpack is called from more than Drive')
    backpack = body(npcai, 'void Backpack(Soldier& s,unsigned char* h,const Enemy* t,const float* eye,unsigned mask,ULONGLONG ms) noexcept')
    checks += 1
    if not before(backpack, 'if(!Cfg().npcBackpack || !NpcBackpackReady())return;', 'NpcPackSlots(h,slots,2)'):
        fail('Backpack: not behind NpcBackpack and the verified profile')
    checks += 1
    if not before(backpack, 'if(NpcPackBusy(h) || frame<s.packHoldUntil) {', 'h[kTrigger]=0;h[kPickWeapon]=h[kPickWeapon+1]=h[kPickWeapon+2]=0;', 'return;'):
        fail('Backpack: the main trigger and the picks not off while a use goes on')
    checks += 1
    if not before(backpack, 'if(!NpcPackCanUse(h,slots[i].slot))continue;', 'npcpack::Fits(pack,target,&pitch)', 'npcpack::ArcClear(', 'Look(h,dir);',
                  'npcpack::AimOn(', 'NpcPackUse(h,slots[i].slot)'):
        fail('Backpack: the look before native readiness / its whole arc checked, or the use before its look')
    checks += 1
    if 'npc::ShotClear(eye,t->aim,kSpread,0.0f,fr,friends)' not in backpack:
        fail('Backpack: a direct weapon\'s line not tested for friends')
    arms = body(npcai, 'Arms ArmsOf(const unsigned char* h) noexcept')
    checks += 1
    if 'NpcPackIndex(h,i)' not in arms:
        fail('ArmsOf: a backpack weapon counted among the arms it picks and fires')
    think = body(npcai, 'void Think(unsigned char* h,int cls) noexcept')
    checks += 1
    if '!IsOnlineAuthority(h)' not in think:
        fail('Think drives soldiers another machine runs')

    dispatch = code_only(read(root, 'src/support_dispatch.cpp'))
    soldier = body(dispatch, 'std::uint64_t SoldierVariant(std::uint32_t catalog,const SupportLoadout& load,int i,std::uint32_t resource) noexcept')
    checks += 1
    if not before(soldier, 'if(look.pack && !SupportPeersApplyBackpacks()) {', 'look.pack=0;', 'if(!look.On())return 0;', 'DecideVariant('):
        fail('SoldierVariant: a pack sent to a room with an older build (its Validate refuses the whole plan)')
    protocol = code_only(read(root, 'src/support_protocol.h'))
    checks += 1
    if not re.search(r'kExtensions=[^;]*kExtBackpack', protocol) or not re.search(r'kExtBuilt=[^;]*kExtBackpack[|;]', protocol):
        fail('support_protocol.h: kExtBackpack not announced by every hello')
    net = code_only(read(root, 'src/support_net.cpp'))
    checks += 1
    if 'PeersHaveExtension(support_net::kExtBackpack)' not in body(net, 'bool SupportPeersApplyBackpacks() noexcept') or \
            '{FeatureExt(support_net::kExtBackpack),Tx::versionFeatBackpack}' not in net:
        fail('support_net.cpp: the peers\' kExtBackpack not asked, or the version notice does not name it')

    loadout = read(root, 'tools/support_loadout.py')
    with_pack = loadout.split('def with_pack(', 1)[1].split('\ndef ', 1)[0] if 'def with_pack(' in loadout else ''
    checks += 1
    if not all(p in with_pack for p in ("loads.items.append(pack_path(carried))", "int(i) < index",
                                         "doc.root.set('soldier_sub_weapon_slot', dsgo.Node([dsgo.Node([float(index), 'weapon', 0.0])]))")):
        fail('tools/support_loadout.py with_pack: the weapon not in a sub slot of its own, out of the weapon sets')
    menu = code_only(read(root, 'src/room_menu.h'))
    change = body(menu, 'inline void ChangeSoldier(Menu& m,Unit& u,int step) noexcept')
    checks += 1
    if not before(change, 'if(m.fieldAt==0) {', 'else if(m.fieldAt==1)l.primary=', 'else if(m.fieldAt==2)l.secondary=', 'else l.pack='):
        fail('room_menu.h: the backpack field is not a 4th one after kind / primary / secondary')
    checks += 1
    if 'kPackField=3;' not in menu:
        fail('room_menu.h: kPackField is not the 4th field')

    plugin, ini, readme = read(root, 'src/plugin.cpp'), read(root, 'EDF6VehicleCrew.ini'), read(root, 'README.md')
    doc, cmake = read(root, 'docs/npc-backpack-re.md'), read(root, 'CMakeLists.txt')
    checks += 1
    if 'ReadBool(L"NpcBackpack",n.npcBackpack)' not in plugin or not re.search(r'^NpcBackpack=1\s*$', ini, re.M) or \
            'NpcBackpack' not in readme or 'NpcBackpack' not in doc:
        fail('NpcBackpack: not read, shipped or documented')
    checks += 1
    if 'EXCLUDE_FROM_ALL tools/npc_backpack_check.cpp' not in cmake or \
            'npc_backpack_check' not in cmake.split('set(EDF6_OFFLINE_CHECKS', 1)[1].split(')', 1)[0] or \
            'src/npc_backpack.cpp' not in cmake:
        fail('CMakeLists.txt: the backpack not built or its offline check not run')

    for f in failures:
        print('FAIL', f)
    print(f'npc_backpack_guard: {checks} checks, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
