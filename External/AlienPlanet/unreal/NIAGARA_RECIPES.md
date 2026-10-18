# Niagara Integration Suggestions

These are visual design starting points for the project, not compiled Niagara Systems. FBX supplies static meshes only. Adjust values for your movement speed, camera, exposure, and performance budget.

Niagara's Mesh Renderer renders static models, Ribbon Renderer handles trails, and Sprite Renderer handles billboard particles such as sparks. Enable the corresponding Niagara usage flags on materials.

Official reference: [Niagara Renderers](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine).

## Four Combat Abilities

| Ability | Reused Model / Object | Suggested Effect | Starting Values (Design Suggestions) |
| --- | --- | --- | --- |
| Shield | Existing shield + FXM_CrystalShard | Brief hit flash, a few cyan shards, and edge ripples | 8-16 shards; 0.15-0.4-second lifetime |
| Fire | FXM_FireBolt | Orange glowing body + Ribbon trail + a few ember Sprites | Trail lifetime 0.12-0.25 seconds; spark lifetime 0.15-0.45 seconds |
| Wave | FXM_WaveOrb + FXM_WaveRing | Purple orb, expanding rings, and brief distortion; separate material for refraction | Rings last 0.4-0.8 seconds; radius approximately 0.5 to 3 meters |
| Laser | FXM_TealBeamEndpoint | Teal beam, bright narrow core, and particles converging at the endpoint | Endpoint diameter approximately 0.1-0.4 meters; hit flash approximately 0.1 seconds |

`FXM_FireBolt` is modeled facing -Y. If Niagara uses another axis for velocity alignment, add a rotation in Mesh Renderer or Mesh Orientation so the projectile points forward.

`FXM_WaveRing` has an original radius of 1 meter; scaling can create expansion directly. Avoid multiplying each ring's triangle count by thousands of particles; only a few rings are needed for the prototype.

`FXM_CrystalShard` provides cyan environmental/shield-hit shards, without adding an Ice/Freeze ability. Crystal colors from freezing reference images can serve as visual references.

## Planetary Core Charging / Repair

- Start point: player or charging device. End point: the glowing core position in the StarCore object.
- The model's core center is approximately 175 cm above its origin; the device's top endpoint is approximately 210 cm above its origin.
- Connect the endpoints with a thin teal beam and overlay `FXM_TealBeamEndpoint` at the end.
- Converge particles on the endpoint, increasing density or brightness as charge rises.
- Progress must come from actual game state; elapsed particle playback time must not directly complete the objective.

## Environmental Effects

- Lava fissures: place sparse orange embers along `LavaFissure_8m`, drifting slowly upward.
- Crystals: add only a few nearby, slowly rising glimmers; atlas emission already provides the main visual identity.
- Boss: the orange heart is a visual weak point and flashes locally when hit; add a few sparks along cracks if desired.
- Energy barrier: animate material opacity/UV scanning, with collision enabled/disabled independently.

## HUD Style Integration

Create the screen HUD and gesture tracking display in UMG; they are outside the FBX models.

- Use cyan as the primary color and amber for warnings, with short labels for the current ability and tracking status.
- Keep the four abilities Shield / Fire / Wave / Laser consistent with the training target symbols.
- Display "Recognizing" when gesture confidence is insufficient rather than directly triggering an ability.
- Use an in-world display for the mission terminal. AI/mission state drives the specific tutorial text.
- The assets do not prescribe specific Fire, Wave, or Laser gestures; follow the definitions from your gesture recognition teammate.
