"""Authored MAB sound events on a converted CAS; preserve channel, bone and tracing metadata."""
from __future__ import annotations

import cas_graft
import mab_legacy
import sgo


def animation_events(cas: bytes, mab: bytes) -> list:
    """[[state, clip, [[time, kind, preset, channel, bone, trace], ...]], ...]."""
    tracks = mab_legacy.mab_event_params(mab)
    result = []
    for state, clips in sorted(cas_graft.state_clips(cas)['main'].items()):
        for number, clip in enumerate(clips):
            events = []
            for kind, time, params in tracks.get(clip, []):
                if kind not in ('se', 'se_stop'):
                    continue
                channel = int(sgo.plain(params.get('channel', -1)))
                trace = int(sgo.plain(params.get('is_trace_node', 0)))
                events.append([float(time), int(kind == 'se_stop'), str(params.get('name', '')), channel,
                               str(params.get('node', '')), trace])
            if events:
                result.append([state, number, events])
    return result
