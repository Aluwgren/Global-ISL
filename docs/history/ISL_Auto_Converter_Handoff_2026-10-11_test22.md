# ISL Auto Converter / Global Point Light ISL — test22 handoff

Prepared: 2026-10-11
Baseline: Global Point Light ISL 1.5-test21
New build: Global Point Light ISL 1.5-test22
Target: Skyrim SE 1.5.97 / SKSE 2.0.20 / Open Shaders

## Current change

Test22 changes only the default local-light Shadow Depth Bias ceiling from 2.0 to 1.0 so Global ISL follows Open Shaders' own promoted-light rule.

```ini
[ShadowBiasCorrection]
Enabled=1
MaximumBias=1.0
```

Effective rule remains `min(originalBias, MaximumBias)`. Values already at or below 1.0 are untouched. Existing/native local point and spot shadow lights above 1.0 are capped to 1.0. Directional sun/moon shadow bias is not modified.

Open Shaders 2.18 currently assigns `p->depthBias = 1.0f` when it promotes a normal local light to a shadow light. Test22 therefore standardizes existing/native local shadow lights to the same maximum instead of retaining test21's looser 2.0 ceiling.

## Regression state

Runtime bias machinery is unchanged from test21. No new scanner, timer, world traversal, per-frame bias rewrite or renderer path was added. All other test21 behavior remains intact: MaxRadius=0 default, fire RGB 255/140/60, fire authority layer, Deathbell suppression, Conjuration hand handling, 20-unit impact policy, continuous/spray shadow limiter, lifecycle protections, whitelists and forced shadow promotion.

## Testing

The earlier Unslaad plugin edit proved 2.0 removed ~99% of the detached-shadow defect without visible acne. 1.0 is now the new runtime test target because it matches Open Shaders' own promoted-light value. Inspect the same Unslaad windows and also ordinary interior/exterior local shadows for self-shadow acne or shimmer. Lower values below 1.0 remain preserved.

## Audit

- clang-cl /O2 /W4 /WX: PASS
- Clang static analyzer: PASS, zero diagnostics
- x64 PE / KERNEL32-only / no CRT: PASS
- deterministic PE timestamp: PASS
- SKSE exports: PASS
- source archive clean rebuild: byte-for-byte identical
- release package DLL matches audited DLL: PASS

DLL SHA256:
`d6c56da4d8d60c8236a3f412d58b585f99d4d833f0d88ab954bfa66ccbc91c86`
