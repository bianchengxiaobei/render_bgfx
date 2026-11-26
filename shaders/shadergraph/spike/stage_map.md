# SG-0 stage map

SG-0 uses the existing forward lighting includes without remapping.

| stage | owner | binding |
|---|---|---|
| 0 | graph | `s_sg_MainTex` (`_MainTex`) |
| 1 | graph probe | `s_sg_Dummy1` |
| 2 | graph probe | `s_sg_Dummy2` |
| 3 | graph probe | `s_sg_Dummy3` |
| 4 | graph probe | `s_sg_Dummy4` |
| 5 | engine | `s_lightData` |
| 6 | engine | `s_shadowMap` |
| 7 | engine | `s_lightList` |
| 8 | engine | `s_shadowContext` storage buffer |
| 9 | engine | `s_shadowMapPunctual` |
| 10 | engine | `s_environmentMap` |
| 11 | engine | `s_contactShadow` |
| 12 | engine | point/IMS SAT shadow (not declared in SG-0) |
| 13 | engine | point/area depth (not declared in SG-0) |
| 14 | engine | `s_probeMaps` |
| 15 | engine | `s_cookieAtlas` |

The graph template omits `material/surface_data.sc`; therefore stages 0-4 are free
for graph textures. SG-0 samples five textures (one real checker plus four 1x1
probes) while forward directional lighting, contact shadow, and environment
samplers stay bound. The default graph budget remains 5.
