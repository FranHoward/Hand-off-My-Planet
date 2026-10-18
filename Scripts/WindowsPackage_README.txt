Hand off My Planet - Windows x64

Starting the Game
1. Extract the entire archive to a local directory, then open VFX.exe in the Windows folder.
2. If the first launch reports a missing Microsoft Visual C++ runtime, run
   Engine\Extras\Redist\en-us\vc_redist.x64.exe, then launch the game again.
3. START MISSION -> CONFIRM skips the voice tutorial and starts the game directly, without an API key.
4. Allow Windows desktop apps to access the webcam. Keep your face and both hands visible, face the camera, and hold steady for about one second to calibrate.

This package includes game levels, characters, animations, Niagara effects, UI, runtime plugins, standalone Python, dependencies, and local recognition models.
Unreal Engine, Python, Visual Studio, and a development environment do not need to be installed. Keep the entire directory structure intact.
A graphics card supporting DirectX 12 / Shader Model 6 and the appropriate driver are required.

Gestures and Menus
Push the open left hand forward to advance; pull it back to move backward; make a left fist to stop.
Turn your head left/right to turn. Right open palm: Shield; push a fist: Fire; sideways swipe: Wave; pinch: Laser.
Hold only the right index finger out to scan. Hold V signs with both hands for 1.5 seconds to recalibrate.
Use the mouse for menus, or arrow keys / Tab and Enter. Esc pauses / resumes.
After death or victory, hold both palms open for 1.5 seconds to retry / restart.

Optional Voice
Set your own XAI_API_KEY in Windows user environment variables (GROK_API_KEY is also supported), then restart the game.
Voice requires internet access, a microphone, and valid xAI service access. Without a key, you can still skip the tutorial for local exploration and combat.
Set COSMO_AI_PROVIDER=mock to force the local offline service.
Do not write keys into the distribution directory.

Configuration and Logs
Runtime: VFX\Plugins\CosmoBridge\Resources\Runtime\Win64\
Default service configuration: runtime.json in the directory above.
Gesture configuration: app\gestures\recognition_settings.json in the directory above.
Binding configuration: VFX\Plugins\GestureBridge\Resources\gesture_bindings.json.
Configuration and resources are protected by manifest.json hash checks. Changes require the build maintainer to regenerate the runtime.
Runtime logs: %LOCALAPPDATA%\COSMO\Logs.
Local ports: HTTP 8765, voice 8766, webcam preview 8767, gesture UDP 5052.
Closing the game cleans up services started by that session and releases the webcam. Avoid running multiple instances simultaneously.

Automated validation uses the local mock service without capturing webcam or microphone input. Human gestures and voice require acceptance testing on the target computer.
Python and third-party component licenses are retained in their corresponding runtime component directories.
