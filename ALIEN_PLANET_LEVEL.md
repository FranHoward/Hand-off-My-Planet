> The input system now uses gesture and face controls exclusively. The old key bindings, text chat, and fallback input instructions in this document are obsolete. See [GESTURE_ONLY.md](GESTURE_ONLY.md) for current gestures and startup instructions.

# Alien Planet: Normal-Difficulty Level

The default game and editor map is `/Game/AlienPlanet/Lvl_AlienPlanet`. Open the project and click Play to enter with the completed `BP_FirstPersonCharacterAI`. Keyboard/mouse and gesture inputs share the actual combat rules. Level objectives, training, combat, death retries, and completion remain available when the COSMO service is offline.

The scene pack's 27 individual models and four atlases have been imported into `Content/AlienPlanet`. The layout was restored from the source `scene_layout.json` and adjusted for traversal and combat: the spacecraft was moved left to keep the central route clear; the static boss placeholder was replaced with an animated boss bound to an ABP; platform ramps retain their actual geometry collision; rock walls block the route edges; and plants and glowing decorations do not block movement. Lava is an environmental visual beside the route, with no added contact damage in the beginner area.

## Route and Encounters

The map is approximately 96 meters long, progressing through the spawn area, energy canyon, and planetary core ruins. Map coordinates are in centimeters, with progress along Unreal's -Y axis.

| Area | Layout and Rules |
| --- | --- |
| Spacecraft landing zone | Safe spawn, cyan navigation devices, and no active enemies. The spacecraft does not block the central passage. The entrance barrier opens automatically as you approach the canyon. |
| Energy canyon | Four training targets for Shield / Fire / Wave / Laser and the first checkpoint. Training requires actually blocking a pulse, hitting with a fireball, dealing area damage, and maintaining a beam hit, respectively. Incorrect abilities do not complete training. Pulses have warnings, deal at most 4 damage each, and cannot kill the player. |
| First canyon encounter | Learning Fire and advancing activates 1 minion near the Wave tutorial. Wave can interrupt it. |
| Second canyon encounter | After all four training tasks and the first minion are complete, approaching the exit activates 2 more minions. The exit barrier opens only after all three canyon minions are defeated. A Crystal Eye scan target and supplies are nearby. |
| Pre-boss preparation | Second checkpoint and supplies. Retrying here preserves the three canyon kills and completed ability training. |
| Planetary core arena | Activates 2 guards first. Defeating them and approaching the arena activates the boss. Guards are positioned clear of the charging pillars. No reinforcements spawn during the boss fight. The circular arena leaves room for sidestepping and circling, with one supply pickup on each side. |
| Finale | After the boss dies, approach the central planetary core for 3 seconds to finish charging it and win. Restart with T or the button on the results screen. |

There are 5 minions, 1 boss, 1 non-hostile Crystal Eye scan target, 5 health pickups, and 2 checkpoints. Enemies cannot take damage before their encounter activates, preventing early long-range kills from skipping the encounter sequence. Separate encounters do not activate simultaneously.

## Normal-Difficulty Values

Shared configuration asset: `/Game/AlienPlanet/DA_NormalBalance`. Double-click it in UE to adjust player abilities, enemy health/damage/speed, and supplies. The values below are the current settings, not results from human difficulty testing.

| Item | Current Value |
| --- | --- |
| Player health | 100 |
| Minion | 70 health; 8 melee damage; 140 cm/s; 1.9-second cooldown after an attack finishes |
| Boss | 720 health; 18 melee damage; 26 pounce damage; 100 cm/s |
| Boss cooldown | 4.5 seconds after an attack; 3.5 seconds below half health, with no additional damage increase |
| Boss armor / weakness | Takes 65% damage normally; takes 150% damage during the 1.5-second recovery after an attack |
| Fire | 60 damage; 0.85-second cooldown; 2 hits to defeat a normal minion |
| Wave | 50 damage; 4-second cooldown; 5-meter area in front, subject to occlusion; interrupts and knocks back minions, but does not launch the boss |
| Laser | 45 damage per second; 22-meter range; stops immediately when the gesture is released; approximately 1.56 seconds of continuous hits to defeat a normal minion |
| Shield | Frontal 140-degree protection; 100 energy; drains 24/second plus 12 per block; regenerates at 18/second starting 1 second after release; cannot reactivate for 1.5 seconds after depletion |
| Sustained ability exclusivity | Shield and Laser cannot be sustained simultaneously; casting Fire / Wave lowers Shield |
| Damage protection | 0.55 seconds after each valid hit, preventing multiple enemies from stacking damage in one frame; 2 additional seconds of protection when respawning for a retry |
| Health pickups | Restore 25 health up to maximum; not consumed at full health; each can be collected once per attempt |

