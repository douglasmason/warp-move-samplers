#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/vendor"
if [ -d "$ROOT/vendor/bungee/.git" ]; then echo "Bungee already present"; exit 0; fi
git clone --recurse-submodules https://github.com/bungee-audio-stretch/bungee.git "$ROOT/vendor/bungee"
cd "$ROOT/vendor/bungee"
git checkout 7354c0c62652dd85af90fddfeec307881f3b4252
git submodule update --init --recursive
echo "Pinned Bungee at $(git rev-parse HEAD)"
