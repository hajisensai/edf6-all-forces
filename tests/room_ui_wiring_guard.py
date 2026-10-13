"""src/room_ui.cpp's switches and screen identity, from its source (no game needed; docs/feature-2026-10-12-room-loadout-menu.md
阶段 R): RoomLoadout=0 at load installs nothing; a frame with the menu off passes the screen straight through before any
key, input or pre-sync call; the room pre-sync is ticked only when room_ui_logic.h PlanSwitches says so and withdrawn
only on its leave / the screen being forgotten; the screen's identity ends with its destructor (both destructor slots
chained, a followed screen forgotten once its destructor ran even when the next one has its address)."""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, 'src', 'room_ui.cpp'), encoding='utf-8').read()
logic = open(os.path.join(ROOT, 'src', 'room_ui_logic.h'), encoding='utf-8').read()
plugin = open(os.path.join(ROOT, 'src', 'plugin.cpp'), encoding='utf-8').read()
crew = open(os.path.join(ROOT, 'src', 'crew.h'), encoding='utf-8').read()
ini = open(os.path.join(ROOT, 'EDF6VehicleCrew.ini'), encoding='utf-8-sig').read()
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def body(text: str, head: str) -> str:
    """The braces-balanced body of the function whose definition starts with `head`."""
    at = text.index(head)
    start = text.index('{', at)
    depth = 0
    for i in range(start, len(text)):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        if depth == 0:
            return text[start:i + 1]
    raise AssertionError(head)


install = body(src, 'bool InstallRoomUi() noexcept')
off_at = install.find('if(!Cfg().roomLoadout)')
check(off_at >= 0 and off_at < install.find('AllMatch(') and off_at < install.find('InstallPreviewHooks') and
      off_at < install.find('Chain('), 'RoomLoadout=0 at load: InstallRoomUi returns before checking or patching anything')
check('return false;' in install[off_at:install.find('}', off_at) + 1], '...returning false')

frame = body(src, 'std::uint64_t Frame(Screen& screen,unsigned char* self,void* ctx) noexcept')
pass_at = frame.find('if(sw.pass)return Off(screen,self,ctx);')
check(pass_at >= 0, 'an off frame goes to Off')
for call in ('ReadRaw(', 'kGetInput', 'TickRoomSync', 'rm::Step', 'TakeInput', 'FollowPreview', 'DrawMenu'):
    at = frame.find(call)
    check(at > pass_at, f'{call} only after the off frame has returned')
check(frame.find('PlanSwitches(st.switches,on,Cfg().roomPreSync,screen.room)') < pass_at, 'the switches planned first')
check(src.count('roomsync::TickRoomSync()') == 1 and 'if(sw.tick)roomsync::TickRoomSync();' in frame,
      'the pre-sync ticks only where PlanSwitches says (never on a bare screen.room)')
check(src.count('roomsync::LeaveRoomSync()') == 2 and re.search(r'if\(sw\.leave\) \{[^}]*roomsync::LeaveRoomSync\(\);\s*roomsync::ForgetRoomHostList\(\);', frame) is not None and
      'if(LeaveOnForget(st.switches))roomsync::LeaveRoomSync();' in src, 'withdrawn only on the plan\'s leave or a forgotten syncing screen')
off = body(src, 'std::uint64_t Off(Screen& screen,unsigned char* self,void* ctx) noexcept')
for call in ('ReadRaw(', 'kGetInput', 'TickRoomSync', 'rm::Step', 'TakeInput', 'DrawMenu', 'ShowPreview', 'memset'):
    check(call not in off, f'an off frame does not call {call}')
check('screen.next(self,ctx)' in off and 'if(st.drew)Draw(self,false);' in off,
      'an off frame chains with the real ctx and only takes back what it drew')
check('st.drew=true' in frame, 'a drawn screen remembered (else an off frame never touches it)')

follow = body(src, 'void Follow(unsigned char* self,bool room) noexcept')
check(follow.find('followedGone.exchange(false)') < follow.find('if(self==st.self)return;'),
      'a followed screen whose destructor ran is forgotten before the pointer is compared')
