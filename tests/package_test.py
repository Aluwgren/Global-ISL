"""Check the mod-manager installation contract against the built ZIP."""
import hashlib
from pathlib import Path
import sys
import tempfile
import zipfile

root = Path(__file__).resolve().parent.parent
build = Path(sys.argv[1]).resolve()
version = (root / "GlobalPointLightISL.cpp").read_text().splitlines()[0].split()[-1]
archive = build / f"GlobalPointLightISL-{version}.zip"
expected = {
    "SKSE/Plugins/GlobalPointLightISL.dll",
    "SKSE/Plugins/GlobalPointLightISL.ini",
    "docs/GlobalPointLightISL/LICENSE.txt",
    "docs/GlobalPointLightISL/README.txt",
}
with zipfile.ZipFile(archive) as package:
    assert set(package.namelist()) == expected
    assert len(package.namelist()) == len(expected)
    assert package.testzip() is None
    with tempfile.TemporaryDirectory() as directory:
        data = Path(directory) / "Data"
        package.extractall(data)
        assert (data / "SKSE/Plugins/GlobalPointLightISL.dll").read_bytes() == (build / "GlobalPointLightISL.dll").read_bytes()
        assert (data / "SKSE/Plugins/GlobalPointLightISL.ini").read_bytes() == (root / "GlobalPointLightISL.ini").read_bytes()
        assert (data / "docs/GlobalPointLightISL/LICENSE.txt").read_bytes() == (root / "LICENSE").read_bytes()
        assert version in (data / "docs/GlobalPointLightISL/README.txt").read_text()
        assert not (data / "Data").exists()
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
assert archive.with_suffix(".zip.sha256").read_text() == f"{digest}  {archive.name}\n"
print("PASS mod-manager installation layout, archive integrity, exact payloads, and checksum")
