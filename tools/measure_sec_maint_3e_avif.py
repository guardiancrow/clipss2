# Copyright (c) 2026 guardiancrow. MIT.
"""Compare isolated SEC-MAINT-3E AVIF probes in alternating order."""

import argparse
import json
import math
from pathlib import Path
import statistics
import subprocess


def sample(process):
    process.stdin.write("sample\n")
    process.stdin.flush()
    values = list(map(float, process.stdout.readline().split()))
    if len(values) != 7:
        raise RuntimeError("AVIF probe did not report a complete sample")
    return values


def summarize(rows):
    result = {}
    keys = ("encodeMs", "decodeMs", "encodedBytes", "peakWorkingSetBytes", "rgbMae", "rgbMax", "rgbMse")
    for key, values in zip(keys, zip(*rows)):
        result[key] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
    mse = result["rgbMse"]["median"]
    result["rgbPsnrDb"] = 10 * math.log10(255**2 / mse) if mse else None
    return result


def compare(reference, candidate, fixture, mode):
    probes = {}
    rows = {"reference": [], "candidate": []}
    try:
        for variant, program in (("reference", reference), ("candidate", candidate)):
            probes[variant] = subprocess.Popen(
                [program, "bench", fixture, mode], cwd=program.parent,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
            )
        for process in probes.values():
            sample(process)
        for index in range(10):
            order = ("reference", "candidate") if index % 2 == 0 else ("candidate", "reference")
            for variant in order:
                rows[variant].append(sample(probes[variant]))
        for process in probes.values():
            process.stdin.close()
        for process in probes.values():
            if process.wait() != 0:
                raise RuntimeError("An AVIF probe failed")
    finally:
        for process in probes.values():
            if process.poll() is None:
                process.terminate()
                process.wait()
            process.stdout.close()
            process.stdin.close()
    return {name: {"summary": summarize(data), "samples": data} for name, data in rows.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        for fixture in ("flat", "text", "photo", "noise", "alpha", "large", "odd"):
            for mode in ("lossless", "lossy"):
                row = compare(args.reference.resolve(), args.candidate.resolve(), fixture, mode)
                print(json.dumps({"fixture": fixture, "mode": mode, **row}), file=output, flush=True)
                print(f"{fixture} {mode} complete", flush=True)


if __name__ == "__main__":
    main()
