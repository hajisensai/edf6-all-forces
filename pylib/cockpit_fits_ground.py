"""Cockpit fits for the ground family (pylib/cockpit_fits.py merges FITS and DIGESTS; its docstring has the rules).
Eyes measured with tools/cockpit_fit.py (--search / --probe) from the seats in tmp/stations/STATIONS.md, each checked
with tools/cockpit_preview.py (previews in tmp/cockpit_preview/ground/). Rooms quoted: metres to the hull left, right,
up, down, ahead, back of the eye. The Japanese trucks drive on the right (their stock driver's seat is at -x)."""
from __future__ import annotations

from cockpits import Fit

_PICKUP = (
    # The driver fires the bed's weapon himself; his cab on the body (room 1.19, 0.55, 0.54, 1.10, 1.14, 0.91; the
    # windscreen is steeply raked, the roof curves down to the doors: pickup_cab).
    Fit(0, 'body', (-0.32, 1.55, 0.0), 'pickup_cab'),
)
_KEI = (
    # The stock cab is modelled inside (dashboard, wheel, seats): screens only (room 1.22, 0.42, 0.54, 0.45, 0.59,
    # 0.25). The four others ride in the open: on the bed's sides (1, 2) and on the tailgate (3, 4).
    Fit(0, 'body', (-0.4, 1.35, 1.05), 'kei_dash'),
    Fit(1, 'body', (-1.25, 1.55, 0.3), 'open_rider'),
    Fit(2, 'body', (1.25, 1.55, 0.3), 'open_rider'),
    Fit(3, 'body', (-0.4, 1.55, -2.25), 'open_rider'),
    Fit(4, 'body', (0.4, 1.55, -2.25), 'open_rider'),
)
_RESCUE = (
    # The driver up front behind the big windows (room 1.95, 1.95, 1.46, 2.55, 0.80, 3.05: a tracked vehicle's yoke);
    # the four in the stock-modelled ambulance bay (benches, monitor).
    Fit(0, 'body', (0.0, 3.4, 2.2), 'vehicle_cab_yoke'),
    Fit(1, 'body', (0.48, 2.541, -3.5), 'troop_bay_l'),
    Fit(2, 'body', (-0.48, 2.541, -3.5), 'troop_bay_r'),
    Fit(3, 'body', (0.48, 2.541, -2.2), 'troop_bay'),
    Fit(4, 'body', (-0.48, 2.541, -2.2), 'troop_bay'),
)
_NAEGLING = (
    # The launcher's crew sits in the front hull (the rocket turret behind has no room: up 0.03 at its roof); the
    # driver-operator front left (room 0.91, 1.61, 0.66, 1.29, 1.90, 5.14), a tracked vehicle's yoke.
    Fit(0, 'body', (0.35, 1.6, 1.4), 'tracked_cab'),
)
_E551 = (
    # The gunner in the turret right of the gun, as an M1A1's (room 1.69, 0.79, 0.34, 0.32, 1.68, 2.52; a low turret
    # whose roof slopes down ahead: the station comes in to 1.52, 0.57, 0.25, 0.29 off its slopes); the gun's cradle
    # fills the vision block's left third, as a breech beside a real gunner does.
    Fit(0, 'cannon_main', (-0.45, 1.7, 0.6), 'tank_gunner'),
)

