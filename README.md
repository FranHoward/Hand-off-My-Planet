# Hand off My Planet

**Cast spells with your hands, look around by turning your head, and embark on an alien adventure with COSMO.**

Hand off My Planet is a first-person game built with Unreal Engine 5.7. A standard webcam tracks your hands and head movements: your left hand controls forward and backward movement, turning your head changes your view, and your right hand casts shields, fireballs, shockwaves, and lasers. Explore an alien landscape, fight monsters, and challenge the guardian of the planetary core.

Your AI companion, **COSMO**, supports real-time voice conversations through Grok and interactive tutorials. Basic exploration and combat work without an API key.

![Alien level development preview: first-person view, enemies, and the HUD for four abilities](docs/images/gameplay.png)

## Gameplay

Start at the spacecraft landing zone, explore the energy canyon and planetary core ruins, practice your abilities on training targets, and use supplies and checkpoints to keep progressing. The current level contains 5 minions, 1 boss, and a scannable, non-hostile Crystal Eye. **Defeat all hostile targets to reach the victory screen.**

Each of the four abilities serves a different purpose:

| Ability | Combat Use |
| --- | --- |
| Shield | Blocks frontal attacks. Continuously drains energy, which regenerates after you lower the shield. Watch for enemies behind you. |
| Fire | Launches a ranged projectile for a single, aimed strike. |
| Wave | Attacks the area directly in front of you at close range, interrupting and knocking back minions. |
| Laser | Fires continuously while you pinch. Move the pinch position to aim, and release to stop. |

Watch for the boss's attack warnings and focus your damage during its recovery after each attack. The HUD displays health, shield energy, ability cooldowns, kills, and the boss's health bar. After dying, use the two-open-palms gesture to retry from a checkpoint; after winning, use the same gesture to restart.

## Gesture Controls

Left and right always refer to **your own left and right hands**. Keep your face and both hands in the webcam's view, ensure adequate lighting, and leave enough room to push your arms forward, pull them back, and swipe sideways.

### Calibrate Before Playing

Face the webcam with your head pointing straight ahead and your left hand open in a natural position in front of your body. Hold still for about 1 second until tracking is ready. To recalibrate, hold a V sign with both hands for 1.5 seconds, lower the V signs, then hold your head and left hand steady again.

### Movement, Combat, and Interaction

| Gesture or Movement | In-Game Action |
| --- | --- |
| Open your left hand and push it toward the webcam | Move forward |
| Open your left hand and pull it toward your body | Move backward |
| Make a fist with your left hand | Stop moving immediately |
| Turn your head left / right | Turn left / right; return to center to stop turning |
| Hold your right hand open and steady | Raise the shield; withdraw your hand to lower it |
| Make a fist with your right hand and push it toward the webcam | Cast a fireball; pull your hand back to prepare another cast |
| Swipe your right hand sideways | Cast a shockwave |
| Pinch your right thumb and index finger together | Fire a continuous laser, aiming with the center of the pinch; release to stop |
| Extend only your right index finger and hold for 0.6 seconds | Start scanning the enemy or scannable object under the crosshair |
| Hold a V sign with both hands for 1.5 seconds | Recalibrate |
| On the death / victory screen, hold both hands open for 1.5 seconds | Retry from the checkpoint / restart |

The recognition layer also retains two tutorial gestures: hold a right-hand V sign for 1 second to request a tutorial hint, or a left-hand V sign for 1 second to repeat the hint. These are used in tutorial command mode. **The current planet level defaults to free conversation mode, so these two tutorial commands are inactive.** With Grok voice enabled, you can ask COSMO questions directly.

Held gestures, such as scanning and retrying, trigger only once per continuous hold. Withdraw your hand or change your pose before triggering them again. If hand or face tracking is lost, the corresponding movement, turning, or sustained ability stops.

### Menu Controls

Use gestures to move and cast abilities. Navigate menus with the mouse or use the arrow keys / Tab to select and Enter / Space to confirm. Press Esc during gameplay to pause, and press it again to resume. The pause menu lets you adjust voice volume, mute the microphone, or return to the main menu.

