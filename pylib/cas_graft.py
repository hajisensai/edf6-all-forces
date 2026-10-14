"""Animation groups (CAS "players") an EDF6 class looks up that a ported object's CAS lacks, grafted from an EDF6 CAS
of that class, each with clips of its own that move nothing.

    graft(target, donor, groups, variables=()) -> bytes     both EDF6 CAS 0x204 (cas_legacy.cas_from_legacy first for 4.1 / EDF5)

Why: EDF.dll resolves a class's players by name (helper 0x1166FB0) and does not check the result: EDF4.1's carrier
(UfoCarrier301) under EDF6's carrier class (UfoCarrier508) would crash on spawn for want of the players in_ring and
out_ring (its rings, which 4.1's carrier does not have; jobs/4bf89026/tmp/alias41 contract6 / gaps). A missing state
or animation inside a player is harmless; a missing player is not.

What is grafted, per group: the anmgroup with its mcanms, anmdatas and datagroups as the donor has them, and the
tcontrols their data commands name (a data command's +0x10 and an mcanm's +0x20 hold a tcontrol's index: its place in
the tcontrol table; a tcontrol's words hold CANM clip indices). The grafted tcontrols go after the target's own and
their indices are renumbered so; each of their clips becomes a new clip of the same name, duration, step and frames
with no track (the donor's tracks move the donor's bones), after the target's own clips. Everything of the target's
stays as it was: its structs (re-emitted by cas_legacy._Emit, which lays out every EDF6 CAS again byte for byte), its
CANM (the clips table grows; every offset past it shifted) and its strings (the new names go after them). With no
group to graft the result is the target, byte for byte.

Variables (vcontrols) the class resolves by name are grafted the same way: EDF.dll's by-name lookup for a variable
(0x1167170) answers -1 when the CAS has none, and the setter (0x116de10) multiplies that index by 0x60 and writes
there: a missing variable the class sets is a write far outside the controller's array (EDF6_Berserker_Large sets
v_move_angle, v_move_velocity, v_tilt_dir, v_tilt_lerp every frame and v_damage_reaction_ratio when it is hit). The
float getter (0x1166de0) answers 0.0 for a missing name instead, so a missing getter-only variable is harmless but
reads as 0 (v_tilt_factor 0 = no lean) instead of the class's tuning. A vcontrol is 0x14 bytes: name, u32 +4 (1 = the
variable follows a tcontrol, whose index is at +0x10), u32 +8 kind, f32 +0xC initial value, u32 +0x10; the plain
ones (+4 = 0) are copied as they are, at the end of the target's table, so no index of its own moves. A variable
that follows a tcontrol is refused: its index would need the same renumbering as a data command's.
"""
from __future__ import annotations

import struct

import cas_legacy as cl

CLIP = 28
MCANM = 0x24
# Data commands whose +0x10 is a tcontrol index (the 'type' word at +0x14 is 2 then): 0 (play) and 4 (the additive
# players' clips: damage / additive_action of the Berserker CAS, 67 of 67 in EDF6 equal the mcanm's own +0x20).
# Any other command in a grafted group has an argument layout this module has not checked: refused.
TCONTROL_OPS = (0, 4)
VCONTROL = 0x14
# An mcanm's +0x20 when it names no tcontrol (the bits of -1.0f): the states that play their clip only through their data
# command (E510_FORTRESS.CAS: core_hatch / generator_hatch open_state, circle rotation_add). Kept as it is.
NO_TCONTROL = 0xBF800000


def _name(b: bytes, at: int) -> str:
    return cl._text(b, at + struct.unpack_from('<i', b, at)[0])


