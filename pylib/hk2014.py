"""EDF4.1's ragdolls: Havok hk_2014.1.0-r1 *binary tagfiles* (magic 1E0DB0CA CEFA11D0), which the object port ships as
they are (tools/make_edf41_objects.py: EDF.dll keeps the old readers). Read, and the rigid bodies' masses scaled in place.

    read(data) -> Tagfile       every type definition and object, each scalar float member's file offset kept
    rigid_bodies(tf) -> [Body]  name, mass, inverse inertia, damping, deactivation class of each moving hkpRigidBody
    scaled_masses(data, factor) -> bytes   every body's mass and inertia x factor, every joint's friction torque x
                                factor (a ragdoll that moves exactly as before, factor times as heavy); the same size,
                                only those floats changed

Format (read from the files themselves; every 4.1 ragdoll the port ships parses to its last byte, 22 files):
  8 bytes magic, then a stream of tagged records; every number is a variable-length int: byte 0 bit 0 the sign, bits 1-6
  the low 6 bits, bit 7 "more", each further byte 7 bits more (high bit "more").
  tag 1 file info: version (5), the SDK string, then 6 bytes 15 00 01 00 14 00 (the same in every file: checked).
  tag 2 type: name, version, parent type index (0 none), member count, per member: name, kind, the tuple count when
        kind & 0x20, the class name when the base kind is object (8) or struct (9). Base kinds: 1 byte, 2 int, 3 real,
        4..7 vec4 / vec8 / vec12 / vec16, 8 object, 9 struct, 10 cstring; 0x10 array, 0x20 tuple.
  tag 3 / 4 object: type index, a bit field over all members (the parents' first), each present member's value.
        Objects are numbered from 1 in file order; an object member holds that number (0 null).
  values: int / byte as above (byte raw), real 4 bytes, vecN N floats, cstring: length > 0 then the bytes, <= 0 a
        string seen before (index -length; 0 and 1 are '' and null), struct: its own bit field and members.
  arrays: count, then: bytes raw; ints: their in-memory width, then each; reals raw; vec4: the number of floats kept
        per element, then those; vec8 / 12 / 16 raw; objects their numbers; structs column by column: a bit field over
        the struct's members, then each present member for every element (a scalar member as an array of n).
  tag 7 end.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

MAGIC = bytes.fromhex('1e0db0cacefa11d0')
PREAMBLE = bytes.fromhex('150001001400')      # after the file info's SDK string, in every 4.1 ragdoll
T_BYTE, T_INT, T_REAL, T_VEC4, T_VEC8, T_VEC12, T_VEC16, T_OBJECT, T_STRUCT, T_CSTRING = range(1, 11)
F_ARRAY, F_TUPLE = 0x10, 0x20
VEC = {T_VEC4: 4, T_VEC8: 8, T_VEC12: 12, T_VEC16: 16}


@dataclass
class Type:
    name: str
    version: int
    parent: 'Type | None'
    own: list[tuple[str, int, str | None, int | None]]   # (member, kind, class, tuple count)

    def members(self) -> list[tuple[str, int, str | None, int | None]]:
        return (self.parent.members() if self.parent else []) + self.own


class Obj(dict):
    """An object's or struct's present members; `at` the file offset of each scalar real / vec member."""

    def __init__(self, t: Type) -> None:
        super().__init__()
        self.type = t
        self.at: dict[str, int] = {}


@dataclass
class Tagfile:
    data: bytes
    types: list[Type | None] = field(default_factory=lambda: [None])
    objects: list[Obj] = field(default_factory=list)        # objects[i - 1] is object number i


class _Reader:
    def __init__(self, data: bytes) -> None:
        self.b, self.p = data, len(MAGIC)
        self.strings: list[str | None] = ['', None]
        self.tf = Tagfile(data)

    def byte(self) -> int:
        v = self.b[self.p]
        self.p += 1
        return v

    def int(self) -> int:
        c = self.byte()
        neg, v, shift = c & 1, (c >> 1) & 0x3F, 6
        while c & 0x80:
            c = self.byte()
            v |= (c & 0x7F) << shift
            shift += 7
        return -v if neg else v

    def str(self) -> str | None:
        n = self.int()
        if n <= 0:
            return self.strings[-n]
        s = self.b[self.p:self.p + n].decode('latin-1')
        self.p += n
        self.strings.append(s)
        return s

    def floats(self, n: int) -> tuple[float, ...]:
        v = struct.unpack_from(f'<{n}f', self.b, self.p)
        self.p += 4 * n
        return v

    def type_named(self, name: str | None) -> Type:
        for t in reversed(self.tf.types):
            if t is not None and t.name == name:
                return t
        raise ValueError(f'tagfile: no type {name!r}')

    def metadata(self) -> None:
        name, version = self.str(), self.int()
        parent = self.tf.types[self.int()]
        own = []
        for _ in range(self.int()):
            mname, kind = self.str(), self.int()
            tup = self.int() if kind & F_TUPLE else None
            cls = self.str() if kind & 0xF in (T_OBJECT, T_STRUCT) else None
            own.append((mname, kind, cls, tup))
        self.tf.types.append(Type(str(name), version, parent, own))

    def bits(self, n: int) -> list[bool]:
        nb = (n + 7) // 8
        v = int.from_bytes(self.b[self.p:self.p + nb], 'little')
        self.p += nb
        return [bool(v >> i & 1) for i in range(n)]

    def value(self, kind: int, cls: str | None, tup: int | None) -> object:
        base = kind & 0xF
        if kind & F_ARRAY:
            return self.array(base, cls, self.int())
        if kind & F_TUPLE:
            return self.array(base, cls, tup or 0)
        if base == T_BYTE:
            return self.byte()
        if base == T_INT:
            return self.int()
        if base == T_REAL:
            return self.floats(1)[0]
        if base in VEC:
            return self.floats(VEC[base])
        if base == T_CSTRING:
            return self.str()
        if base == T_OBJECT:
            return self.int()
        if base == T_STRUCT:
            return self.body(self.type_named(cls))
        raise ValueError(f'tagfile: member kind {kind:#x} at {self.p:#x}')

    def array(self, base: int, cls: str | None, n: int) -> object:
        if base == T_BYTE:
            v = self.b[self.p:self.p + n]
            self.p += n
            return v
        if base == T_INT:
            self.int()   # the ints' width in memory
            return [self.int() for _ in range(n)]
        if base == T_REAL:
            return list(self.floats(n))
        if base in VEC:
            k = self.int() if base == T_VEC4 else VEC[base]
            fl = self.floats(k * n)
            return [fl[i * k:(i + 1) * k] for i in range(n)]
        if base == T_CSTRING:
            return [self.str() for _ in range(n)]
        if base == T_OBJECT:
            return [self.int() for _ in range(n)]
        if base == T_STRUCT:
            t = self.type_named(cls)
            out = [Obj(t) for _ in range(n)]
            for (mname, kind, mcls, tup), on in zip(t.members(), self.bits(len(t.members()))):
                if not on:
                    continue
                if kind & F_ARRAY:
                    for o in out:
                        o[mname] = self.value(kind, mcls, tup)
                elif kind & F_TUPLE:
                    col = self.array(kind & 0xF, mcls, n * (tup or 0))
                    for i, o in enumerate(out):
                        o[mname] = col[i * tup:(i + 1) * tup]
                else:
                    for o, c in zip(out, self.array(kind & 0xF, mcls, n)):
                        o[mname] = c
            return out
        raise ValueError(f'tagfile: array kind {base} at {self.p:#x}')

    def body(self, t: Type) -> Obj:
        o = Obj(t)
        mem = t.members()
        for (mname, kind, mcls, tup), on in zip(mem, self.bits(len(mem))):
            if on:
                if not kind & (F_ARRAY | F_TUPLE) and kind & 0xF in (T_REAL, *VEC):
                    o.at[mname] = self.p
                o[mname] = self.value(kind, mcls, tup)
        return o

    def read(self) -> Tagfile:
        if self.b[:len(MAGIC)] != MAGIC:
            raise ValueError('not a Havok binary tagfile')
        while True:
            if self.p >= len(self.b):
                raise ValueError('tagfile: no end tag')
            tag = self.int()
            if tag == 1:
                self.int()
                self.str()
                if self.b[self.p:self.p + len(PREAMBLE)] != PREAMBLE:
                    raise ValueError(f'tagfile: file info not followed by {PREAMBLE.hex()} (another writer)')
                self.p += len(PREAMBLE)
            elif tag == 2:
                self.metadata()
            elif tag in (3, 4):
                t = self.tf.types[self.int()]
                if t is None:
                    raise ValueError(f'tagfile: object of no type at {self.p:#x}')
                self.tf.objects.append(self.body(t))
            elif tag == 7:
                if self.p != len(self.b):
                    raise ValueError(f'tagfile: {len(self.b) - self.p} bytes after the end tag')
                return self.tf
            else:
                raise ValueError(f'tagfile: tag {tag} at {self.p - 1:#x}')


def read(data: bytes) -> Tagfile:
    return _Reader(bytes(data)).read()


@dataclass(frozen=True)
class Body:
    name: str
    mass: float
    inertia_inv: tuple[float, float, float]
    linear_damping: float
    angular_damping: float
    deactivation_class: int | None


def rigid_bodies(tf: Tagfile) -> list[Body]:
    out = []
    for o in tf.objects:
        if o.type.name != 'hkpRigidBody':
            continue
        motion = o.get('motion') or Obj(o.type)
        state = motion.get('motionState') or {}
        inv = motion.get('inertiaAndMassInv')
        if not inv or not inv[3] > 0:   # a fixed / keyframed body (no inverse mass): not a ragdoll part that moves
            continue
        out.append(Body(str(o.get('name')), 1.0 / inv[3], tuple(inv[:3]), state.get('linearDamping', 0.0),
                        state.get('angularDamping', 0.0), state.get('deactivationClass')))
    return out


def total_mass(data: bytes) -> float:
    return sum(b.mass for b in rigid_bodies(read(data)))


def _atoms(o: Obj) -> list[Obj]:
    """The constraint atoms (struct members) of a constraint data object."""
    atoms = o.get('atoms')
    return [v for v in atoms.values() if isinstance(v, Obj)] if isinstance(atoms, Obj) else []


def scaled_masses(data: bytes, factor: float) -> bytes:
    """`data` with every rigid body factor times as heavy (mass and inertia: the inverse values / factor) and every
    joint's friction torque (hkpAngFrictionConstraintAtom.maxFrictionTorque) factor times as large, so that under
    gravity, contacts and joints the ragdoll moves as before. Joint motors and the stabilisation's impulse limits are
    not scaled: ValueError when a file has a motor or a finite impulse limit (none of the 4.1 dragons' has)."""
    if not factor > 0:
        raise ValueError(f'mass factor {factor}')
    tf = read(data)
    out = bytearray(data)
    bodies = 0
    for o in tf.objects:
        if o.type.name == 'hkpRigidBody':
            motion = o['motion']
            at = motion.at['inertiaAndMassInv']
            inv = struct.unpack_from('<4f', out, at)
            struct.pack_into('<4f', out, at, *(x / factor for x in inv))
            bodies += 1
        for atom in _atoms(o):
            if 'maxFrictionTorque' in atom.at:
                struct.pack_into('<f', out, atom.at['maxFrictionTorque'], atom['maxFrictionTorque'] * factor)
            for limit in ('maxLinImpulse', 'maxAngImpulse', 'breachImpulse'):
                if limit in atom and atom[limit] < 1e37:
                    raise ValueError(f'tagfile: a finite {limit} ({atom[limit]}) would need scaling too')
            motors = atom.get('motors')
            if (isinstance(motors, list) and any(motors)) or atom.get('motor'):
                raise ValueError('tagfile: a joint motor would need scaling too')
    if not bodies:
        raise ValueError('tagfile: no rigid body')
    return bytes(out)
