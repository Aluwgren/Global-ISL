# Global Point Light ISL

An SKSE plugin by **Aluwgren** for Skyrim Special Edition **1.5.97**, SKSE **2.0.20**, and Open Shaders. This repository contains the **1.5-test22** source baseline.

[Source repository](https://github.com/Aluwgren/Global-ISL)

Global ISL manages local inverse-square lighting through existing renderer hooks. Its test22 defaults include:

- A maximum local point/spot shadow depth bias of **1.0**, preserving lower authored values.
- Fire color **255 / 140 / 60**, with intensity preservation and authoritative color handling.
- Dedicated magic hand and impact/hazard radii of **40** and **20** units.
- Normal-light shadow promotion and a limiter for continuous spell shadow creation.
- Exact-owner Deathbell light suppression.
- No broad ordinary-light radius cap (`MaxRadius=0`).

The separate FaceGen Shadow Softener experiment is not included. The imported source and INI defaults are unchanged; see [import provenance](docs/PROVENANCE.md).

## Requirements and compatibility

- Windows x64 and Skyrim SE **1.5.97**. The plugin rejects other Skyrim runtime versions; Anniversary Edition and VR are not supported by this baseline.
- SKSE **2.0.20**.
- A compatible Open Shaders build providing `CommunityShaders.dll`. Optional hooks check the renderer's binary structure and remain disabled if their expected signatures cannot be resolved safely.

Do not assume compatibility with every Open Shaders version. The supplied notes discuss 2.17-era binaries and 2.18 source, but do not establish a complete runtime compatibility matrix. This plugin does not require Address Library, CommonLibSSE, or a Visual C++ runtime installation.

## Installation

Use an installable ZIP from a build or a release, rather than GitHub's automatic source ZIP. The package contains:

```text
Data/SKSE/Plugins/GlobalPointLightISL.dll
Data/SKSE/Plugins/GlobalPointLightISL.ini
```

Install through your mod manager, selecting `Data` as the data directory if prompted, or copy those two files into your game's `Data/SKSE/Plugins/` directory. Launch the game through SKSE. Back up any customized INI before replacing it.

Settings are documented in [GlobalPointLightISL.ini](GlobalPointLightISL.ini). Logging is disabled by default; when enabled, the log is written to `Data/SKSE/Plugins/GlobalPointLightISL.log`.

## Building on Linux

The plugin cross-compiles to a Windows x64 DLL using LLVM, without a Windows SDK or game installation. Use Bash, Python 3, and LLVM **19** (`clang-cl` and `lld-link`). On Debian 13:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends clang-19 lld-19 python3
export CLANG_CL=clang-19
export LLD_LINK=lld-link-19
./scripts/build.sh
```

Use the equivalent root `apt-get` commands in a container without `sudo`. If unversioned tools are already on `PATH`, the exports are unnecessary. `CLANG_CL`, `LLD_LINK`, and `BUILD_DIR` can override tool and output paths.

The build uses C++17, optimization, and warnings as errors. It verifies that the result is an x64 DLL, imports only `KERNEL32.dll`, exports the two legacy SKSE entry points, and has a zero PE timestamp.

```sh
# Verify an existing DLL.
./scripts/verify_pe.sh build/GlobalPointLightISL.dll

# Build and produce an installable ZIP and SHA-256 checksum.
./scripts/package.sh
```

Outputs go under `build/`, which is ignored by Git. Packaging includes the license and this README. GitHub Actions performs the build, PE checks, and a repeated-build comparison, then uploads the ZIP and checksum as a workflow artifact. It does not create a public release automatically. Compiler changes may change the DLL; see [validation](docs/VALIDATION.md) for the distinction between this build and the supplied binary.

## Validation and development

Cloud checks validate compilation and packaging, not the game's rendering behavior. Test changes in Skyrim with the supported runtime and renderer, including save/load, cell transitions, local shadow contact, torch/magic lighting, and optional feature toggles.

The supplied October 11, 2026 status report records successful user testing of test22's 1.0 bias cap. It supersedes the earlier audit's statement that this cap had not been tested. Those are historical reports, not new in-game testing performed for this repository import.

- [Current validation record](docs/VALIDATION.md)
- [Original feature description and historical handoffs](docs/history/README.md)
- [Contribution and bug-report guidance](CONTRIBUTING.md)

## License

GNU General Public License v3. See [LICENSE](LICENSE). The source package's GPL v3 license replaces the empty repository's previous MIT template at the author's request.
