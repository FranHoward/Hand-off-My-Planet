## Automatic Startup and Shutdown (Current Behavior)

After UE opens and Play starts, CosmoClient automatically checks /health on the default local service. It reuses an existing compatible COSMO service; otherwise, it uses the standalone Python in the plugin resources to launch the bundled app/cosmo_service.py, without a manual startup step. Game requests remain queued in order during startup and are sent after the health check passes. Each probe has a 1-second timeout, with a total startup budget of 20 seconds. Missing runtime files, an incompatible service on the port, process exit, or timeout produces an error. Editor logs are written to Saved/Logs/CosmoService.log; packaged games write to COSMO/Logs under the user's local application data directory.

Stopping Play / exiting the game cancels probes and requests and shuts down the COSMO process started by that session. Existing manually started services keep running. If UE exits unexpectedly, the service's parent-process monitor also shuts the service down. Gesture recognition retains its existing automatic startup and shutdown cleanup flow.

The service inherits the environment variables used to launch UE. An explicit COSMO_AI_PROVIDER takes priority; otherwise, XAI_API_KEY selects Grok automatically, and no key selects the local mock. Grok also requires XAI_MODEL; voice uses XAI_VOICE_MODEL / XAI_VOICE or the existing defaults. Launch UE from the same terminal after configuring the environment. An already running UE instance cannot inherit variables later set in another terminal. Keys are not written to the project or launch arguments. A passing health check confirms only that the local service is ready; actual responses determine real Grok availability.

CosmoClient.AutoLaunchService disables automatic management. Automatic startup applies to the default http://127.0.0.1:8765 address and interactive gameplay. Custom addresses, unattended runs, and commandlet tests continue to use external services. A complete bundled Win64 runtime is now available. UE packaging automatically copies Python, dependencies, models, and configuration; see [Runtime Deployment](RUNTIME_DEPLOYMENT.md).

Verification: UE Editor compiled successfully; 187 Python tests and 6 subtests passed; 59 character integration checks passed. Scripts/test_cosmo_lifecycle_smoke.py verifies local mock startup, health readiness, and cleanup after parent-process exit. Human voice acceptance testing after this automatic startup change has not yet been performed.

The manual startup instructions below are retained for standalone debugging.
> The input system now uses gesture and face controls exclusively. The old key bindings, text chat, and fallback input instructions in this document are obsolete. See [GESTURE_ONLY.md](GESTURE_ONLY.md) for current gestures and startup instructions.

# COSMO AI Integration

The character Blueprint is wired and implemented. See [BP_FIRST_PERSON_AI.md](BP_FIRST_PERSON_AI.md).

Run `./Scripts/StartCosmo.ps1` from the project root. By default, the service listens on `127.0.0.1:8765`.
Open UE and Play in the default `Lvl_AlienPlanet` level. GameMode already spawns `BP_FirstPersonCharacterAI`.
Gesture reception uses `127.0.0.1:5052`. The current game uses gesture and face controls exclusively.

## Interfaces

- `POST /events`: actual gameplay confirmations, in this order: `game_started` -> `movement_completed` -> `shield_activated` -> `fire_activated` -> `wave_activated` -> `laser_activated` -> `enemy_scanned` -> `boss_encountered` -> `tutorial_completed`.
- `POST /game/state`: synchronizes facts such as health, ability availability, and targets in real time. It cannot complete tutorials or modify ability confirmation history.
- `POST /player/utterance`: text or final STT transcripts; answers questions without advancing the tutorial.
- `GET /snapshot`, `/tutorial/status`, `/tutorial/step`, `/tutorial/hint`: state, instructions, and hints.
- Damage/danger events: `player_damaged`, `player_in_danger`, `danger_cleared`.
- The Blueprint connects connection status, responses, subtitles, local TTS, and speaking/listening/scanning/celebrating/warning animation events.
- Uses the local guide by default. Server-side `COSMO_AI_PROVIDER=xai`, `XAI_API_KEY`, and `XAI_MODEL` enable real Grok integration.

## Grok Integration and Response Source Verification

Reuse the existing Python 3.12 `.venv` in the project root. Dependencies are in `External/CosmoSystem/requirements.txt`.
The service directory is `External/CosmoSystem`; the current project version is `0.2.0`.
Real questions and answers use the official xAI `POST /v1/responses` endpoint, matching the official `grok-4.7` example.
Tool calls use `function_call` / `function_call_output` and expose only the four registered state/tutorial tools.
The tool loop for a question passes back encrypted reasoning items from the response. Subtitles read only the final `output_text`; incomplete or failed responses fall back locally.
The received `External/cosmo-system.zip` does not contain `README_zh-CN.txt`, `apply_patch.py`, or `test_xai.py`.
The referenced patch script was therefore not run. The archive's `0.2.0` version was inspected, and the existing code was adapted manually for this integration.
The original archive is retained. Per-file SHA-256 comparisons are in `Saved/Logs/GrokIntegration_ArchiveComparison.json`.
Existing files were backed up before modification to `Saved/CodexBackups/GrokIntegration_20261018_004210`, including an original-file SHA-256 inventory.