## Running the Project

### 1. Set Up Your Environment

| Component | Requirement |
| --- | --- |
| Operating system | 64-bit Windows. The current automatic startup and packaging workflows target Win64. |
| Game engine | Unreal Engine 5.7. |
| Build tools | Visual Studio's Game development with C++ tools and a Windows SDK compatible with Unreal, required to compile the project plugins. |
| Python | **CPython 3.12.10 x64**. The runtime builder checks the full version number. |
| Input devices | A webcam. Real-time voice also requires a microphone and an audio output device. |
| Network | Internet access is required for the initial Python dependency installation, the embedded Python download, and Grok services. |

### 2. Clone the Project and Build the Runtime

Run the following in PowerShell:

```powershell
git clone https://github.com/FranHoward/Hand-off-My-Planet.git
Set-Location Hand-off-My-Planet

# Verify that this reports Python 3.12.10 and that you are using a 64-bit interpreter.
python --version
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r Scripts/runtime-requirements.lock
.\.venv\Scripts\python.exe Scripts/build_runtime.py
```

The output is written to `Plugins/CosmoBridge/Resources/Runtime/Win64/` and includes a standalone Python interpreter, dependencies, service code, and recognition models. The game uses this runtime, so **build it before opening or compiling a freshly cloned project**.

You can check the runtime offline before opening Unreal:

```powershell
.\Plugins\CosmoBridge\Resources\Runtime\Win64\python\python.exe -B `
  .\Plugins\CosmoBridge\Resources\Runtime\Win64\app\runtime_check.py
```

The runtime builder refuses to overwrite existing output. Rebuild the runtime after changing the service or recognition source code. See [Runtime Deployment](RUNTIME_DEPLOYMENT.md) for backup, rebuild, and verification steps.

### 3. Open the Game and Try It Out

1. Open `VFX.uproject` with UE 5.7. Allow the plugins to compile when prompted, then wait for assets and shaders to finish preparing.
2. The default map is the main menu, `Content/UI/Maps/Lvl_MainMenu`. Click **Play** in the editor, then click **START MISSION**.
3. Select **CONFIRM** in the **SKIP TUTORIAL?** dialog to skip the voice tutorial and enter the level. This path requires no API key.
4. Allow desktop apps to access the webcam, face the camera to calibrate, then use gestures to move and cast abilities.
5. When you stop Play, the game shuts down the recognition process it automatically started for that session and releases the webcam.

The game automatically manages the required local COSMO service and gesture recognition processes. You do not need to open a separate recognition window. Without an API key, it uses a local mock service, and real-time Grok conversations are unavailable.

### 4. Optional: Enable COSMO Voice and the Interactive Tutorial

To use real-time Grok conversations and the introductory tutorial, configure a valid `XAI_API_KEY`. `GROK_API_KEY` is also accepted as an alias. **You do not need to set `XAI_MODEL` for real-time voice alone.** That variable enables an additional model for text questions and answers over HTTP.

For a temporary configuration, enter the key in PowerShell and launch Unreal from the same window. Close any open instance of the project first, then run:

```powershell
$grokSecret = Read-Host 'Enter your xAI API key' -AsSecureString
$env:XAI_API_KEY = [System.Net.NetworkCredential]::new('', $grokSecret).Password
Remove-Variable grokSecret
$env:COSMO_AI_PROVIDER = 'auto'

