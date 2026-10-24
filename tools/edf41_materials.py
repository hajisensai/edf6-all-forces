"""Plan EDF4.1-only diffuse compatibility files; never write the game directory.

The caller verifies its registry hashes first, passes only private EDF4.1 models,
then writes the returned files under its existing ledger owner. Shared EDF5/6
models must be omitted. Rebuild from original registry bytes when disabled.
"""
from __future__ import annotations

import os
import hashlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import edf41materials  # noqa: E402
import ledger  # noqa: E402
from modfiles import sha256_file  # noqa: E402

ALLOWED_OWNERS = frozenset(('edf41', 'edf41outfit', 'edf41maps', 'testrange', 'testrange_sub'))
needed_names = edf41materials.needed_names


def apply(files: dict[str, bytes], root6: str, enabled: bool = True) -> dict[str, bytes]:
    """Return transformed files plus only the shaders they use, or originals.

    Existing aliases must be unchanged files owned solely by this EDF4.1 suite
    or its registered test-range consumers (which can outlive the writer),
    with exactly the planned bytes. Untracked files and user edits are refused.
    The caller's existing install lock protects the later ledger transaction.
    """
    if not enabled:
        return dict(files)
    names = needed_names(files)
    if not names:
        # no aliased shader, but a gold coat (pylib/goldcoat.py) still converts: it needs no shader of ours
        return {rel: edf41materials.transform_models(rel, data) for rel, data in files.items()}
    shaders = edf41materials.build_shaders(root6, names)
    led = ledger.Ledger(root6)
    for rel, data in shaders.items():
        if any(ledger.key(other) == rel and payload != data for other, payload in files.items()):
            raise ValueError(f'{rel}: alias conflicts with an input file')
        if os.path.exists(led.disk(rel)):
            owners = set(led.owners(rel))
            if (not owners or not owners <= ALLOWED_OWNERS or led.changed(rel) or
                    sha256_file(led.disk(rel)) != hashlib.sha256(data).hexdigest()):
                raise ValueError(f'{rel}: another mod owns the shader alias or it was edited')
    out = {rel: edf41materials.transform_models(rel, data) for rel, data in files.items()}
    out.update(shaders)
    return out
