#!/usr/bin/env python3
"""
trace_decode.py
Offline decoder for iSH ring buffer trace dumps.

Usage:
    python3 trace_decode.py /path/to/dump.ring
    python3 trace_decode.py /path/to/dump.ring -f json -o output.json
    python3 trace_decode.py /path/to/dump.ring --format text --last 50
"""

import sys
import struct
import json
import argparse
from pathlib import Path
from typing import List, Dict, Any, Optional
from dataclasses import dataclass, asdict

# Event names matching trace_types.h
EVENT_NAMES = {
    0: "NONE",
    1: "BLOCK_COMPILE_START",
    2: "BLOCK_COMPILE_END",
    3: "DECODE_FAILURE",
    4: "UNSUPPORTED_INSTRUCTION",
    5: "BLOCK_ENTRY",
    6: "BLOCK_EXIT",
    7: "BLOCK_CACHE_HIT",
    8: "BLOCK_CACHE_MISS",
    9: "FAULT",
    10: "SYSCALL_ENTER",
    11: "SYSCALL_RETURN",
    12: "PROCESS_ENTRY",
    13: "REGISTER_SNAPSHOT",
    14: "PSTATE_SNAPSHOT",
}


# Event payload decoders
def decode_block_compile_end(payload: bytes) -> Dict[str, Any]:
    end_pc = struct.unpack("<Q", payload[0:8])[0]
    insn_count = struct.unpack("<I", payload[8:12])[0]
    return {"end_pc": hex(end_pc), "insn_count": insn_count}


def decode_block_entry(payload: bytes) -> Dict[str, Any]:
    gadget_count = struct.unpack("<I", payload[0:4])[0]
    return {"gadget_count": gadget_count}


def decode_block_exit(payload: bytes) -> Dict[str, Any]:
    exit_reason = struct.unpack("<i", payload[0:4])[0]
    next_pc = struct.unpack("<Q", payload[8:16])[0]
    return {"exit_reason": exit_reason, "next_pc": hex(next_pc)}


def decode_block_cache_hit(payload: bytes) -> Dict[str, Any]:
    cache_level = struct.unpack("<I", payload[0:4])[0]
    return {"cache_level": cache_level}


def decode_fault(payload: bytes) -> Dict[str, Any]:
    fault_addr = struct.unpack("<Q", payload[0:8])[0]
    is_write = struct.unpack("<i", payload[8:12])[0]
    reason = struct.unpack("<i", payload[12:16])[0]
    return {"fault_addr": hex(fault_addr), "is_write": is_write, "reason": reason}


def decode_syscall_enter(payload: bytes) -> Dict[str, Any]:
    num = struct.unpack("<Q", payload[0:8])[0]
    x0 = struct.unpack("<Q", payload[8:16])[0]
    x1 = struct.unpack("<Q", payload[16:24])[0]
    x2 = struct.unpack("<Q", payload[24:32])[0]
    return {"syscall_num": num, "x0": hex(x0), "x1": hex(x1), "x2": hex(x2)}


def decode_syscall_return(payload: bytes) -> Dict[str, Any]:
    retval = struct.unpack("<Q", payload[0:8])[0]
    return {"retval": hex(retval)}


def decode_process_entry(payload: bytes) -> Dict[str, Any]:
    sp = struct.unpack("<Q", payload[0:8])[0]
    at_entry = struct.unpack("<Q", payload[8:16])[0]
    at_base = struct.unpack("<Q", payload[16:24])[0]
    return {"sp": hex(sp), "at_entry": hex(at_entry), "at_base": hex(at_base)}


def decode_insn_failure(payload: bytes) -> Dict[str, Any]:
    raw_insn = struct.unpack("<I", payload[0:4])[0]
    return {"raw_insn": hex(raw_insn)}


def decode_register_snapshot(payload: bytes) -> Dict[str, Any]:
    regs = []
    for i in range(6):
        reg_val = struct.unpack("<Q", payload[i * 8 : (i + 1) * 8])[0]
        regs.append({f"x{i}": hex(reg_val)})
    return {"registers": regs}


def decode_pstate_snapshot(payload: bytes) -> Dict[str, Any]:
    pstate = struct.unpack("<Q", payload[0:8])[0]
    nzcv = struct.unpack("<Q", payload[8:16])[0]
    return {"pstate": hex(pstate), "nzcv": hex(nzcv)}


# Payload decoder dispatch
PAYLOAD_DECODERS = {
    1: None,  # BLOCK_COMPILE_START - just PC
    2: decode_block_compile_end,
    3: decode_insn_failure,
    4: decode_insn_failure,
    5: decode_block_entry,
    6: decode_block_exit,
    7: decode_block_cache_hit,
    8: None,  # BLOCK_CACHE_MISS - no payload
    9: decode_fault,
    10: decode_syscall_enter,
    11: decode_syscall_return,
    12: decode_process_entry,
    13: decode_register_snapshot,
    14: decode_pstate_snapshot,
}


@dataclass
class TraceHeader:
    """Ring dump header structure."""

    magic: str
    version: int
    header_size: int
    endianness: int
    record_header_size: int
    max_payload_size: int
    record_count: int
    dropped_count: int
    start_seq: int


@dataclass
class TraceRecord:
    """Individual trace record."""

    seq: int
    event_id: int
    event_name: str
    level: int
    cpu_id: int
    pc: str
    payload: Optional[Dict[str, Any]]


