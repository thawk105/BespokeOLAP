#!/usr/bin/env python3
"""Validate a bespoke C++ engine's query output against DuckDB."""
import argparse
import subprocess
import sys
from pathlib import Path

import duckdb


def run_duckdb(query_sql: str, data_dir: Path) -> list[tuple]:
    con = duckdb.connect(":memory:")
    for pq in data_dir.glob("*.parquet"):
        table = pq.stem
        con.execute(f"CREATE VIEW {table} AS SELECT * FROM read_parquet('{pq}')")
    return con.execute(query_sql).fetchall()


def run_engine(engine_path: Path, query_id: str, data_dir: Path) -> list[tuple]:
    """Run the bespoke engine and parse its CSV output to rows of tuples."""
    result = subprocess.run(
        [str(engine_path), "--query", query_id, "--data-dir", str(data_dir)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        print(f"Engine exited with {result.returncode}", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        sys.exit(2)
    rows = []
    for line in result.stdout.strip().splitlines():
        if not line or line.startswith("#"):
            continue
        rows.append(tuple(line.split(",")))
    return rows


def normalize(rows):
    return [tuple(str(v).strip() for v in row) for row in rows]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", type=Path, required=True, help="Path to the C++ engine executable")
    ap.add_argument("--query", required=True, help="Query id, e.g. q1")
    ap.add_argument("--data-dir", type=Path, default=Path("data"))
    ap.add_argument("--reference-dir", type=Path, default=Path("reference"))
    args = ap.parse_args()

    sql_file = args.reference_dir / f"{args.query}.sql"
    if not sql_file.exists():
        print(f"Reference SQL not found: {sql_file}", file=sys.stderr)
        sys.exit(2)

    expected = normalize(run_duckdb(sql_file.read_text(), args.data_dir))
    actual = normalize(run_engine(args.engine, args.query, args.data_dir))

    if expected == actual:
        print(f"PASS: {args.query} ({len(expected)} rows)")
        sys.exit(0)
    else:
        print(f"FAIL: {args.query}")
        print(f"  expected rows: {len(expected)}, actual rows: {len(actual)}")
        from difflib import unified_diff
        exp_lines = [",".join(r) for r in expected]
        act_lines = [",".join(r) for r in actual]
        for line in list(unified_diff(exp_lines, act_lines, lineterm="", n=3))[:50]:
            print(f"  {line}")
        sys.exit(1)


if __name__ == "__main__":
    main()
