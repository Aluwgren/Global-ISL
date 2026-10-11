# GitHub import validation

Checks performed on October 11, 2026 in the Linux cloud workspace using Debian LLVM **19.1.7**. These are repository preparation checks, not a new runtime audit of the mod.

| Check | Result |
| --- | --- |
| Source, INI, and import definition compared with supplied archive | Exact byte matches. |
| Inner archive and supplied DLL checksums | Match the supplied checksum records; see [provenance](PROVENANCE.md) for the outer container mismatch. |
| Optimized Windows x64 cross-build, C++17, `/O2 /W4 /WX` | Passed. |
| Clang static analysis of the imported source | Completed with zero diagnostics. |
| PE verification of supplied and rebuilt DLLs | Passed: AMD64 DLL, zero timestamp, Windows subsystem, only KERNEL32 imports, both legacy SKSE exports. |
| PE verifier negative checks | Rejected a text file and a DLL with a nonzero timestamp. |
| Repeat build in separate output directories | DLLs byte-for-byte identical. |
| Repeat packaging | ZIPs byte-for-byte identical; archive integrity and packaged INI/license checked. |
| Preservation of unrelated files in the build directory | Confirmed; the build cleans only its temporary staging directory. |
| In-game testing during this import | Not performed; requires Skyrim, SKSE, and compatible Open Shaders on Windows. |
| GitHub-hosted Actions run | Not verified from the cloud workspace. Equivalent build/package/reproducibility commands passed locally. |

## Binary identity

The supplied DLL has SHA-256:

```text
d6c56da4d8d60c8236a3f412d58b585f99d4d833f0d88ab954bfa66ccbc91c86
```

The LLVM 19.1.7 rebuild has SHA-256:

```text
7e5bf1002604db0270e4bf901768eaf47b6f73b1c6485f1fadf82d8e21bf8062
```

These differ. The original package does not pin the compiler version that produced its DLL; this import establishes reproducibility with the installed LLVM 19.1.7 toolchain, not reproduction of the original binary. The source and default configuration remain unchanged. Do not infer identical in-game behavior from build checks alone.

## Repeat the checks

With the build tools configured as in the README:

```sh
./scripts/package.sh
BUILD_DIR="$PWD/build-repro" ./scripts/package.sh
cmp build/GlobalPointLightISL.dll build-repro/GlobalPointLightISL.dll
cmp build/GlobalPointLightISL-1.5-test22.zip build-repro/GlobalPointLightISL-1.5-test22.zip
(cd build && sha256sum --check GlobalPointLightISL-1.5-test22.zip.sha256)

"${CLANG_CL:-clang-cl}" --driver-mode=cl --target=x86_64-pc-windows-msvc --analyze \
  /nologo /W4 /WX /GS- /GR- /EHs-c- /Zl /std:c++17 \
  GlobalPointLightISL.cpp -Xclang -analyzer-output=text
```

No automated game-runtime test suite was included in the source package. Historical game observations are retained in [the supplied status report](history/CURRENT_STATUS_FOR_ASTRA_CODEX_2026-10-11.md).
