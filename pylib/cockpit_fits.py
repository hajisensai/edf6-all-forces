"""Where each seat's cockpit sits in each vehicle model (pylib/cockpits.py Fit), by the model's stem.

FITS: stem -> the seats' fits. DIGESTS: stem -> the sha-256 of the stock model a fit was measured on, for the stems
the optics do not audit already (vehicle_optics.MODELS; a model of another geometry is left alone:
`python tools/cockpit_fit.py STEM --digest` prints it). Each family's fits are in pylib/cockpit_fits_<family>.py
(their FITS and DIGESTS merged here).

Eyes are model-space rest positions read from the vehicles' own data (tmp/stations/STATIONS.md, made by
tmp/stations/extract.py from Root.cpk: turret axes and roofs, seat and camera locators, the optics' markers), then
moved where the kit's seat wants the eye (e.g. beside the gun, not on its axis). Each is checked offline with
tools/cockpit_preview.py (the view from the eye, and the vehicle from outside: nothing of the cockpit shows through
its hull). In-game checks are pending.
"""
from __future__ import annotations

from cockpits import Fit

FITS: dict[str, tuple[Fit, ...]] = {
    # Blacker (403): the gunner in the turret's dome beside the gun's cradle (measured room: left 0.24 to the cradle,
    # right 0.98, up 0.27, down 0.37, ahead 1.25, back 1.26; the turret's yaw bone cannon_main, so the cockpit turns
    # with the turret, not with the gun's elevation. The stations' first guess, 0.6 m behind the trunnion, was over
    # the rear deck, outside: the trunnion is behind the dome); the two roof machine guns' gunners at their sights
    # (the optics' vc_optic_01 / 02, on the guns' pitch bones).
    'VEHICLE403_TANK': (
        Fit(0, 'cannon_main', (-0.45, 1.8, 0.0), 'tank_gunner'),
        Fit(1, 'MachineGun_A_aim', (1.0311, 2.9768, -0.4158), 'gun_station'),
        Fit(2, 'MachineGun_B_aim', (-1.0311, 2.9768, -0.4158), 'gun_station'),
    ),
}

DIGESTS: dict[str, str] = {}


for _name in ('cockpit_fits_ground', 'cockpit_fits_air', 'cockpit_fits_mech'):
    _mod = __import__(_name)
    for _table, _more in ((FITS, _mod.FITS), (DIGESTS, _mod.DIGESTS)):
        if set(_more) & set(_table):
            raise ValueError(f'{_name}: stems fitted twice: {sorted(set(_more) & set(_table))}')
        _table.update(_more)
