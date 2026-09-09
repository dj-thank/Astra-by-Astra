"""Summarize actual UE CSV frame intervals, retaining startup/capture hitches."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    index = (len(ordered) - 1) * q
    lo, hi = math.floor(index), math.ceil(index)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (index - lo)


def metrics(values: list[float]) -> dict:
    if not values:
        return {"frames": 0}
    return {
        "frames": len(values), "durationSeconds": sum(values) / 1000,
        "averageFps": 1000 * len(values) / sum(values),
        "medianMs": percentile(values, .5), "p95Ms": percentile(values, .95),
        "p99Ms": percentile(values, .99), "maxMs": max(values),
        "over33_34ms": sum(v > 33.34 for v in values),
        "over50ms": sum(v > 50 for v in values),
        "over100ms": sum(v > 100 for v in values),
    }


def summarize(path: Path, warmup: float) -> dict:
    frames: list[float] = []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = csv.reader(stream)
        header = next(rows)
        column = header.index("FrameTime")
        for row in rows:
            if not row or len(row) <= column:
                continue
            try:
                value = float(row[column])
            except ValueError:
                continue
            if math.isfinite(value) and value > 0:
                frames.append(value)
    elapsed = 0.0
    after_warmup = []
    for value in frames:
        elapsed += value / 1000
        if elapsed > warmup:
            after_warmup.append(value)
    return {
        "file": str(path.resolve()),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "allFrames": metrics(frames),
        "afterInitialSeconds": warmup,
        "afterWarmup": metrics(after_warmup),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--warmup", type=float, default=5)
    args = parser.parse_args()
    result = {
        "scope": "UE CSV frame intervals for scripted render poses. Not a physical-input or continuous-flight test.",
        "caveats": "Screenshots and first-use shader/streaming hitches remain in allFrames; no frame-generation estimate.",
        "captures": [summarize(p, args.warmup) for p in args.csv],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
