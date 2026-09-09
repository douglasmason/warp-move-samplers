#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
g++ -O2 -std=c++17 -I"$ROOT/src/common" "$ROOT/src/common/warp_core.cpp" "$ROOT/tests/test_core.cpp" -o "$ROOT/tests/test_core"
"$ROOT/tests/test_core"
g++ -O2 -std=c++17 -I"$ROOT/src/common" "$ROOT/src/common/warp_core.cpp" "$ROOT/tests/test_warp.cpp" -o "$ROOT/tests/test_warp"
"$ROOT/tests/test_warp"
python3 "$ROOT/tests/test_manifests.py"
./scripts/build_host.sh
python3 "$ROOT/tests/test_plugin_abi.py"