# Replace this with the actual path to UnrealEditor.exe on your machine.
$unrealEditorPath = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe'
& $unrealEditorPath (Resolve-Path .\VFX.uproject).Path
```

You can also set `XAI_API_KEY` as a Windows user environment variable. The service reads the saved configuration at startup. Store keys in environment variables; avoid writing them into source code or committing them to the repository.

After clicking **START MISSION** in the main menu:

- Select **CONFIRM** to enter the game directly and talk freely with COSMO.
- Select **CANCEL** to begin the Grok opening conversation and 9 interactive tutorial tasks, then enter the same level. The tutorial reads each instruction aloud, waits for the correct action, plays feedback, and continues.

If the voice connection or playback fails, the interface displays the reason. Press Enter to retry or Esc to return to the main menu. Once in the level, wait for voice status to indicate it is ready before speaking. For example, ask "COSMO, how do I use the shield?" or "How should I fight the boss?"

The default voice model and voice are configured in `Scripts/runtime-defaults.json`. Override them with the `XAI_VOICE_MODEL` and `XAI_VOICE` environment variables. See [COSMO Integration](COSMO_INTEGRATION.md) for more information on connections, debugging, and the server.

## Troubleshooting

| Issue | Solution |
| --- | --- |
| Build reports `Missing COSMO runtime` | Complete the runtime build steps first and verify that `Plugins/CosmoBridge/Resources/Runtime/Win64/manifest.json` exists. |
| Runtime builder reports a Python version mismatch | Create the build environment with CPython **3.12.10 x64** and install the locked dependencies. |
| Runtime builder reports `Output exists` | Back up the existing output and rebuild as described in [Runtime Deployment](RUNTIME_DEPLOYMENT.md). |
| Webcam does not open or gestures do not respond | Check Windows camera permissions, close apps using the device, and inspect `Saved/Logs/GestureRecognition.log`. Tutorial recognition logs are written to `TutorialGestureRecognition.log`. |
| Wrong camera, reversed hands, or reversed turning | Adjust `CameraIndex`, `SwapHands`, and `InvertHeadTurn` in the character's `FirstPersonAIIntegration` component. For the tutorial, adjust the corresponding settings under `Tutorial/Camera` in `BP_PlanetHUD`. |
| Introductory tutorial is stuck on the voice connection screen | Check the API key, network, and audio devices, and inspect `Saved/Logs/CosmoService.log`. If voice is not configured, return to the main menu and choose to skip the tutorial. |
| You want to try the game without Grok | Select **CONFIRM** in the **SKIP TUTORIAL?** dialog. |

The default local ports are COSMO HTTP `8765`, real-time voice `8766`, webcam preview `8767`, and gesture UDP `5052`. Stop other test instances to avoid conflicts over ports or the webcam.

## Project Structure

| Path | Contents |
| --- | --- |
| `VFX.uproject` / `Config/` | Unreal project entry point and default configuration. |
| `Content/` | Imported levels, characters, Blueprints, UI, and game assets. |
| `Plugins/CosmoBridge/` | Combat and level logic, COSMO service management, voice, and HUD. |
| `Plugins/GestureBridge/` | Receives recognition results and passes actions to Unreal. |
| `dubhack_gestures/` | Hand / face recognition, gesture detection, configuration, models, and tests. |
| `External/CosmoSystem/` | COSMO Python service, conversation logic, and tutorial logic. |
| `External/` | Original artwork, animations, UI, and assets available for reimport. |
| `Scripts/` | Scripts for runtime building, service startup, asset import, and verification. |

The repository includes source code, levels, game assets, and original recognition models. `.gitignore` excludes Unreal build artifacts, caches, logs, automatic backups, Python virtual environments, and the standalone runtime that can be regenerated.

When distributing the Windows game, provide the complete packaged directory, including the bundled runtime. Run `Scripts/BuildWindows.ps1` to generate the Shipping game directory, a ZIP archive, and checksum files. See [Windows Standalone Packaging and Validation](WINDOWS_PACKAGING.md) and [Runtime Deployment](RUNTIME_DEPLOYMENT.md) for details.

## Developer Documentation

- [Gesture Configuration and Recognition Diagnostics](GESTURE_ONLY.md)
- [COSMO, Real-Time Voice, and APIs](COSMO_INTEGRATION.md)
- [AI Character Integration](BP_FIRST_PERSON_AI.md)
- [Level Design and Combat Parameters](ALIEN_PLANET_LEVEL.md)
- [Creature Animation Assets](CREATURE_ANIMATIONS.md)
- [Current Menu and Interactive Tutorial Flow](Content/UI/README.txt)

Some topic-specific documents retain notes from earlier implementations. Refer to this README and the current source code for the current entry point, tutorial choices, victory conditions, and setup instructions.
