"""The room / HQ menu's native dialogs (src/room_dialog.cpp, docs/room-native-dialog-re.md) against the game's EDF.dll
and Root.cpk, read as files (no game run): the signatures file is EDF.dll's code, the builder the plugin copies calls
what room_dialog.cpp calls in that order, the data it points at, and the dialog layout is a HUiDialogBox with a text and
a button group. Without the game: the source contracts only.

    python tests/room_dialog_native_audit.py
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


def calls(img: bytes, site: int, callee: int) -> bool:
    return img[site] == 0xE8 and site + 5 + struct.unpack_from('<i', img, site + 1)[0] == callee


def rip(img: bytes, site: int, length: int, disp_at: int) -> int:
    return site + length + struct.unpack_from('<i', img, site + disp_at)[0]


def audit_source() -> None:
    src = open(os.path.join(ROOT, 'src', 'room_dialog.cpp'), encoding='utf-8').read()
    import room_dialog_sigs as sigs
    for name, value in (('kParamCtor', 0x8C320), ('kParamVtable', 0x17621F8), ('kParamBaseDtor', 0x8E540),
                        ('kItemsDtor', 0xA9460), ('kItemDtor', 0x8E170), ('kShow', 0x119C600), ('kClose', 0x7E2EB0),
                        ('kLayoutName', sigs.LAYOUT_NAME_POINTER), ('kLayoutText', sigs.LAYOUT_NAME),
                        ('kLambdaVtable', sigs.LAMBDA_VTABLE)):
        m = re.search(rf'\b{name}=0x([0-9A-F]+)', src)
        check(bool(m) and int(m.group(1), 16) == value, f'room_dialog.cpp {name} = {value:#x}')
    for field, value in (('kParamTitle', 0x108), ('kParamFlags', 0x128), ('kParamCursor', 0x148), ('kParamItems', 0x150),
                         ('kItemSize', 0x90), ('kItemImpl', 0x58), ('kItemTree', 0x80)):
        m = re.search(rf'\b{field}=0x([0-9A-F]+)', src)
        check(bool(m) and int(m.group(1), 16) == value, f'room_dialog.cpp {field} = {value:#x}')
    logic = open(os.path.join(ROOT, 'src', 'room_dialog_logic.h'), encoding='utf-8').read()
    check('kItemsMost=kPerPage+4' in logic, 'a page: its entries, previous, next, one action, back')
    ui = open(os.path.join(ROOT, 'src', 'room_ui.cpp'), encoding='utf-8').read()
    check('nativedialog::CloseDialog(st.dialog)' in ui.split('void ForgetMenu()', 1)[1].split('}', 1)[0],
          'a screen forgotten closes the dialog over it')
    check('nativedialog::CloseDialog(st.dialog)' in ui.split('std::uint64_t Off(', 1)[1].split('}', 1)[0],
          'the menu switched off closes its dialog')


def audit_code(img: bytes, base: int) -> None:
    import room_dialog_sigs as sigs
    with open(sigs.INC, encoding='utf-8') as f:
        check(f.read() == sigs.render(img), 'room_dialog_signatures.inc is EDF.dll\'s code (rerun tools/room_dialog_sigs.py)')
    # The builder (0x8AB310) in the order room_dialog.cpp OpenDialog repeats it.
    check(calls(img, 0x8AB5B0, 0x8C320), 'the param made by 0x8C320')
    check(rip(img, 0x8AB5B5, 7, 3) == sigs.PARAM_VTABLE, 'then given the vtable 0x17621F8')
    check(rip(img, 0x8AB5FE, 7, 3) == 0x1765B30, '+0x130 from 0x1765B30')
    check(calls(img, 0x8AB689, 0x3D450), "an item's text assigned by 0x3D450")
    check(calls(img, 0x8AB6DA, 0x12D85B0), "an item's tree head from 0x12D85B0")
    check(calls(img, 0x8ABB02, 0xB91D0) and calls(img, 0x8ABB31, 0x89950), 'pushed: reserve 0xB91D0, copy 0x89950')
    check(calls(img, 0x8ABBC5, 0x3D450), 'the message assigned by 0x3D450')
    check(rip(img, 0x8ABBE8, 7, 3) == 0x20B2960, 'the system from [0x20B2960]')
    check(rip(img, 0x8ABC05, 7, 3) == sigs.LAYOUT_NAME_POINTER, 'the layout name from [0x1F77810]')
    check(calls(img, 0x8ABC10, 0x119C600), 'shown by 0x119C600')
    check(rip(img, 0x8ABC4D, 7, 3) == 0x8E170, 'the temporaries destroyed by 0x8E170')
    check(calls(img, 0x8ABC73, 0xA9460) and calls(img, 0x8ABD3F, 0x8E540), 'then the vector 0xA9460 and the param 0x8E540')
    check(struct.unpack_from('<Q', img, sigs.LAYOUT_NAME_POINTER)[0] - base == sigs.LAYOUT_NAME,
          '[0x1F77810] points at the layout name')
    check(img[sigs.LAYOUT_NAME:sigs.LAYOUT_NAME + 58].decode('utf-16le') == 'APP:/UI/LYT_HUIDIALOGBOX.SGO\0', 'the layout name')
    # RTTI: the param's vtable is HUiDialogBox::InitParam's
    col = struct.unpack_from('<Q', img, sigs.PARAM_VTABLE - 8)[0] - base
    td = struct.unpack_from('<I', img, col + 12)[0]
    check(img[td + 0x10:td + 0x10 + 32].split(b'\0')[0] == b'.?AUInitParam@HUiDialogBox@ui@@', 'vtable 0x17621F8 is InitParam')
    # The lambda whose _Target_type / _Delete_this / _Get ours borrows: _Copy is vtable + 8-byte capture (16 bytes)
    copy = struct.unpack_from('<Q', img, sigs.LAMBDA_VTABLE)[0] - base
    check(copy == 0x8AD410 and img[copy + 7:copy + 0x16] == bytes.fromhex('488902488b410848894208488bc2c3'),
          'the stock lambda copies its vtable and 8 bytes')
    # The item handed to the stock lambda's call: rdx -> [rdx] the dialog, closed by 0x7E2EB0 (0x8AD560 .. 0x8AD5B0)
    check(img[0x8AD560:0x8AD563] == bytes.fromhex('4c8b02') and img[0x8AD5AD:0x8AD5B0] == bytes.fromhex('498bc8') and
          img[0x8AD5B0] == 0xE9 and 0x8AD5B5 + struct.unpack_from('<i', img, 0x8AD5B1)[0] == 0x7E2EB0,
          'the stock pick: 0x7E2EB0([arg])')


def audit_layout(game) -> None:
    import sgo
    _v, m = sgo.read(game.read('UI', 'LYT_HUIDIALOGBOX.SGO'))
    check(m.get('xgs_ui_object_class') == 'HUiDialogBox', 'LYT_HUIDIALOGBOX is a HUiDialogBox')
    check('HUiMessageBoxDialogText' in m and 'HUiMessageBoxDialogButtonGrp' in m and '_HUiButton' in m,
          'its message text, its button group and its button template')
    check(m.get('default_cancel_action') is not None, 'its own cancel (the plugin learns it from the dialog ending unpicked)')


def main() -> int:
    audit_source()
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if dll and os.path.isfile(dll):
        import room_preview_sigs
        import rootcpk
        pe, img = room_preview_sigs.image(dll)
        audit_code(img, pe.OPTIONAL_HEADER.ImageBase)
        audit_layout(rootcpk.Game(game_dir))
    else:
        print('room_dialog_native_audit: no game; EDF.dll / Root.cpk checks skipped')
    print(f'room_dialog_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
