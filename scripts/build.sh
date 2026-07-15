#!/usr/bin/env sh
set -eu

preset="${1:-dev}"
cmake --preset "$preset"
cmake --build --preset "$preset"
if [ "${2:-}" = "--test" ] && [ "$preset" = "dev" ]; then
  ctest --test-dir "build/dev" --output-on-failure
fi
