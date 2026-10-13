"""Cockpit fits for the plugin's own vehicles, by the stem of the model their generator makes (tools/make_<x>.py
build() -> cockpits.graft_files: OBJECT/<STEM>.MRAB). Generated models need no DIGESTS: their geometry is this repo's.

The plugin's aircraft inherited the V506's seat locator (操縦席１ at (0, 1.45, 1.1): no eye in any of them), so each
cockpit's eye was found in its own model where its canopy, cockpit bump or bridge is (tmp/stations/STATIONS.md for
the bones; the room round each eye measured on the generated model as tools/cockpit_fit.py --probe measures a stock
one, quoted per row as metres left, right, up, down, ahead, back), and checked with
`tools/cockpit_preview.py <STEM> --generator <tool>` (the seat's view, the vehicle from outside).

The gunship's, the transport plane's and the transport helicopter's models are the stock ones they fly, under names of
their own (tools/make_jets.py): EDF6VC_GUNSHIP / EDF6VC_TRANSPORT_PLANE the stock BOMBER401's geometry as it is, its mesh
skinned (pylib/jet_models.py STOCK_COPIES: the stock is rigid, and a cockpit measures its room against skinned meshes),
EDF6VC_HELI_TRANSPORT the stock VEHICLE410_HELI archive byte for byte (its digest the 410's: make_jets
heli_transport_model).
"""
from __future__ import annotations

import cockpit_kits_plugin as plugin_kits
from cockpit_fits_air import FITS as _AIR
from cockpits import Fit

Vec3 = tuple[float, float, float]


def _hold(bone: str, first: Vec3, seats: range) -> tuple[Fit, ...]:
    """Passengers on the C-130J hold's two troop-seat runs (cockpit_kits_plugin HOLD, at HOLD_SCALE): the first
    (`first`, facing across: yaw 90, toward +x) draws the hold (plane_hold); the others are eyes in it where its seats
    are at that scale: on his run beside him (HOLD_ALONG, toward the tail: -z), on the far run (HOLD_ACROSS ahead of
    him, facing back across: yaw -90) at HOLD_FAR_ALONG, in seat order."""
    x, y, z = first
    k = plugin_kits.HOLD_SCALE
    spots = ([(first, 90.0)] + [((x, y, z - a * k), 90.0) for a in plugin_kits.HOLD_ALONG]
             + [((x + plugin_kits.HOLD_ACROSS * k, y, z - a * k), -90.0) for a in plugin_kits.HOLD_FAR_ALONG])
    return _seated(bone, spots, seats, 'plane_hold')


def _heli_cabin(bone: str, first: Vec3, seats: range) -> tuple[Fit, ...]:
    """Passengers in the UH-60M cabin (cockpit_kits_plugin CABIN, at CABIN_SCALE): the first (`first`, in the
    forward-facing row: yaw 0) draws the cabin (heli_cabin); the others are eyes in its seats at that scale
    (CABIN_SEATS: left, ahead of the first one's eye at full size, their facing), in seat order."""
    x, y, z = first
    k = plugin_kits.CABIN_SCALE
    spots = [(first, 0.0)] + [((x + left * k, y, z + ahead * k), yaw) for left, ahead, yaw in plugin_kits.CABIN_SEATS]
    return _seated(bone, spots, seats, 'heli_cabin')


def _seated(bone: str, spots: list[tuple[Vec3, float]], seats: range, kit: str) -> tuple[Fit, ...]:
    """The first of `spots` (eye, yaw) draws `kit`, the rest are passengers' eyes in it, one per seat."""
    if len(spots) < len(seats):
        raise ValueError(f'{len(spots)} passenger places for seats {seats}')
    return tuple(Fit(seat, bone, (round(eye[0], 4), round(eye[1], 4), round(eye[2], 4)),
                     kit if n == 0 else 'passenger', yaw)
                 for n, (seat, (eye, yaw)) in enumerate(zip(seats, spots)))