def _shift_layout(lay: cl.CasLayout, by: int) -> cl.CasLayout:
    """`lay` with every address moved by `by` (the donor's, placed after the target in one buffer)."""
    def g(x: cl.Group | None) -> cl.Group | None:
        return None if x is None else cl.Group(x.at + by, [d + by for d in x.data])

    def a(x: cl.AnmData) -> cl.AnmData:
        return cl.AnmData(x.at + by, g(x.group))

    t = lay.tree
    tree = cl.Tree([(at + by, [w + by for w in ws]) for at, ws in t.tcontrols], [v + by for v in t.vcontrols],
                   [x + by for x in t.bones],
                   [(at + by, [cl.McAnm(m.at + by, a(m.first), [a(x) for x in m.more], [g(x) for x in m.groups])
                               for m in ms]) for at, ms in t.anmgroups], g(t.unnamed))
    out = cl.CasLayout(lay.version, lay.canm_at + by, {k + by: v for k, v in lay.structs.items()},
                       [f + by for f in lay.string_fields], {s + by for s in lay.strings}, lay.canm,
                       lay.strings_at + by, tree)
    return out


def _data_of(m: cl.McAnm) -> list[int]:
    out = []
    for x in [m.first, *m.more]:
        if x.group is not None:
            out += x.group.data
    for x in m.groups:
        if x is not None:
            out += x.data
    return out


def variable_names(b: bytes) -> list[str]:
    """The names of an EDF6 CAS's variables (vcontrols), in table order."""
    return [_name(b, at) for at in cl.cas_layout(b).tree.vcontrols]


def _donor_variables(donor: bytes, ld: cl.CasLayout, want: list[str]) -> list[int]:
    """The donor's vcontrol addresses (in the donor's own buffer) of the variables `want`, each a plain one."""
    by_name = {_name(donor, at): at for at in ld.tree.vcontrols}
    missing = [v for v in want if v not in by_name]
    if missing:
        raise ValueError(f'the donor CAS has no variable {missing}')
    out = [by_name[v] for v in want]
    for v, at in zip(want, out):
        if struct.unpack_from('<I', donor, at + 4)[0] != 0:
            raise ValueError(f'variable {v} follows a tcontrol: not a plain variable, not grafted')
    return out


