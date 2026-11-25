# Copyright (c) 2026 guardiancrow. MIT.
"""Measure the isolated 3E and 3F WebP product paths."""
import argparse
import importlib.util
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    path = Path(__file__).with_name("measure_sec_maint_3e_avif.py")
    spec = importlib.util.spec_from_file_location("avif_measurements", path)
    measurement = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(measurement)
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        for fixture in ("tiny", "text", "detail", "noise", "alpha", "full-hd", "4k"):
            result = measurement.compare(args.reference.resolve(), args.candidate.resolve(), fixture, "lossless")
            print(json.dumps({"fixture": fixture, **result}), file=output, flush=True)
            print(f"{fixture} complete", flush=True)


if __name__ == "__main__":
    main()