Start with mock mode:

```powershell
./Scripts/StartCosmo.ps1 -Mode mock
Invoke-RestMethod http://127.0.0.1:8765/health
```

Set `XAI_API_KEY` and an `XAI_MODEL` available to your account in the PowerShell window running the Python service.
Keep keys only in the server environment, outside UE Blueprints, project configuration, chat, and command-line arguments.
Use the following to enter the key without storing the key itself in PowerShell command history:

```powershell
$xaiSecret = Read-Host 'xAI API Key' -AsSecureString
$env:XAI_API_KEY = [System.Net.NetworkCredential]::new('', $xaiSecret).Password
Remove-Variable xaiSecret
# Query with --list-models below, then set XAI_MODEL to an available model ID.
$env:XAI_MODEL = '<model-ID-available-to-your-account>'
```

Then run these commands from the project root:

```powershell
# List models visible to the current account without generating a response.
./.venv/Scripts/python.exe ./External/CosmoSystem/test_xai.py --list-models
# Select grok-4.7 if it appears in the list above.
$env:XAI_MODEL = 'grok-4.7'
# Once XAI_MODEL is configured, this makes a real model request and may use multiple read-only tool calls.
./.venv/Scripts/python.exe ./External/CosmoSystem/test_xai.py
```

If `--list-models` returns HTTP 400, first inspect the script's `Server detail (credentials redacted)` output.
This step calls `GET /v1/models`, does not use `XAI_MODEL`, and does not generate a model response.
Before sending, the script checks for accidental quotes, `Bearer`, spaces, or newlines in the key. Keys in server errors are replaced with `[REDACTED]`.
For troubleshooting, rerun from the same PowerShell window where the key was configured and share the redacted error message.
Diagnostic output and key redaction passed 7 offline checks; the server response remains authoritative for the actual error cause.

Stop the mock service using port 8765, then start:

```powershell
./Scripts/StartCosmo.ps1 -Mode grok
Invoke-RestMethod http://127.0.0.1:8765/player/utterance -Method Post `
  -ContentType 'application/json; charset=utf-8' -Body '{"text":"Which combat abilities can I use?"}'
