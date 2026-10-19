#!/usr/bin/env python3
"""Pack existing native ABI fixture JSONL traces without synthesizing events.

Output grammar (ASCII, one record per line)::
    EDF6TRACE 1
    SCENARIO <id>
    CLOCK fixed-us <source-native-hz>  (optional; absent means native tick clock)
    TICK <native invocation tick> <actual units, or -1 if unknown>
    PACKET <lower-case hex>
    END <exclusive end tick> <actual total units, or -1 if unknown>
    SKIP <id> <reason-token>

Each PACKET belongs to its preceding TICK. Packet bytes and source order are
preserved; packetless ticks remain present. No render Hz, MAB duration, skill
repeat, concurrent-controller count, or game execution is inferred. Supported
schemas are Boss and Merman scenario/frame/packet/end, Walking packet-only
records, and monster-native-factory-trace-v1 metadata/tick/drain_summary. Walking
expression cadence and zero-payload cases are explicit SKIP records, not passes.
Merman uses an inclusive source end tick, normalized to the exclusive grammar.
Legacy Deiroi time_us text traces retain their source fixed clock and timestamps.
No absent Walking frame records or missing unit metadata are invented. Add another
explicit adapter when a new producer schema is available; unknown schemas fail.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile


class TraceError(ValueError):
    """An input trace is malformed or has no supported schema."""


_ID = re.compile(r"[A-Za-z0-9_.:-]+\Z")
_HEX = re.compile(r"(?:[0-9a-fA-F]{2})+\Z")
_MAX_INT = (1 << 63) - 1


def number(record: dict, key: str, *, unknown: bool = False) -> int:
    value = record.get(key)
    if unknown and value is None:
        return -1
    if type(value) is not int or not 0 <= value <= _MAX_INT:
        raise TraceError(f"{key} must be a nonnegative signed-64-bit integer")
    return value


def scenario_id(record: dict) -> str:
    value = record.get("scenario")
    if not isinstance(value, str) or not _ID.fullmatch(value):
        raise TraceError("scenario must be a nonempty ASCII token [A-Za-z0-9_.:-]+")
    return value


def packet_hex(record: dict) -> str:
    value = record.get("hex")
    if not isinstance(value, str) or not _HEX.fullmatch(value):
        raise TraceError("packet hex must be nonempty, even-length hexadecimal without whitespace")
    for key in ("encodedBytes", "bytes"):
        if key in record and number(record, key) != len(value) // 2:
            raise TraceError(f"packet {key} does not match hex byte length")
    return value.lower()


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise TraceError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def invalid_constant(value: str) -> None:
    raise TraceError(f"non-finite JSON number: {value}")


@dataclass
class Tick:
    native: int
    units: int
    packets: list[str] = field(default_factory=list)


@dataclass
class Scenario:
    name: str
    schema: str
    ticks: list[Tick] = field(default_factory=list)
    end_tick: int = -1
    units: int = -1
    declared_ticks: int | None = None
    eligible: bool = True
    skip_reason: str = ""
    packet_sizes: dict[str, int] = field(default_factory=dict)
    clock: str = "native-tick"
    source_hz: int | None = None

    def add_tick(self, tick: Tick) -> None:
        if self.ticks and tick.native <= self.ticks[-1].native:
            raise TraceError("native ticks must increase strictly within a scenario")
        self.ticks.append(tick)

    def add_packet(self, native: int, units: int, packet: str) -> None:
        if self.ticks and native == self.ticks[-1].native:
            tick = self.ticks[-1]
            tick.units = -1 if tick.units < 0 or units < 0 else tick.units + units
            tick.packets.append(packet)
        else:
            self.add_tick(Tick(native, units, [packet]))

    def finish(self, end_tick: int, units: int) -> None:
        if self.end_tick >= 0:
            raise TraceError("duplicate scenario end")
        if self.ticks and end_tick <= self.ticks[-1].native:
            raise TraceError("end tick must be exclusive, after the final native tick")
        if self.declared_ticks is not None and end_tick != self.declared_ticks:
            raise TraceError("summary totalNativeTicks differs from metadata")
        if units >= 0 and all(t.units >= 0 for t in self.ticks):
            if sum(t.units for t in self.ticks) != units:
                raise TraceError("summary actual units differs from the sum of tick actual units")
        self.end_tick, self.units = end_tick, units

    def stats(self) -> dict:
        packets = [p for t in self.ticks for p in t.packets]
        digest = hashlib.sha256()
        for value in packets:
            digest.update(bytes.fromhex(value))
        return {"scenario": self.name, "schema": self.schema,
                "skipped": bool(self.skip_reason), "skipReason": self.skip_reason,
                "clock": self.clock, "sourceNativeHz": self.source_hz,
                "ticks": len(self.ticks), "endTick": self.end_tick,
                "actualUnits": self.units, "packets": len(packets),
                "packetBytes": sum(len(p) // 2 for p in packets),
                "packetSha256": digest.hexdigest()}


def known_sum(values: list[int]) -> int:
    return -1 if any(value < 0 for value in values) else sum(values)


def record_schema(record: dict) -> str:
    if "traceEligible" in record:
        if type(record["traceEligible"]) is not bool or not isinstance(record.get("guns"), list):
            raise TraceError("Walking metadata requires boolean traceEligible and guns array")
        schema = "walking-native-factory-trace-v1"
    elif any(key in record for key in ("birthBytes", "activeBytes", "stopBytes")):
        for key in ("birthBytes", "activeBytes", "stopBytes"):
            if number(record, key) == 0:
                raise TraceError("Merman declared packet size must be positive")
        schema = "merman-native-factory-trace-v1"
    else:
        schema = "boss-scenario-frame-v1"
    if "schema" in record and record["schema"] != schema:
        raise TraceError(f"unknown schema: {record['schema']!r}")
    return schema


def check_packet_total(record: dict, scenario: Scenario, key: str) -> None:
    if key in record and number(record, key) != sum(len(t.packets) for t in scenario.ticks):
        raise TraceError("summary packet count differs from actual packet records")


def load_json_trace(path: Path) -> list[Scenario]:
    scenarios: list[Scenario] = []
    current: Scenario | None = None
    pending: list[str] = []
    pending_units: list[int] = []
    pending_tick: int | None = None
    local_names: set[str] = set()
    with path.open("r", encoding="utf-8-sig") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line, object_pairs_hook=unique_object,
                                    parse_constant=invalid_constant)
                if not isinstance(record, dict):
                    raise TraceError("each JSONL record must be an object")
                has_record, has_kind = "record" in record, "kind" in record
                # Merman packet.kind is a typed event, not a second discriminator.
                merman_packet = (current is not None and current.schema == "merman-native-factory-trace-v1"
                                 and record.get("record") == "packet")
                if not (has_record or has_kind) or (has_record and has_kind and not merman_packet):
                    raise TraceError("unknown or ambiguous record/kind schema discriminator")
                name = scenario_id(record)
                kind = record.get("record", record.get("kind"))
                starts_record = has_record and kind == "scenario"
                starts_monster = not has_record and kind == "metadata"
                if starts_record or starts_monster:
                    if current is not None:
                        raise TraceError("new scenario before the previous scenario end")
                    if name in local_names:
                        raise TraceError(f"duplicate scenario: {name}")
                    if starts_monster and record.get("schema") != "monster-native-factory-trace-v1":
                        raise TraceError(f"unknown schema: {record.get('schema')!r}")
                    schema = record_schema(record) if starts_record else record["schema"]
                    current = Scenario(name, schema)
                    if starts_monster:
                        current.declared_ticks = number(record, "totalNativeTicks")
                    if schema == "walking-native-factory-trace-v1":
                        current.eligible = record["traceEligible"]
                    if schema == "merman-native-factory-trace-v1":
                        current.packet_sizes = {k: number(record, k + "Bytes") for k in ("birth", "active", "stop")}
                    scenarios.append(current)
                    local_names.add(name)
                    continue
                if current is None or name != current.name:
                    raise TraceError("record has no matching open scenario")
                schema = current.schema
                if schema == "monster-native-factory-trace-v1":
                    if has_record:
                        raise TraceError("mixed schemas within a Monster scenario")
                    if kind == "tick":
                        packets = record.get("packets")
                        if not isinstance(packets, list) or any(not isinstance(p, dict) for p in packets):
                            raise TraceError("Monster packets must be an array of packet objects")
                        current.add_tick(Tick(number(record, "nativeTick"),
                                              number(record, "factoryAccepted", unknown=True),
                                              [packet_hex(p) for p in packets]))
                    elif kind == "drain_summary":
                        check_packet_total(record, current, "totalPackets")
                        current.finish(number(record, "totalNativeTicks"),
                                       number(record, "totalAccepted", unknown=True))
                        current = None
                    else:
                        raise TraceError(f"unknown Monster record: {kind!r}")
                    continue
                if not has_record:
                    raise TraceError("mixed kind/record schemas within a scenario")
                if schema == "walking-native-factory-trace-v1":
                    if kind == "packet":
                        if not current.eligible:
                            raise TraceError("metadata-only Walking scenario cannot contain packets")
                        current.add_packet(number(record, "nativeUpdateTick"),
                                           number(record, "actualUnits", unknown=True), packet_hex(record))
                    elif kind == "end":
                        if not current.eligible:
                            if (record.get("skippedExpressionCadence") is not True or current.ticks
                                    or number(record, "factoryAttempts") != 0
                                    or number(record, "actualSuccessfulUnits") != 0):
                                raise TraceError("Walking expression skip must be explicit and contain no observations")
                            current.skip_reason = "expression-cadence-unresolved"
                            current.units = 0
                        else:
                            if record.get("skippedExpressionCadence"):
                                raise TraceError("eligible Walking scenario cannot skip expression cadence")
                            check_packet_total(record, current, "packets")
                            current.finish(number(record, "nativeUpdates"),
                                           number(record, "actualSuccessfulUnits", unknown=True))
                            if not current.ticks and current.units == 0:
                                current.skip_reason = "no-accepted-payload"
                        current = None
                    else:
                        raise TraceError(f"unknown Walking record: {kind!r}")
                    continue
                if kind == "packet":
                    native = number(record, "nativeUpdateTick")
                    if current.ticks and native <= current.ticks[-1].native:
                        raise TraceError("packet must precede its frame record")
                    if pending_tick is not None and native != pending_tick:
                        raise TraceError("packet tick has no frame record")
                    packet = packet_hex(record)
                    units = number(record, "actualUnits", unknown=True)
                    if schema == "merman-native-factory-trace-v1":
                        event = record.get("kind")
                        if event not in current.packet_sizes:
                            raise TraceError(f"unknown Merman packet kind: {event!r}")
                        if len(packet) // 2 != current.packet_sizes[event]:
                            raise TraceError("Merman captured packet length differs from declared kind size")
                        if units >= 0 and units != (1 if event == "birth" else 0):
                            raise TraceError("Merman active/stop packets cannot count as native Unit births")
                    pending_tick = native
                    pending.append(packet)
                    pending_units.append(units)
                elif kind == "frame":
                    native = number(record, "nativeUpdateTick")
                    if pending_tick is not None and pending_tick != native:
                        raise TraceError("packet tick differs from its frame")
                    if number(record, "packets") != len(pending):
                        raise TraceError("frame packet count differs from packet records")
                    units = number(record, "actualUnits", unknown=True)
                    packet_units = known_sum(pending_units)
                    if units >= 0 and packet_units >= 0 and units != packet_units:
                        raise TraceError("frame actual units differs from its packet actual-unit metadata")
                    current.add_tick(Tick(native, units, pending))
                    pending, pending_units, pending_tick = [], [], None
                elif kind == "end":
                    if pending:
                        if schema != "merman-native-factory-trace-v1" or pending_tick is None:
                            raise TraceError("scenario ends with packets missing their frame")
                        # Merman's post-quiesce stop has a packet but no frame row.
                        # Use its explicit actualUnits, never infer a new Unit from an event.
                        current.add_tick(Tick(pending_tick, known_sum(pending_units), pending))
                        pending, pending_units, pending_tick = [], [], None
                    check_packet_total(record, current, "packets")
                    end_tick = number(record, "nativeUpdates")
                    if schema == "merman-native-factory-trace-v1":
                        if end_tick == _MAX_INT:
                            raise TraceError("inclusive Merman end cannot fit the exclusive tick grammar")
                        end_tick += 1
                    current.finish(end_tick, number(record, "actualSuccessfulUnits", unknown=True))
                    current = None
                else:
                    raise TraceError(f"unknown {schema} record: {kind!r}")
            except (TraceError, json.JSONDecodeError) as exc:
                raise TraceError(f"{path}:{line_number}: {exc}") from exc
    if current is not None:
        raise TraceError(f"{path}: unterminated scenario {current.name}")
    if not scenarios:
        raise TraceError(f"{path}: no supported scenarios")
    return scenarios


_DEIROI_HEADER = "# production-including fixture; native allocation seam succeeds; NOT gameplay or observed native cadence"


def decimal_token(value: str, label: str) -> int:
    if not re.fullmatch(r"[0-9]+", value):
        raise TraceError(f"{label} must be a nonnegative decimal integer")
    return number({label: int(value)}, label)


def load_deiroi_trace(path: Path) -> list[Scenario]:
    # Legacy producer format has already mapped source logic invocations to time_us.
    # Never run this 60-second series at a second assumed native update cadence.
    current: Scenario | None = None
    expected_units: int | None = None
    saw_header = False
    with path.open("r", encoding="utf-8-sig") as source:
        for line_number, raw in enumerate(source, 1):
            line = raw.strip()
            if not line:
                continue
            try:
                if not saw_header:
                    if line != _DEIROI_HEADER:
                        raise TraceError("unknown text trace schema")
                    saw_header = True
                    continue
                if line.startswith("# source="):
                    if current is not None:
                        raise TraceError("duplicate Deiroi source/clock header")
                    fields = {}
                    for item in line[2:].split(";"):
                        if "=" not in item:
                            raise TraceError("malformed Deiroi header field")
                        key, value = (part.strip() for part in item.split("=", 1))
                        if key in fields:
                            raise TraceError(f"duplicate Deiroi header field: {key}")
                        fields[key] = value
                    if fields.get("source") != "E509_DEIROI_XL.SGO":
                        raise TraceError("unsupported legacy Deiroi source asset")
                    hz = decimal_token(fields.get("native_logic_updates_per_second", ""), "native_logic_updates_per_second")
                    seconds = decimal_token(fields.get("duration_s", ""), "duration_s")
                    if not hz or not seconds or seconds > _MAX_INT // 1000000:
                        raise TraceError("Deiroi source clock and duration must be positive and bounded")
                    current = Scenario(f"deiroi_native_{hz}hz_source0", "deiroi-accepted-text-v1")
                    current.clock, current.source_hz = "fixed-us", hz
                    current.declared_ticks = seconds * 1000000
                    continue
                if line.startswith("# captured_actual_outer_units="):
                    if current is None or expected_units is not None:
                        raise TraceError("Deiroi summary requires exactly one preceding source header")
                    value = line[2:].split(";", 1)[0].split("=", 1)[1].strip()
                    expected_units = decimal_token(value, "captured_actual_outer_units")
                    continue
                if line.startswith("#"):
                    continue  # descriptive provenance is data, never an instruction
                if current is None or expected_units is not None:
                    raise TraceError("Deiroi packet must follow its source header and precede its summary")
                fields = line.split()
                if len(fields) != 4:
                    raise TraceError("Deiroi packet requires time_us source_index actual_units hex")
                native_us = decimal_token(fields[0], "time_us")
                source_index = decimal_token(fields[1], "source_index")
                if source_index != 0:
                    raise TraceError("legacy compact adapter supports only the observed single source_index=0")
                units = decimal_token(fields[2], "actual_units")
                current.add_packet(native_us, units, packet_hex({"hex": fields[3]}))
            except TraceError as exc:
                raise TraceError(f"{path}:{line_number}: {exc}") from exc
    if current is None or expected_units is None or not current.ticks:
        raise TraceError(f"{path}: incomplete legacy Deiroi trace header, packets, or summary")
    assert current.declared_ticks is not None
    current.finish(current.declared_ticks, expected_units)
    return [current]


def load_trace(path: Path) -> list[Scenario]:
    with path.open("r", encoding="utf-8-sig") as source:
        first = next((line.strip() for line in source if line.strip()), "")
    if first.startswith("#"):
        return load_deiroi_trace(path)
    return load_json_trace(path)


def write_pack(destination: Path, scenarios: list[Scenario]) -> None:
    # Fully validate before opening output; replacement never leaves a partial pack.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="ascii", newline="\n",
                                         dir=destination.parent, prefix=destination.name + ".",
                                         suffix=".tmp", delete=False) as target:
            temporary = Path(target.name)
            target.write("EDF6TRACE 1\n")
            for scenario in scenarios:
                if scenario.skip_reason:
                    target.write(f"SKIP {scenario.name} {scenario.skip_reason}\n")
                    continue
                target.write(f"SCENARIO {scenario.name}\n")
                if scenario.clock == "fixed-us":
                    target.write(f"CLOCK fixed-us {scenario.source_hz}\n")
                for tick in scenario.ticks:
                    target.write(f"TICK {tick.native} {tick.units}\n")
                    for packet in tick.packets:
                        target.write(f"PACKET {packet}\n")
                target.write(f"END {scenario.end_tick} {scenario.units}\n")
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", type=Path, nargs="*", help="supported native ABI JSONL or explicit legacy Deiroi text trace")
    parser.add_argument("--source", type=Path, action="append", default=[], help="additional JSONL input; repeatable, packed after positional inputs")
    parser.add_argument("-o", "--output", type=Path, required=True, help="compact output path")
    args = parser.parse_args(argv)
    args.inputs.extend(args.source)
    if not args.inputs:
        parser.error("at least one positional input or --source is required")
    try:
        destination = args.output.resolve()
        if any(path.resolve() == destination for path in args.inputs):
            raise TraceError("output must not overwrite an input JSONL")
        scenarios = []
        names = set()
        for path in args.inputs:
            for scenario in load_trace(path):
                if scenario.name in names:
                    raise TraceError(f"duplicate scenario across inputs: {scenario.name}")
                names.add(scenario.name)
                scenarios.append(scenario)
        write_pack(destination, scenarios)
        stats = [scenario.stats() for scenario in scenarios]
        print(json.dumps({"format": "EDF6TRACE 1", "scenarios": stats,
                          "totals": {"scenarios": sum(not s["skipped"] for s in stats),
                                     "sourceScenarios": len(stats),
                                     "skipped": sum(s["skipped"] for s in stats),
                                     "ticks": sum(s["ticks"] for s in stats),
                                     "packets": sum(s["packets"] for s in stats),
                                     "packetBytes": sum(s["packetBytes"] for s in stats)}},
                         separators=(",", ":")))
        return 0
    except (OSError, UnicodeError, TraceError) as exc:
        print(f"trace pack error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