def read_header(f) -> TraceHeader:
    """Read and parse dump header."""
    header_data = f.read(40)  # Fixed header size
    if len(header_data) < 40:
        raise ValueError("File too short for header")

    magic = header_data[0:4].decode("ascii").rstrip("\x00")
    version = struct.unpack("<H", header_data[4:6])[0]
    header_size = struct.unpack("<H", header_data[6:8])[0]
    endianness = struct.unpack("<B", header_data[8:9])[0]
    record_header_size = struct.unpack("<B", header_data[9:10])[0]
    max_payload_size = struct.unpack("<H", header_data[10:12])[0]
    record_count = struct.unpack("<Q", header_data[12:20])[0]
    dropped_count = struct.unpack("<Q", header_data[20:28])[0]
    start_seq = struct.unpack("<Q", header_data[28:36])[0]

    return TraceHeader(
        magic=magic,
        version=version,
        header_size=header_size,
        endianness=endianness,
        record_header_size=record_header_size,
        max_payload_size=max_payload_size,
        record_count=record_count,
        dropped_count=dropped_count,
        start_seq=start_seq,
    )


def read_record(f, offset: int) -> Optional[TraceRecord]:
    """Read a single trace record at given file offset."""
    f.seek(offset)

    # Read header (24 bytes based on trace_record_header_t)
    header_data = f.read(24)
    if len(header_data) < 24:
        return None

    seq = struct.unpack("<Q", header_data[0:8])[0]
    event_id = struct.unpack("<B", header_data[8:9])[0]
    level = struct.unpack("<B", header_data[9:10])[0]
    cpu_id = struct.unpack("<H", header_data[10:12])[0]
    pc = struct.unpack("<Q", header_data[12:20])[0]
    payload_size = struct.unpack("<H", header_data[20:22])[0]

    # Read payload
    payload_data = f.read(payload_size) if payload_size > 0 else b""

    # Decode payload
    payload = None
    if event_id in PAYLOAD_DECODERS and PAYLOAD_DECODERS[event_id]:
        try:
            payload = PAYLOAD_DECODERS[event_id](payload_data)
        except Exception as e:
            payload = {"error": str(e), "raw": payload_data.hex()}

    return TraceRecord(
        seq=seq,
        event_id=event_id,
        event_name=EVENT_NAMES.get(event_id, f"UNKNOWN_{event_id}"),
        level=level,
        cpu_id=cpu_id,
        pc=hex(pc),
        payload=payload,
    )


def decode_dump(path: str) -> tuple:
    """Decode entire trace dump file."""
    with open(path, "rb") as f:
        header = read_header(f)

        if header.magic != "ISH":
            raise ValueError(f"Invalid magic: {header.magic}")

        records = []
        record_offset = header.header_size
        record_struct_size = 64  # Fixed record size (header + max payload)

        for i in range(min(header.record_count, 100000)):  # Safety limit
            record = read_record(f, record_offset + i * record_struct_size)
            if record:
                records.append(record)

        return header, records


def format_text(header: TraceHeader, records: List[TraceRecord], args) -> str:
    """Format as human-readable text."""
    lines = []

    lines.append("=" * 80)
    lines.append("iSH Trace Dump")
    lines.append("=" * 80)
    lines.append(f"Version:        {header.version}")
    lines.append(f"Records:        {header.record_count}")
    lines.append(f"Dropped:        {header.dropped_count}")
    lines.append(f"Endianness:     {'little' if header.endianness == 1 else 'big'}")
    lines.append("")

    # Filter records if needed
    display_records = records
    if args.last and args.last < len(records):
        display_records = records[-args.last :]
        lines.append(f"(showing last {args.last} of {len(records)} records)")
        lines.append("")

    lines.append(f"{'Seq':>8} {'Event':<25} {'Level':>5} {'PC':>18} Details")
    lines.append("-" * 80)

    for rec in display_records:
        line = f"{rec.seq:>8} {rec.event_name:<25} {rec.level:>5} {rec.pc:>18}"
        if rec.payload:
            payload_str = " ".join([f"{k}={v}" for k, v in rec.payload.items()])
            if len(payload_str) > 40:
                payload_str = payload_str[:37] + "..."
            line += f" {payload_str}"
        lines.append(line)

    lines.append("=" * 80)
    return "\n".join(lines)


def format_json(header: TraceHeader, records: List[TraceRecord]) -> str:
    """Format as JSON."""
    data = {
        "header": asdict(header),
        "records": [
            {
                "seq": r.seq,
                "event_id": r.event_id,
                "event_name": r.event_name,
                "level": r.level,
                "cpu_id": r.cpu_id,
                "pc": r.pc,
                "payload": r.payload,
            }
            for r in records
        ],
    }
    return json.dumps(data, indent=2)


def main():
    parser = argparse.ArgumentParser(description="Decode iSH trace ring buffer dumps")
    parser.add_argument("input", help="Path to trace dump file")
    parser.add_argument(
        "-f", "--format", choices=["text", "json"], default="text", help="Output format"
    )
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument("--last", type=int, help="Show only last N records")
    parser.add_argument("--filter-event", help="Filter by event name")
    parser.add_argument("--min-level", type=int, help="Minimum trace level")

    args = parser.parse_args()

    try:
        header, records = decode_dump(args.input)

        # Apply filters
        if args.filter_event:
            records = [
                r for r in records if args.filter_event.lower() in r.event_name.lower()
            ]

        if args.min_level is not None:
            records = [r for r in records if r.level >= args.min_level]

        # Format output
        if args.format == "json":
            output = format_json(header, records)
        else:
            output = format_text(header, records, args)

        # Write output
        if args.output:
            Path(args.output).write_text(output)
            print(f"Output written to {args.output}", file=sys.stderr)
        else:
            print(output)

    except FileNotFoundError:
        print(f"Error: File not found: {args.input}", file=sys.stderr)
        sys.exit(1)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        import traceback

        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