def graft(target: bytes, donor: bytes, groups: list[str], variables: tuple[str, ...] | list[str] = (),
          state_aliases: dict[str, list[tuple[str, str]]] | None = None) -> bytes:
    """`target` with the donor's anmgroups `groups` and plain variables `variables` it lacks (module docstring), and
    `state_aliases` ({player: [(new state, existing state)]}: see alias_states)."""
    lt, ld = cl.cas_layout(target), cl.cas_layout(donor)
    if lt.version != cl.CAS_6 or ld.version != cl.CAS_6:
        raise ValueError('graft takes EDF6 CAS 0x204 files')
    have = {_name(target, at) for at, _ms in lt.tree.anmgroups}
    want = [g for g in groups if g not in have]
    by_name = {_name(donor, at): (at, ms) for at, ms in ld.tree.anmgroups}
    missing = [g for g in want if g not in by_name]
    if missing:
        raise ValueError(f'the donor CAS has no group {missing}')
    have_vars = set(variable_names(target))
    want_vars = [v for v in dict.fromkeys(variables) if v not in have_vars]
    base = len(target)
    b = target + donor
    sd = _shift_layout(ld, base)
    new_vcontrols = [at + base for at in _donor_variables(donor, ld, want_vars)]
    own_groups, alias_fields, b = _aliased_groups(target, lt, b, state_aliases or {})
    synth_at = base + len(donor)       # what is past here is alias_states' own, taken from the target's structs
    donor_groups = [next(x for x in sd.tree.anmgroups if x[0] == by_name[g][0] + base) for g in want]
    # the donor tcontrols those groups name, in the donor's order
    named: list[int] = []
    for _at, ms in donor_groups:
        for m in ms:
            for d in _data_of(m):
                op, = struct.unpack_from('<I', b, d)
                if op not in TCONTROL_OPS or struct.unpack_from('<I', b, d + 0x14)[0] != 2:
                    raise ValueError(f'a grafted group holds data command {op} (type '
                                     f'{struct.unpack_from("<I", b, d + 0x14)[0]}): its arguments are not known to '
                                     f'hold only a tcontrol index, so it cannot be renumbered')
                idx, = struct.unpack_from('<I', b, d + 0x10)
                if idx not in named:
                    named.append(idx)
            idx, = struct.unpack_from('<I', b, m.at + 0x20)
            if idx != NO_TCONTROL and idx not in named:
                named.append(idx)
    named.sort()
    t_count = len(lt.tree.tcontrols)
    new_index = {old: t_count + k for k, old in enumerate(named)}
    pose_t = cl_pose(target)
    pose_d = cl_pose(donor)
    clips: list[tuple[str, int]] = []           # (name, donor clip index), in new clip order
    for old in named:
        at, words = sd.tree.tcontrols[old]
        for w in words:
            ci, = struct.unpack_from('<I', b, w)
            if all(c != ci for _n, c in clips):
                clips.append((pose_d.clips[ci].name, ci))
    clip_index = {ci: len(pose_t.clips) + k for k, (_n, ci) in enumerate(clips)}
    merged = cl.Tree(lt.tree.tcontrols + [sd.tree.tcontrols[i] for i in named], lt.tree.vcontrols + new_vcontrols,
                     lt.tree.bones, own_groups + donor_groups, lt.tree.unnamed)
    lay = cl.CasLayout(cl.CAS_6, lt.canm_at, {**lt.structs, **sd.structs},
                       lt.string_fields + sd.string_fields + alias_fields, lt.strings | sd.strings, lt.canm,
                       lt.strings_at, merged)
    emit = cl._Emit(b, lay, cl.COMMON_SIZE[cl.CAS_6])
    area = emit.run()
    struct.pack_into('<I', area, 0x0C, len(merged.tcontrols))   # _Emit keeps the header's counts: set them
    struct.pack_into('<I', area, 0x14, len(merged.vcontrols))
    struct.pack_into('<I', area, 0x1C, len(merged.anmgroups))
    # renumber what the grafted structs point at (an emitted struct came from the donor when its source did)
    counts = {at: len(ms) for at, ms in merged.anmgroups}
    for new, old in emit.sources.items():
        if emit.kinds[new] == 'anmgroup':          # an alias adds states to a group of the target's: its count
            struct.pack_into('<I', area, new + 4, counts[old])
        if old < base or old >= synth_at:
            continue
        kind = emit.kinds[new]
        if kind == 'data' and struct.unpack_from('<I', area, new)[0] in TCONTROL_OPS:
            idx, = struct.unpack_from('<I', area, new + 0x10)
            struct.pack_into('<I', area, new + 0x10, new_index[idx])
        elif kind == 'mcanm':
            idx, = struct.unpack_from('<I', area, new + 0x20)
            if idx != NO_TCONTROL:
                struct.pack_into('<I', area, new + 0x20, new_index[idx])
        elif kind == 'tcontrol_anim':
            ci, = struct.unpack_from('<I', area, new)
            struct.pack_into('<I', area, new, clip_index[ci])
    canm, extra, name_fields = _canm_with_clips(target, lt, pose_d, clips)
    canm_at = len(area)
    blob_at = canm_at + len(canm)
    extra_strings = {}
    tail = b''
    for s in sorted({emit_old for _f, emit_old in emit.strings if emit_old >= base}):
        extra_strings[s] = len(target) - lt.strings_at + len(extra) + len(tail)
        tail += b[s:cl._text_end(b, s)] + b'\0\0'
    for field_at, old in emit.strings:
        if old < base:
            struct.pack_into('<i', area, field_at, blob_at + (old - lt.strings_at) - field_at)
        else:
            struct.pack_into('<i', area, field_at, blob_at + extra_strings[old] - field_at)
    canm = bytearray(canm)
    for clip_at, rel in name_fields:     # new clips' names: in `extra`, after the target's strings
        struct.pack_into('<i', canm, clip_at + 4, len(canm) + (len(target) - lt.strings_at) + rel - clip_at)
    struct.pack_into('<I', area, 8, canm_at)
    return bytes(area + canm + target[lt.strings_at:] + extra + tail)


