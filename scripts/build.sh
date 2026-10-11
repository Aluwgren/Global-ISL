#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
CLANG_CL="${CLANG_CL:-clang-cl}"
LLD_LINK="${LLD_LINK:-lld-link}"

for tool in "$CLANG_CL" "$LLD_LINK" python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Required tool not found: $tool" >&2
    echo "Install Clang and LLD, or set CLANG_CL and LLD_LINK to their executable paths." >&2
    exit 1
  fi
done

mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"
# Compile in our own directory. Do not erase an existing build directory.
STAGE="$(mktemp -d "$BUILD/.compile.XXXXXX")"
trap 'rm -rf -- "$STAGE"' EXIT
cp "$ROOT/GlobalPointLightISL.cpp" "$ROOT/kernel32.def" "$STAGE/"

"$CLANG_CL" --version
"$LLD_LINK" --version
(
  cd "$STAGE"
  "$LLD_LINK" /lib /def:kernel32.def /machine:x64 /out:kernel32.lib
  "$CLANG_CL" --driver-mode=cl --target=x86_64-pc-windows-msvc /nologo /c /O2 /W4 /WX /GS- /GR- /EHs-c- /Zl /std:c++17 /FoGlobalPointLightISL.obj GlobalPointLightISL.cpp
  "$LLD_LINK" /dll /machine:x64 /entry:DllMain /nodefaultlib /timestamp:0 /subsystem:windows /out:GlobalPointLightISL.dll GlobalPointLightISL.obj kernel32.lib /export:SKSEPlugin_Query /export:SKSEPlugin_Load
)

"$ROOT/scripts/verify_pe.sh" "$STAGE/GlobalPointLightISL.dll"
mkdir -p "$BUILD/package/SKSE/Plugins"
cp "$STAGE/GlobalPointLightISL.dll" "$BUILD/GlobalPointLightISL.dll"
cp "$STAGE/GlobalPointLightISL.dll" "$ROOT/GlobalPointLightISL.ini" "$BUILD/package/SKSE/Plugins/"
echo "Built and verified: $BUILD/GlobalPointLightISL.dll"
