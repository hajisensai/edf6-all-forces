"""EDF4.1's weapon sounds and voices on EDF6: converted at install time from the player's own EDF4.1 into Mods, so 4.1's
weapons and mission scripts find their sounds and their radio lines under 4.1's own names. 4.1's audio (2.3 MB of weapon
waves, 1.6 GB of voice) cannot ship with the tools: this part needs EDF4.1 installed, or an EDF4.1 asset pack (pylib/edf41src.py: these
same files in the same folders, with a manifest; tools/export_edf41_assets.py makes it); without either nothing is
written and the reason is said.

Four groups (GROUPS), each written whole or not at all:
  weapon     SOUND/PC/EDF41_WEAPON_SE.ACB  4.1's SOUND/PC/TIKYUUX_SE.ACB (a CRI ACB 1.23 with its waves embedded; EDF6's
             runtime keeps a path for banks older than 1.37) with three fields patched in place, all of fixed width:
             AcfMd5Hash = EDF6's (the runtime refuses a bank built against another ACF: EDF6's 76 banks share one hash, 4.1's
             ACF has another), and Name / AcbGuid made our own: 4.1's bank is called 'tikyuuX_SE' with the GUID of EDF6's
             own SE bank, and two banks of one name and GUID are one bank to the engine. 'tikyuu41_W' is as long as the name
             it replaces, so no table moves and no @UTF writer is needed. The cue names stay 4.1's (a weapon SGO names its
             sound by cue); 1087 of the 1469 collide with EDF6's own SE cues, so the plugin loads this bank AFTER the stock
             ones and asks it by name only where the stock banks do not answer (or first, for 4.1 objects).
  dragon     SOUND/PC/EDF41_EN_DRAGON.ACB  4.1's SOUND/PC/TIKYUU4_EN_DRAGON.ACB (ACB 1.22, waves embedded) patched as the weapon
             bank is (OBJECT_BANKS: EDF6's AcfMd5Hash, Name 'tikyuu41_enDragon' as long as 4.1's 'tikyuu4_en_Dragon', a GUID
             of ours: 4.1's bank has the GUID of EDF6's own bank of that name). EDF6's TIKYUU4_EN_DRAGON.ACB was cut for its
             frog dragons: it keeps 4.1's en_DragonBig_v_* voices but not the giant's en_DragonBig_move_* (steps, wing beats,
             landing, the fall) nor its flame hit. DragonBig401's SGO lists this bank in game_sound as
             app:/sound/pc/edf41_en_dragon.acb (the /cri_bind/ route to Mods, as the weapon bank's path; the object loader
             0x5466DF skips a bank that fails to load: the giant is then silent, nothing else), so the giant's own cues are
             loaded while it is in the mission and the plugin plays them by name (tools/make_edf41_objects.py DRAGON_*,
             src/edf41dragon.cpp).
  voice.JA / voice.EN
             SOUND/PC/EDF41_VOICE.<LANG>.AWB and EDF41_VOICE.<LANG>_LIST.AWE (the game builds the list's name from the AWB's
             by inserting _LIST before .AWE). The AWB is 4.1's with its version byte raised to 2, as EDF6 writes it (the
             layout is the same); the list is version 2 (pylib/awe.py): 4.1's cue names and wave ids, and per cue its
             length in seconds from the wave's HCA header (what the subtitle stays up for).
  table      MISSION/EDF6_VOICETABLE.<CN|EN|JA|KR|SC>.SGO: EDF6's own table plus one row per 4.1 cue key. The game finds a
             cue's caption by its key with one trailing '_E' or '_S' removed (EDF.dll 0x7B17F0), so a row is the cut key; no row
             prints '＊＊不明な再生キー＊＊'. The text is read at install from the subtitle plugin the player has in
             EDF4.1/mods/Plugins (subtitles.txt: Simplified, subtitles_zh.txt: Traditional, subtitles_en.txt): CN gets the
             Traditional, SC the Simplified, EN / JA / KR the English (the plugin has no Japanese or Korean text); a key
             with no line there gets the English line, else its cue name. The speaker (name) member is empty. None of the text is shipped with the tools.

    build(root6, root41)   -> {path under Mods: bytes}      everything, in memory (the voice AWBs are ~1.6 GB together)
    install(root6, root41, say) -> (written, skipped)       group by group, each written as soon as it is converted
    remove(root6) -> (deleted, kept)

A file of the same name in Mods that the ledger does not record as ours (another mod's) is never written over: that
group is left out and said so. A group installed by this version (VERSIONS) from the same EDF4.1 files is not converted again.
"""
from __future__ import annotations