The shield faces the player's view; attacks from behind still deal damage. At full energy with no incoming attacks, it lasts approximately 4.17 seconds, preventing indefinite shielding while dealing damage. Minion attacks have an approximately 0.77-second windup. The boss's landing warning stays at the position selected during attack preparation rather than following a player who has dodged. Boss pounce movement and hit detection run on the actual animation's Launch / Hit_Controlled_1s notifications rather than repeatedly applying damage in Tick.

With the current ability damage, one Fire plus one Wave also defeats a full-health minion. Fire deals 39 damage to the boss normally and 90 during recovery; Laser deals 29.25 / 67.5 damage per second, respectively.

Damage and healing always stay within valid health bounds. Death counts a kill only once, disables collision, plays Death, holds the final frame, and then hides the body. Checkpoint retries restore subsequent enemies and the current section's pickups, as well as health, shield energy, and ability cooldowns.

## Controls and Display

Controls currently use gestures and face tracking exclusively: push / pull an open left hand to move, make a left fist to stop, and turn your head to turn. Hold your right palm open and steady for Shield, push a right fist forward for Fire, swipe sideways for Wave, and pinch your right thumb and index finger for a continuous Laser (release to stop). Hold only the right index finger out for 0.6 seconds to scan, a right V sign for 1 second to request a hint, a left V sign for 1 second to repeat an instruction, and both V signs for 1.5 seconds to recalibrate. After death / victory, hold both palms open for 1.5 seconds to retry / restart. See [GESTURE_ONLY.md](GESTURE_ONLY.md) for full instructions.

The HUD displays the current area objective, checkpoint, kills, threats, player health bar, shield energy, ability cooldowns, and boss health bar. On failure, it prompts you to hold both palms open for 1.5 seconds to retry. Actual player health, boss health, objectives, mission progress, and core energy are synchronized with COSMO. Telemetry does not fabricate tutorial completion events.

## Verification and Regeneration

All 68 automated level checks passed. Report: `Saved/PlanetVerification.json`; damage-increase verification log: `Saved/Logs/CombatBalance_Verify.log`. Coverage includes saved map loading, animation bindings, collision and traversal, actual navigation pursuit, training hits, applied damage configuration, two-fireball minion kills, boss armor and recovery damage, supplies, checkpoint recovery after death, dodging boss warnings, and core victory. Additional checks confirmed that shield training remains possible at 1 health and that closed barriers block the player while open barriers allow passage.

The existing 47 UE character/gesture/COSMO regression checks and 113 Python service tests (plus 6 subtests) also passed. Offscreen previews in the actual game mode checked the full navigation path from spawn to arena, the death screen, and retry restoration to 100 health. Screenshots are in `Saved/Screenshots/Planet_*.png`. Automated verification and offscreen visual checks do not include a complete human gesture playthrough, difficulty surveys, or Windows package acceptance testing.

```powershell
# By default, only build and verify the saved map, preserving manual layout edits.
.\Scripts\VerifyPlanetLevel.ps1

# Rebuild the generated map layout, then verify; preserve DA_NormalBalance adjustments.
.\Scripts\VerifyPlanetLevel.ps1 -RebuildLevel

# Import missing assets from extracted models and textures, then rebuild and verify.
.\Scripts\VerifyPlanetLevel.ps1 -ImportSources
```

Use `-SkipBuild` to reuse the current plugin build and `-EngineRoot` to specify the UE 5.7 path. Regenerating the map replaces manual placements in that map; default verification does not save or modify it. Source archive hashes are recorded in `External/AlienPlanet/source_manifest.json`. Python files inside the archive were not executed.