def _aliased_groups(target: bytes, lt: cl.CasLayout, b: bytes, aliases: dict[str, list[tuple[str, str]]]
                    ) -> tuple[list[tuple[int, list[cl.McAnm]]], list[int], bytes]:
    """(the target's anmgroups with the aliased states added, the new structs' string fields, `b` with those structs
    appended). A state is an mcanm; the alias is a copy of the existing one under another name: the same tcontrol, the
    same data, so it plays the same clips. The copy is a 0x24-byte struct appended to `b` with its name pointing at a
    string appended after it (_Emit re-emits every struct from its source address and re-aims the offsets, so
    the copy needs only its name field of its own)."""
    synth = bytearray()
    fields: list[int] = []
    groups = []
    for at, ms in lt.tree.anmgroups:
        pairs = aliases.get(_name(target, at), [])
        states = {_name(target, m.at): m for m in ms}
        more = []
        for new, old in pairs:
            if new in states:
                continue                       # the player has it already: nothing to alias
            if old not in states:
                raise ValueError(f'player {_name(target, at)} has no state {old} to alias as {new}')
            where = len(b) + len(synth)
            name = new.encode('utf-16le') + b'\0\0'
            src = bytearray(b[states[old].at:states[old].at + MCANM])
            struct.pack_into('<i', src, 0, MCANM)                  # the name follows the struct
            synth += src + name + bytes(-len(name) % 4)
            fields.append(where)
            m = states[old]
            more.append(cl.McAnm(where, m.first, m.more, m.groups))
        groups.append((at, ms + more))
    unknown = set(aliases) - {_name(target, at) for at, _ms in lt.tree.anmgroups}
    if unknown:
        raise ValueError(f'the target CAS has no player {sorted(unknown)}')
    return groups, fields, b + bytes(synth)


def states(cas: bytes) -> dict[str, set[str]]:
    """{player: its state names} of a CAS."""
    lt = cl.cas_layout(cas)
    return {_name(cas, at): {_name(cas, m.at) for m in ms} for at, ms in lt.tree.anmgroups}


def alias_states(cas: bytes, aliases: dict[str, list[tuple[str, str]]]) -> bytes:
    """`cas` (EDF6 0x204) with states added to its players as copies of states it has: {player: [(new, existing)]}.

    Why: EDF6_Berserker_Large asks the player main for walk_battle_f / _b / _l / _r by name when it fights, and plays
    them through 0x116BFC0, which answers false for a state the player lacks (index -1): the robot keeps its last
    state, so a Hector chasing its target stands in its stand pose and slides. A walking state aliased to its plain
    walk gives it the walk it already has. Only states whose meaning is continuous movement are worth aliasing: a state
    the class waits to finish (an appeal, an attack) played as a looping stand would never end."""
    return graft(cas, cas, [], (), aliases)


def rename_group(cas: bytes, old: str, new: str) -> bytes:
    """`cas` (EDF6 0x204) with its player `old` named `new`: the name appended to the strings at the end of the file and
    the anmgroup's name offset (+0, relative to the struct) aimed at it; every other byte as it was (the old name stays
    where it was, unreferenced by this player). Its states, clips and tracks are untouched, so the player animates as it
    did. Why: a class looks a player up by name (0x1166FB0) and the ported object's player that does the job has another
    name (4.1's fortress opens its belly hatch with `hatch`; EDF6's MovingFortress plays `core_hatch`)."""
    lay = cl.cas_layout(cas)
    if lay.version != cl.CAS_6:
        raise ValueError('rename_group takes an EDF6 CAS 0x204')
    return _renamed(cas, [at for at, _ms in lay.tree.anmgroups], old, new, 'player')


def rename_state(cas: bytes, player: str, old: str, new: str) -> bytes:
    """rename_group for a state (an mcanm, its name offset at +0 too) of the player `player`. Why: the ported object's
    clip that does a job the class asks for by another name, where that name is taken by a clip doing another job
    (4.1's fortress turns in place with turn_ll and turns while walking with turn_l; MovingFortress asks for turn_l to
    turn in place and walk_fwd_l to turn while walking): renamed in order, turn_l -> walk_fwd_l, then turn_ll -> turn_l."""
    lay = cl.cas_layout(cas)
    if lay.version != cl.CAS_6:
        raise ValueError('rename_state takes an EDF6 CAS 0x204')
    groups = [ms for at, ms in lay.tree.anmgroups if _name(cas, at) == player]
    if len(groups) != 1:
        raise ValueError(f'the CAS has {len(groups)} players named {player}')
    return _renamed(cas, [m.at for m in groups[0]], old, new, f'state of {player}')