import hashlib
import json
import os
import struct
import sys
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import awe  # noqa: E402
import cpk  # noqa: E402
import dsgo  # noqa: E402
import edf41src  # noqa: E402
from i18n import tr  # noqa: E402
import ledger  # noqa: E402
import rootcpk  # noqa: E402
from modfiles import load_json, sha256, sha256_file  # noqa: E402

OWNER = 'edf41sound'   # pylib/ledger.py OWNERS
# raise a group's number when its converter's output changes: that group (only) is converted again. table 2: the rows
# gained the 'name' / 'caption' member names the game reads them by (version 1 wrote unnamed [speaker, text] lists).
# table 4: the native lookup removes one trailing _E / _S, never an embedded one (E115_001_E_B keeps its full key).
VERSIONS = {'weapon': 1, 'dragon': 1, 'voice.JA': 1, 'voice.EN': 1, 'table': 4}
STAMP = '.edf41sound.json'   # Mods/: {group: {'version': VERSIONS[group], 'source': {file: [size, mtime ns]}}}

Blob = bytes | bytearray

SE_BANK = 'TIKYUUX_SE.ACB'
WEAPON_REL = 'SOUND/PC/EDF41_WEAPON_SE.ACB'
BANK_NAME = b'tikyuu41_W'   # as long as EDF6's and 4.1's 'tikyuuX_SE': the string is patched where it stands
OLD_NAME = b'tikyuuX_SE'
GUID_SEED = b'edf41sound:weapon-se:' + BANK_NAME


class ObjectBank:
    """A 4.1 object's sound bank shipped whole under a name of ours (the dragon group in the module doc)."""
    def __init__(self, source: str, rel: str, old: bytes, new: bytes) -> None:
        if len(old) != len(new):
            raise ValueError(f'{new!r} is not as long as {old!r}: the name is patched where it stands')
        self.source, self.rel, self.old, self.new = source, rel, old, new
        self.seed = b'edf41sound:object:' + new

    def app_path(self) -> str:
        """How an SGO's game_sound names the bank (the /cri_bind/ route: src/edf41sound.cpp, top)."""
        return 'app:/' + self.rel.lower()


OBJECT_BANKS: dict[str, ObjectBank] = {
    'dragon': ObjectBank('TIKYUU4_EN_DRAGON.ACB', 'SOUND/PC/EDF41_EN_DRAGON.ACB', b'tikyuu4_en_Dragon', b'tikyuu41_enDragon'),
}

# language -> (4.1's AWB, 4.1's list); 4.1 ships two voice languages. EDF6 names them <base>.<LANG>.AWB, <base>.<LANG>_LIST.AWE.
VOICES = {'JA': ('TIKYUU4_VOICE.AWB', 'TIKYUU4_VOICE_LIST.AWE'),
          'EN': ('TIKYUU4_VOICE_EN.AWB', 'TIKYUU4_VOICE_LIST_EN.AWE')}
# EDF6's caption languages -> the subtitle file of the player's 4.1 plugin that serves it (JA/KR: the plugin has none).
CAPTIONS = {'CN': 'subtitles_zh.txt', 'EN': 'subtitles_en.txt', 'JA': 'subtitles_en.txt', 'KR': 'subtitles_en.txt',
            'SC': 'subtitles.txt'}
PLUGINS = ('mods', 'Plugins')   # under the EDF4.1 folder


def voice_rels(lang: str) -> list[str]:
    return [f'SOUND/PC/EDF41_VOICE.{lang}.AWB', f'SOUND/PC/EDF41_VOICE.{lang}_LIST.AWE']


def table_rel(lang: str) -> str:
    return f'MISSION/EDF6_VOICETABLE.{lang}.SGO'


GROUPS: dict[str, list[str]] = {
    'weapon': [WEAPON_REL],
    **{group: [b.rel] for group, b in OBJECT_BANKS.items()},
    **{f'voice.{lang}': voice_rels(lang) for lang in VOICES},
    'table': [table_rel(lang) for lang in CAPTIONS],
}


def sound_path(root: str, name: str) -> str:
    return os.path.join(root, 'SOUND', 'PC', name)


