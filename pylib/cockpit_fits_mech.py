"""Cockpit fits for the mech family (pylib/cockpit_fits.py merges FITS and DIGESTS; its docstring has the rules).

A mech's pilot is hidden and his seat locator is either on the ground (Begaruta, Nix, the old robot, the crawler:
globalSRT) or a chest bone's origin, so the pilots' eyes were searched for (the points in the torso round which a box
stays enclosed by the armour, cockpits.exposed; tmp/mech_scratch/growbox.py) and each pod (pylib/cockpit_kits_mech.py
PODS) is that box. Each pilot's cockpit rides the bone that carries the torso's aim, so it turns with the view:
Begaruta / Nix `body` (under `spine`, the two aim bones of begaruta_pilot_class, docs/nix-re.md), the old robot and
Balam `mune`, the crawler `body`, Proteus `neck`.

Proteus's (407 and Mk2) gunners sit at remote weapons (the side cannons and the missile / rocket pod: nobody stands at
them), so their seats are cockpit_kits.remote_display: the eye is a sight camera just over the barrel's breech /
over the pod, riding the barrel's pitch bone (the gun mount's yaw above it) or the pod's own bone (where the stock
launcher camera rides too), the whole view the operator's monitor.

Skins share their base model's geometry and fit (v612_nix_blue, the stock Nix, and four skins whose model has no SGO of
its own name are added at the bottom).
"""
from __future__ import annotations

from cockpits import Fit

BEGARUTA = (Fit(0, 'body', (0.0, 7.2, -0.3), 'mech_begaruta'),)
NIX = (Fit(0, 'body', (0.0, 7.2, -0.3), 'mech_nix'),)
GROUNDROBO = (Fit(0, 'body', (0.0, 2.1, -0.3), 'mech_groundrobo'),)
BALAM = (Fit(0, 'mune', (0.0, 43.0, 5.5), 'mech_large'),)

FITS: dict[str, tuple[Fit, ...]] = {
    'V504_BEGARUTA': BEGARUTA,
    'V504_BEGARUTA_BLUE': BEGARUTA,
    'V504_BEGARUTA_YELLOW': BEGARUTA,
    'V504_BEGARUTA_RED': BEGARUTA,
    'V504_BEGARUTA_GOLD': BEGARUTA,
    'V504_BEGARUTA_PINK': BEGARUTA,
    'V612_NIX_BLACK': NIX,
    'V612_NIX_RED': NIX,
    'V608_OLDROBOT': (Fit(0, 'mune', (0.0, 7.2, -0.2), 'mech_oldrobot'),),
    'VEHICLE502_GROUNDROBO': GROUNDROBO,
    'VEHICLE502_GROUNDROBOGOLD': GROUNDROBO,
    'V605_BARGA_CANNON': BALAM,  # same enclosed chest station, measured on the cannon model
    'V515_RETROBALAM': BALAM,
    'V515_RETROBALAM_GREEN': BALAM,
    'V515_RETROBALAM_GOLD': BALAM,
    'V515_RETROBALAM_GRAY': BALAM,
    'V614_PROTEUS_MK2': (
        Fit(0, 'neck', (0.0, 12.8, 3.0), 'mech_large'),
        Fit(1, 'gun_mount_barrel_l', (6.87, 14.35, 3.0), 'remote_display'),
        Fit(2, 'gun_mount_barrel_r', (-6.87, 14.35, 3.0), 'remote_display'),
        Fit(3, 'missile_launcher', (0.0, 10.15, 3.6), 'remote_display'),
    ),
    'VEHICLE407_BIGBEGARUTA': (
        Fit(0, 'neck', (0.0, 13.2, 3.0), 'mech_large'),
        Fit(1, 'gun_mount_barrel_l', (5.47, 12.9, 1.2), 'remote_display'),
        Fit(2, 'gun_mount_barrel_r', (-5.47, 12.9, 1.2), 'remote_display'),
        Fit(3, 'rocket_launcher', (0.0, 10.7, 3.0), 'remote_display'),
    ),
}

