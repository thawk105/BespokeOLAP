#!/bin/bash
set -e

echo "=== Bespoke OLAP (Claude) ==="
echo "Tools:"
echo "  - $(g++ --version | head -n1)"
echo "  - $(duckdb -version)"
echo "  - $(python3 --version)"
echo "  - $(uv --version 2>/dev/null || echo 'uv: not installed')"

# Phase 2+: Python deps needed by validate.py and friends.
# Idempotent: pip skips already-installed satisfied versions.
REQ_FILE="$(dirname "$0")/../requirements.txt"
if [ -f "$REQ_FILE" ]; then
    echo ""
    echo "Installing Python deps from requirements.txt..."
    pip3 install --quiet -r "$REQ_FILE"
fi

echo ""
echo "Next steps:"
echo "  1. Generate TPC-H data: bash scripts/gen_tpch.sh 1"
echo "  2. Synthesize a query:  claude  -> /synthesize tpch q1"
