> The input system now uses gesture and face controls exclusively. The old key bindings, text chat, and fallback input instructions in this document are obsolete. See [GESTURE_ONLY.md](GESTURE_ONLY.md) for current gestures and startup instructions.

# BP_FirstPersonCharacterAI Features and Setup

Actual asset: `Content/Blueprint/BP_FirstPersonCharacterAI.uasset`. The project spelling is **Character**, not Characater.
The default `BP_FirstPersonGameMode` already uses this character. Original keyboard/mouse, gamepad, and touchscreen movement remain available.
The Blueprint Event Graph connects input and service events. The `FirstPersonAIIntegration` component implements abilities, tutorials, UI, and HTTP coordination.

## Startup and Controls

Run `./Scripts/StartCosmo.ps1` from the project root, open `VFX.uproject`, and Play in the default `Lvl_FirstPerson` level.
For gesture mode, also start `dubhack_gestures/gesture_recognition.py`. Without a webcam, use the keyboard/mouse controls below.
The HUD displays the COSMO connection, provider, gesture connection, tutorial stage, health, sustained abilities, and subtitles.

| Feature | Keyboard / Mouse | Gesture / Blueprint Entry Point |
|---|---|---|
| Movement / view | WASD / mouse | Push or pull the left hand / turn the head left or right |
| Shield | Hold LeftShift; release to lower | Hold the right palm open; Pressed / Released |
| Fire | F | Make a right fist, then push forward; fires once per event |
| Wave | Q | Swipe the right hand horizontally; casts once per event |
| Laser | Hold the left mouse button | Keep the right thumb and index finger pinched; release to stop; pinch center maps to the viewport |
| Scan | E; keep the target visible | `ScanEnemy` |
| Tutorial hint / repeat | H / R | `RequestHint` / `RepeatInstruction` |
| Questions and answers | Enter opens the input box; Enter again sends; Escape closes | `SubmitVoiceTranscript(Text)` |
| Voice input status | Call when STT starts / ends | `SetVoiceListening(bool)` |
| Danger / clear danger | Called by game logic | `SetDanger(bool)` |

Chat, death, and window focus loss release sustained abilities. If no valid camera frame arrives within the configured 400 ms, the receiver automatically releases Shield/Laser and stops axis input.
Keyboard and gesture inputs use the same ability implementations. Held events do not spawn Fire/Wave every frame; the receiver handles JSON deduplication.

## Implemented Behavior

- Shield: visible shield, sustain/release, and blocking damage to this character's health.
- Fire: Niagara fireball, actual projectile movement, single collision damage, cooldown, and automatic destruction.
- Wave: SoundWave Niagara effect, frontal hemisphere area damage, occlusion checks, per-Actor deduplication, knockback, and physics impulses.
- Laser: sustained beam, gesture aiming/mouse crosshair, ray hits, DeltaTime-based damage, and shutdown on release or disconnection.
- Movement confirmation: accumulates actual grounded movement within the current stage; empty input, walking into walls, and teleportation do not complete it.
- Scan confirmation: hit the target and keep it visible for the scan duration; interruption requires restarting the scan.
- Boss: checks distance and visibility against the actual Boss Actor and confirms the encounter at the correct stage.
- Nine-stage tutorial: follows the server's expected_event strictly; reports only genuinely successful abilities; voice/text does not complete stages.
- Damage/death: health deduction, shield blocking, rate-limited danger announcements, low-health danger state, and movement/ability shutdown on death.
- COSMO: ordered HTTP, state preservation on reconnect, real-time game facts, hints, repeats, questions and answers, subtitles, local TTS, and five animation events.
- AI context: health, current shield state, ability cooldown availability, scan target, boss target, and its readable `Health`; telemetry does not advance the tutorial.

The new default level has a completed normal-difficulty configuration; see [Alien Planet Level and Balance](ALIEN_PLANET_LEVEL.md). `DA_NormalBalance` applies player, enemy, and supply values consistently. The component values below are prototype defaults used when no level Director is loaded.
Adjust them in the character Blueprint's `FirstPersonAIIntegration` component Details: Fire 25 damage/0.5 s cooldown; Wave 15 damage/1 s cooldown/600 cm radius; Laser 20 damage per second/6000 cm range; movement confirmation 100 cm; scan 1 s/3000 cm; boss encounter 1500 cm.

## Level and Animation Assets

