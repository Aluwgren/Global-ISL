# Global Point Light ISL 1.5-test22 audit

## Objective

Carry the test21 Shadow Depth Bias correction forward while changing only the default ceiling from `2.0` to `1.0`, matching Open Shaders' own promoted-light depth-bias value. No second scanner is added and established light behavior remains otherwise unchanged.

Default policy:

```ini
[ShadowBiasCorrection]
Enabled=1
MaximumBias=1.0
```

Conceptual result: `effective = min(original, MaximumBias)` for finite local-light biases, with values already at/below the cap untouched.

## Runtime-field investigation

The implementation deliberately does **not** write `TESObjectLIGH`, `ExtraLightDataStruct::shadowDepthBias`, save data, or `BSShadowLight::shadowBiasScale`.

Current CommonLibSSE-NG defines `ShadowSceneNode::LIGHT_CREATE_PARAMS::depthBias` at offset `0x14`. Light Placer independently populates `params.depthBias = shadowDepthBias` before calling `ShadowSceneNode::AddLight`, confirming that this structure is the creation-time path used for a local shadow light's depth bias.

Open Shaders' current `Hook_ConvertLights_Add` runs at the start of `ShadowSceneNode::AddLight` with:

- `RCX` = ShadowSceneNode;
- `RDX` = NiLight;
- `R8` = LIGHT_CREATE_PARAMS*.

Its normal-light promotion path writes `p->depthBias = 1.0f`. The test20 binary signature already resolves the compiled equivalent and verifies that the compiler keeps the NiLight in `RSI` and params in `RDI` around the promotion gate. Test22 reuses that exact validated interception rather than inventing a new renderer hook.

## Implementation

The former test20 continuous-shadow relay is generalized to one `process_shadow_add_policy(NiLight*, LIGHT_CREATE_PARAMS*)` call.

Bias stage:

1. read only the float at `params + 0x14`;
2. reject NaN/Inf using an IEEE-754 exponent check;
3. if and only if the finite value is greater than `MaximumBias`, write the maximum;
4. otherwise leave the bytes untouched.

The bias stage runs before the normal/already-shadow branch, so native shadow casters are included. It is not gated by ISL conversion success, the ordinary radius cap, whitelist membership, or the continuous-spell classifier.

The operation occurs only when the engine/Open Shaders is already processing `AddLight`; no stored scene pointer, timer, cell scan, per-frame callback or second light traversal was added.

## Open Shaders promotion interaction

Default `MaximumBias=1.0` needs no modification to Open Shaders' promoted-light constant because Open Shaders already assigns exactly `1.0`.

For configured maxima below `1.0`, test22 searches only inside the validated promotion block for the unique machine instruction encoding `mov dword ptr [rdi+14h], 1.0f` and rewrites only its immediate float. No module-wide pattern is used. If it cannot be resolved uniquely, shadow-bias correction disables itself for safety.

The native `PromoteNormalToShadow` setting address is resolved from the exact RIP-relative CMP replaced by the relay. This allows the shared relay to preserve Open Shaders' native setting when Global ISL `ForcePromoteNormalLights=0`.

## Directional-light boundary

Open Shaders documents the kSHADOWMAPS / Shadow Caster Manager path as point/spot-light handling, with sun directional shadows using separate shadow-map state. Test22 hooks the local `ShadowSceneNode::AddLight` / NiLight promotion path and does not patch directional shadow code.

## Regression audit

Direct test21 -> test22 source diff was reviewed. Intentional changes are restricted to version identifiers, the default `MaximumBias` value and documentation describing the new 1.0 policy. Runtime bias machinery itself is unchanged from test21. The underlying test20 -> test21 feature additions remain:

- version identifiers;
- two bias settings;
- finite-float helper;
- ModuleInfo image bounds used only to validate the native promotion-setting pointer;
- shared shadow-add relay/policy replacing the old continuous-only relay;
- optional sub-1.0 promoted-bias immediate handling;
- INI/docs.

Unchanged behavior/code paths include:

- inverse-square attenuation and ordinary `MaxRadius=0` default;
- environmental whitelist / exact exceptions;
- hand radius/shadow policy, including Conjuration test20 fix;
- impact/hazard 20-unit policy;
- CreatePointLight dynamic-stream bookkeeping;
- Deathbell exact-owner and parent-chain suppression;
- ProcessLight lifecycle admission and save/load settling;
- fire classification and RGB `255,140,60`;
- authoritative fire reassertion at ProcessLight / Effects11 / final push;
- final LightData radius/color/shadow handling.

The continuous-spell limiter remains active only as part of Global ISL forced promotion, matching test20 semantics. Enabling bias correction while forced promotion is off does not newly apply the continuous limiter to Open Shaders' native promotion setting.

## Logic checks

For default `MaximumBias=1.0`:

- `0.25 -> 0.25` PASS
- `1.0 -> 1.0` PASS
- `2.0 -> 1.0` PASS
- `8.0 -> 1.0` PASS
- `25.0 -> 1.0` PASS
- `40.0 -> 1.0` PASS
- negative finite values remain unchanged PASS
- NaN/Inf remain untouched PASS

The cap changes no adjacent LIGHT_CREATE_PARAMS fields; offset `0x14` alone is written.

## Build / static checks

- optimized `clang-cl /O2`: PASS;
- `/W4 /WX`: PASS;
- Clang static analyzer: PASS, zero diagnostics;
- PE verification: PASS;
- x64 PE32+ DLL: PASS;
- imports: KERNEL32.dll only;
- no CRT import: PASS;
- deterministic PE timestamp: zero;
- exports: `SKSEPlugin_Load`, `SKSEPlugin_Query`;
- no new scanner/background worker/per-frame bias writer: source diff verified.

## In-game status

The earlier controlled Lux - Unslaad ESP test established that reducing the problematic ~19-39 biases to `2.0` removed approximately 99% of the detached actor-shadow defect without visible acne. Test22 deliberately goes one step lower to Open Shaders' own `1.0` promoted-light value. **The 1.0 global local-light ceiling has not yet been user-tested in game.** Acceptance testing should therefore focus both on attachment improvement and on watching for new self-shadow acne/shimmer.

## Function-level regression comparison

A brace-aware test21/test22 source comparison should report all runtime functions byte-for-byte source-identical except version/default-loading text; the shadow-bias algorithm itself is unchanged. The following stability-critical functions remain unchanged:

- `runtime_instance_admitted`
- `normalize_point_light_raw`
- `apply_consistent_fire_color`
- `universal_point_light_push_hook`
- `universal_strict_process_light_hook`
- `universal_create_point_light_hook`
- `should_skip_continuous_shadow_promotion`

The continuous classifier itself is unchanged; only the relay that calls it now also receives `LIGHT_CREATE_PARAMS*` so the independent bias cap can run before shadow-type branching.

## Audited DLL

SHA256: `d6c56da4d8d60c8236a3f412d58b585f99d4d833f0d88ab954bfa66ccbc91c86`

## Reproducibility

A clean extraction of the final source archive was rebuilt with the included `scripts/build.sh`; the resulting DLL was byte-for-byte identical to the audited release DLL and produced the SHA256 above.