def _renamed(cas: bytes, structs: list[int], old: str, new: str, what: str) -> bytes:
    """`cas` with the one struct of `structs` named `old` (its +0 a name offset relative to it) named `new`: the name
    appended to the strings at the end of the file, every other byte as it was."""
    if any(_name(cas, at) == new for at in structs):
        raise ValueError(f'the CAS has a {what} {new} already')
    found = [at for at in structs if _name(cas, at) == old]
    if len(found) != 1:
        raise ValueError(f'the CAS has {len(found)} {what} named {old}')
    out = bytearray(cas)
    out += bytes(len(out) % 2)
    name_at = len(out)
    out += new.encode('utf-16le') + b'\0\0'
    struct.pack_into('<i', out, found[0], name_at - found[0])
    return bytes(out)


def hollow(cas: bytes) -> bytes:
    """An EDF6 CAS whose every clip moves nothing: each CANM clip's track count set to 0, in place (the players,
    states, clips' names, durations and frames as they were; the track data left unread). For a ported object that
    has no CAS of its own under a class that resolves players by name: its template's CAS, hollowed, gives it the
    players without the template's motion on bones it does not have."""
    lay = cl.cas_layout(cas)
    if lay.version != cl.CAS_6:
        raise ValueError('hollow takes an EDF6 CAS 0x204')
    out = bytearray(cas)
    _sig, _ver, nc, co = struct.unpack_from('<4sIII', out, lay.canm_at)
    for i in range(nc):
        struct.pack_into('<I', out, lay.canm_at + co + i * CLIP + 20, 0)
    return bytes(out)


def cl_pose(b: bytes):  # noqa: ANN201 - cas_pose.CasPose
    import cas_pose
    return cas_pose.CasPose(b)


def _canm_with_clips(target: bytes, lt: cl.CasLayout, pose_d, clips: list[tuple[str, int]]):  # noqa: ANN001,ANN202
    """The target's CANM with a trackless clip appended per (name, donor clip index): (bytes, the new names' strings,
    [(field in the CANM, offset of its string within those strings)])."""
    canm = bytearray(target[lt.canm_at:lt.strings_at])
    if not clips:
        return bytes(canm), b'', []
    base = 0
    sig, ver, nc, co, nch, po, nn, no = struct.unpack_from('<4sI6I', canm, base)
    insert = co + nc * CLIP
    grow = CLIP * len(clips)
    # offsets that cross the insertion point move by `grow`: the header's bone-name table, each clip's name and tracks
    if no >= insert:
        struct.pack_into('<I', canm, 0x1C, no + grow)
    for i in range(nc):
        at = co + i * CLIP
        name_rel, = struct.unpack_from('<i', canm, at + 4)
        struct.pack_into('<i', canm, at + 4, name_rel + grow)
        off, = struct.unpack_from('<I', canm, at + 24)
        struct.pack_into('<I', canm, at + 24, off + grow)
    if po >= insert or co > insert:
        raise ValueError('CANM channels after its clips: not laid out as documented')
    new = bytearray()
    extra = b''
    fields = []
    for k, (name, ci) in enumerate(clips):
        src = pose_d.clips[ci].at
        loop, _n, duration, step, frames, _count, _off = struct.unpack_from('<IiffIII', pose_d.data, src)
        rec = bytearray(struct.pack('<IiffIII', loop, 0, duration, step, frames, 0, 0))
        fields.append((insert + k * CLIP, len(extra)))   # a clip's name counts from the clip's start
        extra += name.encode('utf-16le') + b'\0\0'
        new += rec
    canm[insert:insert] = new
    struct.pack_into('<I', canm, 8, nc + len(clips))
    # the name fields were computed against the final CANM length by the caller (strings follow the CANM)
    return bytes(canm), extra, fields