FITS: dict[str, tuple[Fit, ...]] = {
    # Grape (401): the driver front left in the hull (the remote turret has no room; room 0.74, 1.64, 0.48, 0.80,
    # 1.63, 0.92; the hull leaks at its seams, so the cab comes in a lot: apc_cab, tucked in at the floor); the four in the stock-modelled troop bay (seats 1/3 and 2/4 share a seat locator: their cameras
    # sit 0.12 m left / right of it).
    'VEHICLE401_STRIKER': (
        Fit(0, 'mainBody', (0.3, 1.3, 1.6), 'apc_cab'),
        Fit(1, 'mainBody', (0.4, 1.5, -1.79), 'troop_bay_l'),
        Fit(2, 'mainBody', (-0.4, 1.5, -1.79), 'troop_bay_r'),
        Fit(3, 'mainBody', (0.4, 1.5, -0.772), 'troop_bay'),
        Fit(4, 'mainBody', (-0.4, 1.5, -0.772), 'troop_bay'),
    ),
    'VEHICLE402_ROCKET': _NAEGLING,
    # Titan (404): the commander-gunner in the main turret right of the gun (room 5.42, 2.42, 0.46, 1.31, 2.41, 9.49:
    # the turret is a hall, the kit takes what it wants of it; on the axis the gun's breech filled the slot 0.85 m
    # ahead); the two side cannons are unmanned sub-turrets: their seats see the guns' camera monitors
    # (remote_display), the eye at each turret's sight glass (the optics' vc_optic_01 / 02) on the gun's pitch bone.
    'VEHICLE404_BIGTANK': (
        Fit(0, 'cannon_main', (-1.5, 5.0, 2.0), 'tank_gunner'),
        Fit(1, 'subCannon_A_aim', (2.0396, 7.6543, -3.9414), 'remote_display'),
        Fit(2, 'subCannon_B_aim', (-2.0396, 7.6543, -3.9414), 'remote_display'),
    ),
    'V402_ROCKET_EDF6BENEFITS': _NAEGLING,
    # Bikes (503, 613): the rider (hidden by the plugin) at the stations' crouched-rider eye; the stock dashboard on
    # the handlebars stays, two small screens beside its gauges.
    'V503_BIKE': (Fit(0, 'body', (0.0, 1.586, -0.069), 'bike_rider'),),
    'V503_BIKE_EDF6BENEFITS': (Fit(0, 'body', (0.0, 1.586, -0.069), 'bike_rider'),),
    'V613_BIKE': (Fit(0, 'body', (0.0, 1.626, -0.048), 'bike_rider'),),
    'V505_TANK': _E551,
    'V505_TANK_EDF6BENEFITS': _E551,
    'V507_RESCUETANK': _RESCUE,
    'V507_RESCUETANK_HAPPY': _RESCUE,   # the SIAWASE paint (v507_rescuetank_happy): the same hull
    'V512_KEITRUCK': _KEI,
    'V512_KEITRUCK_BGP': _KEI,
    # Trailer truck cab (513): modelled inside like the kei truck; screens only (room 1.97, 0.47, 0.45, 0.78, 0.79,
    # 1.03).
    'V513_TRAILERTRUCK01CAB': (
        Fit(0, 'body', (-0.75, 2.45, 2.1), 'trailer_dash'),
    ),
    # Robo truck (607): the driver in the flat-fronted cab (room 1.97, 0.77, 1.12, 0.70, 0.94, 1.21).
    'V607_ROBOTRUCK': (
        Fit(0, 'body', (-0.6, 2.05, 3.0), 'truck_cab'),
    ),
    # Kepler (603): the gunner in the turret a little left of its middle, between the twin guns' cradles (room 0.32,
    # 0.75, 0.29, 0.49, 1.76, 1.15 after the kit came in off the turret's slopes; further out a cradle is 0.28 m away,
    # too near for the console).
    'V603_FLAK': (
        Fit(0, 'cannon_main', (0.3, 2.2, 0.0), 'tank_gunner'),
    ),
    # Maser (510): the operator in the front of the turret's box right of its axis (room 0.44, 1.82, 1.31, 1.53, 0.75,
    # 6.60); higher up, under the emitter's boom, the slot looked into the boom and its struts came through the room.
    'V510_MASER': (
        Fit(0, 'cannon_main', (-1.0, 5.5, 1.5), 'tank_gunner'),
    ),
    'V601_TANK': (
        # The gunner on the turret's axis behind the gun (the stock turret is hollow, no breech; room 1.26, 1.26, 0.42,
        # 0.44, 1.42, 2.26; off the axis the turret's slopes leave too little below the eye).
        Fit(0, 'cannon_main', (0.0, 1.9, 0.0), 'tank_gunner'),
    ),
    'V610_PTRUCK_GUN': _PICKUP,
    'V611_PTRUCK_ROCKET': _PICKUP,
}

DIGESTS: dict[str, str] = {
    'V503_BIKE': 'bec72a62adca8e00aeb32bac0452d3019b2527a74221088cfa34a973961bfa01',
    'V503_BIKE_EDF6BENEFITS': '57ff72523f79465239fc557e8c62328efcedfb67cedbc61b637d912f6665c319',
    'V613_BIKE': '23578e80f04b75d18a3eaec56fe7cdfb0cf8fa3473e2b1b15e194929dfbb7905',
    'V402_ROCKET_EDF6BENEFITS': 'd7f9bf802893eca1a96980837435d13a98a43e5397f4b580ba8407f63b17c4b1',
    'V505_TANK_EDF6BENEFITS': '430b46f3686ab37283e4dabbb9a2a39741553db397184c314f572a7a2a65fe2f',
    'V507_RESCUETANK': '06138f5835c7ca47a6ca44def67e52e9264003fbc624c399c364183d2ecb79f4',
    'V507_RESCUETANK_HAPPY': '0dfc81ca1700dc9dc43516a9349c265f47950c68095ab441fd4d5468bd11f545',
    'V512_KEITRUCK': '9360d8a8193c910fd8de3ee57e01b5ca798658ff3965ca2dc5fcd95e349c01ff',
    'V512_KEITRUCK_BGP': '38c1169dd5fc2306de802bd7b7c33d725b5a9baa7f29961d5e8cf71eb3188f5d',
    'V513_TRAILERTRUCK01CAB': 'bef70584c99e264a9e372bf54b17b9e95569414d56688e6ae76b0c9f61fffe4f',
    'V607_ROBOTRUCK': 'eb94c16a9238434872ace9986541e5338641022d70fe0b2e91b95c80c66fe0fd',
    'V610_PTRUCK_GUN': 'a4977efc24792e702c11a4dd3ffe50f58429946c84d32508203cd65544f0d573',
    'V611_PTRUCK_ROCKET': '051178032741087ac0bc959644564ae62c0630f968e3370bb8e3cd95d0fdd953',
    'V603_FLAK': '17e007c959575d55faf1f69d48adacb1114fd0a7bcb7ee37542916097e9efb7a',
    'V510_MASER': '9b074aa8d857ee881f7736ee5ff20bab2aa9ba51e4f94c06ae8f6ea176e17cf6',
    'VEHICLE401_STRIKER': 'f54ebc238331b9b357ee6b01ad74ec8f4d442695a3db66bfeea69ff5f81fe0f9',
    'VEHICLE402_ROCKET': 'ee49bd49f106c3300c6a8cab423740f58f4bf4a28dedb382c7339781b90532ed',
}