# The stock BOMBER401 (the gunship's and the transport plane's hull, jet_models.STOCK_COPIES: its own frame, the belly
# 0.127 m over the origin): the flight deck in the cockpit bump, the pilot in the left seat (room 4.16, 5.16, 0.64, 2.67,
# 2.87, 4.35: the hull is a hall; under the bump's low roof the deck is scaled down); the hold behind and under it, the
# first passenger on the right-hand troop seats looking across (yaw 90). The hull's section there is a wide flat hall
# open into the wings, round in its corners: the box round that eye that the hull closes (every face point enclosed,
# seen along the axes and the diagonals; a 0.1 m grid) is 1.35 m aft, 2 m forward, 1.5 m up, 1.0 m down, 2.2 m across
# and 0.4 m behind him; the hold at HOLD_SCALE fits it (its roof 1.33 m up, its floor 0.95 m down, its far wall 1.98 m
# across), cut 1 m forward of him (the deck's floor is over the hold beyond).
_B401_DECK = (0.5, 3.03, 4.0)
_B401_HOLD = (-1.0, 1.7, 2.6)
# The 410's cabin (the transport helicopter's): the first passenger in the forward-facing row right of the middle, the
# box the hull closes round him 0.98 m left, 0.68 right, 0.73 up, 0.83 down, 1.98 ahead, 0.83 behind (its cabin doors
# stand open behind that); the UH-60M's cabin at CABIN_SCALE fits it (its walls 0.92 / 0.63 m out, its floor 0.73 m
# down, the cockpit 1.9 m ahead), its rearmost row cut off.
_410_CABIN = (-0.15, 1.9, 0.3)