Scan targets need an `Enemy` or `Boss` Actor Tag, or must be non-player Pawns. Boss objects need the `Boss` Tag.
The default level is now `/Game/AlienPlanet/Lvl_AlienPlanet`, with 5 minions supporting actual pursuit/attack/hit/death behavior, 1 animated boss, and a Crystal Eye scan target. `CosmoPlanetDirector` manages normal difficulty, training targets, supplies, checkpoint retries, and core completion.
When adding other enemies/bosses, configure Tags, collision, and damage handling. Encounter logic can also call `ConfirmBossEncounter(Boss)`, which still checks distance, line of sight, and stage.
The prototype's final tutorial stage confirms automatically by default. The new level disables `AutoCompleteFinalStage` and calls `CompleteTutorial()` from actual level objectives after the boss is defeated and core charging finishes.

Grok is represented by Crystal Eye. `BP_AICompanionManager`'s `CompanionMesh` uses
`/Game/Creatures/CrystalEye/Mesh/SK_CrystalEye`, the original materials, and `ABP_CrystalEye`.
The player's `AICompanionManager` Child Actor component is attached under Capsule at relative position `(160, 100, 20)` cm,
with model scale `0.30` and relative Yaw `-90` degrees, placing it ahead and to the right. Movement, turning, and teleportation preserve this relative position.
Collision and overlap events are disabled. The ABP reads the parent player's speed, playing Idle while stationary and FloatForward while moving.
On connection, Integration's `CompanionMesh` is assigned to this mesh automatically, reusing the player's existing Grok client.

Crystal Eye's original animations are only Idle, FloatForward, and Controlled. Voice, scanning, celebration, and warning events remain available through
`OnCompanionAnimationRequested`. For dedicated actions, configure `CompanionMontages` to map
`talk_gesture`, `listen_lean_in`, `scan_pulse`, `celebrate`, and `warn_point` to Montages.

Offline verification: `UnrealEditor-Cmd.exe VFX.uproject -run=CodexCompleteCharacter -VerifyCompanion -EnablePlugins=CodexBlueprintTools -unattended -nullrhi`.
The 20 checks cover reloaded mesh/ABP/materials, child relationships, following translation/rotation/teleportation, actual skeletal playback, and Idle/Move transitions.
Results: `Saved/Logs/CrystalEyeCompanion_Verify.log`. Pre-change backup: `Saved/CodexBackups/CrystalEyeCompanion_20261018_030919`.

## Real Grok, Audio, and Microphone

The default `COSMO_AI_PROVIDER=local` displays `local` on the HUD and runs the local deterministic tutorial.
Real Grok uses the [official xAI Responses/function calling API](https://docs.x.ai/developers/tools/function-calling), matching the official `grok-4.7` example.
Configure it on the **server**, then start:

```powershell
$env:COSMO_AI_PROVIDER = 'xai'
$env:XAI_API_KEY = '<server-side-key>'
$env:XAI_MODEL = '<model-ID-available-to-your-account>'
./Scripts/StartCosmo.ps1
```

Keys are not written into Blueprints, assets, or configuration files. The model has only four read-only tutorial/game-state tools and cannot cast abilities or complete stages.
Model failures fall back to the local tutorial and display a notice. No API key was available for this verification, so no real paid model calls were made.
Local TTS uses UE's TextToSpeech plugin and preserves subtitles when no audio device is available. It is separate from Grok Realtime Voice; the default local voice mainly supports English.
The original project had no microphone capture/STT service. Pass the recognition service's **final text** to `SubmitVoiceTranscript` and call `SetVoiceListening` on start/end.
Text questions through Enter can verify the same AI response path end to end.

## Verification and Maintenance

Run `./Scripts/VerifyCharacterAI.ps1`; add `-SkipBuild` if already compiled, or use `-EngineRoot` to specify the UE installation.
The script starts a temporary service on the separate port 18765 and shuts it down afterward. It does not use a real model or API quota.
Verification covers actual Blueprints, ability damage, the full tutorial, JSON deduplication, Pointer, timeout release, damage, death, subtitles, service tests, and gesture tests.

Blueprint rebuild command: `UnrealEditor-Cmd.exe VFX.uproject -run=CodexCompleteCharacter -EnablePlugins=CodexBlueprintTools -unattended -nullrhi`.
Add `-Inspect` to inspect existing assets only. The old `CodexWireInput` overwrites ability entry points; use `CodexCompleteCharacter` going forward.
Original Blueprint and GameMode backups are in `Saved/CodexBackups/CharacterAI_FullIntegration`.

Latest UE integration verification: Unreal build succeeded and 47 runtime checks passed. After the subsequent Responses API adaptation, all 83 Python service tests passed.
The default level was rendered and checked at 1280 x 720. Preview: `Saved/Screenshots/CharacterAI_Preview.png`.
Verification logs: `Saved/Logs/CharacterAI_Verification.log` and `Saved/Logs/CharacterAI_RuntimeTest.log`.
