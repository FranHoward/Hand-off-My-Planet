# UE Gesture and Face Controls

The current VFX project integrates the recognition program from the supplied archive and uses gestures and face tracking exclusively for gameplay interaction.

## Startup

1. Open `VFX.uproject` with UE 5.7, enter the default planet level, and click Play.
2. UE automatically starts Python from the project's `.venv`, loads the hand and face models, and sends data to local UDP port 5052. No separate recognition window is needed.
3. Face the webcam, hold your left palm naturally in front of your body, and stay still for about one second. Start when the HUD displays Hands and face ready.
4. Stopping Play ends the recognition process started by that game session and releases the webcam.

The top-right corner now shows a mirrored webcam preview and gesture annotations so players can see their hands, face, and framing. Frames come from the same recognition process without opening another camera, and stale frames are cleared on disconnection. See [COSMO_INTEGRATION.md](COSMO_INTEGRATION.md#ue-real-time-voice-and-top-right-webcam-preview) for Grok real-time voice startup and status. Voice questions do not replace gameplay gestures or advance the tutorial.

## Gestures

| Gesture | In-Game Action |
|---|---|
| Push the left hand toward the camera / pull it toward your body | Move forward / backward |
| Make a left fist | Stop moving immediately |
| Turn your head left / right, then return to center | Turn; returning to center stops turning |
| Hold the right palm open and steady | Shield; withdraw the hand to lower it |
| Make a right fist, then push toward the camera | Fireball; pull back to prepare another cast |
| Swipe the right hand sideways | Shockwave |
| Pinch the right thumb and index finger together | Continuous laser; release to stop; aim with the pinch center |
| Extend only the right index finger for 0.6 seconds | Scan the enemy under the crosshair |
| Hold a right-hand V sign for 1 second | Request a hint from COSMO |
| Hold a left-hand V sign for 1 second | Repeat the current hint and stop left-hand movement input |
| Hold V signs with both hands for 1.5 seconds | Recalibrate; then lower the V signs, face forward, and hold the left palm steady for about one second |
| On the death or victory screen, hold both palms open for 1.5 seconds | Retry from the checkpoint / restart |

Scan, hint, repeat, and retry trigger once per continuous hold. Withdraw the hand or change the pose before triggering again. The game ignores the two-palms retry event during normal gameplay. Two-hand V calibration takes priority over single-hand hints; movement and abilities are suppressed during calibration.

## Removed Inputs

The project's Blueprints had 12 old input nodes removed, `IMC_Default` and `IMC_MouseLook` cleared, and C++ keyboard/mouse fallback bindings, the text chat box, mouse retry button, and key prompts removed. At runtime, Enhanced Input mappings are also cleared and character hardware input disabled. UE editor Play/Stop controls remain editor functions.

Gestures send action names directly. The `key` in `gesture_bindings.json` must be empty; no keyboard simulation or Enhanced Input triggering is used.

## Tracking Loss and Configuration

- Left hand lost: movement resets to zero. Right hand lost: Shield and Laser release. Face lost: turning stops.
- No valid UDP data for more than 400 ms: movement/turning reset, sustained abilities release, and scanning cancels.
- Window focus loss or character death: movement stops and abilities release.
- For camera errors, inspect `Saved/Logs/GestureRecognition.log`; restart Play after restoring device permissions.
- If the camera, hands, or directions do not match, adjust `CameraIndex`, `SwapHands`, and `InvertHeadTurn` on `BP_FirstPersonCharacterAI`'s `FirstPersonAIIntegration` component.
- Automatic startup applies to the current Windows UE project and its `.venv`. A packaged release containing Python and the models had not yet been produced at this stage.
- Gesture thresholds are in `recognition_settings.json`. Push/pull uses monocular palm-scale estimation; keep the camera fixed and palm orientation as steady as possible.

## Manual Diagnostics

First disable the component's `AutoLaunchRecognition` to prevent two processes competing for the webcam, then run from the project directory:

```powershell
.\.venv\Scripts\python.exe dubhack_gestures\gesture_recognition.py --udp-port 5052 --preview-port 8767
```

The preview shows recognition status only, with no keyboard shortcuts. Close the preview window to stop. Standalone scripts do not use networking by default; specify `--udp-port 5052`. To reuse this plugin, copy both the UE and Python code.

## Verification

UE 5.7.4 compilation and reflection code generation passed. After real-time voice and webcam preview integration, 185 Python tests, 6 subtests, 59 character runtime checks, and 21 offline audiovisual checks passed, along with 15 actual Grok voice response checks. Character verification includes actual local UDP, disabled keyboard/mouse input, scanning, hints, sustained ability release, and tutorial flow. An earlier 63 level checks passed, including gesture retries after death and gesture restarts after victory.

A real webcam completed hand/face model processing, with face pose output observed. Standalone game startup also verified automatic recognition process creation and clean exit. A complete human playthrough exercising every gesture has not been completed; sensitivity, lighting, and distance still need on-site adjustment. Tests did not save webcam images.

Run regressions with `Scripts/VerifyCharacterAI.ps1` and `Scripts/VerifyPlanetLevel.ps1`; add `-SkipBuild` after compilation. Input asset audit: `-run=CodexGestureOnly -Verify -EnablePlugins=CodexBlueprintTools`.

Pre-change backup: `Saved/CodexBackups/GestureOnly_20261018_014057`.