FITS: dict[str, tuple[Fit, ...]] = {
    # The player's and the NPC interceptors and the enemy fighter (BOMBER501's mesh at 0.65): the front seat of the
    # painted tandem canopy (room 0.77, 0.77, 0.35, 0.77, 3.15, 14.46): a modern fighter's cockpit (the J-10S's).
    'EDF6VC_INTERCEPTOR': (Fit(0, 'bomber501', (0.0, 1.8, 6.8), 'jet_j10s'),),
    # The fighter and the strike jet (BOMBER501 at full size): the same seat scaled (room 1.19, 1.19, 0.54, 1.18,
    # 4.84, 22.25): an F-16's (the F-16D Block 60's front cockpit).
    'EDF6VC_JET': (Fit(0, 'bomber501', (0.0, 2.77, 10.46), 'jet_f16'),),
    # The multirole (BOMBER401 at 0.5): the cockpit bump over the wing's leading edge (room 2.18, 2.18, 0.32, 1.32,
    # 1.94, 1.71; lower, the nose gear's struts stand over the panel). The bump is narrow: seen along the diagonals the
    # box round the eye is closed only 0.23 m either side and 0.18 m up, so it takes the narrow cockpit (scaled to it).
    # x 2 mm off the centre plane: a point exactly on it is seen through the seam of the hull's two halves.
    'EDF6VC_MULTIROLE': (Fit(0, 'bomber401', (0.002, 2.0, 1.6), 'jet_j10s_narrow'),),
    # The Primer fighter: low in the body under its red canopy (room 2.06, 2.07, 0.43, 1.97, 3.76, 4.39; in the canopy
    # itself its floor is 0.04 m under the eye). Its canopy's frame is the model's own: seen from the seat it arches
    # overhead and its rim crosses the view a little over the horizon.
    'EDF6VC_PRIMER_FIGHTER': (Fit(0, 'body', (0.0, 2.2, 0.4), 'jet_j10s'),),
    # The drones (blast / doll / airstrike): nobody aboard; the seat sees the nose camera's monitor.
    'EDF6VC_DRONE': (Fit(0, 'body', (0.0, 1.3, 2.4), 'remote_display'),),
    # The air carrier (V508 at 1.6): a flight deck in the nose, the pilot left (room 2.23, 2.24, 1.83, 2.81, 2.30,
    # 56.75 at the centre line).
    'EDF6VC_CARRIER': (Fit(0, 'body', (0.5, 9.0, 26.0), 'plane_deck'),),
    # The submarine carrier (EV603_MARINE): the bridge high in the tower's front (room 10.6, 10.6, 7.6, 54.5, 6.6, 61.9:
    # the tower is hollow; the deck takes what it wants).
    'EDF6VC_SUB': (Fit(0, 'body', (0.5, 332.0, -192.0), 'plane_deck'),),
    # The Katyusha: the V607 truck's cab (the same room as the stock truck's: 1.97, 0.77, 1.13, 0.68, 0.94, 1.21); its
    # driver fires the launcher, its read-outs on the cab's screens.
    'EDF6VC_KATYUSHA': (Fit(0, 'body', (-0.6, 2.05, 3.0), 'truck_cab'),),
    # The sidecar: the Freed bike's rider (the bike's own crouched eye; the stock dashboard stays). It has no second
    # seat: the sidecar carries no crew.
    'EDF6VC_SIDECAR': (Fit(0, 'body', (0.0, 1.586, -0.069), 'bike_rider'),),
    # The twin-gun artillery: the gunner in the turret between the two guns, high under its roof (room after the
    # station came in off the roof 0.74, 0.75, 0.23, 1.27, 1.52, 0.58): lower, at the guns' height, their breeches'
    # angular backs filled the sight; from up here the view runs ahead between them.
    'EDF6VC_ARTILLERY': (Fit(0, 'cannon_main', (0.0, 2.65, -0.3), 'tank_gunner'),),
    # The drill tank: from inside its turret the drill and its mount fill every view ahead (the imported model is
    # modelled inside; a tank_gunner station's sight looked at the mount's back wall from every place tried), so its
    # one seat sees a camera's monitor (remote_display): the camera on the turret's right flank beside the drill,
    # looking ahead past it (the drill at the view's left edge).
    'EDF6VC_DRILL': (Fit(0, 'cannon_main', (-1.9, 2.6, 1.5), 'remote_display'),),
    # The Sazabi: the user's model is modelled inside (armour plates, joints, the head's cables overlap through the
    # chest), so from any eye in its chest or head the plates' inner faces fill the view (a mech pod at (0, 16.6, 0.5)
    # in the chest looked into the collar's armour). Its pilot sees the monoeye camera's picture instead, as a
    # Gundam's panoramic monitor does (remote_display): the camera before the head's face, looking ahead over the
    # chest.
    'EDF6VC_SAZABI': (Fit(0, 'sz_head', (0.0, 20.4, 2.4), 'remote_display'),),
    # The gunship's pilot and side gunner (its guns are the plugin's shells leaving from beside the airframe: his seat
    # sees the gun camera's monitor, on the left flank).
    'EDF6VC_GUNSHIP': (
        Fit(0, 'bomber401', _B401_DECK, 'plane_deck_gun'),
        Fit(1, 'bomber401', (2.6, 1.73, 1.0), 'remote_display', 90.0),
    ),
    # The transport plane's pilot and its twelve passengers in the hold (the far run 1.75 m across).
    'EDF6VC_TRANSPORT_PLANE': (Fit(0, 'bomber401', _B401_DECK, 'plane_deck'),) + _hold('bomber401', _B401_HOLD,
                                                                                       range(1, 13)),
    # The transport helicopter (410 with ten passengers): the stock 410's pilot and pod guns (pylib/cockpit_fits_air.py
    # VEHICLE410_HELI, the same model) and its cabin's passengers in the UH-60M's troop seats.
    'EDF6VC_HELI_TRANSPORT': _AIR['VEHICLE410_HELI'] + _heli_cabin('body', _410_CABIN, range(3, 13)),
}
DIGESTS: dict[str, str] = {}
