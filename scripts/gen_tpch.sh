#!/usr/bin/env bash
set -euo pipefail

SF="${1:-1}"
OUT_DIR="$(cd "$(dirname "$0")/.." && pwd)/data"
mkdir -p "$OUT_DIR"

echo "=== Generating TPC-H SF=$SF Parquet files to $OUT_DIR ==="

duckdb :memory: <<EOF
INSTALL tpch;
LOAD tpch;
CALL dbgen(sf=$SF);

EXPORT DATABASE '$OUT_DIR' (FORMAT PARQUET);
EOF

# Move *.parquet to OUT_DIR root if they ended up in a subdir
if [ -f "$OUT_DIR/lineitem.parquet" ]; then
    echo "OK: $OUT_DIR/lineitem.parquet"
else
    # EXPORT DATABASE creates a subdirectory; flatten if needed
    find "$OUT_DIR" -name "*.parquet" -exec mv -t "$OUT_DIR" {} +
fi

echo ""
echo "Generated:"
ls -lh "$OUT_DIR"/*.parquet
echo ""
echo "Smoke check:"
duckdb -c "SELECT count(*) AS n_lineitem FROM read_parquet('$OUT_DIR/lineitem.parquet')"
