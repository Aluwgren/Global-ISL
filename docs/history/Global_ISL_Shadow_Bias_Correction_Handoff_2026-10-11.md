# Global Point Light ISL — Shadow Depth Bias Correction
## Development handoff for the next test version
**Prepared:** 2026-10-11  
**Status:** Proposed feature; **not yet implemented in Global ISL**  
**Target:** Skyrim Special Edition 1.5.97 / SKSE 2.0.20; Global ISL with Open Shaders

## 1. Executive decision

Add an **optional, configurable maximum Shadow Depth Bias** to Global ISL's **existing light-processing pipeline**. The agreed initial rule is:

```cpp
// Conceptual behaviour; not an engine-specific implementation.
newBias = std::min(originalBias, 2.0f);
```

- Apply to qualifying **local point lights and spotlights**, both **interiors and exteriors**.
- Include **already shadow-casting lights** as well as lights that Global ISL **converts/promotes to shadow casters**.
- **Never raise** a bias below the cap; leave a value of 2.0 or lower **unchanged**.
- Do **not** modify sun/moon/directional-shadow bias systems.
- Reuse Global ISL's **current discovery/processing/caching**, not an additional scanner, timer or standalone DLL.
- Keep independently toggleable in the INI. Start with a fixed cap; **adaptive distance/FOV/falloff calculations are out of scope for v1**.

Proposed INI:

```ini
[ShadowBiasCorrection]
Enabled=true
MaximumBias=2.0
```

Do not change any other Global ISL defaults or previously working light behavior merely to introduce this feature.

## 2. Reproducible discovery and tests

**Location:** Unslaad castle-related interiors using `Lux - Unslaad.esp` (Castle Unslaad, Bell Tower, Chapel of Kyne and Chapel of Bormahu); especially repeated castle-window spotlights.

**Original visible defect:** Actors and followers cast progressively disconnected shadows as they move away from the windows. Feet/lower legs cease to connect with the projected shadow; farther away, much of the silhouette is missing. It appears as if characters are hovering.

**Diagnostic exclusions/observations:**

- Changing the camera position with `tfc` without moving actors did not change the defect.
- Multiple actors/followers showed the same effect; no first-person body mod was involved.
- Toggling Skyrim Performance Solution on/off did not affect the defect.
- This was associated with a repeating *type* of window light across Unslaad interiors, not a single misconfigured cell.
- A different light elsewhere had cast a shadow about 40–50 metres along a corridor and up walls, so Skyrim/Open Shaders is demonstrably capable of very long shadows in at least some configurations. This observation does **not** identify the optimal bias for all lights.
- Light-record investigation in the Lux patch found **20 shadow spotlights** selected for a targeted correction: **7 Castle Unslaad, 9 Bell Tower, 3 Chapel of Kyne, 1 Chapel of Bormahu**. Original reference Shadow Depth Bias values were approximately **19.16–38.89**. The relevant spotlight family includes Lux RuinsSpot near-clip variants; bias should not be confused with spotlight range/FOV.
- At least some affected Lux spotlights **already cast shadows** before Global ISL modification. A bias correction gated strictly behind *ISL conversion success* would miss them.

**Controlled plugin replacement test:** A binary-preserving replacement of `Lux - Unslaad.esp` changed **only the selected 20 placed-light Shadow Depth Bias values**, leaving light placement, radius, colour, FOV, near clip and all other record data unchanged. The user confirmed that the replacement ESP loaded and functioned in-game.

| Tested per-light bias | User's in-game observation |
|---|---|
| Original ~19–39 | Strong detachment; missing lower-body/contact shadows |
| 8.0 | Noticeable improvement, but separation persisted |
| **2.0** | **Approximately 99% of visible detachment removed**, only a tiny residual gap; **no shadow acne or other new artifacts observed** |

**Conclusion supported by the test:** Excessive **placed-light Shadow Depth Bias** is a major cause of this specific Unslaad artifact, and `2.0` is a **visually clean, in-game-tested starting cap** for those lights. The result does **not** prove that `2.0` is optimal for *every* Skyrim light or that all shadow cutoff problems are bias-related.

## 3. Required behavior

1. When an eligible local light is handled by the existing Global ISL light-processing pass, obtain the **effective relevant Shadow Depth Bias** for that specific light.
2. If the optional module is enabled and the value is **finite and greater than `MaximumBias`**, set the effective value to the configured maximum.
3. If the value is **at or below the maximum**, **do not touch it**.
4. Handle **existing shadow casters** whether or not their ISL conversion is required, possible, or specifically excluded. Preserve existing conversion exclusions for every *other* operation.
5. Include **interiors and exteriors by default**, but only local-light depth bias, not directional sun/moon shadow bias.
6. Preserve all other light state: colour, intensity, ISL falloff, radius, FOV, shadow near/far clipping, placement, shadow enablement, budgets and priority.
7. Perform the operation when the light is safely initialized/updated, within the plugin's existing lifecycle protections and queue; **do not mutate during incomplete load/save or while a render traversal is using the light**.
8. Avoid introducing per-frame work or a second light scanner. Reuse existing processed-light caching, but account for reinitialized lights and changes imposed by Open Shaders/other runtime processing. Reapply only when necessary.
9. Do not permanently modify plugin files or the user's saves. Preserve or safely restore the original runtime value when needed, particularly across disabling/reloading and light reuse; do not mistakenly treat a previously clamped value as the light's original value.
10. Logging **off by default**; a temporary diagnostic log may report original bias, applied bias, light FormID/type and cell for verification.

