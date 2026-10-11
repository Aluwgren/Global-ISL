#!/usr/bin/env python3
"""Package a verified build with stable archive metadata and a SHA-256 checksum."""

import argparse
import hashlib
from pathlib import Path
import re
import zipfile

from verify_pe import verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    first_line = (root / "GlobalPointLightISL.cpp").read_text().splitlines()[0]
    match = re.fullmatch(r"// Global Point Light ISL ([0-9][A-Za-z0-9.-]*)", first_line)
    if match is None:
        parser.error("could not read version from the source header")
    archive = build / f"GlobalPointLightISL-{match[1]}.zip"
    plugins = build / "package/SKSE/Plugins"
    verify(plugins / "GlobalPointLightISL.dll")
    # Enumerate only release files; never include arbitrary build directory contents.
    files = {
        "SKSE/Plugins/GlobalPointLightISL.dll": plugins / "GlobalPointLightISL.dll",
        "SKSE/Plugins/GlobalPointLightISL.ini": plugins / "GlobalPointLightISL.ini",
        "docs/GlobalPointLightISL/LICENSE.txt": root / "LICENSE",
        "docs/GlobalPointLightISL/README.txt": root / "docs/INSTALL.txt",
    }
    payloads = {name: path.read_bytes() for name, path in files.items()}
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for name in sorted(payloads):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(info, payloads[name], compresslevel=9)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    print(f"Packaged: {archive}")
    print(f"SHA-256: {digest}")


if __name__ == "__main__":
    main()
