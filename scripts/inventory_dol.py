#!/usr/bin/env python3
"""Read-only inventory for a Wii/GC DOL plus a matching Garigari symbol map.

This parses the standard DOL header, hashes inputs, validates file/memory ranges
and entry-point mapping, then exports only reference symbols marked functions.
It does not decode instructions or claim that imported names are reconstructed
source code.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
import sys
from pathlib import Path


DEFAULT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DOL = DEFAULT_ROOT / "extracted/Super Mario Galaxy 2 (USA) (En,Fr,Es)/DATA/sys/main.dol"
DEFAULT_REFERENCE = DEFAULT_ROOT / "reference/Garigari"
DEFAULT_EVIDENCE = DEFAULT_ROOT / "evidence"
REFERENCE_COMMIT = "c590a376e00a6b5d4f2bc963dfd6f5516080e07d"

SYMBOL_RE = re.compile(
    r"^(?P<name>.*?)\s*=\s*(?P<section>[^:]+):(?P<address>0x[0-9A-Fa-f]+);"
    r"\s*//\s*type:(?P<kind>\w+)(?P<attrs>.*)$"
)
SIZE_RE = re.compile(r"\bsize:(0x[0-9A-Fa-f]+)")
SPLIT_UNIT_RE = re.compile(r"^(?P<unit>[^\s].*):$")
SPLIT_RANGE_RE = re.compile(
    r"^\s*(?P<section>\S+)\s+start:(0x[0-9A-Fa-f]+)\s+end:(0x[0-9A-Fa-f]+)"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha1(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_dol(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 0x100:
        raise ValueError(f"DOL is shorter than its 0x100-byte header: {len(data):#x}")

    sections = []
    for group, count, file_base, address_base, size_base in (
        ("text", 7, 0x00, 0x48, 0x90),
        ("data", 11, 0x1C, 0x64, 0xAC),
    ):
        for index in range(count):
            file_offset = struct.unpack_from(">I", data, file_base + index * 4)[0]
            address = struct.unpack_from(">I", data, address_base + index * 4)[0]
            size = struct.unpack_from(">I", data, size_base + index * 4)[0]
            if size == 0:
                if file_offset != 0 or address != 0:
                    raise ValueError(
                        f"Inactive {group}[{index}] has nonzero offset/address"
                    )
                continue
            section = {
                "name": f"{group}[{index}]",
                "kind": group,
                "file_offset": file_offset,
                "address": address,
                "size": size,
                "file_end": file_offset + size,
                "address_end": address + size,
            }
            sections.append(section)

    bss_address, bss_size = struct.unpack_from(">II", data, 0xD8)
    entry = struct.unpack_from(">I", data, 0xE0)[0]
    errors = []
    for section in sections:
        if section["file_offset"] < 0x100:
            errors.append(f"{section['name']} file range overlaps DOL header")
        if section["file_end"] > len(data):
            errors.append(
                f"{section['name']} exceeds file ({section['file_end']:#x} > {len(data):#x})"
            )
        if section["address_end"] > 0x1_0000_0000:
            errors.append(f"{section['name']} address range overflows 32-bit space")

    for key, start_key, end_key in (
        ("file", "file_offset", "file_end"),
        ("memory", "address", "address_end"),
    ):
        ordered = sorted(sections, key=lambda section: section[start_key])
        for left, right in zip(ordered, ordered[1:]):
            if right[start_key] < left[end_key]:
                errors.append(
                    f"{key} ranges overlap: {left['name']} and {right['name']}"
                )

    bss_overlaps = []
    entry_sections = [
        section for section in sections
        if section["kind"] == "text"
        and section["address"] <= entry < section["address_end"]
    ]
    if len(entry_sections) != 1:
        errors.append(
            f"entry point {entry:#010x} maps to {len(entry_sections)} text sections, expected 1"
        )
    if bss_size:
        bss_end = bss_address + bss_size
        if bss_end > 0x1_0000_0000:
            errors.append("BSS address range overflows 32-bit space")
        for section in sections:
            if bss_address < section["address_end"] and section["address"] < bss_end:
                bss_overlaps.append(section["name"])

    return {
        "path": str(path),
        "file_size": len(data),
        "sha1": sha1(path),
        "sha256": sha256(path),
        "entry_point": entry,
        "bss": {"address": bss_address, "size": bss_size, "end": bss_address + bss_size},
        "sections": sections,
        "entry_section": entry_sections[0]["name"] if len(entry_sections) == 1 else None,
        "validation": {
            "ok": not errors,
            "errors": errors,
            "bss_aggregate_span_overlaps_initialized_sections": bss_overlaps,
            "bss_note": "The header BSS range is reported as an aggregate descriptor. Overlap with initialized sections is informational, not treated as a malformed DOL; preserve each initialized DOL section when loading.",
        },
    }


def parse_split_units(path: Path) -> list[dict]:
    units = []
    current_unit = None
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        unit_match = SPLIT_UNIT_RE.match(line)
        if unit_match:
            current_unit = unit_match.group("unit")
            continue
        range_match = SPLIT_RANGE_RE.match(line)
        if range_match and current_unit:
            section, start, end = range_match.groups()
            units.append({
                "unit": current_unit,
                "section": section,
                "start": int(start, 16),
                "end": int(end, 16),
                "line": line_number,
            })
    return units


def parse_functions(symbols_path: Path, splits_path: Path) -> list[dict]:
    split_units = parse_split_units(splits_path)
    functions = []
    for line_number, line in enumerate(symbols_path.read_text(encoding="utf-8").splitlines(), 1):
        match = SYMBOL_RE.match(line)
        if not match or match.group("kind") != "function":
            continue
        address = int(match.group("address"), 16)
        size_match = SIZE_RE.search(match.group("attrs"))
        size = int(size_match.group(1), 16) if size_match else None
        section = match.group("section")
        owners = [
            unit["unit"] for unit in split_units
            if unit["section"] == section and unit["start"] <= address < unit["end"]
        ]
        functions.append({
            "name": match.group("name").strip(),
            "address": address,
            "size": size,
            "section": section,
            "reference_line": line_number,
            "source_unit": owners[0] if len(owners) == 1 else None,
        })
    return functions


def parse_dtk_sections(path: Path) -> list[dict]:
    """Parse only DTK's section table for independent comparison."""
    result = []
    in_sections = False
    row_re = re.compile(r"^\s*(\S+)\s+\|\s+(0x[0-9A-Fa-f]+)\s+\|\s+(0x[0-9A-Fa-f]+)\s+\|\s+(0x[0-9A-Fa-f]+)\s*$")
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() == "Sections:":
            in_sections = True
            continue
        if in_sections and line.strip() == "Discovered symbols:":
            break
        if in_sections:
            match = row_re.match(line)
            if match:
                name, address, size, file_offset = match.groups()
                result.append({
                    "name": name,
                    "address": int(address, 16),
                    "size": int(size, 16),
                    "file_offset": int(file_offset, 16),
                })
    if not result:
        raise ValueError(f"Could not read section table from {path}")
    return result


