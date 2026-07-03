#!/usr/bin/env python3
"""Decode and compare raw ir-ctl timing captures for Gree-like HVAC remotes.

Input is the signed timing format emitted by:

    ir-ctl -d /dev/lirc0 -r -m

The decoder treats +9000 -4500 as a frame header, +~675 -~500 as bit 0,
+~675 -~1600 as bit 1, and long spaces as section/frame gaps.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


TIMING_RE = re.compile(r"([+-])\s*(\d+)")


@dataclass
class Section:
    bits: list[int]
    gap_us: int | None


@dataclass
class Frame:
    header_mark_us: int
    header_space_us: int
    sections: list[Section]


def parse_timings(text: str) -> list[int]:
    timings: list[int] = []
    for sign, value in TIMING_RE.findall(text):
        raw = int(value)
        timings.append(raw if sign == "+" else -raw)
    return timings


def is_header(timings: list[int], index: int) -> bool:
    if index + 1 >= len(timings):
        return False
    return timings[index] >= 8000 and -5500 <= timings[index + 1] <= -3000


def decode_timings(
    timings: list[int],
    *,
    bit_threshold_us: int = 1000,
    long_gap_us: int = 8000,
) -> list[Frame]:
    frames: list[Frame] = []
    i = 0

    while i < len(timings):
        if not is_header(timings, i):
            i += 1
            continue

        frame = Frame(
            header_mark_us=timings[i],
            header_space_us=-timings[i + 1],
            sections=[],
        )
        i += 2
        bits: list[int] = []

        while i + 1 < len(timings):
            if is_header(timings, i):
                break

            mark = timings[i]
            space = timings[i + 1]
            if mark <= 0:
                i += 1
                continue

            if space >= 0:
                i += 1
                continue

            space_us = -space
            if space_us >= long_gap_us:
                frame.sections.append(Section(bits=bits, gap_us=space_us))
                bits = []
                i += 2
                if i < len(timings) and is_header(timings, i):
                    break
                continue

            bits.append(1 if space_us >= bit_threshold_us else 0)
            i += 2

        if bits:
            frame.sections.append(Section(bits=bits, gap_us=None))

        frames.append(frame)

    return frames


def bits_to_bytes(bits: Iterable[int], *, lsb_first: bool) -> list[int]:
    values: list[int] = []
    chunk: list[int] = []
    for bit in bits:
        chunk.append(bit)
        if len(chunk) == 8:
            if lsb_first:
                value = sum((b & 1) << pos for pos, b in enumerate(chunk))
            else:
                value = 0
                for b in chunk:
                    value = (value << 1) | (b & 1)
            values.append(value)
            chunk = []
    if chunk:
        if lsb_first:
            value = sum((b & 1) << pos for pos, b in enumerate(chunk))
        else:
            value = 0
            for b in chunk:
                value = (value << 1) | (b & 1)
            value <<= 8 - len(chunk)
        values.append(value)
    return values


def hex_bytes(values: Iterable[int]) -> str:
    return " ".join(f"{value:02X}" for value in values)


def bit_string(bits: Iterable[int]) -> str:
    return "".join(str(bit) for bit in bits)


def all_bits(frame: Frame) -> list[int]:
    bits: list[int] = []
    for section in frame.sections:
        bits.extend(section.bits)
    return bits


def gree_like_data_bits(frame: Frame) -> tuple[list[int], list[int]] | None:
    """Return likely 64-bit Gree data and footer bits for 35+32 section frames."""
    if len(frame.sections) < 2:
        return None
    first = frame.sections[0].bits
    second = frame.sections[1].bits
    if len(first) != 35 or len(second) != 32:
        return None
    return first[:32] + second, first[32:]


def summarize_timings(timings: list[int]) -> dict[str, float | int | None]:
    marks: list[int] = []
    short_spaces: list[int] = []
    long_spaces: list[int] = []
    gaps: list[int] = []

    for i in range(0, len(timings) - 1, 2):
        mark = timings[i]
        space = -timings[i + 1] if timings[i + 1] < 0 else None
        if mark > 0:
            marks.append(mark)
        if space is None:
            continue
        if 200 <= space < 1000:
            short_spaces.append(space)
        elif 1000 <= space < 3000:
            long_spaces.append(space)
        elif space >= 8000:
            gaps.append(space)

    def median(values: list[int]) -> float | None:
        return statistics.median(values) if values else None

    return {
        "mark_median_us": median(marks),
        "short_space_median_us": median(short_spaces),
        "long_space_median_us": median(long_spaces),
        "gap_count": len(gaps),
    }


def frame_as_dict(frame: Frame) -> dict[str, object]:
    sections: list[dict[str, object]] = []
    for section in frame.sections:
        bits = section.bits
        sections.append(
            {
                "bit_count": len(bits),
                "bits": bit_string(bits),
                "bytes_lsb_first": hex_bytes(bits_to_bytes(bits, lsb_first=True)),
                "bytes_msb_first": hex_bytes(bits_to_bytes(bits, lsb_first=False)),
                "gap_us": section.gap_us,
            }
        )
    bits = all_bits(frame)
    gree_like = gree_like_data_bits(frame)
    gree_like_data: dict[str, object] | None = None
    if gree_like is not None:
        data_bits, footer_bits = gree_like
        gree_like_data = {
            "data_bits": bit_string(data_bits),
            "footer_bits": bit_string(footer_bits),
            "bytes_lsb_first": hex_bytes(bits_to_bytes(data_bits, lsb_first=True)),
            "bytes_msb_first": hex_bytes(bits_to_bytes(data_bits, lsb_first=False)),
        }
    return {
        "header_mark_us": frame.header_mark_us,
        "header_space_us": frame.header_space_us,
        "section_count": len(frame.sections),
        "total_bits": len(bits),
        "bytes_lsb_first": hex_bytes(bits_to_bytes(bits, lsb_first=True)),
        "bytes_msb_first": hex_bytes(bits_to_bytes(bits, lsb_first=False)),
        "gree_like_data": gree_like_data,
        "sections": sections,
    }


def print_frame(frame: Frame, index: int) -> None:
    data = frame_as_dict(frame)
    print(f"Frame {index}: header +{data['header_mark_us']} -{data['header_space_us']}")
    print(f"  sections: {data['section_count']}, total bits: {data['total_bits']}")
    print(f"  bytes, LSB-first per byte: {data['bytes_lsb_first']}")
    print(f"  bytes, MSB-first per byte: {data['bytes_msb_first']}")
    if data["gree_like_data"] is not None:
        gree_data = data["gree_like_data"]
        assert isinstance(gree_data, dict)
        print(
            "  Gree-like 64 data bits, LSB bytes: "
            f"{gree_data['bytes_lsb_first']} (footer {gree_data['footer_bits']})"
        )

    for section_index, section in enumerate(frame.sections):
        print(
            f"  section {section_index}: {len(section.bits)} bits"
            + (f", gap {section.gap_us} us" if section.gap_us is not None else "")
        )
        print(f"    bits: {bit_string(section.bits)}")
        print(f"    LSB:  {hex_bytes(bits_to_bytes(section.bits, lsb_first=True))}")
        print(f"    MSB:  {hex_bytes(bits_to_bytes(section.bits, lsb_first=False))}")


def read_capture(path: str) -> tuple[str, str]:
    if path == "-":
        return "stdin", sys.stdin.read()
    return path, Path(path).read_text(encoding="utf-8", errors="replace")


def decode_capture(path: str, args: argparse.Namespace) -> tuple[str, list[int], list[Frame]]:
    name, text = read_capture(path)
    timings = parse_timings(text)
    frames = decode_timings(
        timings,
        bit_threshold_us=args.bit_threshold_us,
        long_gap_us=args.long_gap_us,
    )
    return name, timings, frames


def compare_frames(label_a: str, frame_a: Frame, label_b: str, frame_b: Frame) -> None:
    bits_a = all_bits(frame_a)
    bits_b = all_bits(frame_b)
    max_len = max(len(bits_a), len(bits_b))
    print(f"Compare {label_a} -> {label_b}")
    print(f"  bit lengths: {len(bits_a)} -> {len(bits_b)}")
    print(f"  LSB bytes A: {hex_bytes(bits_to_bytes(bits_a, lsb_first=True))}")
    print(f"  LSB bytes B: {hex_bytes(bits_to_bytes(bits_b, lsb_first=True))}")
    print(f"  changed bit indexes:")
    changed = 0
    for i in range(max_len):
        a = bits_a[i] if i < len(bits_a) else None
        b = bits_b[i] if i < len(bits_b) else None
        if a != b:
            changed += 1
            print(f"    bit {i:03d}: {a} -> {b} (byte {i // 8}, bit {i % 8})")
    if changed == 0:
        print("    none")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="*", help="Capture files, or '-' for stdin")
    parser.add_argument("--bit-threshold-us", type=int, default=1000)
    parser.add_argument("--long-gap-us", type=int, default=8000)
    parser.add_argument("--frame", type=int, default=0, help="Frame index to compare")
    parser.add_argument("--compare", action="store_true", help="Compare selected frames from captures")
    parser.add_argument("--json", action="store_true", help="Print decoded data as JSON")
    args = parser.parse_args()

    captures = args.captures or ["-"]
    decoded = [decode_capture(path, args) for path in captures]

    if args.json:
        print(
            json.dumps(
                [
                    {
                        "name": name,
                        "timing_summary": summarize_timings(timings),
                        "frames": [frame_as_dict(frame) for frame in frames],
                    }
                    for name, timings, frames in decoded
                ],
                indent=2,
            )
        )
        return 0

    if args.compare:
        if len(decoded) < 2:
            print("--compare needs at least two captures", file=sys.stderr)
            return 2
        first_name, _, first_frames = decoded[0]
        if args.frame >= len(first_frames):
            print(f"{first_name}: frame {args.frame} is not present", file=sys.stderr)
            return 2
        first_frame = first_frames[args.frame]
        for name, _, frames in decoded[1:]:
            if args.frame >= len(frames):
                print(f"{name}: frame {args.frame} is not present", file=sys.stderr)
                return 2
            compare_frames(first_name, first_frame, name, frames[args.frame])
        return 0

    for name, timings, frames in decoded:
        print(f"Capture: {name}")
        print(f"  timings: {len(timings)}")
        print(f"  timing summary: {summarize_timings(timings)}")
        print(f"  frames: {len(frames)}")
        for index, frame in enumerate(frames):
            print_frame(frame, index)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
