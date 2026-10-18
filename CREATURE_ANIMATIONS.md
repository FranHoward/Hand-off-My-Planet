# Animation Blueprints for Three Creatures

UE 5.7. Three independent skeletons, 16 animations, materials, and textures have been imported into `Content/Creatures`. Each ABP includes an editable `CreatureStateMachine`, Sequence Players, and transition rules. Their shared parent class, `CosmoCreatureAnimInstance`, reads movement and maintains action priorities.

| Creature | Animation Blueprint | States |
| --- | --- | --- |
| Crystal Eye | `/Game/Creatures/CrystalEye/ABP_CrystalEye` | Idle, Move (FloatForward), Controlled |
| Minion | `/Game/Creatures/Minion/ABP_Minion` | Idle, Move (Walk), Attack, Hit, Jump, Death |
| Boss | `/Game/Creatures/Boss/ABP_Boss` | Idle, Move (Walk), Attack, Hit, Jump, Pounce, Death |

Each directory contains a `BP_<Creature>_AnimationPreview`. Drag it into a level to observe Idle. Preview Actors have the corresponding mesh, materials, and ABP assigned, with no collision or enemy logic. Previews show the source assets' appearance; gameplay characters define actual size, collision, and orientation.

## Connect to a Character

1. Assign the same directory's `SK_<Creature>` to the Skeletal Mesh component. Set Animation Mode to Use Animation Blueprint and Anim Class to the corresponding ABP.
2. `Get Anim Instance` -> Cast to `CosmoCreatureAnimInstance`. By default, movement reads the owner's planar speed. Characters also read `IsFalling` and automatically enter Jump.
3. Call `RequestAttack`, `RequestHit`, `RequestJump`, and `RequestPounce` for attack, hit reaction, voluntary jump, and boss pounce. A true return value means the request was accepted. Unsupported actions return false. Crystal Eye has no attack, hit, or death animations and does not borrow another creature's skeleton.
4. Call `SetDead` when a minion or boss dies. Normal actions cannot interrupt death; its non-looping animation holds the final frame. To respawn, restore character health and the death flag first, then call `ResetForRespawn`. If the owner has a numeric `Health` or boolean `bDead`, death conditions are also read automatically.
5. Bind the instance's `OnAnimationEvent(EventName)` to the collision windows, footsteps, and effects below. Gameplay Actors / AI should handle damage, displacement, actual hit detection, and enemy decisions. Animation notifications emit events without applying damage directly.

Movement uses hysteresis thresholds of 5 cm/s to enter and 2 cm/s to exit, preventing idle jitter. Move playback rate is speed / `ReferenceMoveSpeed`, clamped to 0.1-4. Reference speeds are 80.882 cm/s for minions and 77.381 cm/s for the boss. Crystal Eye currently defaults to 80.882 cm/s; adjust it in ABP Class Defaults for the actual flight speed. Tests or custom movement on non-Characters can use `SetManualMovement(Speed, IsInAir)` and return to owner-driven movement with `ResumeOwnerMovement`.

Action priority is Death > Controlled > Hit > Pounce > Jump > Attack > movement/idle. Higher-priority actions can interrupt lower-priority actions. Attack/Pounce reject repeated per-frame triggers; repeated Hit restarts the hit reaction. One-shot actions return to movement/idle when finished. Jump holds its final frame while airborne and resumes after landing.

## Notifications and Crystal Eye Control

All notifications from the source `events.json` files are on the animations' `COSMO Events` track at their original 30 fps timing. Root motion is disabled; gameplay handles world displacement for jumps and pounces.

| Animation | Notification Times (Seconds) |
| --- | --- |
| CrystalEye Controlled | ControlBegin 0; ControlEnd 1.0 |
| Minion Walk | Footstep_LF 0; Footstep_LB 0.2; Footstep_RF 0.4; Footstep_RB 0.6 |
| Minion Attack | Attack 11/30 (0.366667) |
| Minion Jump | TakeOff 0.2; Land 20/30 (0.666667) |
| Boss Walk | Footstep_LF / RM / LB 0; Footstep_RF / LM / RB 0.6 |
| Boss Attack | Attack 17/30 (0.566667) |
| Boss Pounce | Launch 17/30; Hit_Controlled_1s 29/30; Land 1.1 |
| Boss Jump | TakeOff 0.3; Land 0.9 |

When the boss receives `Hit_Controlled_1s`, gameplay performs collision or area detection and calls `ApplyControlled(1.0)` on the target Crystal Eye's animation instance **only after confirming a hit**. Playing Pounce alone does not confirm a hit. Crystal Eye's control timer is independent of the boss. For example, a hit at 0.966667 seconds keeps control active until 1.966667 seconds, even though the boss animation ends at 1.5 seconds. Another confirmed control hit restarts the full duration from that call.

`ControlBegin` fires when a control request is accepted; an independent timer emits `ControlEnd`. Same-name notifications at the animation endpoints are markers and do not duplicate lifecycle events. Old attack notifications interrupted by hit reactions or death are filtered out. Each same-name gameplay event fires at most once per one-shot action; footsteps fire per movement cycle.

In the gameplay character, connect `ControlBegin` / `ControlEnd` to disabling and restoring movement and input. The ABP manages poses and timing events; the gameplay character enforces actual movement restrictions.

## Regeneration and Verification

All three ABPs/preview Blueprints compiled with zero errors and warnings. All 128 UE checks of actual skeletal playback passed, covering skeletons and durations, material slots, notification frames, movement rates, hit interruptions, attack event deduplication, jump landing, pounce events, one-second control and refresh, final death frames, and respawning.

```powershell
# By default, only build the plugin and verify existing assets, preserving manual ABP edits.
.\Scripts\VerifyCreatureAnimations.ps1

# Explicitly regenerate state machines, preview Actors, and COSMO notification tracks, then verify.
.\Scripts\VerifyCreatureAnimations.ps1 -RebuildAssets

# Import missing source FBX/textures, then regenerate and verify.
.\Scripts\VerifyCreatureAnimations.ps1 -ImportSources
```

Use `-SkipBuild` to reuse the current plugin build and `-EngineRoot` to specify the engine path. Source packs are in `External/CreatureAnimations`; archive hashes are in `source_manifest.json`. Import scripts preserve existing imported assets. After changing source content, explicitly reimport it in the editor before regenerating.

Results: `Saved/CreatureAnimationVerification.json`; full log: `Saved/Logs/CreatureAnimation_Verify.log`. Offscreen previews: `Saved/Screenshots/Creatures_Idle.png`, `Creatures_Move.png`, and `Creatures_Actions.png`.

Reproduce the screenshots with `UnrealEditor.exe VFX.uproject "/Engine/Maps/Entry?game=/Script/Engine.GameModeBase" -game -CreatureAnimationPreview -RenderOffscreen -windowed -ResX=1600 -ResY=900 -ForceRes -NoSound -unattended -dx11`. This preview is enabled only in editor builds, creates a temporary scene, exits automatically, and does not change game maps.
