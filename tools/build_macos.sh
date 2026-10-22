#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake --preset macos-release -S "$ROOT"
cmake --build "$ROOT/out/build/macos-release" --config Release --parallel
ctest --test-dir "$ROOT/out/build/macos-release" -C Release --output-on-failure
