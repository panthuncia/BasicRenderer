#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'USAGE'
Usage:
  collect_nif_shader_flags.sh <target-dir> [brnifly-exe] [output-dir]

Environment:
  BRNIFLY_EXE may be set instead of passing brnifly-exe.

Outputs:
  shader_flags.txt
  shader_flag_combinations.txt
  shader_flag_report.jsonl
USAGE
}

if [[ $# -lt 1 || $# -gt 3 ]]; then
    usage
    exit 2
fi

target_dir=$1
brnifly_exe=${2:-${BRNIFLY_EXE:-BRNifly}}
output_dir=${3:-.}

if [[ ! -d "$target_dir" ]]; then
    echo "Target directory does not exist: $target_dir" >&2
    exit 2
fi

mkdir -p "$output_dir"

export BRNIFLY_SHADER_TARGET_DIR="$target_dir"
export BRNIFLY_SHADER_EXE="$brnifly_exe"
export BRNIFLY_SHADER_OUTPUT_DIR="$output_dir"

python - <<'PY'
import json
import os
import subprocess
import sys
from pathlib import Path

target_dir = Path(os.environ["BRNIFLY_SHADER_TARGET_DIR"])
brnifly_exe = os.environ["BRNIFLY_SHADER_EXE"]
output_dir = Path(os.environ["BRNIFLY_SHADER_OUTPUT_DIR"])

flags_by_key = {}
combinations_by_key = {}
raw_reports = []
errors = []

def sort_flag_key(item):
    set_order = {"shaderFlags1": 0, "shaderFlags2": 1}
    return (set_order.get(item.get("set", ""), 99), item.get("name", ""))

def format_flag(item):
    return f'{item["set"]}\t{item["name"]}\t{item["maskHex"]}'

def format_combo(item):
    names1 = ", ".join(item.get("shaderFlags1Names", [])) or "<none>"
    names2 = ", ".join(item.get("shaderFlags2Names", [])) or "<none>"
    return f'{item["shaderFlags1Hex"]}\t{item["shaderFlags2Hex"]}\tshaderFlags1=[{names1}]\tshaderFlags2=[{names2}]'

nif_paths = sorted(p for p in target_dir.rglob("*") if p.is_file() and p.suffix.lower() == ".nif")

for nif_path in nif_paths:
    try:
        completed = subprocess.run(
            [brnifly_exe, "--shader-flags-json", str(nif_path)],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as exc:
        print(f"Failed to launch BRNifly: {exc}", file=sys.stderr)
        sys.exit(1)

    if completed.returncode != 0:
        errors.append({"path": str(nif_path), "error": completed.stderr.strip()})
        continue

    try:
        report = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        errors.append({"path": str(nif_path), "error": f"invalid JSON: {exc}"})
        continue

    raw_reports.append(report)
    if report.get("status") != "ok":
        errors.append({"path": str(nif_path), "error": report.get("message", "unknown error")})
        continue

    for flag in report.get("discoveredFlags", []):
        flags_by_key[(flag.get("set"), flag.get("name"))] = flag

    for combo in report.get("uniqueCombinations", []):
        key = (combo.get("shaderFlags1Hex"), combo.get("shaderFlags2Hex"))
        existing = combinations_by_key.setdefault(key, {**combo, "files": set(), "shapeCount": 0})
        existing["files"].add(str(nif_path))
        existing["shapeCount"] += sum(
            1
            for shape in report.get("shapes", [])
            if shape.get("shader", {}).get("shaderFlags1Hex") == key[0]
            and shape.get("shader", {}).get("shaderFlags2Hex") == key[1]
        )

flags_path = output_dir / "shader_flags.txt"
combos_path = output_dir / "shader_flag_combinations.txt"
jsonl_path = output_dir / "shader_flag_report.jsonl"

with flags_path.open("w", encoding="utf-8") as out:
    out.write("set\tname\tmask\n")
    for item in sorted(flags_by_key.values(), key=sort_flag_key):
        out.write(format_flag(item) + "\n")

with combos_path.open("w", encoding="utf-8") as out:
    out.write("shaderFlags1\tshaderFlags2\tshaderFlags1Names\tshaderFlags2Names\tshapeCount\tfileCount\n")
    for item in sorted(combinations_by_key.values(), key=lambda x: (x.get("shaderFlags1Hex", ""), x.get("shaderFlags2Hex", ""))):
        out.write(format_combo(item) + f'\t{item["shapeCount"]}\t{len(item["files"])}\n')

with jsonl_path.open("w", encoding="utf-8") as out:
    for report in raw_reports:
        out.write(json.dumps(report, separators=(",", ":")) + "\n")

print(f"Scanned {len(nif_paths)} .nif file(s).")
print(f"Wrote {flags_path}")
print(f"Wrote {combos_path}")
print(f"Wrote {jsonl_path}")

if errors:
    errors_path = output_dir / "shader_flag_errors.json"
    with errors_path.open("w", encoding="utf-8") as out:
        json.dump(errors, out, indent=2)
    print(f"{len(errors)} file(s) failed; details in {errors_path}", file=sys.stderr)
PY
