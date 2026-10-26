#!/usr/bin/env python3
"""Compare the native asset loader with independently decoded real-game fixtures."""
import argparse
import json
import math
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scan", type=Path, default=root / "evidence/native-stage-scan.json")
    parser.add_argument("--fixtures", type=Path, default=root / "evidence/asset-fixtures.json")
    parser.add_argument("--out", type=Path, default=root / "evidence/native-asset-verification.json")
    args = parser.parse_args()
    scan = json.loads(args.scan.read_text())
    fixtures = json.loads(args.fixtures.read_text())
    if scan["summary"]["errors"]:
        raise ValueError("Native scan has archive/table errors")
    checked = []
    for fixture in fixtures["maps"]:
        if fixture.get("missing"):
            continue
        suffix = f"{fixture['stage']}/{fixture['mapArc']}"
        matches = [arc for arc in scan["archives"] if arc["path"].endswith(suffix)]
        if len(matches) != 1:
            raise ValueError(f"Expected one archive for {suffix}")
        tables = {table["path"]: table for table in matches[0]["tables"]}
        expected_tables = {fixture["objInfo"]["entry"]: fixture["objInfo"]}
        expected_tables.update(fixture["gravityTables"])
        count = 0
        for path, expected in expected_tables.items():
            actual = tables[path]
            if actual["rows"] != expected["rowCount"]:
                raise ValueError(f"Row count mismatch in {suffix}:{path}")
            first = expected["firstRow"]
            if first:
                obj = actual["objects"][0]
                if obj["name"] != first["name"]:
                    raise ValueError(f"Object name mismatch in {suffix}:{path}")
                for axis, value in zip(("pos_x", "pos_y", "pos_z"), obj["position"]):
                    if not math.isclose(value, first[axis], rel_tol=1e-7, abs_tol=0.001):
                        raise ValueError(f"Position mismatch in {suffix}:{path}:{axis}")
            count += 1
        checked.append({"stage": fixture["stage"], "tables_checked": count})
    if len(checked) < 3:
        raise ValueError("Expected at least three independent map fixtures")
    report = {"result": "pass", "native_scan_summary": scan["summary"], "fixtures": checked}
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(checked)} maps, {sum(item['tables_checked'] for item in checked)} tables; native rows and object transforms match independent fixtures")


if __name__ == "__main__":
    main()
