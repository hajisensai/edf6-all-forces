"""Cockpit fits for the air family (pylib/cockpit_fits.py merges FITS and DIGESTS; its docstring has the rules).

The stock helicopters' cockpits ride `body` (the fuselage: it pitches and rolls with the flight, the rotor turns on
its own bone). Eyes: over the seat locator 操縦席１ (tmp/stations/STATIONS.md), at a seated head's height, measured
with tools/cockpit_fit.py --probe (metres to the hull round the eye noted per row); the hull has no interior, so the
kit is the whole cabin (a real cockpit's model, cockpit_kits_air.py, scaled to the room) and the eye only has to sit
inside the fuselage with room for it: the more room, the nearer to full size the cockpit is.
"""
from __future__ import annotations

from cockpits import Fit

# N9 Eros (506) and its benefits skin (the same geometry, another digest): a narrow tandem gunship, the single seat at
# z 1.1 (locator y 1.45); the canopy roof at y ~2.33, the floor at ~0.8, +-0.45 m wide at the eye: room at
# (0, 1.95, 1.0) left/right 0.44, up 0.38, down 1.15, ahead 2.0.
_EROS = (Fit(0, 'body', (0.0, 1.95, 1.0), 'heli_gunship'),)

FITS: dict = {
    'V506_HELI': _EROS,
    'V506_HELI_EDF6BENEFITS': _EROS,
    # 602 (the 506 class, another hull): the seat at z 1.1; room at (0, 2.15, 1.0) left/right 0.4, up 0.68,
    # down 1.2, ahead 2.3.
    'V602_HELI': (Fit(0, 'body', (0.0, 2.15, 1.0), 'heli_gunship'),),
    # 409 (the chin gun 'balkan'): the seat locator at (0, 1.7, 2.95), the canopy low over it and rising toward the
    # back: the eye a little behind and below the first guess, (0, 2.15, 2.6): room left/right 0.49, up 0.65,
    # down 1.41, ahead 1.23 (at (0, 2.25, 2.7) the canopy's front bow came out through the hull).
    'VEHICLE409_HELI': (Fit(0, 'body', (0.0, 2.15, 2.6), 'heli_gunship'),),
    # 410 (the transport with the two pod guns): the pilot in the right seat as in the UH-60M whose cockpit the kit is
    # (heli_utility): over the seat locator (0, 2, 4.55) moved back where the nose is wide enough and 0.3 m right of
    # the centre line, (-0.3, 2.5, 3.8): room left 1.09, right 0.49, up 0.83, down 1.8, ahead 1.47 (the cockpit,
    # 1.31 m left of the pilot's eye to the far wall and 0.49 right, fits at 0.82 of full size; on the centre line,
    # room 0.73 either side, only at 0.54). The two guns are
    # pods on the stub wings (gun_emplacement_l/_r at x +-2.77, outside the cabin): nobody stands at them, so their
    # seats get the remote weapon station's display, the eye at the stock gun cameras (カメラ２ / カメラ３ on
    # gunCam_l / gunCam_r, a body child: the stock view turns with the rider's aim, not with the barrel).
    'VEHICLE410_HELI': (
        Fit(0, 'body', (-0.3, 2.5, 3.8), 'heli_utility'),
        Fit(1, 'gunCam_l', (3.337, 1.628, 0.432), 'remote_display'),
        Fit(2, 'gunCam_r', (-3.337, 1.628, 0.432), 'remote_display'),
    ),
}

DIGESTS: dict[str, str] = {
    'V506_HELI': '288e68797a5ec6d8abd7851e149ed4a4ea6b68914252b95e8a810d7bf0f9d367',
    'V506_HELI_EDF6BENEFITS': '1943529bb163ff90deff2150a38ecb92854a6523ca9d4c1b353ee8fd4ef8074e',
    'V602_HELI': '28360ea9a6ec5df54a60441ac3086ff053026623a6f1e77398c5432c84409a2f',
    'VEHICLE409_HELI': '315c8cfded2f8174199ca4eef47e27363008088ba899885e8f690287eeb4308b',
    'VEHICLE410_HELI': '0913f5b2ab1430e94a74760dccdd072acbb22f9c93d41dee61dbd9a488add4fc',
}