check('followed.store(self);' in follow, 'the followed screen published for the destructor hooks')
check('void NoteGone(unsigned char* self) noexcept' in src and 'followed.load()==self)followedGone.store(true)' in src,
      'the destructor hook marks the followed screen gone, nothing else')
chain = body(src, 'bool Chain(Screen& s,void* hook,void* goneHook) noexcept')
check(chain.find('ChainGone(s,goneHook)') < chain.find('PatchVtableSlot'), 'the destructor chained before the OnUpdate')
check('&RoomGoneHook' in install and '&HqGoneHook' in install, 'both destructors chained')
pending_coop = body(src, 'bool CoopStillToLoad() noexcept')
check('!HasDllExport(path,"EDF6CoopGetMemberAttributeApi")' in pending_coop and
      '!GetModuleHandleW(L"EDF6Coop.dll")' in pending_coop,
      'reverse load order: new Coop ABI allowed, only unloaded old/unknown Coop refused')
check('s.next=reinterpret_cast<UpdateFn>(current);' in chain and 'screen.next(self,copy)' in frame,
      'the room chains the current slot, including a Coop already loaded')

# A menu save is one write and one reload (not one per key on the UI thread), all lines or none.
save = body(src, 'void Save() noexcept')
check(save.count('SaveSupportLoadouts(') == 1 and 'SaveSupportLoadout(' not in save, 'the menu saves every line in one call')
check(save.index('SaveSupportLoadouts(') < save.index('MarkItemSaved'), 'lines marked saved only after the one write succeeded')
store = open(os.path.join(ROOT, 'src', 'loadout_store.cpp'), encoding='utf-8').read()
many = body(store, 'bool SaveSupportLoadouts(')
check(many.count('ReadFileBytes(') == 1 and many.count('ReplaceFileAtomic(') == 1 and many.count('ReloadConfigNow()') == 1 and
      'for(' not in many.split('ReadFileBytes(')[1].split('ReloadConfigNow()')[0].split('EditLoadoutIniAll(')[0],
      'SaveSupportLoadouts: one read, one atomic write, one reload')
check(many.index('EditLoadoutIniAll(') < many.index('ReplaceFileAtomic('), 'every line checked before anything is written')
one = body(store, 'bool SaveSupportLoadout(LoadoutField')
check('SaveSupportLoadouts(&one,1,' in one and 'ReplaceFileAtomic' not in one, 'a single save is a save of one')
# The mission's preload: exactly what plans can ask for (support_variants_logic.h MissionVariants), never a directory scan.
variants = open(os.path.join(ROOT, 'src', 'support_variants.cpp'), encoding='utf-8').read()
preload = body(variants, 'void PreloadSupportVariants() noexcept')
check('FindFirstFile' not in variants and 'FindNextFile' not in variants, 'no directory scan of Mods/OBJECT')
check('variants::MissionVariants(&wanted' in preload and 'ForEachConfiguredVariant' in preload and
      'roomsync::ForEachRoomHostVariant' in preload and 'PendingText()' in preload,
      'the preload set: configuration + the room host\'s list + the pending list')
check('variants::HelloBloom(ready,&hostList,bloom)' in preload, 'the hello names the host list\'s files when there is one')
sync = open(os.path.join(ROOT, 'src', 'room_sync.cpp'), encoding='utf-8').read()
check('engine.Tick(Api(),GetTickCount64(),TheEnv());PublishHostList();' in sync and 'AcquireSRWLockShared(&hostLock)' in sync,
      'the host list handed to the mission start under a lock (another thread)')
check('struct Switches' in logic and 'inline SwitchPlan PlanSwitches(' in logic, 'room_ui_logic.h PlanSwitches')
check('bool roomPreSync=true;' in crew and 'n.roomPreSync=ReadBool(L"RoomPreSync",n.roomPreSync);' in plugin,
      'RoomPreSync read, default 1')
check(re.search(r'^RoomPreSync=1\r?$', ini, re.M) is not None, 'the shipped ini has RoomPreSync=1 (installers add it to old inis)')
print(f'room_ui_wiring_guard: {checks} checks passed')
