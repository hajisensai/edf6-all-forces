"""Cockpit fits for the plugin's own vehicles, by the stem of the model their generator makes (tools/make_<x>.py
build() -> cockpits.graft_files: OBJECT/<STEM>.MRAB). Generated models need no DIGESTS: their geometry is this repo's.

The plugin's aircraft inherited the V506's seat locator (操縦席１ at (0, 1.45, 1.1): no eye in any of them), so each
cockpit's eye was found in its own model where its canopy, cockpit bump or bridge is (tmp/stations/STATIONS.md for
the bones; the room round each eye measured on the generated model as tools/cockpit_fit.py --probe measures a stock
one, quoted per row as metres left, right, up, down, ahead, back), and checked with
`tools/cockpit_preview.py <STEM> --generator <tool>` (the seat's view, the vehicle from outside).

Stems no generator makes yet (the gunship's, the transport plane's and the transport helicopter's models: their SGOs
point at the stock BOMBER401 / VEHICLE410_HELI models, which graft_files never sees; the stock BOMBER401's meshes are
not even skinned, so its hull cannot be measured) are fitted on the models their generator would have to make:
EDF6VC_GUNSHIP / EDF6VC_TRANSPORT_PLANE as jet_models makes EDF6VC_MULTIROLE of BOMBER401 but at full scale, and
EDF6VC_HELI_TRANSPORT a copy of VEHICLE410_HELI (preview: --stock-model VEHICLE410_HELI.MRAB). They do nothing until
make_jets writes those files and points the SGOs at them.
"""
from __future__ import annotations

from cockpits import Fit


def _cabin(bone: str, first: tuple[float, float, float], near: tuple[float, ...], far_x: float,
           far: tuple[float, ...], seats: range) -> tuple[Fit, ...]:
    """Passengers on a hold's two side benches: the first (`first`, facing across: yaw 90, toward +x) draws the hold
    (plane_hold), the others are eyes in it: on the near bench at those z (`near`, beside the first), on the far bench
    (x `far_x`, facing back across: yaw -90) at `far`, in seat order."""
    x, y, _ = first
    spots = [(first, 90.0)] + [((x, y, z), 90.0) for z in near] + [((far_x, y, z), -90.0) for z in far]
    if len(spots) != len(seats):
        raise ValueError(f'{len(spots)} passenger places for seats {seats}')
    return tuple(Fit(seat, bone, eye, 'plane_hold' if k == 0 else 'passenger', yaw)
                 for k, (seat, (eye, yaw)) in enumerate(zip(seats, spots)))


# The full-scale BOMBER401 (the gunship's and the transport plane's hull, jet_models' MULTIROLE recipe at scale 1):
# the flight deck in the cockpit bump, the pilot in the left seat (room 4.18, 5.18, 0.65, 2.67, 2.87, 4.36: the hull
# is a hall; the deck takes the size it wants); the hold behind it, a passenger on the right-hand bench looking across
# (yaw 90; after the hold came in off the hull: 3.44 aft, 6.34 forward, up 0.46, down 1.46; across, the hull is open
# into the wings: the hold's far wall stands 3.2 m off).
_B401_DECK = (0.5, 3.9, 4.0)
_B401_HOLD = (-1.3, 3.05, 0.9)

FITS: dict[str, tuple[Fit, ...]] = {
    # The player's and the NPC interceptors and the enemy fighter (BOMBER501's mesh at 0.65): the front seat of the
    # painted tandem canopy (room 0.77, 0.77, 0.35, 0.77, 3.15, 14.46): an F-35's cockpit.
    'EDF6VC_INTERCEPTOR': (Fit(0, 'bomber501', (0.0, 1.8, 6.8), 'jet_f35'),),
    # The fighter and the strike jet (BOMBER501 at full size): the same seat scaled (room 1.19, 1.19, 0.54, 1.18,
    # 4.84, 22.25): an F-16A's.
    'EDF6VC_JET': (Fit(0, 'bomber501', (0.0, 2.77, 10.46), 'jet_f16'),),
    # The multirole (BOMBER401 at 0.5): the cockpit bump over the wing's leading edge (room 2.18, 2.18, 0.32, 1.32,
    # 1.94, 1.71).
    'EDF6VC_MULTIROLE': (Fit(0, 'bomber401', (0.0, 2.0, 1.6), 'jet_f35'),),
    # The Primer fighter: low in the body under its red canopy (room 2.06, 2.07, 0.43, 1.97, 3.76, 4.39; in the canopy
    # itself its floor is 0.04 m under the eye). Its canopy's frame is the model's own: seen from the seat it arches
    # overhead and its rim crosses the view a little over the horizon.
    'EDF6VC_PRIMER_FIGHTER': (Fit(0, 'body', (0.0, 2.2, 0.4), 'jet_f35'),),
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
    # Proposed stems (the module docstring): the gunship's pilot and side gunner (its guns are the plugin's shells
    # leaving from beside the airframe: his seat sees the gun camera's monitor, on the left flank).
    'EDF6VC_GUNSHIP': (
        Fit(0, 'bomber401', _B401_DECK, 'plane_deck_gun'),
        Fit(1, 'bomber401', (2.6, 2.6, 1.0), 'remote_display', 90.0),
    ),
    # The transport plane's pilot and its twelve passengers in the hold (the far bench 2.7 m across).
    'EDF6VC_TRANSPORT_PLANE': (Fit(0, 'bomber401', _B401_DECK, 'plane_deck'),) + _cabin(
        'bomber401', _B401_HOLD, (0.2, 1.6, -0.5, 2.3, -1.0), 1.4, (0.9, 0.2, 1.6, -0.5, 2.3, -1.0), range(1, 13)),
    # The transport helicopter (410 with ten passengers): the stock 410's pilot and pod guns (pylib/cockpit_fits_air.py
    # VEHICLE410_HELI) and its cabin, the first passenger on the right-hand bench looking across (yaw 90; after the
    # hold came in off the round hull: 3.26 aft, 4.52 forward, up 0.60, down 0.92, 1.74 across, 0.29 behind).
    'EDF6VC_HELI_TRANSPORT': (
        Fit(0, 'body', (0.0, 2.6, 4.2), 'heli_utility'),
        Fit(1, 'gunCam_l', (3.337, 1.628, 0.432), 'remote_display'),
        Fit(2, 'gunCam_r', (-3.337, 1.628, 0.432), 'remote_display'),
    ) + _cabin('body', (-0.8, 2.0, 0.5), (-0.3, 1.3, -1.1, 2.1), 0.5, (0.5, -0.3, 1.3, -1.1, 2.1), range(3, 13)),
}
DIGESTS: dict[str, str] = {}
