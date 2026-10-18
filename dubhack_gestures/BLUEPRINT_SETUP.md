# Blueprint Wiring in the Current Project

This project is already wired; no keyboard mapping is needed. See [GESTURE_ONLY.md](../GESTURE_ONLY.md) for full controls.

`GestureJsonReceiver` events in `BP_FirstPersonCharacterAI` connect to `FirstPersonAIIntegration`:

| Receiver Event | Integration Handler |
|---|---|
| OnAxes | HandleGestureAxes |
| OnActionPressed | HandleGesturePressed |
| OnActionHeld | HandleGestureHeld |
| OnActionReleased | HandleGestureReleased |
| OnConnectionChanged | HandleGestureConnection |

Continuous actions: MoveForward, MoveBackward, TurnLeft, TurnRight, Shield, Laser.
One-shot actions: Fire, Wave, Scan, Hint, Repeat, Retry.

Actions dispatch by name. KeyName remains as a compatibility pin with value None; the configuration's key is empty and does not trigger keyboard input. Retry also works after death or victory, bypassing the CanAct gate that prevents combat.

The receiver listens only on 127.0.0.1:5052. The automatic recognition process starts at the local player's BeginPlay and ends at EndPlay; command-line automated tests do not occupy the webcam.

The `CodexGestureOnly` editor command removes hardware input nodes, clears old mappings, and updates tutorial labels. `-Verify` performs a read-only audit. The actual project plugin is in `Plugins/GestureBridge`; this directory's Unreal subdirectory is a source copy.