def plugin_path(root41: str, name: str) -> str:
    return os.path.join(root41, *PLUGINS, name)


def sources41() -> list[str]:
    """Every EDF4.1 file any group is converted from, as a path under the EDF4.1 folder (what an EDF4.1 asset pack
    holds: tools/export_edf41_assets.py); the subtitle files are optional."""
    return ([f'SOUND/PC/{SE_BANK}'] + [f'SOUND/PC/{b.source}' for b in OBJECT_BANKS.values()]
            + [f'SOUND/PC/{n}' for lang in VOICES for n in VOICES[lang]]
            + ['/'.join((*PLUGINS, n)) for n in sorted(set(CAPTIONS.values()))])


def sources_of(group: str, root6: str, root41: str) -> list[str]:
    """The files a group is converted from."""
    if group == 'weapon':
        return [sound_path(root41, SE_BANK), sound_path(root6, SE_BANK)]
    if group in OBJECT_BANKS:   # with EDF6's SE bank: its AcfMd5Hash (EDF6's banks share one)
        return [sound_path(root41, OBJECT_BANKS[group].source), sound_path(root6, SE_BANK)]
    if group.startswith('voice.'):
        return [sound_path(root41, n) for n in VOICES[group[6:]]]
    return ([sound_path(root41, n) for lang in VOICES for n in VOICES[lang][1:]] + [os.path.join(root6, 'Root.cpk')]
            + [plugin_path(root41, n) for n in sorted(set(CAPTIONS.values()))])


# ------------------------------------------------------------------ the weapon bank


def utf_fields(acb: Blob) -> tuple[int, int, dict[str, object]]:
    """An ACB's table (absolute offsets in the file) -> (start of its string pool, start of its data area, its one
    row: a data column as (offset in the data area, length))."""
    if bytes(acb[:4]) != b'@UTF':
        raise ValueError('not an ACB (@UTF)')
    _version, _rows, strings_at, data_at, _name, _cols, _width, _count = struct.unpack_from('>HHIIIHHI', acb, 8)
    _name, _columns, _count, rows = cpk.parse_utf(bytes(acb[:8 + struct.unpack_from('>I', acb, 4)[0]]))
    return 8 + strings_at, 8 + data_at, next(rows())


def patch_bank(se41: bytes, hash6: bytes, old: bytes = OLD_NAME, new: bytes = BANK_NAME, seed: bytes = GUID_SEED) -> bytes:
    """A 4.1 bank (the SE bank by default) with AcfMd5Hash = `hash6`, Name `old` -> `new` and a GUID of ours (md5 of
    `seed`), patched where they stand."""
    if len(old) != len(new):
        raise ValueError(f'{new!r} is not as long as {old!r}')
    strings_at, data_at, row = utf_fields(se41)
    if row['Name'] != old.decode():
        raise ValueError(f'bank name {row["Name"]!r}, expected {old.decode()!r}')
    pool = se41[strings_at:data_at]
    at = pool.find(old + b'\0')
    if at < 0 or pool.count(old) != 1:
        raise ValueError('the bank name is not stored once in the string pool')
    out = bytearray(se41)
    out[strings_at + at:strings_at + at + len(old)] = new
    for field, value in (('AcfMd5Hash', hash6), ('AcbGuid', hashlib.md5(seed).digest())):
        off, length = row[field]
        if length != 16 == len(value):
            raise ValueError(f'{field} is {length} bytes')
        out[data_at + off:data_at + off + 16] = value
    return bytes(out)


def acf_hash6(root6: str) -> bytes:
    """The AcfMd5Hash EDF6's banks are built against (its SE bank's)."""
    with open(sound_path(root6, SE_BANK), 'rb') as f:
        se6 = f.read()
    _, data_at, row = utf_fields(se6)
    off, length = row['AcfMd5Hash']
    return se6[data_at + off:data_at + off + length]


def object_bank(group: str, root6: str, root41: str) -> dict[str, Blob]:
    """An OBJECT_BANKS group's file: 4.1's bank under our name and GUID, built against EDF6's ACF."""
    b = OBJECT_BANKS[group]
    with open(sound_path(root41, b.source), 'rb') as f:
        data = f.read()
    return {b.rel: patch_bank(data, acf_hash6(root6), b.old, b.new, b.seed)}


