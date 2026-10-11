# Deathbell mesh audit

Supplied archive: `Deathbell meshes.zip`

| Mesh | NiPointLight / NiLight | BSEffectShaderProperty | EnbParticleLight marker | Notes |
|---|---|---|---|---|
| `arnimadeathbell01.nif` | no | yes | yes | BSLightingShaderProperty, NiBillboardNode, Rudy ENB glow effect nodes |
| `arnimafloradeathbell01.nif` | no | yes | yes | BSLightingShaderProperty, NiBillboardNode, Rudy ENB glow effect nodes |
| `arnimafloradeathbell02.nif` | no | yes | yes | BSLightingShaderProperty, NiBillboardNode, Rudy ENB glow effect nodes |
| `byohhouseingrddeathbell01.nif` | no | no | no | BSLightingShaderProperty |
| `deathbell01.nif` | no | no | no | BSLightingShaderProperty |
| `floradeathbell01.nif` | no | no | no | BSLightingShaderProperty |
| `floradeathbell02.nif` | no | no | no | BSLightingShaderProperty |

Key result: the photographed `TreeFloraDeathBell01/02` models are `floradeathbell01.nif` and `floradeathbell02.nif`; neither contains an embedded literal NiPointLight. The runtime illumination is therefore consistent with an externally/runtime-attached child light or another generated lighting path tied to the reference.

Test18 first fixes inherited `TESObjectREFR` ownership on attached child NiAVObjects. This preserves the requested zero-scan design.