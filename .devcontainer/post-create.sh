#!/bin/bash
set -e

echo "=== Bespoke OLAP (Claude) ==="
echo "Tools:"
echo "  - $(g++ --version | head -n1)"
echo "  - $(duckdb -version)"
echo "  - $(python3 --version)"
echo "  - $(uv --version 2>/dev/null || echo 'uv: not installed')"
echo ""
echo "Next steps:"
echo "  1. Generate TPC-H data (next phase)"
echo "  2. Run 'claude' to start synthesis"
