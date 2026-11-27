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
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import gen_config  # noqa: E402

# with the config.inc sections they include (tools/gen_config.py expand: the ini's keys)
plugin = gen_config.read_expanded('src/plugin.cpp')
crew = gen_config.read_expanded('src/crew.h')
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
off_at = install.find('if(!Cfg().roomLoadout && !Cfg().hqAutoMissionSelect)')
check(off_at >= 0 and off_at < install.find('AllMatch(') and off_at < install.find('InstallPreviewHooks') and
      off_at < install.find('Chain('),
      'RoomLoadout=0 and HqAutoMissionSelect=0 at load: InstallRoomUi returns before checking or patching anything')
check('return false;' in install[off_at:install.find('}', off_at) + 1], '...returning false')
# RoomLoadout=0 with HqAutoMissionSelect=1: only the HQ chained, by the hook that runs nothing of the menu.
alone_at = install.find('if(!Cfg().roomLoadout) {')
alone = install[alone_at:install.find('}', alone_at) + 1]
check(alone_at > 0 and alone_at < install.find('InstallPreviewHooks') and alone_at < install.find('InstallNativeDialog') and
      alone_at < install.find('nativeEntryOk=') and alone_at < install.find('padOk=') and
      'Chain(screens[1],reinterpret_cast<void*>(&HqAutoHook),reinterpret_cast<void*>(&HqGoneHook))' in alone and
      'screens[0]' not in alone and 'return hq;' in alone, 'RoomLoadout=0: the HQ alone, before any of the menu is installed')
auto_hook = body(src, 'std::uint64_t __fastcall HqAutoHook(unsigned char* self,void* ctx)')
check('screens[1].next(self,ctx)' in auto_hook and 'Frame(' not in auto_hook and 'AutoOpenMission(self,ctx,false)' in auto_hook,
      'the HQ-alone hook chains with the real ctx and only opens the mission select')
hq_hook = body(src, 'std::uint64_t __fastcall HqHook(unsigned char* self,void* ctx)')
check(hq_hook.find('Frame(screens[1],self,ctx)') < hq_hook.find('AutoOpenMission(self,ctx,st.menu.open || st.dialog.control || st.gate.latched || st.armoryTake || armoryui::IsOpen())'),
      'the HQ opens its mission select after its frame, never with 整备 / the 对战 pages (or a closing key) up')
check('AutoGone(self)' in body(src, 'void* __fastcall HqGoneHook(unsigned char* self,unsigned flags)') and
      'AutoGone' not in body(src, 'void* __fastcall RoomGoneHook(unsigned char* self,unsigned flags)'),
      "the HQ's destructor ends its 'tried' mark (a new HQ at its address opens again)")
auto = body(src, 'void AutoOpenMission(unsigned char* self,void* ctx,bool menuBusy) noexcept')
check(auto.find('AutoOpenDue(') < auto.find('autoTried.store(self)') < auto.find('Press(self,button)') and
      'Cfg().enabled && Cfg().hqAutoMissionSelect' in auto, 'tried once per HQ object, before the press, with both switches')
check('bool hqAutoMissionSelect=true;' in crew and 'n.hqAutoMissionSelect=ReadBool(L"HqAutoMissionSelect",n.hqAutoMissionSelect);' in plugin,
      'HqAutoMissionSelect read, default 1')
check(re.search(r'^HqAutoMissionSelect=1\r?$', ini, re.M) is not None, 'the shipped ini has HqAutoMissionSelect=1')

frame = body(src, 'std::uint64_t Frame(Screen& screen,unsigned char* self,void* ctx) noexcept')
pass_at = frame.find('if(sw.pass)return Off(screen,self,ctx);')
check(pass_at >= 0, 'an off frame goes to Off')
for call in ('ReadRaw(', 'kGetInput', 'TickRoomSync', 'armoryui::Frame(', 'TakeInput', 'OpenArmory(', 'DriveDialog('):
    at = frame.find(call)
    check(at > pass_at, f'{call} only after the off frame has returned')
check(frame.find('PlanSwitches(st.switches,on,Cfg().roomPreSync,screen.room)') < pass_at, 'the switches planned first')
check(src.count('roomsync::TickRoomSync()') == 1 and 'if(sw.tick)roomsync::TickRoomSync();' in frame,
      'the pre-sync ticks only where PlanSwitches says (never on a bare screen.room)')
check(src.count('roomsync::LeaveRoomSync()') == 2 and re.search(r'if\(sw\.leave\) \{[^}]*roomsync::LeaveRoomSync\(\);\s*roomsync::ForgetRoomList\(\);', frame) is not None and
      'if(LeaveOnForget(st.switches))roomsync::LeaveRoomSync();' in src, 'withdrawn only on the plan\'s leave or a forgotten syncing screen')
off = body(src, 'std::uint64_t Off(Screen& screen,unsigned char* self,void* ctx) noexcept')
for call in ('ReadRaw(', 'kGetInput', 'TickRoomSync', 'armoryui::Frame(', 'OpenArmory(', 'TakeInput', 'ShowPreview', 'memset'):
    check(call not in off, f'an off frame does not call {call}')
check('screen.next(self,ctx)' in off and 'armoryui::Forget();' in off,
      'an off frame chains with the real ctx and closes 整备 (nothing committed)')
