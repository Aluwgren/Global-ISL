# Global Point Light ISL

An SKSE plugin by **Aluwgren** for Skyrim Special Edition **1.5.97**, SKSE **2.0.20**, and Open Shaders. The current maintenance prerelease is **1.5-test22.1**, based on the tested test22 source.

[Source repository](https://github.com/Aluwgren/Global-ISL)

Global ISL manages local inverse-square lighting through existing renderer hooks. Its test22 defaults include:

- A maximum local point/spot shadow depth bias of **1.0**, preserving lower authored values.
- Fire color **255 / 140 / 60**, with intensity preservation and authoritative color handling.
- Dedicated magic hand and impact/hazard radii of **40** and **20** units.
- Normal-light shadow promotion and a limiter for continuous spell shadow creation.
- Exact-owner Deathbell light suppression.
- No broad ordinary-light radius cap (`MaxRadius=0`).

The separate FaceGen Shadow Softener experiment is not included. The test22 lighting defaults and INI are unchanged. This update fixes hook rollback and lifecycle bookkeeping; see [changes and review findings](docs/REVIEW.md) and [import provenance](docs/PROVENANCE.md).

## Requirements and compatibility

- Windows x64 and Skyrim SE **1.5.97**. The plugin rejects other Skyrim runtime versions; Anniversary Edition and VR are not supported by this baseline.
- SKSE **2.0.20**.
- A compatible Open Shaders build providing `CommunityShaders.dll`. Optional hooks check the renderer's binary structure and remain disabled if their expected signatures cannot be resolved safely.

Do not assume compatibility with every Open Shaders version. The supplied notes discuss 2.17-era binaries and 2.18 source, but do not establish a complete runtime compatibility matrix. This plugin does not require Address Library, CommonLibSSE, or a Visual C++ runtime installation.

## Installation

Download **GlobalPointLightISL-1.5-test22.1.zip** from the [release page](https://github.com/Aluwgren/Global-ISL/releases/tag/v1.5-test22.1). The separately labeled source ZIP is for developers. The package contains:

```text
SKSE/Plugins/GlobalPointLightISL.dll
SKSE/Plugins/GlobalPointLightISL.ini
```

Install the ZIP as a normal mod in **MO2 or Vortex**. Its root already corresponds to Skyrim's `Data` directory, so no folder selection or FOMOD is needed. For manual installation, extract it into `Data`; the DLL should end up at `Data/SKSE/Plugins/GlobalPointLightISL.dll`. Launch the game through SKSE. Back up any customized INI before replacing it.

Settings are documented in [GlobalPointLightISL.ini](GlobalPointLightISL.ini). Logging is disabled by default; when enabled, the log is written to `Data/SKSE/Plugins/GlobalPointLightISL.log`.

## Building on Linux

The plugin cross-compiles to a Windows x64 DLL using LLVM, without a Windows SDK or game installation. Use Bash, Python 3, and LLVM **19** (`clang-cl` and `lld-link`), plus a native C++17 compiler for regression tests. On Debian 13:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends clang-19 lld-19 g++ python3
export CLANG_CL=clang-19
export LLD_LINK=lld-link-19
./scripts/build.sh
```

Use the equivalent root `apt-get` commands in a container without `sudo`. If unversioned tools are already on `PATH`, the exports are unnecessary. `CLANG_CL`, `LLD_LINK`, and `BUILD_DIR` can override tool and output paths.

The build uses C++17, optimization, and warnings as errors. It verifies that the result is an x64 DLL, imports only `KERNEL32.dll`, exports the two legacy SKSE entry points, and has a zero PE timestamp.

```sh
# Run regression tests, static analysis, build, package, and reproducibility checks.
./scripts/check.sh

# Verify an existing DLL.
./scripts/verify_pe.sh build/GlobalPointLightISL.dll

# Build and produce an installable ZIP and SHA-256 checksum.
./scripts/package.sh
```

Outputs go under `build/`, which is ignored by Git. Packaging includes the license and installation notes under `docs/GlobalPointLightISL/`. GitHub Actions runs the regression, analysis, PE, installation-layout, and reproducibility checks. Version tags publish a prerelease only after those checks, with the installable ZIP, matching source ZIP, and SHA-256 files. Compiler changes may change the DLL; see [validation](docs/VALIDATION.md) for the distinction between this build and the supplied binary.

## Validation and development

This new DLL has **not yet been tested in Skyrim**. Native regression tests exercise the actual source with simulated Win32 services; they do not execute renderer machine code. Keep the tested test22 build available for rollback. Test changes in Skyrim with the supported runtime and renderer, including save/load, cell transitions, local shadow contact, torch/magic lighting, and optional feature toggles.

The supplied October 11, 2026 status report records successful user testing of test22's 1.0 bias cap. It supersedes the earlier audit's statement that this cap had not been tested. Those are historical reports, not new in-game testing performed for this repository import.

- [Review findings and follow-up testing](docs/REVIEW.md)
- [Current validation record](docs/VALIDATION.md)
- [Original feature description and historical handoffs](docs/history/README.md)
- [Contribution and bug-report guidance](CONTRIBUTING.md)

## License

GNU General Public License v3. See [LICENSE](LICENSE). The source package's GPL v3 license replaces the empty repository's previous MIT template at the author's request.
