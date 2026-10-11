# Global Point Light ISL 1.5-test22

Target: Skyrim SE 1.5.97 / SKSE 2.0.20. Built from the audited test21 branch.

Test22 keeps test21's configurable **maximum local-light Shadow Depth Bias** correction, but changes the default ceiling from `2.0` to `1.0` so Global ISL follows Open Shaders' own promoted-light depth-bias value. The cap remains one-way only: lower authored values are preserved. All test21 ISL, fire, Deathbell, spell-hand/impact, lifecycle and continuous-spray behavior is retained.

## Shadow Depth Bias correction

```ini
[ShadowBiasCorrection]
Enabled=1
MaximumBias=1.0
```

Effective rule:

```text
if finite(originalBias) and originalBias > MaximumBias:
    effectiveBias = MaximumBias
else:
    effectiveBias = originalBias
```

With the default maximum, `0.25` and `1.0` remain untouched; `2.0`, `8.0`, `25.0` and `40.0` become `1.0`.

The correction is applied to Skyrim's transient `ShadowSceneNode::LIGHT_CREATE_PARAMS::depthBias` at the existing Open Shaders `ShadowSceneNode::AddLight` processing point. This is intentionally not a TESObjectLIGH/plugin-record rewrite and not a `BSShadowLight::shadowBiasScale` guess.

Consequences:

- native/already-shadow-casting local lights are included even when Global ISL does not need to convert/promote them;
- newly promoted normal lights are included in the same shadow-add pipeline;
- point and spot lights in interiors and exteriors use the same cap;
- lower bias values are never raised;
- sun/moon directional shadow bias is not touched;
- radius, FOV, near distance, falloff, color, intensity, position, shadow enablement and shadow budgets are not changed by this feature;
- no world scan, second light scanner, timer or per-frame bias writer is added;
- no plugin file or save data is modified.

Open Shaders itself assigns `depthBias=1.0` when it promotes a normal light to a shadow light. Test22 deliberately adopts that same `1.0` value as Global ISL's default maximum. For deliberately configured maxima below `1.0`, test22 additionally resolves that one immediate assignment inside the already-validated promotion block and lowers it to the configured maximum. If that optional sub-1.0 assignment cannot be resolved uniquely, the bias correction disables itself rather than claiming a partial configurable result.

## Why this is independent of ISL conversion

The Unslaad defect was reproduced on Lux spotlights that already cast shadows. Therefore the bias correction is deliberately not structured as:

```text
ConvertToISL();
if (!converted) return;
CapBias();
```

Instead it shares the existing Open Shaders shadow-add interception. An already-shadow-casting spotlight therefore reaches the bias cap even if no ISL conversion or shadow promotion is necessary.

## Interaction with existing shadow promotion / concentration limiter

Test20 introduced the shared Open Shaders normal-light promotion hook used for forced shadows and concentration/spray rate limiting; test21 consolidated bias correction into that same relay. Test22 leaves that runtime machinery unchanged and only adopts `1.0` as the default maximum.

Order inside the relay:

1. cap only `LIGHT_CREATE_PARAMS.depthBias` when enabled and above the maximum;
2. preserve Global ISL's existing `ForcePromoteNormalLights` behavior;
3. preserve the test19/test20 continuous-spell promotion limiter exactly when Global ISL forced promotion is enabled.

If `ShadowBiasCorrection=0`, the shadow-promotion/continuous behavior remains test20-equivalent. If `ForcePromoteNormalLights=0`, the native Open Shaders `PromoteNormalToShadow` setting remains authoritative; adding bias correction does not silently enable the continuous limiter on that native path.

## Unslaad acceptance test

Use the original, unedited `Lux - Unslaad.esp` and the same Castle Unslaad / Bell Tower / chapel window spotlights used for the plugin-edit experiment.

Expected result with `MaximumBias=1.0`:

- actor/follower shadows should remain much better connected to feet/lower legs at distance, comparable to the successful edited-ESP test;
- the affected Lux spots should be corrected even though some are already shadow casters;
- no new shadow acne was seen in the earlier direct ESP test, but exterior/general-light behavior still needs broader in-game validation.

For a clean comparison, restart Skyrim or leave/re-enter the relevant cell so the shadow lights are rebuilt through `AddLight`.

## Retained test20 defaults

- ordinary `[InverseSquare] MaxRadius=0` (broad radius reducer disabled);
- fire RGB `255,140,60`;
- Conjuration `MagicLightSummon*` uses the dedicated 40-unit hand policy;
- impact/hazard lights use the 20-unit impact cap;
- magic hand shadows remain disabled by their dedicated policy;
- continuous/spray shadow limiter defaults remain `250 / 4 / 125 / 1200 ms`;
- Deathbell exact-owner no-light suppression remains enabled;
- authoritative fire color remains active across ProcessLight / Effects11 / clustered submission;
- lifecycle/save/load protections are unchanged.

## Compatibility / fail-closed behavior

The shadow-bias feature reuses the same semantic Open Shaders promotion-gate signature and register-contract validation already required by test20's forced-promotion/concentration path. The current Open Shaders 2.18 source still performs local-light promotion in `Hook_ConvertLights_Add` and writes `p->depthBias = 1.0f` there. The runtime binary is still validated structurally at startup; if the expected compiler contract is not recognized, the optional shadow-add policy is not applied rather than patching unknown code.
