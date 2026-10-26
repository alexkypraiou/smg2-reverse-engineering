#!/usr/bin/env python3
"""Read-only independent fixture inspector for extracted SMG2 RARC/Yaz0/BCSV data.

Format layout follows the local Garigari JKRArchive/JMapInfo sources and is checked
against the native loaders. This tool only reads files and emits compact expectations.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import sys


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from(">H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from(">I", data, off)[0]


def yaz0(data: bytes) -> bytes:
    if data[:4] != b"Yaz0" or len(data) < 16:
        return data
    expected = u32(data, 4)
    out = bytearray()
    src = 16
    while len(out) < expected:
        if src >= len(data):
            raise ValueError("truncated Yaz0 flags")
        control = data[src]
        src += 1
        for bit in range(7, -1, -1):
            if len(out) == expected:
                break
            if control & (1 << bit):
                if src >= len(data):
                    raise ValueError("truncated Yaz0 literal")
                out.append(data[src])
                src += 1
            else:
                if src + 2 > len(data):
                    raise ValueError("truncated Yaz0 reference")
                a, b = data[src], data[src + 1]
                src += 2
                length = a >> 4
                distance = ((a & 15) << 8) | b
                if length:
                    length += 2
                else:
                    if src >= len(data):
                        raise ValueError("truncated Yaz0 long reference")
                    length = data[src] + 0x12
                    src += 1
                back = distance + 1
                if back > len(out):
                    raise ValueError("invalid Yaz0 back-reference")
                for _ in range(length):
                    out.append(out[-back])
                    if len(out) > expected:
                        raise ValueError("Yaz0 output exceeds declared size")
    return bytes(out)


def rarc(data: bytes) -> dict[str, bytes]:
    data = yaz0(data)
    if data[:4] != b"RARC":
        raise ValueError("not RARC")
    file_data_base = 0x20 + u32(data, 0x0C)
    info = data[0x20:file_data_base]
    node_count, node_off, entry_count, entry_off, str_size, str_off = struct.unpack_from(">6I", info, 0)
    strings = info[str_off:str_off + str_size]

    def cstr(off: int) -> str:
        end = strings.find(b"\0", off)
        if off >= len(strings) or end < 0:
            raise ValueError("invalid RARC string offset")
        return strings[off:end].decode("ascii")

    nodes = []
    for i in range(node_count):
        at = node_off + i * 0x10
        nodes.append((cstr(u32(info, at + 4)), u16(info, at + 0xA), u32(info, at + 0xC)))
    records = []
    for i in range(entry_count):
        at = entry_off + i * 0x14
        records.append((u16(info, at), u16(info, at + 4), u16(info, at + 6),
                        u32(info, at + 8), u32(info, at + 0xC)))
    result: dict[str, bytes] = {}

    def walk(idx: int, parent: str = "") -> None:
        _, count, first = nodes[idx]
        for eid, flags, noff, off, size in records[first:first + count]:
            name = cstr(noff)
            if name in (".", ".."):
                continue
            path = f"{parent}/{name}" if parent else name
            typ = flags & 0xFF00
            if typ == 0x0200:
                walk(off, path)
            elif typ == 0x1100:
                result[path] = data[file_data_base + off:file_data_base + off + size]
            else:
                raise ValueError(f"unsupported RARC type {flags:#x} at {path}")

    walk(0)
    return result


def jhash(name: str) -> int:
    value = 0
    for b in name.encode("utf-8"):
        signed = b if b < 128 else b - 256
        value = (value * 31 + signed) & 0xFFFFFFFF
    return value


KNOWN_FIELDS = ["name", "Name", "pos_x", "pos_y", "pos_z", "PosX", "PosY", "PosZ",
                "Obj_arg0", "Obj_arg1", "Obj_arg2", "Obj_arg3", "Gravity_type",
                "Gravity", "Type", "AreaShape", "CameraSetId", "GroupId", "CommonPath_ID"]


def bcsv_header(data: bytes) -> dict:
    count, fields, data_off, row_size = struct.unpack_from(">4I", data, 0)
    if 16 + fields * 12 > data_off or data_off + count * row_size > len(data):
        raise ValueError("invalid BCSV bounds")
    descriptors = []
    for i in range(fields):
        at = 16 + i * 12
        h, mask, off, shift, typ = struct.unpack_from(">IIHBB", data, at)
        matches = [name for name in KNOWN_FIELDS if jhash(name) == h]
        descriptors.append({"hash": f"0x{h:08x}", "field": matches[0] if matches else None,
                            "mask": f"0x{mask:08x}", "offset": off, "shift": shift, "type": typ})
    return {"rows": count, "rowSize": row_size, "fields": descriptors}


def scalar(data: bytes, row_base: int, f: dict):
    off, typ = row_base + f["offset"], f["type"]
    if typ == 2:
        return struct.unpack_from(">f", data, off)[0]
    if typ in (0, 3):
        return u32(data, off)
    if typ == 4:
        return u16(data, off)
    if typ == 5:
        return data[off]
    return None


def read_bcsv(data: bytes) -> dict:
    header = bcsv_header(data)
    data_off = u32(data, 8)
    fields = header["fields"]
    names = {f["field"]: f for f in fields if f["field"]}
    first = {}
    if header["rows"] == 0:
        header["firstRow"] = None
        header["gravityTypeFields"] = [f for f in fields if f["field"] == "Gravity_type"]
        return header
    for key in ("name", "Name"):
        f = names.get(key)
        if f and f["type"] == 6:
            rel = u32(data, data_off + f["offset"])
            st = data_off + header["rows"] * header["rowSize"]
            end = data.find(b"\0", st + rel)
            first["name"] = data[st + rel:end].decode("ascii") if end >= 0 else None
            break
    if "Gravity_type" in names and names["Gravity_type"]["type"] == 6:
        f = names["Gravity_type"]
        rel = u32(data, data_off + f["offset"])
        st = data_off + header["rows"] * header["rowSize"]
        end = data.find(b"\0", st + rel)
        first["Gravity_type"] = data[st + rel:end].decode("ascii") if end >= 0 else None
    for field_name in ("pos_x", "pos_y", "pos_z", "PosX", "PosY", "PosZ"):
        if field_name in names:
            first[field_name] = scalar(data, data_off, names[field_name])
    header["firstRow"] = first
    header["gravityTypeFields"] = [f for f in fields if f["field"] in ("Gravity_type", "Gravity")]
    return header


def inspect_map(path: pathlib.Path) -> dict:
    archive_bytes = path.read_bytes()
    contents = rarc(archive_bytes)
    obj = None
    planet_tables = {}
    for name, content in contents.items():
        if name.lower().endswith("/objinfo"):
            try:
                if obj is None or "/common/" in name.lower():
                    obj = (name, read_bcsv(content))
            except (ValueError, struct.error):
                continue
        if name.lower().endswith("/planetobjinfo"):
            try:
                planet_tables[name] = read_bcsv(content)
            except (ValueError, struct.error):
                continue
    gravity = {name: {"rowCount": table["rows"],
                      "gravityTypeField": next((f for f in table["gravityTypeFields"]), None),
                      "firstRow": table["firstRow"]}
               for name, table in planet_tables.items()}
    scenario_path = path.with_name(path.name.replace("Map.arc", "Scenario.arc"))
    scenario = None
    if scenario_path.exists():
        scenario_bytes = scenario_path.read_bytes()
        scenario_content = rarc(scenario_bytes)
        scenario = {"arc": scenario_path.name,
                    "sha256": hashlib.sha256(scenario_bytes).hexdigest(),
                    "entries": sorted(scenario_content),
                    "bcsvTables": {name: {"sha256": hashlib.sha256(content).hexdigest(),
                                          "rowCount": bcsv_header(content)["rows"],
                                          "rowSize": bcsv_header(content)["rowSize"]}
                                   for name, content in scenario_content.items()}}
    obj_table = None
    if obj:
        table = obj[1]
        obj_table = {"entry": obj[0], "rowCount": table["rows"],
                     "rowSize": table["rowSize"], "firstRow": table["firstRow"],
                     "sha256": hashlib.sha256(contents[obj[0]]).hexdigest()}
    return {"mapArc": path.name, "sha256": hashlib.sha256(archive_bytes).hexdigest(),
            "archiveEntries": sorted(contents),
            "objInfo": obj_table,
            "gravityTables": gravity, "scenario": scenario}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--files-root", type=pathlib.Path, required=True)
    ap.add_argument("--maps", nargs="+", required=True, help="stage names from StageData")
    ap.add_argument("--output", type=pathlib.Path, required=True)
    args = ap.parse_args()
    out = {"source": "extracted user-supplied game data; no files modified",
           "formatSources": ["reference/Garigari/src/Game/Util/JMapInfo.cpp",
                             "reference/Garigari/src/JSystem/JKernel/JKRArchivePri.cpp"],
           "maps": []}
    for name in args.maps:
        path = args.files_root / "StageData" / name / f"{name}Map.arc"
        if not path.exists():
            out["maps"].append({"stage": name, "missing": True})
            continue
        parsed = inspect_map(path)
        parsed["stage"] = name
        out["maps"].append(parsed)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.output} with {len(out['maps'])} map fixture records")
    return 0


if __name__ == "__main__":
    sys.exit(main())
