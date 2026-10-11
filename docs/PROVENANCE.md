# Test22 import provenance

Imported from the author-supplied `Global_ISL_1.5-test22_Astra_Codex_Review_Package.zip` on October 11, 2026.

At import commit `37826c1`, `GlobalPointLightISL.cpp`, `GlobalPointLightISL.ini`, and `kernel32.def` were byte-for-byte copies of the source archive. The GPL v3 license was also copied from that archive, as explicitly requested by the author. Historical Markdown documents are preserved under `docs/history/`.

Build intermediates, duplicate source copies under `build/`, and precompiled DLLs are excluded from Git. Build/verification/packaging scripts and GitHub documentation were prepared for this repository. The uploaded release remains the historical reference binary; a new compiler's output must not be described as that exact binary without a successful comparison.

The maintenance prerelease `1.5-test22.1` modifies the C++ source as documented in [REVIEW.md](REVIEW.md). The INI, import definition, and GPL license remain exact copies of the supplied files. The hashes below describe the original import, not the revised DLL.

## Verified hashes

SHA-256 values calculated from the supplied files:

| File | SHA-256 |
| --- | --- |
| Installable `Global Point Light ISL 1.5-test22.zip` | `137099a21e9546b29aad6c7667b88a49dd48acb293d44022d157650e45423a3f` |
| `Global Point Light ISL 1.5-test22 Source.zip` | `d2bc2a2c3449883102b2f5e4d7613c4db240ec9cb3630f83985d3a613159348e` |
| Supplied `GlobalPointLightISL.dll` | `d6c56da4d8d60c8236a3f412d58b585f99d4d833f0d88ab954bfa66ccbc91c86` |
| `GlobalPointLightISL.cpp` | `10cfcca6ec39c0fdafa34e43fdda1acc7545f545c708cfd7ffbe8c141675c605` |
| `GlobalPointLightISL.ini` | `dd4912114e09b1d59b60ab2e05b1963398330a29fa960d5dfbce6f28a4ec0b95` |
| `kernel32.def` | `8e90f283367f8a8df2eba1feba72be8b7a763ce9e5d0ab3180a60a5ba579c6b8` |

Both inner ZIPs match the package's `SHA256SUMS.txt`. The DLL in the installable ZIP also matches the source archive's build DLL and the documented DLL checksum.

The outer uploaded ZIP hashes to `6aa77b1cc798b7b15979155a9d04535460f7305ecbbc63c18dbe0aa907c29719`, which differs from the outer-archive entry inside its own `SHA256SUMS.txt` (`10fedcdd791b228eb974d59a7c7e6b7ad7e6cd8937c563039fb91f3643ad4eea`). That outer entry cannot verify the uploaded container; the inner archive and DLL matches are the verified provenance checks. Checksums establish these file comparisons, not independent authorship verification.
