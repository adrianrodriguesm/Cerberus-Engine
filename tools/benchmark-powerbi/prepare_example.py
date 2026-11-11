"""Validate the real T2 fixtures and prepare an immediately importable flat CSV.

Python 3.10+, standard library only. Does not modify the engine or its captures.
"""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HEADER = "camera,pass,type,mean_ms,median_ms,min_ms,max_ms,p95_ms,samples"


def read_capture(path):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    start = lines.index(HEADER)
    metadata = dict(line[2:].split(": ", 1) for line in lines[:start] if line.startswith("# "))
    rows = list(csv.DictReader(lines[start:]))
    assert rows and len({(r["camera"], r["pass"], r["type"]) for r in rows}) == len(rows)
    for row in rows:
        assert int(row["samples"]) > 0
        for field in ("mean_ms", "median_ms", "min_ms", "max_ms", "p95_ms"):
            assert float(row[field]) >= 0
    return metadata, rows


def main():
    captures = sorted((ROOT / "example/captures").glob("*.csv"))
    assert len(captures) == 2, "Example requires its two original captures"
    flattened, runs = [], []
    for capture in captures:
        meta, rows = read_capture(capture)
        runs.append((capture.name, meta, rows))
        flattened.extend(dict(Run=capture.name, **meta, **row) for row in rows)
    output = ROOT / "example/Benchmarks-flat.csv"
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(flattened[0]))
        writer.writeheader()
        writer.writerows(flattened)
    a, b = runs
    assert {k: v for k, v in a[1].items() if k != "git_commit"} == {
        k: v for k, v in b[1].items() if k != "git_commit"}
    frame_a = next(r for r in a[2] if r["pass"] == "FrameTotal")
    frame_b = next(r for r in b[2] if r["pass"] == "FrameTotal")
    comparison = {}
    for field in ("mean_ms", "median_ms", "p95_ms"):
        av, bv = float(frame_a[field]), float(frame_b[field])
        comparison[field] = dict(A=av, B=bv, delta_ms=bv-av, delta_percent=100*(bv-av)/av)
    passes = sorted((r for r in b[2] if r["type"] not in ("CPU", "Frame")),
                    key=lambda r: float(r["mean_ms"]), reverse=True)
    summary = dict(run_a=a[0], run_b=b[0], rows=len(flattened), frame=comparison,
                   top_gpu_passes_b=[dict(pass_name=r["pass"], mean_ms=float(r["mean_ms"])) for r in passes[:5]],
                   validation="CSV parsing/arithmetic only; Power Query, DAX and Desktop not executed")
    (ROOT / "example/expected-results.json").write_text(json.dumps(summary, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
