"""Writes src/py_consts.inc, the numbers and names the plugin shares with the Python that builds its files (the SGOs'
radii and speeds, the models' bone names and sizes, the marks the plugin knows its objects by): the C++ takes them from
here (`pyconst::kX`) instead of keeping a hand copy that tools/selftest.py compared with the Python one by text
(docs/reviews/guards-conversion-1127.md).

  python tools/gen_py_consts.py            write it
  python tools/gen_py_consts.py --check    exit 1 when the committed file is not what this would write, or when a constant
                                           of it is used by no source (a C++ copy kept by hand again) (CI)
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import drill_model  # noqa: E402
import srcscan  # noqa: E402
import vcobjects as vc  # noqa: E402

OUT = os.path.join(ROOT, 'src', 'py_consts.inc')


def constants() -> list[tuple[str, object, str]]:
    """(the C++ name, its value: float / int / str, where the Python keeps it) in the file's order."""
    return [
        # marks the plugin knows its bodies by (src/body506.cpp kMarks names them too)
        ('kSazabiMark', float(vc.SAZABI_MARK), 'pylib/vcobjects.py SAZABI_MARK (src/sazabi.cpp)'),
        ('kDreadnoughtMark', float(vc.DREADNOUGHT_MARK), 'pylib/vcobjects.py DREADNOUGHT_MARK (src/dreadnought.cpp)'),
        ('kDreadnoughtKeel', float(vc.DREADNOUGHT_KEEL), 'pylib/vcobjects.py DREADNOUGHT_KEEL (src/dreadnought.cpp kKeel)'),
        # the drill tank (src/drill.cpp) and its charge (src/jet_bay.cpp)
        ('kDrillBone', drill_model.DRILL_BONE, 'pylib/drill_model.py DRILL_BONE: the marker bone'),
        ('kSpinBone', drill_model.SPIN_BONE, 'pylib/drill_model.py SPIN_BONE: the bone the drill turns on'),
        ('kDrillLength', float(drill_model.DRILL_LENGTH), 'pylib/drill_model.py DRILL_LENGTH'),
        ('kDrillRadius', float(drill_model.DRILL_RADIUS), 'pylib/drill_model.py DRILL_RADIUS: at its base'),
        ('kDrillBaseY', float(drill_model.DRILL_BASE[1]), 'pylib/drill_model.py DRILL_BASE[1]'),
        ('kDrillBaseZ', float(drill_model.DRILL_BASE[2]), 'pylib/drill_model.py DRILL_BASE[2]'),
        ('kDrillFolds', float(drill_model.DRILL_FOLDS), 'pylib/drill_model.py DRILL_FOLDS: its rotational repeat'),
        ('kDrillChargeFile', vc.DRILL_CHARGE_FILE, 'pylib/vcobjects.py DRILL_CHARGE_FILE'),
        ('kDrillChargeSgo', f'app:/object/{vc.DRILL_CHARGE_FILE.lower()}', 'its app:/object path'),
        # the EMC (src/emc_plan.h, src/emc.cpp)
        ('kEmcBeamRange', float(vc.EMC_BEAM_RANGE), 'pylib/vcobjects.py EMC_BEAM_RANGE'),
        ('kEmcSightSize', float(vc.EMC_SIGHT_SIZE), 'pylib/vcobjects.py EMC_SIGHT_SIZE: the glow\'s first thickness'),
        ('kEmcBlastRadius', float(vc.EMC_BLAST_RADIUS), 'pylib/vcobjects.py EMC_BLAST_RADIUS: the blast SGO\'s'),
        # the ultimate strike (src/ultimate_plan.h)
        ('kUltBoltRounds', int(vc.ULT_BOLT_ROUNDS), 'pylib/vcobjects.py ULT_BOLT_ROUNDS: a bolt\'s rounds'),
        ('kUltBlastRadius', float(vc.ULT_BLAST_RADIUS), 'pylib/vcobjects.py ULT_BLAST_RADIUS: a ring\'s blast'),
        ('kUltSparkRadius', float(vc.ULT_SPARK_RADIUS), 'pylib/vcobjects.py ULT_SPARK_RADIUS'),
        ('kUltGatherReach', float(vc.ULT_GATHER_REACH), 'pylib/vcobjects.py ULT_GATHER_REACH'),
        # the salvo (src/salvo_volley.h, src/sortie_plan.h) and the glow declaration (src/gpufx/gpufx_glow.h)
        ('kSalvoRounds', int(vc.SALVO_ROUNDS), 'pylib/vcobjects.py SALVO_ROUNDS'),
        ('kSalvoVolley', int(vc.SALVO_VOLLEY), 'pylib/vcobjects.py SALVO_VOLLEY: rounds a volley'),
        ('kSalvoVolleyGap', int(vc.SALVO_VOLLEY_GAP), 'pylib/vcobjects.py SALVO_VOLLEY_GAP: frames between volleys'),
        ('kSalvoChildSpeed', vc.SALVO_CHILD_SPEED * 60.0, 'pylib/vcobjects.py SALVO_CHILD_SPEED x 60: m/s'),
        # the salvo's rounds as its SGO flies them (src/salvo_flight.h): m and frames
        ('kSalvoEject', float(vc.SALVO_EJECT), 'pylib/vcobjects.py SALVO_EJECT: AmmoSpeed'),
        ('kSalvoDropFrames', int(vc.SALVO_DROP[0]), 'pylib/vcobjects.py SALVO_DROP[0]: CP[7][0]'),
        ('kSalvoDropKeep', float(vc.SALVO_DROP[1]), 'pylib/vcobjects.py SALVO_DROP[1]: CP[7][1]'),
        ('kSalvoDropGravity', float(vc.SALVO_DROP_GRAVITY), 'pylib/vcobjects.py SALVO_DROP_GRAVITY'),
        ('kSalvoFan', float(vc.SALVO_FAN), 'pylib/vcobjects.py SALVO_FAN: FireAccuracy'),
        ('kSalvoAccel', float(vc.SALVO_ACCEL), 'pylib/vcobjects.py SALVO_ACCEL: CP[4]'),
        ('kSalvoTurn', float(vc.SALVO_TURN), 'pylib/vcobjects.py SALVO_TURN: CP[5]'),
        ('kSalvoTop', float(vc.SALVO_TOP), 'pylib/vcobjects.py SALVO_TOP: CP[6]'),
        ('kSalvoHomingDelay', int(vc.SALVO_HOMING_DELAY), 'pylib/vcobjects.py SALVO_HOMING_DELAY: CP[8]'),
        ('kSalvoSplitAfter', int(vc.SALVO_SPLIT_AFTER), 'pylib/vcobjects.py SALVO_SPLIT_AFTER: CP[12][1]'),
        ('kSalvoSplitCone', float(vc.SALVO_SPLIT_CONE), 'pylib/vcobjects.py SALVO_SPLIT_CONE: CP[13][2]'),
        ('kSalvoChildren', int(vc.SALVO_CHILDREN), 'pylib/vcobjects.py SALVO_CHILDREN: CP[15][2]'),
        ('kSalvoChildAccel', float(vc.SALVO_CHILD_ACCEL), 'pylib/vcobjects.py SALVO_CHILD_ACCEL'),
        ('kSalvoChildTurn', float(vc.SALVO_CHILD_TURN), 'pylib/vcobjects.py SALVO_CHILD_TURN'),
        ('kSalvoChildTop', float(vc.SALVO_CHILD_TOP), 'pylib/vcobjects.py SALVO_CHILD_TOP'),
        ('kSalvoChildHomingDelay', int(vc.SALVO_CHILD_HOMING_DELAY), 'pylib/vcobjects.py SALVO_CHILD_HOMING_DELAY'),
        ('kSalvoChildTopLow', float(vc.SALVO_CHILD_SPREAD[0]), 'pylib/vcobjects.py SALVO_CHILD_SPREAD[0]'),
        ('kGlowMark', float(vc.GLOW_MARK), 'pylib/vcobjects.py GLOW_MARK'),
    ]


