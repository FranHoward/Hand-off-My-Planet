# VFX Recognition Client (Gesture and Face Interaction Only)

This directory comes from the supplied Python archive and has been integrated with the current UE project.

See [GESTURE_ONLY.md](../GESTURE_ONLY.md) for complete startup, gestures, configuration, and verification. UE Play starts recognition automatically; no manual keyboard control of the recognition window is needed.

- `gesture_recognition.py`: MediaPipe hand/face models, automatic calibration, UDP, and diagnostic preview.
- `game_controls.py`, `gesture_logic.py`: retain the supplied archive's recognition fixes, including fist-to-stop, pinch laser, and head turning from the face rotation matrix.
- `interaction_controls.py`: adds scanning, hints, repeats, retries, and two-hand V calibration, sharing incrementing event IDs with combat.
- `recognition_settings.json`: gesture thresholds; `gesture_bindings.json`: UE action switches, cooldowns, and local port. The key must be empty.
- `Unreal/Plugins/GestureBridge`: a source copy matching the actual project plugin.

For manual preview, first disable AutoLaunchRecognition on the UE component, then run `python gesture_recognition.py --udp-port 5052`. Without udp-port, recognition stays local.