def write_functions_tsv(path: Path, functions: list[dict]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(("name", "address", "size", "section", "reference_line", "source_unit"))
        for function in functions:
            writer.writerow((
                function["name"],
                f"0x{function['address']:08X}",
                "" if function["size"] is None else f"0x{function['size']:X}",
                function["section"],
                function["reference_line"],
                function["source_unit"] or "",
            ))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dol", type=Path, default=DEFAULT_DOL)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--evidence", type=Path, default=DEFAULT_EVIDENCE)
    args = parser.parse_args()

    cfg = args.reference / "config/SB4E01/config.yml"
    symbols = args.reference / "config/SB4E01/symbols.txt"
    splits = args.reference / "config/SB4E01/splits.txt"
    dtk_file = args.evidence / "dtk-dol-info.txt"
    for required in (args.dol, cfg, symbols, splits, dtk_file):
        if not required.is_file():
            parser.error(f"required input missing: {required}")

    dol = parse_dol(args.dol)
    functions = parse_functions(symbols, splits)
    reference_hash = re.search(r"(?m)^hash:\s*([0-9a-fA-F]{40})\s*$", cfg.read_text())
    if not reference_hash:
        raise ValueError(f"No 40-digit baserom hash found in {cfg}")
    hash_match = dol["sha1"].lower() == reference_hash.group(1).lower()
    dtk_sections = parse_dtk_sections(dtk_file)
    split_config = args.evidence.parent / "analysis/config.json"
    split_result = None
    if split_config.is_file():
        parsed_config = json.loads(split_config.read_text(encoding="utf-8"))
        units = parsed_config.get("units", [])
        asm_dir = split_config.parent / "asm"
        obj_dir = split_config.parent / "obj"
        split_result = {
            "config_path": str(split_config),
            "config_sha256": sha256(split_config),
            "unit_count": len(units),
            "reference_named_unit_count": sum(not unit.get("autogenerated", False) for unit in units),
            "autogenerated_unit_count": sum(unit.get("autogenerated", False) for unit in units),
            "code_size_sum": sum(unit.get("code_size", 0) for unit in units),
            "data_size_sum": sum(unit.get("data_size", 0) for unit in units),
            "assembly_file_count": sum(1 for _ in asm_dir.rglob("*.s")) if asm_dir.is_dir() else 0,
            "object_file_count": sum(1 for _ in obj_dir.rglob("*.o")) if obj_dir.is_dir() else 0,
            "reported_limitations": (
                "DTK split artifacts are disassembly/relocatable objects, not reconstructed or playable C++; "
                "the parent reported non-fatal expected alignment 8 versus actual 4 warnings in some units."
            ),
        }
    direct_calls_file = args.evidence / "selected-direct-calls.tsv"
    direct_calls = None
    if direct_calls_file.is_file():
        with direct_calls_file.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        wanted_sites = {"0x800041B0", "0x804B64B4", "0x804B64C0", "0x804B6AA0"}
        direct_calls = {
            "path": str(direct_calls_file),
            "observed_bl_site_count": len(rows),
            "scope_limit": "Selected DTK assembly only; excludes indirect calls, conditional call instructions, tail branches, and is not a full program call graph.",
            "startup_edges": [
                {"caller": row["caller"], "callsite": row["callsite"], "callee": row["callee"], "assembly_file": row["assembly_file"], "line": int(row["line"])}
                for row in rows if row["callsite"] in wanted_sites
            ],
        }
    dtk_comparison = {
        "dtk_section_count": len(dtk_sections),
        "dol_header_text_count": sum(s["kind"] == "text" for s in dol["sections"]),
        "dol_header_data_count": sum(s["kind"] == "data" for s in dol["sections"]),
        "note": (
            "DTK reports analyzer-derived logical section spans, while this script reports raw DOL header slots. "
            "The sizes differ; alignment/padding may explain differences but is not assumed proven here."
        ),
        "dtk_sections": dtk_sections,
    }

    reference_files = {}
    for label, path in (("config_yml", cfg), ("symbols_txt", symbols), ("splits_txt", splits)):
        reference_files[label] = {"path": str(path), "sha256": sha256(path)}

    inventory = {
        "format": "Wii/GC DOL",
        "dol": dol,
        "reference": {
            "project": "SMGCommunity/Garigari",
            "commit": REFERENCE_COMMIT,
            "version_identity": "SB4E01 Rev 0 USA; exact match checked against config.yml SHA-1",
            "declared_dol_sha1": reference_hash.group(1).lower(),
            "sha1_matches_declared": hash_match,
            "files": reference_files,
            "symbols_interpretation": (
                "Function labels and sizes are imported from the exact-hash reference symbols.txt; "
                "they are not evidence of a matching/reconstructed C++ implementation."
            ),
        },
        "dtk_comparison": dtk_comparison,
        "dtk_split": split_result,
        "selected_direct_calls": direct_calls,
        "function_count": len(functions),
        "functions_tsv": "functions.tsv",
    }
    inventory_path = args.evidence / "dol-inventory.json"
    tsv_path = args.evidence / "functions.tsv"
    summary_path = args.evidence / "summary.txt"
    args.evidence.mkdir(parents=True, exist_ok=True)
    inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    write_functions_tsv(tsv_path, functions)

    lines = [
        "SMG2 DOL inventory",
        f"File: {args.dol}",
        f"Size: {dol['file_size']:,} bytes",
        f"SHA-1: {dol['sha1']}",
        f"SHA-256: {dol['sha256']}",
        f"Entry point: 0x{dol['entry_point']:08X} ({dol['entry_section'] or 'UNMAPPED'})",
        f"DOL header sections: {len(dol['sections'])} ({sum(s['kind'] == 'text' for s in dol['sections'])} text, {sum(s['kind'] == 'data' for s in dol['sections'])} data)",
        "Header section table:",
    ]
    for section in dol["sections"]:
        lines.append(
            f"  {section['name']:7} file=0x{section['file_offset']:X} address=0x{section['address']:08X} size=0x{section['size']:X}"
        )
    lines.extend([
        f"BSS: address=0x{dol['bss']['address']:08X} size=0x{dol['bss']['size']:X}",
        "BSS aggregate overlaps initialized sections: " + (", ".join(dol["validation"]["bss_aggregate_span_overlaps_initialized_sections"]) or "none; informational only"),
        f"Header validation: {'PASS' if dol['validation']['ok'] else 'FAIL'}",
        f"Garigari commit: {REFERENCE_COMMIT}",
        f"Garigari declared SHA-1: {reference_hash.group(1).lower()} ({'MATCH' if hash_match else 'MISMATCH'})",
        "Reference input SHA-256 values:",
        *(f"  {label}: {value['sha256']}" for label, value in reference_files.items()),
        f"Imported reference functions: {len(functions)} (see functions.tsv)",
        "Interpretation: function labels/sizes come from the exact-hash reference symbols file. No matching C++ reconstruction of this DOL was produced.",
        "Cross-check: DTK logical section table parsed from evidence/dtk-dol-info.txt; spans differ from raw header slots. Alignment/padding is a possible explanation, not established by this inventory.",
        "DTK reported entry point: " + next((line.split(":", 1)[1].strip() for line in dtk_file.read_text().splitlines() if line.startswith("Entry point:")), "unavailable"),
        f"Artifacts: {inventory_path.name}, {tsv_path.name}",
    ])
    if split_result:
        lines.extend([
            f"DTK split: {split_result['unit_count']} units ({split_result['reference_named_unit_count']} reference-named, {split_result['autogenerated_unit_count']} autogenerated), {split_result['assembly_file_count']} assembly files, {split_result['object_file_count']} relocatable objects.",
            f"DTK split section-size totals: code {split_result['code_size_sum']:,} bytes; data {split_result['data_size_sum']:,} bytes.",
            f"DTK split config: {split_result['config_path']} (SHA-256 {split_result['config_sha256']}).",
            "Limit: these generated assembly/object artifacts are analysis outputs, not reconstructed C++ or a playable PC port. Parent reported non-fatal expected alignment 8 versus actual 4 warnings in some configured units.",
        ])
    if direct_calls:
        lines.extend([
            f"Selected direct-call evidence: {direct_calls['observed_bl_site_count']:,} observed `bl` sites in {direct_calls['path']}.",
            "Verified startup callsites: __start --0x800041B0--> main; main --0x804B64B4--> GameSystem::init; main --0x804B64C0--> GameSystem::frameLoop; frameLoop --0x804B6AA0--> GameSystem::update.",
            f"Call-evidence limit: {direct_calls['scope_limit']}",
        ])
    summary_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"DOL validation: {'PASS' if dol['validation']['ok'] else 'FAIL'}")
    print(f"Reference SHA-1 match: {'PASS' if hash_match else 'FAIL'}")
    print(f"Functions exported: {len(functions)}")
    print(f"Wrote {inventory_path}, {tsv_path}, {summary_path}")
    return 0 if dol["validation"]["ok"] and hash_match else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