```

Only a `/player/utterance` response with `source: "grok"` confirms actual model content.
Mock responses use `source: "mock"`. Local replies after upstream network failures, timeouts, or similar errors use `source: "fallback"` and include `provider_error`.
The `/health` fields `ok` and `provider: "xai"` indicate only the local service and selected configuration; they do not prove upstream connectivity.
UE's `FCosmoResponse.Source`, `GuideReplySource`, and HUD `reply` also show the actual response source. Subsequent health checks do not overwrite it.
The character Blueprint already has a 30-second request timeout. New clients now also default to 30 seconds, exceeding Python's 20-second model request budget.

Windows integration acceptance testing passed 47 automated UE5 runtime checks. At that point, all 100 Python tests and 6 subtests passed.
After the subsequent Responses API adaptation, all 83 current Python service tests passed; see `Saved/Logs/XaiResponses_Test.log`.
Automated UE checks use a mode without rendering or audio devices and cannot replace in-person audio or animation acceptance testing.
The actual ability-success events, HTTP completion callbacks, subtitles, TextToSpeech, and animation delegates of `BP_FirstPersonCharacterAI` are wired.
`HttpBlueprint` and `JsonBlueprintUtilities` are explicitly enabled in the project.
`/Game/Blueprint/BP_AICompanionManager` has been created and spawns automatically as the `AICompanionManager` Child Actor component of `BP_FirstPersonCharacterAI`.
It inherits from `AAICompanionManager`, directly reuses the player's `CosmoClient` and `FirstPersonAIIntegration`, and creates no second session, ability implementation, or speech flow.
External input can call `SubmitPlayerQuestion(Text)`. Completion callbacks use `OnResponse`, subtitles use `OnSpokenLine`, and animation uses `OnAnimation`.
`LastResponse`, `Subtitle`, `ReplySource`, and `AnimationEvent` are directly readable in Blueprints.
Existing gameplay logic already reports successful abilities; do not report the same success again when binding the companion.
The companion AnimBP can read speaking/listening/scanning/celebrating/warning from the Manager's `AnimationEvent.State`.
Call the character Integration's `SetVoiceListening(bool)` when voice listening starts/ends. The Manager's `OnAnimation` forwards both local listening state and service response animations.
To rebuild only the Manager asset, run `UnrealEditor-Cmd.exe VFX.uproject -run=CodexCompleteCharacter -CompanionManager -EnablePlugins=CodexBlueprintTools -unattended -nullrhi`.
The verification script now builds `VFXEditor` and uses UBT's `-EnablePlugin=CodexBlueprintTools` to compile verification tools and runtime plugins together.
Dedicated companion SkeletalMesh / AnimBP / Montage assets were still unavailable at this stage. Existing `CompanionMesh`, `CompanionMontages`, and
`OnCompanionAnimationRequested` are mapping entry points, not confirmation that companion animation assets are integrated.
The user accepted real API questions and answers locally through `test_xai.py`: `source: "grok"`, `model: "grok-4.7"`,
a correct listing of Shield / Fire / Wave / Laser, and `advanced: false`. The user-provided result is recorded in `Saved/Logs/Grok_RealApi_UserAcceptance.json`.
Native Grok real-time voice and UE microphone capture are now integrated. See the next section for startup and verification; in-person listening still requires human confirmation.
The key exists only in the user's configured PowerShell process environment, so run `./Scripts/StartCosmo.ps1 -Mode grok -Model grok-4.7` from that window.

## UE Real-Time Voice and Top-Right Webcam Preview

Stop the old service in the PowerShell window with `XAI_API_KEY` configured, then run:

```powershell
.\Scripts\StartCosmo.ps1 -Mode grok -Model grok-4.7 -VoiceModel grok-voice-latest -Voice eve
```

Keep the service running, open UE, and click Play. UE automatically starts the same gesture/face recognition program. The top-right corner displays a mirrored webcam preview, gesture annotations, voice status, and player transcripts; COSMO subtitles appear at the bottom. The webcam keeps its original aspect ratio, and stale frames are cleared on disconnection. No additional browser or recognition window is needed.

Wait for `Listening - speak to COSMO`, then speak directly, for example, "COSMO, what should I do now?" The server detects the end of speech and returns Grok's native audio and subtitles. By default, microphone capture pauses while COSMO speaks to prevent speaker echo from becoming a new question, then resumes afterward. With headphones, enable `AllowVoiceInterruptions` on the character's `FirstPersonAIIntegration` to allow interruptions.

The same component's `VoiceVolume` (0-1) and `VoicePitch` (0.5-2, default 1) control UE playback volume and pitch. Restart Play after changing Blueprint defaults, or adjust them through the runtime `Realtime` component. `SetMicrophoneMuted(bool)` supports Blueprint muting. Select a voice with the `-Voice` launch argument, such as `eve`; voice selection and playback pitch control different aspects of the sound.

If microphone status is missing, check Windows Settings > Privacy & security > Microphone > Let desktop apps access your microphone, and the default input device. Text after `Grok Voice unavailable` is a redacted server error; `Voice offline` means the Python service needs checking. Disconnections reconnect automatically. Actual game events, subtitles, and existing local TTS continue working when voice is unavailable. Stopping Play releases the UE microphone, voice connection, and audio queue, and ends the webcam process started by that session.

| Route | Address | Content |
|---|---|---|
| Existing game HTTP | `http://127.0.0.1:8765` | Actual gameplay events, state, and text questions and answers |
| UE -> Python -> Grok Voice | `ws://127.0.0.1:8766` | 24 kHz mono PCM16 audio, transcripts, and subtitles |
| Recognition program -> UE preview | `http://127.0.0.1:8767/frame.jpg` | In-memory JPEG at approximately 10 FPS, maximum width 480 |

Python always holds the long-lived API key. The webcam reuses the recognition program's capture device and sends frames only to local UE, without saving images or sending them to Grok. Microphone audio is forwarded through Python to Grok. The original browser test page's system speech settings are separate from UE's native voice settings.

Voice exposes only the existing four state/tutorial query tools and shares the same authoritative tutorial state with HTTP. Saying "done" does not call `/events`, activate abilities, or advance the tutorial; actual ability successes continue through the existing event chain. Real-time subtitles are forwarded through Integration and `BP_AICompanionManager.OnSpokenLine`, with `ReplySource` set to `grok`, and do not trigger a second local TTS playback. Voice events do not fabricate HTTP `OnResponse`; HTTP completion callbacks remain for original requests.