**Important integration boundary:** A simplistic `ApplyISLConversion(); if (!converted) return; CapBias();` sequence is **incorrect**. Eligible existing shadow-casters must reach the bias stage even if the ISL stage skips them. Likewise, exceptions from radius reduction / conversion must not automatically exempt the bias-only stage.

## 4. Engineering investigation before coding

- Audit the **current, actual Global ISL source** and the code path that enumerates lights and forces shadows. Preserve the current known-good version as rollback. Do not replace the scanner or weaken stability protections.
- Verify which engine field carries the **placed-reference** Shadow Depth Bias at runtime and whether that value is copied into a live renderer shadow-light value. Document which field Open Shaders actually reads and whether it can overwrite or cache that field.
- CommonLibSSE exposes fields named `RE::ExtraLightDataStruct::shadowDepthBias` and `RE::BSShadowLight::shadowBiasScale`; **do not assume they are interchangeable, identically scaled or mutable at the same lifecycle stage**. Confirm with source/code and a live test.
- Decide whether the correction must happen before shadow-light creation, after promotion, or during an existing safe refresh. A written value that Open Shaders never consumes does not count as a working implementation.
- Ensure a changed bias causes any needed shadow-state invalidation/refresh **without** forcing unnecessary shadow-map redraws.
- Validate values (finite, sensible non-negative configuration); avoid disturbing unusual or missing light data.

**Conceptual flow only:**

```cpp
void ProcessDiscoveredLight(Light& light)
{
    // Existing Global ISL eligibility/lifecycle logic remains authoritative.
    ProcessExistingISLAndShadowRules(light);

    if (config.shadowBias.enabled && IsLocalLight(light) &&
        HasEffectiveShadowBias(light) && IsFinite(GetEffectiveShadowBias(light))) {
        const float oldBias = GetEffectiveShadowBias(light);
        if (oldBias > config.shadowBias.maximum) {
            ApplyEffectiveShadowBiasSafely(light, config.shadowBias.maximum);
        }
    }
}
```

This pseudocode is deliberately **not** a claim that a simple setter is sufficient; the engine/Open Shaders effective path must be verified.

## 5. Acceptance and regression testing

- **Baseline:** With the new option disabled, shadows, conversions and runtime behavior exactly match the existing Global ISL build.
- **Unslaad:** With the original unmodified Lux patch installed and correction enabled at `2.0`, the castle-window shadows improve comparably to the successful edited-ESP test. Confirm on both player and followers.
- **Already-shadow-casting Lux spots:** Confirm these receive the correction even though they did not need shadow promotion.
- **Newly promoted lights:** Confirm shadow bias correction works on lights that Global ISL converts and enables for shadows.
- **Lower-value preservation:** Bias `0.25`, `1.0`, and `2.0` remain exactly unchanged; `8.0`, `25.0` and `40.0` cap to `2.0`.
- **Exteriors:** Examine torches, lanterns, campfires and spotlights, including long/shallow-angle shadows on terrain, rock, grass and architecture; watch for shadow acne, shimmer and clipping. Exterior effects are **not yet experimentally validated**.
- **Directional sun/moon:** No modification or change in their shadow rendering.
- **Performance:** No new light-discovery pass and no per-frame bias rewrite. Compare FPS, cell-entry stutter, shadow-map redraw activity and stability with the option disabled/enabled.
- **Lifecycle:** Test load/reload, cell transitions, save/load, moving lights, disabled/re-enabled lights and interaction with Open Shaders' shadow scheduler.
- **Backward compatibility:** No regression in existing Global ISL fire colour defaults, radius rules/whitelists, hand/impact light handling, shadow budgets, or other known-good features.
- **Source validation:** Compile for the user's Skyrim SE 1.5.97 / SKSE 2.0.20 toolchain; do not claim tested until a user has actually tested the DLL in-game.

## 6. Scope and explicit non-goals

**In scope:** One INI-toggleable `MaximumBias=2.0` cap for qualifying local lights, within the existing Global ISL scan/processing pipeline, interiors and exteriors, including existing shadow casters and newly enabled casters.

**Out of scope for this iteration:** A separate SKSE DLL; independent light scanning or polling; per-pixel/slope-aware renderer modification; distance-based or falloff-based adaptive bias; blanket disabling of shadows; changing light ranges, clipping, FOV, radius, intensity, colours or shadow budget; sun/moon directional bias adjustments.

Do not advertise the feature as released until implemented and verified. If runtime bias cannot be safely modified at the current lifecycle stage, investigate that limitation explicitly rather than silently falling back to a different field.

## 7. References and handoff artifacts

- Previous diagnostic: `Lux - Unslaad.esp` (user supplied), with master definitions from `Lux.esp` and `Unslaad.esm`.
- User-confirmed test replacements: bias `8.0`, then bias `2.0` on the same 20 Lux placed window spotlights. The latter is the **confirmed in-game working experiment**.
- Microsoft Direct3D depth-bias definition: https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage-depth-bias
- Microsoft shadow-map guide (shadow acne / Peter Panning): https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/DxTechArts/common-techniques-to-improve-shadow-depth-maps.md
- CommonLibSSE member listing: https://po3.commonlib.dev/functions_s.html

**Final agreed design:** `AppliedBias = min(OriginalBias, MaximumBias)` with a default maximum of **2.0**, **enabled and toggleable**, applied to local lights in **interiors and exteriors** through Global ISL's **existing light-processing system**, with lights below the maximum untouched and directional sun/moon shadows excluded.
