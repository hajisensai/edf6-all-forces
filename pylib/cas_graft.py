"""Animation groups (CAS "players") an EDF6 class looks up that a ported object's CAS lacks, grafted from an EDF6 CAS
of that class, each with clips of its own that move nothing.

    graft(target, donor, groups) -> bytes     both EDF6 CAS 0x204 (cas_legacy.cas_from_legacy first for 4.1 / EDF5)

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
"""
from __future__ import annotations

import struct

import cas_legacy as cl

CLIP = 28


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


def graft(target: bytes, donor: bytes, groups: list[str]) -> bytes:
    lt, ld = cl.cas_layout(target), cl.cas_layout(donor)
    if lt.version != cl.CAS_6 or ld.version != cl.CAS_6:
        raise ValueError('graft takes EDF6 CAS 0x204 files')
    have = {_name(target, at) for at, _ms in lt.tree.anmgroups}
    want = [g for g in groups if g not in have]
    by_name = {_name(donor, at): (at, ms) for at, ms in ld.tree.anmgroups}
    missing = [g for g in want if g not in by_name]
    if missing:
        raise ValueError(f'the donor CAS has no group {missing}')
    base = len(target)
    b = target + donor
    sd = _shift_layout(ld, base)
    donor_groups = [next(x for x in sd.tree.anmgroups if x[0] == by_name[g][0] + base) for g in want]
    # the donor tcontrols those groups name, in the donor's order
    named: list[int] = []
    for _at, ms in donor_groups:
        for m in ms:
            for d in _data_of(m):
                op, = struct.unpack_from('<I', b, d)
                if op == 0:
                    idx, = struct.unpack_from('<I', b, d + 0x10)
                    if idx not in named:
                        named.append(idx)
            idx, = struct.unpack_from('<I', b, m.at + 0x20)
            if idx not in named:
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
    merged = cl.Tree(lt.tree.tcontrols + [sd.tree.tcontrols[i] for i in named], lt.tree.vcontrols, lt.tree.bones,
                     lt.tree.anmgroups + donor_groups, lt.tree.unnamed)
    lay = cl.CasLayout(cl.CAS_6, lt.canm_at, {**lt.structs, **sd.structs}, lt.string_fields + sd.string_fields,
                       lt.strings | sd.strings, lt.canm, lt.strings_at, merged)
    emit = cl._Emit(b, lay, cl.COMMON_SIZE[cl.CAS_6])
    area = emit.run()
    struct.pack_into('<I', area, 0x0C, len(merged.tcontrols))   # _Emit keeps the header's counts: set them
    struct.pack_into('<I', area, 0x1C, len(merged.anmgroups))
    # renumber what the grafted structs point at (an emitted struct came from the donor when its source did)
    for new, old in emit.sources.items():
        if old < base:
            continue
        kind = emit.kinds[new]
        if kind == 'data' and struct.unpack_from('<I', area, new)[0] == 0:
            idx, = struct.unpack_from('<I', area, new + 0x10)
            struct.pack_into('<I', area, new + 0x10, new_index[idx])
        elif kind == 'mcanm':
            idx, = struct.unpack_from('<I', area, new + 0x20)
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