def _literal(value: object) -> tuple[str, str]:
    """(the C++ declaration's type and name suffix, its literal)."""
    if isinstance(value, bool):
        raise TypeError(value)
    if isinstance(value, int):
        return 'int ', str(value)
    if isinstance(value, float):
        s = repr(value)
        return 'float ', (s if '.' in s or 'e' in s else s + '.0') + 'f'
    assert isinstance(value, str) and value.isascii() and '"' not in value and '\\' not in value, value
    return 'wchar_t ', f'L"{value}"'


def render() -> str:
    out = ['// Generated by tools/gen_py_consts.py from pylib/vcobjects.py and pylib/drill_model.py: do not edit (CI checks',
           '// it). The values the plugin shares with the Python that writes its files; the sources use pyconst::kX.',
           '#pragma once',
           '',
           'namespace pyconst {']
    for name, value, where in constants():
        kind, lit = _literal(value)
        decl = f'{name}[]' if kind == 'wchar_t ' else name
        out.append(f'inline constexpr {kind}{decl}={lit};   // {where}')
    out.append('}  // namespace pyconst')
    return '\n'.join(out) + '\n'


def unused() -> list[str]:
    """The constants no source of src/ (but this file) names as pyconst::kX."""
    text = []
    for d, _, files in os.walk(os.path.join(ROOT, 'src')):
        for f in files:
            if f.endswith(('.cpp', '.h', '.inc')) and f != 'py_consts.inc':
                with open(os.path.join(d, f), encoding='utf-8-sig') as fh:
                    text.append(srcscan.masked(fh.read()))
    joined = '\n'.join(text)
    return [n for n, _, _ in constants() if f'pyconst::{n}' not in joined]


def main(argv: list[str]) -> int:
    text = render()
    if '--check' in argv:
        try:
            with open(OUT, encoding='utf-8', newline='') as f:
                stale = f.read().replace('\r\n', '\n') != text
        except FileNotFoundError:
            stale = True
        if stale:
            print('src/py_consts.inc is stale: run python tools/gen_py_consts.py')
        idle = unused()
        if idle:
            print(f'src/py_consts.inc: used by no source: {idle}')
        return 1 if stale or idle else 0
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
