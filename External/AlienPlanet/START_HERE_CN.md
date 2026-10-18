# Alien Volcanic Planet Scene Pack

Consistent style: dark volcanic rock, faceted shapes, cyan navigation and energy devices, orange lava and dangerous cores, with small amounts of purple for Wave.

This pack contains **27 individual model FBXs + 3 regional layout FBXs + 1 complete map FBX**. The full layout has 93 placed instances, approximately 72,400 triangles, and a route approximately 96 meters long. The models are original stylized prototypes created for this delivery.

## Quick Start

1. Extract the archive first and retain `textures/`.
2. For a ready-made map, use Unreal's **File -> Import Into Level** and select `fbx/scenes/AlienPlanet_FullLayout.fbx`. Scene import preserves object placement; dragging the FBX into the Content Browser primarily imports model assets. Import interfaces may vary slightly across UE versions.
3. Use all models as **Static Meshes** with import scale **1**. Coordinates use centimeters, Z up, and -Y forward. Enable Unreal's scene coordinate conversion options; do not multiply the scale by another 100.
4. For a single area, select `01_Spawn_Layout.fbx`, `02_Training_Layout.fbx`, or `03_Arena_Layout.fbx`. All three retain their world positions from the full map for easy assembly.
5. To create your own layout, use individual models in `fbx/assets/`, placing rocks, plants, cliff walls, and pillars repeatedly.

Official references: [FBX Scene Import](https://dev.epicgames.com/documentation/unreal-engine/fbx-scene-import-in-unreal-engine), [FBX Import Options](https://dev.epicgames.com/documentation/unreal-engine/fbx-import-options-reference-in-unreal-engine).

## Three Areas

| Area | Included Assets | Layout Purpose |
| --- | --- | --- |
| 01 Spacecraft landing zone | Damaged spacecraft, two basalt variants, ground, cyan glowing plants, mission terminal, and lava fissures | Player spawn, AI mission introduction, and movement tutorial |
| 02 Energy canyon | Cyan/orange crystals, modular canyon walls, energy barrier, four Shield/Fire/Wave/Laser targets, small hovering robot, and footbridge | Sequential tutorials for four abilities and small encounters |
| 03 Planetary core ruins | 20-meter circular combat platform with entrance ramp, rock guardian boss, floating planetary core, glowing rune pillars, and charging device | Boss fight and core charging |
| VFX meshes | Orange firebolt, purple wave orb, expanding ring, cyan crystal shards, and teal beam endpoint | Geometry for Niagara Mesh Renderers |

The four combat abilities remain **Shield, Fire, Wave, and Laser**. Cyan crystal shards are environmental/hit visuals; the freezing style in supplementary reference terms does not introduce another combat ability. Charging/repair endpoints visualize the final objective.

## Materials and Emission

All models share `M_Planet_Atlas`. Each FBX embeds four 1024 x 1024 PNGs, also supplied as original images in `textures/`.

| Texture | Unreal Connection | Color Space |
| --- | --- | --- |
| Planet_BaseColor.png | Base Color | sRGB on |
| Planet_Emissive.png | Emissive Color; optionally multiply by 2-4 | sRGB on |
| Planet_Roughness.png | Roughness | sRGB off |
| Planet_Metallic.png | Metallic | sRGB off |

FBX material transfer does not transfer a complete shader. Check all four connections after import, especially Emissive. Reuse one material for consistent scene-wide adjustments. Preview bloom requires Unreal post-processing settings and is not automatically preserved by FBX.

The barrier has an additional `M_EnergyField_Transparent` material slot. Create a Translucent, Unlit, Two Sided material in Unreal, connect cyan `(0.015, 0.21, 0.28)` to Emissive, and start Opacity at `0.20`. Setting opacity to 0 hides the film while retaining the frame and grid lines.

UV0 uses an overlapping shared palette atlas. No dedicated non-overlapping Lightmap UV exists; start with dynamic lighting. For baked static lighting, unwrap a separate Lightmap UV. No LODs were generated or target-device frame rates tested.

## Collision and Gameplay Logic

- Start with the ground and circular platform as static scenery, setting collision for walkable surfaces. The platform has a southern entrance ramp; avoid enclosing the entire platform and ramp in a single convex hull.
- Rocks, spacecraft, and pillars can use simple collision. Canyon walls can use simplified or static complex collision as needed.
- Separate Box Collision and material visibility control barrier opening/closing. FBX contains no barrier logic.
- Plants, glowing lines, crystal shards, and VFX meshes generally need no collision.
- Walk the route with an Unreal character before adjusting capsule size, slopes, collision, and navigation. No Unreal runtime was available for this delivery, so actual traversal was not tested.

The layout's suggested spawn point is Blender coordinate `(0, -850, 120)` cm. Axes may be converted during Unreal import; place PlayerStart near the spacecraft at the southern end. Area centers are in `scene_layout.json`.

**The boss and small robot in this pack are static models without skeletons, walking, attack, or death animations, or AI.** Move/rotate whole Actors for initial prototypes, then rig them or animate separate parts later. Connect tutorial progress, attack hits, boss behavior, and core charging through your existing Blueprint/AI systems.

**FBX does not include Niagara systems, screen HUDs, UMG widgets, or gesture recognition code.** The mission terminal is a physical in-world display. Create actual sparks, trails, refraction, beams, and dynamic particles in UE; see `unreal/NIAGARA_RECIPES.md`.

## Source Files and Directories

- `AlienPlanet_ThreeZones.blend`: complete scene source with packed textures.
- `00_MODULAR_ASSETS_REAL_SCALE`: individual model collection in the source, hidden by default, with all model origins at world zero.
- `01_Spawn` / `02_Training` / `03_Arena`: visible, placed scene instances.
- `99_PREVIEW_LIGHTS_AND_BACKGROUND`: preview lighting and camera, excluded from FBX exports.
- `preview/`: four actual Blender renders rather than concept images.
- `asset_catalog.csv`: individual model catalog, dimensions, and triangle counts.
- `asset_manifest.json` / `scene_layout.json`: model data and placements.
- `validation_report.json`: actual FBX reimport check results.
- `source/`: reproducible model and scene generation scripts. Blender 4.5 / bpy 4.5.14; dependencies: numpy and Pillow. Run `build_planet.py`; set `SKIP_RENDER=1` to generate only models and source files.

All 31 FBXs passed Blender reimport checks for mesh counts, triangles, non-degenerate geometry, UVs, unit dimensions, model origins, embedded textures, and scene positions. Actual import and execution in Unreal Engine have not been performed for this pack.