DIGESTS: dict[str, str] = {
    'V605_BARGA_CANNON': '16d0f3aa0ea1611d72b2426b4249df0c68d884f70cd10f9fc10f24669ce28c24',
    'V515_RETROBALAM': '523667315adf5e9eaa48a082f820a6764f065d3d4a34510fcd3249e6a538133b',
    'V515_RETROBALAM_GREEN': '85355689c9fb7307eacf73082223fb7994558f57499ac5f0e528ae2e3a7ccff9',
    'V515_RETROBALAM_GOLD': 'ee8058ef915280f1960cb593068c297826d120cacc18705cb47068c56ff0dca2',
    'V515_RETROBALAM_GRAY': '152c7a895bb625248d449f0d26db36472c1de69d796adbb02019f8012fc3c451',
    'V504_BEGARUTA': '18ba70eaac254f9ad34c27665eaca9231b638eb022d4c37b7cab6a6c37163a35',
    'V504_BEGARUTA_BLUE': 'f8b6f7c91f8837f91a8c1d9aa6999be1a078fb6469417fbf151199d3a50e81fa',
    'V504_BEGARUTA_YELLOW': '40045f85ae80c92b0efbb81a771e955aabfee1eab60eb8a1d4250b499068b076',
    'V504_BEGARUTA_RED': '4f4b09a04c7b6ef88438fe39ad6887b0d399dd16aab87003ce10fac56d7ce3e1',
    'V504_BEGARUTA_GOLD': '1f3973d3852f150dcd10202ed5b7cdeccb98d035feb2d0b10c69151ef90080c2',
    'V504_BEGARUTA_PINK': 'd735a61501dfb94a85875016d0ca8bcee74024f7f1c61723dfdfc35cc9d6c12a',
    'V612_NIX_BLACK': 'dc12f39fa74f5d140e5f57c8d8e4da6bd628fbb19c1d883328ce24d784904ecc',
    'V612_NIX_RED': '63042ba667c2ac3f58d63f77ff22e37f5aaf053a78d425d6c11df2c35062bf06',
    'V608_OLDROBOT': '044a65608b8d632ecb4b9c6465b8802e8747674c35d30b5d66b678b319bc6220',
    'VEHICLE502_GROUNDROBO': 'a3a57d3c80ad5f09745a8977f3436510640fe7c8d42fa8792b724c523d21554a',
    'VEHICLE502_GROUNDROBOGOLD': '6fd5afe7872864da6bf8ffd2bef4e3957492962def6495f6596d090385f6f500',
    'V614_PROTEUS_MK2': 'e9fb338e6e2cf8c3c2cb8693313254dbb1d7c83e967973ca5766c6b47480b2ed',
    'VEHICLE407_BIGBEGARUTA': 'bb1e36b5391d6b4f153ca9b58e5f63d37c266eda16f75d4de5090efee36d23f5',
}

# Skins whose model has no SGO of its own name (V612_NIX_BLUE ...): fitted like their base models.
for _stem, (_fits, _digest) in {
    'V612_NIX_BLUE': (NIX, '918d6d87d5f1d9726e1e176cd9a0f9b5a194d65861c16bc1a084c59177b22a62'),
    'V504_BEGARUTA_GRAY': (BEGARUTA, 'e10d20a36a59425d6c985b8154700442c63ec8606da4ec2d7cc0b76e7a8ae8ab'),
    'V504_BEGARUTA_HAISPEED': (BEGARUTA, '0451d9ce721917efb1cfd8ab42eeda5775f46f49ec8ebe25f7f04452d00fdf71'),
    'V515_RETROBALAM_ACE': (BALAM, 'e10a5f6531683b524809a6ce4ca791c8bd224c8b7a625075b389c39d210454ec'),
    'V515_RETROBALAM_ULT': (BALAM, 'f7a1b62997a9c09272a34260af7a8b3567131287939b34d25b8a03c63e75f853'),
}.items():
    FITS[_stem] = _fits
    DIGESTS[_stem] = _digest
