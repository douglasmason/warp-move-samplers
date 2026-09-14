#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXPECTED=9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
TARGET="$ROOT/third_party/json.hpp"
if [ -f "$TARGET" ] && [ "$(sha256sum "$TARGET" | cut -d' ' -f1)" = "$EXPECTED" ]; then exit 0; fi
mkdir -p "$ROOT/third_party"
TEMP="$(mktemp "$ROOT/third_party/json.XXXXXX")"
trap 'rm -f "$TEMP"' EXIT
curl -fL --retry 3 https://raw.githubusercontent.com/nlohmann/json/9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03/single_include/nlohmann/json.hpp -o "$TEMP"
printf '%s  %s\n' "$EXPECTED" "$TEMP" | sha256sum -c -
mv "$TEMP" "$TARGET"
