#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}/tests"
mkdir -p "$BUILD"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -fno-builtin -ffunction-sections -fdata-sections \
  "$ROOT/tests/runtime_tests.cpp" -Wl,--gc-sections -o "$BUILD/runtime_tests"
"$BUILD/runtime_tests"
