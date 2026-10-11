# Review for 1.5-test22.1

Reviewed the test22 runtime source, configuration, hook installation, lifecycle state, lighting policies, and release tooling. This is a focused maintenance update, not a replacement lighting system. The source uses the same renderer signatures, Skyrim offsets, and lighting defaults as test22.

## Changes made

- **Hook rollback ownership.** A failed shadow-bias installation could free a relay even when restoring its callsite failed. Effects11 rollback could similarly clear the original-function pointer or free a relay still referenced by a callsite. Restoration now verifies the original bytes at every affected site, including a failed patch attempt. Any surviving hook retains its target and forwarding function. Complete rollback still frees the relay.
- **Continuous-light identity.** Spray-light state now resets on load/new-game generation changes. Every successful creation updates its pointer association, including ordinary creations that invalidate an old spray tag. Nested ordinary creations and stack overflow cannot inherit an outer spell's classification. Contention that prevents invalidation clears the instance cache at the next safe lock acquisition; untracked concurrent creation fails open toward preserving a shadow.
- **Load-time detached fire association.** Metadata-only reference-to-light association now remains available during save/load settling. The creation's generation travels with the association, so a result from an old scene is not tagged as a new-scene light. Reuse without an eligible fire reference clears the old marker.
- **Particle color batch lifetime.** Transition cleanup now discards the previous batch's fire anchors together with pending light policy, preventing old positions from influencing a later batch.
- **Conservative placement scanning.** Detached pairing requires a complete, successfully parsed definition pass before creating associations.
- **Distribution.** The installable ZIP uses `SKSE/Plugins/` at its root for MO2/Vortex. Release automation publishes the matching source and checksums after validation.

## Fire hazards

The author's clarified intent is preserved: fire-associated hazards use the same fire chromaticity as other fire sources. A HAZD link is not a blanket reason to suppress fire recoloring. The existing warm-color, source-association, and configured classification rules remain in place. Tests cover both LIGH/HAZD group orders, a cold hazard control, and the actual ProcessLight path applying the standard fire color while retaining the independent 20-unit impact-radius policy.

The INI is byte-for-byte unchanged. In particular, MaximumBias remains 1.0, ordinary MaxRadius remains 0, fire RGB remains 255/140/60, and the hand/impact caps remain 40/20.

## Evidence and limits

The tests compile and call the actual plugin source, using Win32 stubs, synthetic renderer code buffers, and a small in-memory plugin-file fixture. They exercise successful and failed installations, denied rollbacks, scene generations, nested creation, pointer reuse, cache contention, bias edge cases, classification, and installation layout. The original test22 source fails the three hook-rollback regression cases that motivated the patch.

These tests do not execute the generated x64 renderer relays or prove live game-thread/renderer-thread behavior. Compilation, static analysis, PE validation, deterministic rebuilds, and ZIP extraction checks are additional evidence, not substitutes for Skyrim testing. See [VALIDATION.md](VALIDATION.md).

## Remaining game-test and review targets

- Confirm supported Open Shaders binaries still resolve all intended hooks; source inspection alone does not establish current binary compatibility.
- Exercise loading, repeated saves, cell changes, nested/concurrent spell creation, long continuous casts, and light removal/reuse in the game.
- Check Unslaad shadow contact, torch shadows, ordinary fire, fire hazards, cold magic, and directional sun/moon behavior.
- Profile long sessions and fixed-capacity admission/detached caches. Admission slots are not reclaimed within a scene generation; saturation and long probe chains remain follow-up work requiring a careful lifetime/reclamation design.

For these reasons this version is published as a **prerelease**. The original test22 in-game reports apply to that historical build, not this new DLL.
