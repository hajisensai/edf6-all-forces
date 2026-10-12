"""Cockpit fits for the plugin's own vehicles, by the stem of the model their generator makes (tools/make_<x>.py
build() -> cockpits.graft_files: OBJECT/<STEM>.MRAB). Generated models need no DIGESTS: their geometry is this repo's."""
from __future__ import annotations

from cockpits import Fit  # noqa: F401 - the rows below

FITS: dict = {}
DIGESTS: dict[str, str] = {}
