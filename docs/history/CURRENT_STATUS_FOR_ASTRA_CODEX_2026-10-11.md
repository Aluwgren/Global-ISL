# Global Point Light ISL — Current review status for Astra / Codex

Prepared: 2026-10-11
Current known-good baseline: **Global Point Light ISL 1.5-test22**
Target: Skyrim SE 1.5.97 / SKSE 2.0.20 / Open Shaders
Public author name: **Aluwgren**

## Important scope boundary

Review **test22** as the current Global ISL baseline.

Do **not** treat the later FaceGen Shadow Softener / test23 experiment as part of Global ISL. Face-specific receiver-side shadow softening was intentionally split into a separate specialized Open Shaders companion project because it crosses into broader shader behavior rather than local-light normalization/management.

## Current in-game validation update

The original test22 source audit says the new global local-light `MaximumBias=1.0` ceiling had not yet been user-tested. That statement is now outdated.

The user has since tested test22 in Unslaad and reported a broad improvement:

- shadows on many actors and objects are now properly attached;
- several problems that had looked unrelated were corrected at once;
- the previous Unslaad detached / floating-shadow behavior is substantially resolved;
- no general torch-shadow regression was noticed;
- the main remaining torch observation is close-range moving self-shadowing on the player, which is considered a separate softness/filtering issue rather than a reason to undo the 1.0 bias ceiling;
- no new broad shadow-acne problem has been reported from the 1.0 cap so far.

Therefore **ShadowBiasCorrection MaximumBias=1.0 is the current preferred baseline**, not 2.0.

## Current default behavior in test22

### Ordinary inverse-square conversion

```ini
[InverseSquare]
MaxRadius=0
```

The broad ordinary radius reducer is disabled by default. Global ISL conversion still runs. Dedicated hand/impact caps remain independent.

### Fire color

```ini
[FireColor]
Red=255
Green=140
Blue=60
PreserveIntensity=1
Authoritative=1
```

Current fire-color baseline is **255 / 140 / 60**. The authoritative layer reasserts classified fire chromaticity across the relevant Open Shaders processing paths.

### Magic hand lights

- enabled;
- dedicated radius: **40 units**;
- hand shadows disabled by policy;
- vanilla Conjuration `MagicLightSummon*` is explicitly included because it does not contain `Hand` in its name.

### Magic impact / hazard lights

- dedicated radius: **20 units**;
- structural HAZD->LIGH handling retained;
- narrow impact-name / exact-form fallbacks retained.

### Shadow promotion

```ini
[Shadows]
ForcePromoteNormalLights=1
```

Global ISL keeps normal-light promotion available through Open Shaders' real-shadow infrastructure rather than manufacturing a separate shadow system.

### Shadow bias correction

```ini
[ShadowBiasCorrection]
Enabled=1
MaximumBias=1.0
```

Policy is one-way only:

`effectiveBias = min(originalBias, 1.0)`

Lower authored values remain untouched. This acts on the local point/spot `ShadowSceneNode::LIGHT_CREATE_PARAMS::depthBias` path and is independent of whether the light needed ISL conversion or promotion. Directional sun/moon shadow bias is outside this feature.

The 1.0 value was chosen because Open Shaders itself assigns `depthBias=1.0` when it promotes a normal light to a shadow light. The earlier controlled Unslaad plugin edit proved 2.0 was already visually clean; runtime test22 at 1.0 then improved a much broader set of shadow-contact problems in game.

### Continuous / spray spell shadows

Event-driven limiter remains present for rapidly spawning dynamic lights (Flames-type sprays, concentration-like streams, dragon-breath-like behavior when it follows the same creation pattern). Ordinary one-shot projectiles should remain outside the limiter unless they meet the rapid repeated-creation threshold.

Defaults:

```ini
[ContinuousSpellShadows]
Enabled=1
BurstWindowMs=250
BurstThreshold=4
ShadowIntervalMs=125
HoldMs=1200
```

Intent: keep all ISL illumination but limit creation of excessive new full shadow casters during continuous streams. No world/cell/projectile scanner is added.

### Deathbell no-light rule

Deathbell remains an event-driven exact-owner suppression rule. It is intended to veto new matching lights and suppress already-live matching `NiLight` instances that enter the existing light-processing path. The architecture is deliberately extensible for future exact-owner offenders without adding a periodic scanner.

## Architecture principles that should be preserved during review

1. **No additional global scanner unless absolutely necessary.** Prefer existing Open Shaders / Global ISL interception points.
2. **Fail closed on unknown Open Shaders binaries/signatures.** Never patch an ambiguous target.
3. **Preserve authored lighting unless a feature explicitly owns that property.**
4. **Do not modify sun/moon directional-light systems.**
5. **Do not merge the FaceGen shadow-softness experiment back into Global ISL.**
6. **Do not re-enable the broad ordinary radius cap by default.** `MaxRadius=0` is intentional.
7. **Do not change fire RGB away from 255/140/60 without a new explicit decision.**
8. **Do not gate shadow-bias correction behind ISL-conversion success.** Existing native/Lux shadow casters must still receive the cap.
9. **Do not use shadow bias as a softness control.** Bias correction is for attachment / Peter-Panning behavior; face/torch softness is a separate filtering problem.

## What Astra / Codex should review

Please audit the actual source rather than redesigning from scratch. Areas worth scrutiny:

- safety and uniqueness of Open Shaders binary/signature resolution;
- relay/trampoline correctness and register preservation;
- thread/lifecycle safety around save/load/cell transitions;
- whether the authoritative fire-color stages can race or double-apply incorrectly;
- exact behavior of the continuous-spell shadow limiter and whether stale pointer/state cleanup is sufficient;
- exact Deathbell ownership traversal and whether false positives are possible;
- whether the local-light bias clamp at `LIGHT_CREATE_PARAMS + 0x14` is applied at the safest/effective stage for existing shadow casters and promoted lights;
- whether any feature can accidentally touch directional sun/moon lighting;
- whether any hot-path work can be reduced without changing behavior;
- opportunities to simplify duplicated resolver/hook machinery while preserving fail-closed behavior;
- compatibility risks moving from Open Shaders 2.17-era development assumptions toward 2.18/current source.

## Verification artifacts included

This review package contains:

- `Global Point Light ISL 1.5-test22.zip` — installable current baseline;
- `Global Point Light ISL 1.5-test22 Source.zip` — full source, INI, README, audit, build scripts and reproducible build artifact;
- `ISL_Auto_Converter_Handoff_2026-10-11_test22.md` — test22 handoff;
- `Global_ISL_Shadow_Bias_Correction_Handoff_2026-10-11.md` — detailed discovery/testing record for the bias feature;
- this current-status note with the post-build in-game validation that occurred after the original audit was written.

## Audited test22 DLL

SHA256:

`d6c56da4d8d60c8236a3f412d58b585f99d4d833f0d88ab954bfa66ccbc91c86`

The included source package previously passed optimized `/O2 /W4 /WX`, Clang static analysis, PE/import/export checks, and a clean byte-for-byte reproducibility rebuild.

## Current conclusion

**Use test22 as the rollback/stable baseline.**

The 1.0 local-light shadow-bias ceiling is now positively user-validated and fixed substantially more shadow-contact problems than the original Unslaad test alone revealed. Future work should preserve this baseline unless a reproducible regression is found.
