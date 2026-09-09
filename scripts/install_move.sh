#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -d "$ROOT/dist/warpmrsample" ] || { echo "Run ./scripts/build_move.sh first"; exit 1; }
for id in warpmrsample warpmelodic warpmrdrums warpdrumkit; do
  ssh ableton@move.local "mkdir -p /data/UserData/schwung/modules/sound_generators/$id"
  scp -r "$ROOT/dist/$id/"* "ableton@move.local:/data/UserData/schwung/modules/sound_generators/$id/"
  ssh ableton@move.local "chmod -R a+rw /data/UserData/schwung/modules/sound_generators/$id"
done
echo "Installed four modules. Restart Schwung/Move UI to reload modules."