def weapon_bank(root6: str, root41: str) -> dict[str, Blob]:
    with open(sound_path(root41, SE_BANK), 'rb') as f:
        se41 = f.read()
    with open(sound_path(root6, SE_BANK), 'rb') as f:
        se6 = f.read()
    _, data_at, row = utf_fields(se6)
    off, length = row['AcfMd5Hash']
    return {WEAPON_REL: patch_bank(se41, se6[data_at + off:data_at + off + length])}


# ------------------------------------------------------------------ the voices


def read_file(path: str) -> bytearray:
    """A big file whole, without a second copy of it in memory."""
    data = bytearray(os.path.getsize(path))
    with open(path, 'rb') as f:
        f.readinto(data)
    return data


def voice_cues(awb: Blob, awe1: bytes) -> list[awe.Cue]:
    """4.1's list with the length of each cue's wave, from the HCA header in the AWB."""
    waves = awe.afs2_waves(awb)
    out = []
    for cue in awe.read_awe(awe1):
        start, end = waves[cue.wave]
        out.append(awe.Cue(cue.name, cue.wave, awe.hca_seconds(bytes(awb[start:min(end, start + 0x100)]))))
    return out


def voice_bank(root41: str, lang: str) -> dict[str, Blob]:
    awb_name, awe_name = VOICES[lang]
    awb = read_file(sound_path(root41, awb_name))
    with open(sound_path(root41, awe_name), 'rb') as f:
        cues = voice_cues(awb, f.read())
    awb[4] = 2   # EDF6 writes AFS2 version 2 (EDF4.1: 1); same header and tables
    awb_rel, awe_rel = voice_rels(lang)
    return {awb_rel: awb, awe_rel: awe.write_awe2(cues)}


# ------------------------------------------------------------------ the captions


def cut_key(cue: str) -> str:
    """EDF.dll 0x7B1900..0x7B1A41 removes one terminal _E / _S, in that order.

    It starts the comparison at len(key)-len(suffix), then shortens by len(suffix)
    and exits the loop. Embedded markers are part of the key: E115_001_E_B is
    an actual cue and caption in both source languages, not E115_001.
    """
    return cue[:-2] if cue.endswith(('_E', '_S')) else cue


def read_captions(path: str) -> dict[str, str]:
    """The subtitle plugin's KEY=text lines ({} when the player has no such file)."""
    if not os.path.isfile(path):
        return {}
    out: dict[str, str] = {}
    with open(path, encoding='utf-8-sig') as f:
        for line in f:
            key, eq, text = line.rstrip('\r\n').partition('=')
            if eq and key:
                out[key] = text
    return out


def cue_keys(root41: str) -> list[str]:
    """Every native caption key of 4.1's two voice lists, sorted."""
    keys: set[str] = set()
    for _awb, awe_name in VOICES.values():
        with open(sound_path(root41, awe_name), 'rb') as f:
            keys.update(cut_key(c.name) for c in awe.read_awe(f.read()))
    return sorted(keys)


def row_schema(rows: dsgo.Node) -> dict[int, str]:
    """The member names every row of EDF6's voice table carries ({0: 'name', 1: 'caption'}). The game reads a row's
    speaker and caption BY these names (EDF.dll 0x7B17F0: a column lookup, 0x55A30, that answers -1 for a row without
    them and leaves the reader with a null node: the access violation of 4.1 mission M126), so a row we add must carry
    exactly them; the schema is taken from the table itself, and a table whose rows disagree is refused."""
    schemas = {tuple(sorted(r.names.items())) for r in rows.items}
    if len(schemas) != 1:
        raise ValueError(f'the voice table rows have {len(schemas)} different member layouts')
    schema = dict(next(iter(schemas)))
    if sorted(schema.values()) != ['caption', 'name'] or sorted(schema) != [0, 1]:
        raise ValueError(f'the voice table rows are laid out {schema}, expected name / caption')
    return schema


# Keys the EDF5-era mission scripts (EDF6's EDF5_OLD_SCRIPT runtime: the EDF5 packs, the 4.1 packs, EDF6's own 146) play
# through the voice path that EDF6's table has no row for: the radio's open and close beeps (each radio line is
# MusenBegin, the line, MusenEnd). The caption lookup (EDF.dll 0x7B17F0) answers a key without a row with
# '＊＊不明な再生キー＊＊' shown for 5 s, so every radio line was followed by it (2026-10-14, DM202 / M104: the row
# lookup 0x55A30 answered -1 for exactly these two). NO_SUBTITLE is the engine's own mark for a voice without a
# subtitle: 0x5DD5F8 compares the caption with it and shows nothing.
SILENT_KEYS = ('MusenBegin', 'MusenEnd')
NO_SUBTITLE = '***no_subtitle***'