# 整备 (armory.h): its input claim joins the gate's, its screen identity follows the destructors and the forgetting.
check(frame.find('st.armoryTake=armoryui::Frame(self,hasInput,down.toggle);') < frame.find('const bool take=') and
      'TakeInput(st.gate,st.menu.open,AnyDown(down)) || st.armoryTake' in frame,
      "整备's frame before the input decision; its claim takes the screen's input")
check(frame.find('const bool take=') < frame.find('screen.next(self,copy)'), 'the input decided before the screen updates')
for hook in ('void* __fastcall RoomGoneHook(unsigned char* self,unsigned flags)', 'void* __fastcall HqGoneHook(unsigned char* self,unsigned flags)'):
    check('armoryui::ScreenGone(self);' in body(src, hook), f'{hook}: 整备 never draws on a destroyed screen')
check('armoryui::Forget();' in body(src, 'void ForgetMenu() noexcept'), 'a forgotten screen closes 整备')
check(install.find('armoryui::Install()') > install.find('if(!Cfg().roomLoadout) {'), '整备 installed only with RoomLoadout=1')
check('roomdialog::Build(' not in src and 'roomdialog::Apply(' not in src and 'OpenMenu' not in src and 'DrawMenu' not in src,
      'the loadout dialog chain and the layout overlay are gone (整备 replaces them)')

follow = body(src, 'bool Follow(unsigned char* self,bool room) noexcept')
check(0 <= follow.find('followedGone.exchange(false)') < follow.find('TrackScreen(track,st.self,self)'),
      'a followed screen whose destructor ran is forgotten before the pointer is compared')
check('followed.store(self);' in follow, 'the followed screen published for the destructor hooks')
# 2026-11-27 (the HQ's pack switch): the old HQ and the new one update together for a while; the replaced object's frames
# pass straight through (room_ui_logic.h ScreenTrack) instead of alternating "replaced by" and building a button each time.
check(re.search(r'case Track::Stale:[^}]*?return false;', follow) is not None and 'ClearRetired(track);' in follow,
      'a retired screen is not followed again (until the followed one ends)')
follow_at = frame.find('if(!Follow(self,screen.room))return screen.next(self,ctx);')
check(follow_at >= 0 and all(follow_at < frame.find(c) for c in ('PvpRoomTick();', 'PlanSwitches(', 'EnsureNativeEntry(self);')),
      "a retired screen's frame chains straight to its own update before anything of the menu")
check('if(self==st.self)AutoOpenMission(self,ctx,' in hq_hook, 'a retired HQ never opens its mission select')
gone = body(src, 'void NoteGone(unsigned char* self) noexcept')
check(gone.find('Unretire(track,self)') < gone.find('followed.load()!=self)return;'),
      'the destructor forgets a retired screen (a new object at its address is a new screen)')
check('followed.load()!=self)return;' in gone and 'followedGone.store(true);' in gone,
      'the destructor hook marks the followed screen gone')
# 2026-10-20 (#218 review): the dialog hangs on the global UI, so the destructor closes it at once, but only on the UI
# thread (the menu's state is that thread's) and nothing else of the menu (preview, room sync: the next frame's Forget)
check(gone.find('GetCurrentThreadId()!=uiThread.load())return;') < gone.find('nativedialog::CloseDialog(st.dialog);') and
      'nativedialog::CloseDialog(st.dialog);' in gone, 'on the UI thread the destructor closes the dialog over the screen')
check('HidePreview' not in gone and 'Forget(' not in gone and 'LeaveRoomSync' not in gone,
      'the destructor touches nothing else of the menu')
check('uiThread.store(GetCurrentThreadId());' in body(src, 'std::uint64_t Frame(Screen& screen,unsigned char* self,void* ctx) noexcept'),
      'the UI thread is the screens\' frames\' thread')
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
presets = open(os.path.join(ROOT, 'src', 'armory_presets.cpp'), encoding='utf-8').read()
check(presets.count('SaveSupportLoadouts(') == 1 and 'SaveSupportLoadout(' not in presets, 'a preset switch saves every line in one call')
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
      'roomsync::ForEachRoomVariant' in preload and 'PendingText()' in preload,
      'the preload set: configuration + the room\'s set (every member\'s loadout) + the pending list')
check('variants::HelloBloom(ready,&hostList,bloom)' in preload, 'the hello names the room set\'s files when there is one')
sync = open(os.path.join(ROOT, 'src', 'room_sync.cpp'), encoding='utf-8').read()
check('engine.Tick(Api(),GetTickCount64(),TheEnv());PublishRoomList();PublishPlayers();PublishMembers();' in sync and
      'AcquireSRWLockShared(&roomLock)' in sync and 'AcquireSRWLockShared(&membersLock)' in sync,
      'the room\'s set and the members\' loadouts handed to the mission start / the host\'s plans under a lock (other threads)')
check('struct Switches' in logic and 'inline SwitchPlan PlanSwitches(' in logic, 'room_ui_logic.h PlanSwitches')
check('bool roomPreSync=true;' in crew and 'n.roomPreSync=ReadBool(L"RoomPreSync",n.roomPreSync);' in plugin,
      'RoomPreSync read, default 1')
check(re.search(r'^RoomPreSync=1\r?$', ini, re.M) is not None, 'the shipped ini has RoomPreSync=1 (installers add it to old inis)')
print(f'room_ui_wiring_guard: {checks} checks passed')
