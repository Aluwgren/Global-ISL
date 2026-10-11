# Validation for 1.5-test22.1

Checked on October 11, 2026 with Debian LLVM **19.1.7**. Run `./scripts/check.sh` with `CLANG_CL` and `LLD_LINK` configured as shown in the README.

| Check | Result |
| --- | --- |
| Actual-source native regression cases | **22 passed**, including full hook installers and in-memory plugin parsing. |
| AddressSanitizer and UndefinedBehaviorSanitizer | All 22 cases passed with no diagnostics. |
| Original-source rollback regression comparison | Original test22 fails all three targeted rollback cases; the patched source passes. |
| Windows x64 cross-build | Passed with C++17, `/O2 /W4 /WX`. |
| LLVM static analysis | Completed with zero diagnostics. |
| PE contract | Passed: AMD64 DLL, zero timestamp, KERNEL32-only imports, Windows subsystem, two legacy SKSE exports. |
| Repeated clean build and packaging | DLL and ZIP byte-for-byte identical. |
| Mod-manager installation simulation | ZIP extracted into a temporary Data directory; exact DLL/INI/license payloads, correct SKSE/Plugins path, no nested Data folder. |
| Package integrity and checksum | Passed. |
| INI, kernel32.def, GPL license | Unchanged from the imported test22 files. |
| Skyrim runtime validation | **Not performed**. This version is a prerelease. |

Fire-associated hazards retain standard fire chromaticity while their impact-radius policy remains independent. Tests cover both plugin group orders and a cold-hazard control. See [the review](REVIEW.md) for the fixes and outstanding game-test targets.

## Build identities

Local LLVM 19.1.7 DLL SHA-256:

```text
19460ea133ebfaa6806ab6a40e8e2a8cfb5ae71051c4be8a7f6fc3ea92a153be
```

Installable ZIP SHA-256:

```text
84c451474d3d93b980c7708bafbf36c412dc4d5d1a0efa077243352d3790a9c7
```

The release workflow repeats the checks before publishing assets. Verify downloaded assets against their accompanying checksum files. This document records local checks; publication and hosted workflow outcomes are verified separately when the release is created.

## Scope of the tests

The native harness includes the actual C++ source with simulated Win32 services. It injects protection failures, inspects synthetic patched code and allocation ownership, drives lifecycle state, and parses a generated binary plugin fixture. It never executes renderer machine code and is not a test of Windows ABI behavior or the game's rendering/thread scheduling.

The original import validation is preserved in [history/test22-import-validation.md](history/test22-import-validation.md). Earlier reported in-game results belong to the original test22 DLL, not this new build.
