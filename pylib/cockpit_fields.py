"""The one table of a physical cockpit instrument's readings and of a weapon's round classes.

A widget bone's name carries a field's number (pylib/cockpit_instruments.py builds them) and the plugin poses it by
the same number (src/cockpit_instruments.h Field); a weapons screen's picture is picked by a round class's number
(src/hud_cue.h CockpitRound). tools/gen_cockpit_fields.py writes both C++ enums from this table into
src/cockpit_fields.inc (CI: --check), so the two sides cannot drift apart.

Fields are appended only: a model built before a field keeps every older field's number.
"""
from __future__ import annotations

ROWS = 3   # the weapons screen's cells: a window over the seat's weapons

# (C++ name, Python name, how many consecutive numbers): the fields in their numbers' order.
FIELDS = (
    ('hp', 'HP', 1), ('hull', 'HULL', 1), ('fuel', 'FUEL', 1), ('speed', 'SPEED', 1), ('altitude', 'ALT', 1),
    ('heading', 'HEADING', 1), ('slot', 'SLOT', 1), ('ammo', 'AMMO', 1), ('warn', 'WARN', 1), ('climb', 'CLIMB', 1),
    ('pitch', 'PITCH', 1), ('power', 'POWER', 1),
    # The weapons screen's (cockpit_instruments.h ArmReadings).
    ('arm', 'ARM', 1), ('mag', 'MAG', 1), ('load', 'LOAD', 1), ('empty', 'EMPTY', 1), ('pick', 'PICK', 1),
    ('row', 'ROW', ROWS), ('rowRound', 'ROWROUND', ROWS),   # + i: cell i's rounds (its bar), its round class
    ('roll', 'ROLL', 1),
)

# Round classes, in their numbers' order: (C++ name, the tag the weapons screen shows).
ROUND_CLASSES = (
    ('mg', 'MG'), ('gun', 'GUN'), ('ap', 'AP'), ('he', 'HE'), ('heat', 'HEAT'), ('can', 'CAN'), ('beam', 'BEAM'),
    ('rkt', 'RKT'), ('msl', 'MSL'), ('aam', 'AAM'), ('bomb', 'BOMB'), ('spc', 'SPC'),
)


def numbers() -> dict[str, int]:
    """Each field's Python name -> its (first) number, and COUNT."""
    out, at = {}, 0
    for _cpp, py, width in FIELDS:
        out[py] = at
        at += width
    out['COUNT'] = at
    return out


_n = numbers()
HP, HULL, FUEL, SPEED, ALT, HEADING, SLOT, AMMO, WARN, CLIMB, PITCH, POWER = (
    _n[k] for k in ('HP', 'HULL', 'FUEL', 'SPEED', 'ALT', 'HEADING', 'SLOT', 'AMMO', 'WARN', 'CLIMB', 'PITCH', 'POWER'))
ARM, MAG, LOAD, EMPTY, PICK, ROW, ROWROUND, ROLL, COUNT = (
    _n[k] for k in ('ARM', 'MAG', 'LOAD', 'EMPTY', 'PICK', 'ROW', 'ROWROUND', 'ROLL', 'COUNT'))
ROUNDS = tuple(tag for _cpp, tag in ROUND_CLASSES)
MG, GUN, AP, HE, HEAT, CAN, BEAM, RKT, MSL, AAM, BOMB, SPC = range(len(ROUNDS))
del _n
