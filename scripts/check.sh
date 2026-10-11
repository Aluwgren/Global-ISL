#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
mkdir -p "$BUILD_DIR"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
./scripts/test.sh
"${CLANG_CL:-clang-cl}" --driver-mode=cl --target=x86_64-pc-windows-msvc --analyze \
  /nologo /W4 /WX /GS- /GR- /EHs-c- /Zl /std:c++17 \
  GlobalPointLightISL.cpp -Xclang -analyzer-output=text
./scripts/package.sh
python3 -B tests/package_test.py "$BUILD_DIR"
REPRO="$(mktemp -d "$BUILD_DIR/repro.XXXXXX")"
trap 'rm -rf -- "$REPRO"' EXIT
BUILD_DIR="$REPRO" ./scripts/package.sh
cmp "$BUILD_DIR/GlobalPointLightISL.dll" "$REPRO/GlobalPointLightISL.dll"
for archive in "$REPRO"/*.zip; do
  cmp "$archive" "$BUILD_DIR/$(basename "$archive")"
done
echo "PASS repeatable DLL and mod-manager ZIP builds"