def add_rows(table: bytes, keys: list[str], text: dict[str, str]) -> bytes:
    """EDF6's voice table (a DSGO: root[0] holds one {name: speaker, caption: text} row per key, named by it) with a row
    more for each of `keys` it lacks, and a NO_SUBTITLE row for each of SILENT_KEYS it lacks. EDF6's rows are not touched;
    a key EDF6 has keeps EDF6's row."""
    doc = dsgo.parse(table)
    rows = doc.root.items[0]
    schema = row_schema(rows)
    have = set(rows.names.values())
    for key in [*keys, *(k for k in SILENT_KEYS if k not in keys)]:
        if key not in have:
            caption = NO_SUBTITLE if key in SILENT_KEYS else text.get(key) or key
            fields = {'name': '', 'caption': caption}   # 4.1's subtitles have no speaker
            have.add(key)
            rows.names[len(rows.items)] = key
            rows.items.append(dsgo.Node([fields[schema[i]] for i in sorted(schema)], dict(schema)))
    return dsgo.write(doc)


def voice_tables(root6: str, root41: str) -> dict[str, Blob]:
    game = rootcpk.Game(root6)
    keys = cue_keys(root41)
    out: dict[str, Blob] = {}
    for lang, name in CAPTIONS.items():
        text = {**read_captions(plugin_path(root41, CAPTIONS['EN'])), **read_captions(plugin_path(root41, name))}
        out[table_rel(lang)] = add_rows(game.read('MISSION', f'EDF6_VOICETABLE.{lang}.SGO'), keys, text)
    return out


# ------------------------------------------------------------------ build, install, remove


def make(group: str, root6: str, root41: str) -> dict[str, Blob]:
    """One group's files (rel under Mods -> bytes)."""
    if group == 'weapon':
        return weapon_bank(root6, root41)
    if group == 'table':
        return voice_tables(root6, root41)
    if group in OBJECT_BANKS:
        return object_bank(group, root6, root41)
    return voice_bank(root41, group[6:])


def build(root6: str, root41: str) -> dict[str, Blob]:
    """Every file of every group."""
    out: dict[str, Blob] = {}
    for group in GROUPS:
        out.update(make(group, root6, root41))
    return out


def rel41(path: str, root41: str) -> str | None:
    """A source's path under the EDF4.1 folder (an asset pack's rel), None for one of EDF6's. EDF6 may be on another
    drive than the pack (a share package unpacked to C:, Steam on D:): a path on another drive is not under it, and
    os.path.relpath refuses that pair outright (ValueError), so the drives are compared first."""
    if os.path.splitdrive(os.path.abspath(path))[0].lower() != os.path.splitdrive(os.path.abspath(root41))[0].lower():
        return None
    rel = os.path.relpath(path, root41)
    return None if rel.startswith('..') or os.path.isabs(rel) else rel.replace(os.sep, '/')


def fingerprint(paths: list[str], root41: str | None = None) -> dict[str, list[int]]:
    """Each source's size and modification time: a changed file, without reading it (the AWBs are 800 MB). An asset
    pack's file (pylib/edf41src.py): the size and time the manifest records of it in EDF4.1, when it is still of the
    size the manifest says (else its own: not current, and converting it verifies it)."""
    out = {}
    pack = edf41src.pack(root41) if edf41src.is_pack(root41) else None
    for p in paths:
        st = os.stat(p)
        rel = rel41(p, root41) if pack is not None else None
        if rel is not None and edf41src.key(rel) in pack.files and not pack.problem(rel):
            out[os.path.basename(p)] = pack.source(rel)
        else:
            out[os.path.basename(p)] = [st.st_size, st.st_mtime_ns]
    return out


