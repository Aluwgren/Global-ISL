# Contributing

Use the existing checkout and make focused changes against the test22 baseline. See the [README](README.md) for the build commands and supported runtime.

Before opening a pull request:

1. Run `./scripts/check.sh` for native regressions, static analysis, the Windows cross-build, PE checks, packaging, and reproducibility.
2. Describe the behavior changed, the reason, and the checks performed.
3. Clearly separate build/static checks from in-game observations. Include the Skyrim, SKSE, and Open Shaders versions for runtime tests.
4. Keep generated DLLs, object files, ZIPs, and logs out of commits.

For bug reports, include the mod version, relevant INI settings, reproduction steps, expected and actual behavior, and any useful log excerpt. For visual defects, describe the cell/location, light type, and other lighting mods involved. Remove personal paths or unrelated information before sharing logs.

The historical handoffs document prior investigation and test reports. They are reference material, not a list of changes required for every contribution. Preserve runtime behavior and defaults unless the change explicitly intends to alter them.