Integration follows the [xAI real-time voice documentation](https://docs.x.ai/developers/model-capabilities/audio/speech-to-speech) and the [user-provided Voice Agent example](https://docs.x.ai/cookbook/examples/voice-agent-web). The voice model uses `XAI_VOICE_MODEL`, defaulting to `grok-voice-latest`, configured separately from the text model `XAI_MODEL=grok-4.7`. `/health.realtime.status=listening` indicates only that the local port is listening; a complete real audio response proves upstream availability.

Check the real service without recording:

```powershell
.\.venv\Scripts\python.exe .\External\CosmoSystem\test_xai_voice.py
```

This sends one real voice question through the running local service, consumes API usage, and outputs `source`, model, voice, subtitles, and `audio_bytes` without saving audio. On 2026-10-18, complete audio and subtitles were received with `source: "grok"`, `model: "grok-voice-latest"`, and `voice: "eve"`. Real audio was then received through UE's native WebSocket, subtitles updated, and companion Blueprint callbacks forwarded. All 15 online UE checks passed; log: `Saved/Logs/RealtimeAV_LiveGrok.log`. This verification did not record the microphone or replace human listening.

All 187 current Python tests and 6 subtests passed. Offline UE regressions include 27 audiovisual checks, 59 character checks, and 68 level checks. `Scripts/VerifyRealtimeAV.ps1` uses separate ports 18766/18767 and synthetic audio/images. Test values of `source:grok` verify parsing only and are not evidence of upstream connectivity. A real webcam successfully provided a 480 x 360 preview after hand/face recognition and exited normally; no actual images were saved. Layouts at 1280 x 720, 888 x 500, and 1024 x 768 were verified with synthetic webcam images. At 4:3, the existing level camera preserves its aspect ratio, and the HUD stays within the active game image.

Pre-change backup: `Saved/CodexBackups/RealtimeAV_20261018_021441`. UE runtime implementation: `CosmoRealtimeComponent`; Python forwarding: `companion/realtime_voice.py`; preview provider: `dubhack_gestures/camera_preview.py`.

## Tutorial Priority and Location Dialogue

Tutorial and preset location lines take priority over real-time conversation. Triggering them immediately clears current conversation audio, cancels its generation, and pauses player microphone capture. Subtitles remain fixed to the preset text; late conversation subtitles or audio cannot overwrite them. Consecutive tutorial lines play in order, with each starting only after UE finishes playing the previous line. After the last line, microphone capture and new questions resume; canceled old answers do not resume. `AllowVoiceInterruptions` applies only to ordinary conversations and cannot interrupt tutorials.

Locations follow the current planet level's route and unlock conditions, with `route progress = -player Y coordinate`:

| Location ID | Actual Trigger | Existing Default Line Source |
|---|---|---|
| `landing` | The player enters the landing area in this session | Original Welcome tutorial text |
| `training` | First passes route progress 2150 and enters the training checkpoint | Original Shield tutorial instruction |
| `ruins` | Progress exceeds 5300, with all four training targets and both canyon enemy groups completed | Original level objective text, "Starcore ruins" |
| `boss` | Progress exceeds 6200, both ruins guards are defeated, and the boss actually activates | Original level objective text, "Defeat the guardian" |

Each area announces once per session. Staying, walking back and forth, and checkpoint retries do not repeat it; restarting the entire game resets triggers. Reaching a location triggers only dialogue, never replacing an ability or tutorial completion event. Saying "done" still cannot advance the tutorial.

To edit location dialogue, select `BP_PlanetDirector` in the level and change the relevant ID's text under `Tutorial -> Locations -> RegionTutorialLines`. New Blueprint triggers can call the Director's `AnnounceRegion(Region)` from actual area-entry callbacks. Existing tutorial/game events automatically use the same priority queue. For other preset Blueprint lines, call the character Integration's `PlayTutorialLine(Line)` rather than sending preset lines through the real-time question endpoint. Consecutive identical pending lines are merged to prevent duplicate announcements.

Grok narration uses a separate native voice connection with `force_message` to synthesize the exact text without chat-model rewriting; the normal conversation connection retains its context. Tutorial priority is released only after UE confirms playback completion. Network failures switch to the existing local TTS while preserving exact subtitles and priority. If local TTS is disabled, subtitle reading time is retained. Preset subtitles use source `scripted`; ordinary real Grok answers remain `grok`.

On 2026-10-18, UE received actual Grok preset tutorial audio (214560 bytes of PCM), matching the original text and Blueprint callbacks. Online verification expanded to 17 passing checks; log: `Saved/Logs/TutorialPriority_LiveGrok.log`. The microphone was not recorded; in-person listening remains to be confirmed. Pre-tutorial-change backup: `Saved/CodexBackups/TutorialPriority_20261018_025126`. Restart the Python service and reopen UE after updating to load the new tutorial priority controls.

## Required Project Assets and Configuration

Actual enemies/boss, health/damage values, and the full victory/defeat flow are now integrated into `/Game/AlienPlanet/Lvl_AlienPlanet`. See [Alien Planet Level](ALIEN_PLANET_LEVEL.md).
Dedicated COSMO companion animation assets still require mapping. The Grok account needs access to the current voice model and sufficient quota.
The corresponding Blueprint entry points, asset mapping hooks, and configuration locations are ready; see the feature documentation. Local tutorials do not impersonate a real model.