def missing(group: str, root6: str, root41: str) -> list[str]:
    """The sources of `group` that must exist and do not (the subtitle files are optional: the cue names stand in);
    from an asset pack, also what of it the manifest does not record so (edf41src.problems: missing, another size, not
    listed; an optional file the manifest lists must be there too: a pack is used whole)."""
    plugins = os.path.join(root41, *PLUGINS)
    need = [p for p in sources_of(group, root6, root41) if os.path.dirname(p) != plugins]
    out = [os.path.basename(p) for p in need if not os.path.isfile(p)]
    if edf41src.is_pack(root41):
        mine = [p for p in sources_of(group, root6, root41) if rel41(p, root41) is not None]
        out += edf41src.problems(root41, [rel41(p, root41) for p in mine if os.path.dirname(p) != plugins])
        out += edf41src.problems(root41, [rel41(p, root41) for p in mine if os.path.dirname(p) == plugins],
                                 optional=True)
    return list(dict.fromkeys(out))


def corrupt(group: str, root6: str, root41: str) -> list[str]:
    """An asset pack's sources of `group` whose SHA-256 is not the manifest's (read whole: only before converting)."""
    if not edf41src.is_pack(root41):
        return []
    p41 = edf41src.pack(root41)
    rels = [rel41(p, root41) for p in sources_of(group, root6, root41) if rel41(p, root41) is not None]
    return [why for rel in rels if edf41src.key(rel) in p41.files for why in [p41.verify(rel)] if why]


def _foreign(led: ledger.Ledger, rel: str) -> bool:
    """A file no tool of ours wrote, another tool's, or edited since our recorded write: preserve its bytes."""
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    return not owners or any(o != OWNER for o in owners) or led.changed(rel)


def _current(led: ledger.Ledger, stamp: dict, group: str, source: dict[str, list[int]]) -> bool:
    """`group` is installed by its current version from these 4.1 files, every file still as written."""
    entry = stamp.get(group, {})
    if entry.get('version') != VERSIONS[group] or entry.get('source') != source:
        return False
    mine = set(led.owned_by(OWNER))
    return all(ledger.key(rel) in mine and os.path.exists(led.disk(rel)) for rel in GROUPS[group])


def install(root6: str, root41: str | None, say: Callable[[str], None] = print) -> tuple[list[str], list[tuple[str, str]]]:
    """Every group written as OWNER's; (paths written, [(group, why not)])."""
    if not root41:
        return [], [(g, '没有找到 EDF4.1 也没有 EDF4.1 素材包（需要其中之一才能转换它的音效和语音）') for g in GROUPS]
    led = ledger.Ledger(root6)
    stamp_path = os.path.join(root6, 'Mods', STAMP)
    stamp = load_json(stamp_path, {})
    written: list[str] = []
    skipped: list[tuple[str, str]] = []
    wanted: set[str] = set()
    for group, rels in GROUPS.items():
        gone = missing(group, root6, root41)
        if gone:
            skipped.append((group, f"缺少文件：{', '.join(gone)}"))
            continue
        foreign = [rel for rel in rels if _foreign(led, rel)]
        if foreign:
            skipped.append((group, f"Mods 里已有别的 MOD 的文件：{', '.join(foreign)}"))
            continue
        source = fingerprint([p for p in sources_of(group, root6, root41) if os.path.isfile(p)], root41)
        wanted.update(ledger.key(rel) for rel in rels)
        if _current(led, stamp, group, source):
            continue
        bad = corrupt(group, root6, root41)
        if bad:   # an asset pack's file changed since it was made: the group is not converted from it
            skipped.append((group, '；'.join(bad)))
            wanted.difference_update(ledger.key(rel) for rel in rels)
            continue
        say(tr(f'  转换 4.1 的{group}……', f'  Converting 4.1: {group}...'))
        try:
            files = make(group, root6, root41)
        except (ValueError, KeyError, struct.error) as e:
            skipped.append((group, f'转换失败：{e}'))
            wanted.difference_update(ledger.key(rel) for rel in rels)
            continue
        for rel, data in files.items():
            if ledger.key(rel) in set(led.owned_by(OWNER)) and sha256_file(led.disk(rel)) == sha256(data):
                continue
            written.append(led.put(OWNER, rel, data))
        del files
        stamp[group] = {'version': VERSIONS[group], 'source': source}
        with open(stamp_path, 'w', encoding='utf-8') as f:
            json.dump(stamp, f, indent=1)
    led.release(OWNER, sorted(set(led.owned_by(OWNER)) - wanted))
    return written, skipped


def remove(root6: str) -> tuple[list[str], list[str]]:
    stamp = os.path.join(root6, 'Mods', STAMP)
    if os.path.exists(stamp):
        os.remove(stamp)
    led = ledger.Ledger(root6)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)
