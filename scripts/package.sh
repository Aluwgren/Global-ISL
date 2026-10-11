#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/scripts/build.sh"
exec python3 -B "$ROOT/scripts/package.py" "${BUILD_DIR:-$ROOT/build}"
