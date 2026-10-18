COSMO - Character Animation Delivery Notes
========================================

Contents
CrystalEye: 3 actions. Minion: 6 actions. Boss: 7 actions.
Each character directory includes a SkeletalMesh FBX, separate animation FBXs, a Blender source file with materials/skinning/all Actions, Textures, Skeleton.json, events.json, and separate MP4 previews.
The boss retains its original six-legged rock-armored creature structure. Arm/two-leg actions in the specification were adapted to forelimb attacks and six-legged support, without adding human body parts or equipment.

Timing and Frame Counts
All animations use 30 FPS. An N-frame action exports over 0..N, including N+1 samples, for an exact duration of N/30 seconds. The endpoint sample ensures precise endings and seamless loops.
Use 0..N-1 for looping playback in Blender; frame N is the loop-closing endpoint.
Frame -1 of each Action is a neutral bind reference pose, outside the animation and excluded from export. Do not re-export the entire Action range; use the 0..N range in events.json.
One-shot preview videos add short holds before and after the action for clarity; these holds are not in the animation FBX. Loop videos show two cycles.

Action List (All at 30 FPS)
CrystalEye_Idle          3.0s / 90 frames / loop
CrystalEye_FloatForward  1.0s / 30 frames / loop
CrystalEye_Controlled   1.0s / 30 frames / one-shot
Minion_Idle             2.0s / 60 frames / loop
Minion_Walk             0.8s / 24 frames / loop
Minion_Attack           0.9s / 27 frames / one-shot
Minion_Hit              0.5s / 15 frames / one-shot
Minion_Death            1.2s / 36 frames / one-shot, hold final pose
Minion_Jump             0.9s / 27 frames / one-shot
Boss_Idle               3.0s / 90 frames / loop
Boss_Walk               1.2s / 36 frames / loop
Boss_Attack             1.2s / 36 frames / one-shot
Boss_Hit                0.6s / 18 frames / one-shot
Boss_Death              2.0s / 60 frames / one-shot, hold final pose
Boss_Pounce             1.5s / 45 frames / one-shot
Boss_Jump               1.3s / 39 frames / one-shot

Opening in Blender
1. Open the character directory's .blend file. Textures are packed.
2. Select Armature and switch named Actions in Dope Sheet -> Action Editor.
3. Set the time range to 0..N (0..N-1 for loops) and press Space to play.
4. Viewport overlays are hidden by default for a clearer model view. Enable Overlays to see bones and controllers.
5. Baked actions use FK. Armature custom properties such as IK_LF and IK_RF default to 0. To create a new pose, first unlink the current Action, set the relevant property to 1, and move CTRL_Foot_*. Leg IK stretching is disabled. Controllers are excluded from FBX.
6. Root is a fixed internal reference; body is movable. They intentionally do not require connected endpoints, preventing Blender FBX automatic connection from erasing body translation. The overall hierarchy still has one Root and complete parent-child relationships.

Importing into Unreal Engine 5
1. Import each character's *_SkeletalMesh.fbx first: enable Skeletal Mesh, disable Import Animations, and leave Skeleton empty to create that character's Skeleton. Import scale 1; enable Convert Scene / Convert Scene Unit.
2. Import that character's separate animation FBXs: disable Import Mesh, enable Import Animations, select the same Skeleton created in step 1, set Animation Length to Exported Time, and use sample rate 30.
3. Do not enable Use T0 As Ref Pose or replace the Skeleton bind pose with the first animation frame.
4. All animations are In-Place: disable Root Motion. Do not mix engine-driven displacement and root motion.
5. Models use centimeters, Blender -Y forward, and +Z up. All FBXs for one character use the same coordinate conversion. If MeshComponent orientation needs adjustment, change the component's relative rotation consistently rather than changing import axes for an individual animation.
6. Matching Walk speeds: Minion approximately 80.882 cm/s; Boss approximately 77.381 cm/s. For faster movement, adjust animation playback rate and movement speed together to prevent foot sliding in-game.
7. UE controls Jump world height/trajectory and Pounce forward distance. Additional whole-body jump/pounce trajectories in preview videos are explicitly labeled as presentation-only and are not stored in FBX.
8. FloatForward is also an in-place pose loop. Blend back to Idle through a state machine; start tuning at approximately 0.1-0.2s blend time.
9. Death does not loop. Hold the last frame after playback rather than returning to Idle.
10. Skeleton is included in the SkeletalMesh FBX. Skeleton.json is a readable bone hierarchy description, not an Unreal .uasset. UE creates the Skeleton .uasset during import.

Materials
Original BaseColor / Emissive / Roughness atlases are retained, with original images in Textures.
If UE's automatic material connections are incomplete: BaseColor -> Base Color; Emissive -> Emissive Color (multiply by 2 to approximate source intensity); Roughness -> Roughness, with sRGB disabled for Roughness. Source Metallic is approximately 0.18. UE lighting, exposure, and post-processing still affect brightness.

Attack Events and One-Second Control Synchronization
FBX does not convert Blender markers into Unreal AnimNotify events. The delivery includes pose markers for every Action and events.json. Create same-name Notifies in UE's animation editor and connect them to gameplay logic.
Minion_Attack: frame 11, 0.366667s, Attack.
Boss_Attack: frame 17, 0.566667s, Attack.
Boss_Pounce: frame 17 at 0.566667s, Launch; frame 29 at 0.966667s, Hit_Controlled_1s; frame 33 at 1.1s, Land.
Perform actual collision/hit detection at Boss_Pounce frame 29. Set Controlled and play CrystalEye_Controlled from frame 0 only if the Crystal Eye is hit.
CrystalEye_Controlled ends control at its local 1.0s (frame 30). Use a gameplay state timer for the exact one-second duration, with animation providing visual feedback. Do not end control early when the boss animation finishes.
Synchronization example: Pounce starts at t=0; hit at t=0.966667; Crystal Eye control ends at t=1.966667. The boss's own action ends at t=1.5.
Attack damage, control, collision, VFX, camera shake, and Energy Wave interruption logic are outside FBX. No new weapons or abilities were added.

Validation Scope
See VALIDATION_REPORT.txt, FBX_Validation.json, and Animation_Quality.json.
Validation includes reopening actual Blender source files, reimporting all FBXs, bone hierarchy, bind poses, per-frame skinned position comparisons, animation durations, fixed Root, loop endpoints, non-stretching rigid parts, ground height, and foot contact.
Unreal Editor was unavailable in the delivery environment, so UE editor import and gameplay tests were not performed. Blender reimport validation does not mean integration in your UE project has passed.
FBXs are actual Blender binary FBX 7.4 (7400). Epic documents the UE FBX pipeline as based on 2020.2. This delivery passed independent Blender reimport checks; final acceptance must still take place in the project.

Official Import References
https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-animation-pipeline-in-unreal-engine
https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-import-options-reference-in-unreal-engine
https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AnimationLibrary?application_version=5.6
