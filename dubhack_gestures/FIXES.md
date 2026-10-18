# Current Action Mapping Update (2026-10-17)

This update changes only Python recognition and JSON. Head yaw, left/right hand mirroring fixes, fist-then-push Fire, and sideways-swipe Wave retain their original implementations.

| Gesture | Current Result |
|---|---|
| Open left hand at the neutral position | NONE, movement axis 0, no shield |
| Push / pull the open left hand | FORWARD / BACKWARD |
| Left fist | STOP, immediately resets movement regardless of distance |
| Hold the right palm open and still | SHIELD |
| Make a right fist, then push | FIRE |
| Swipe the right hand sideways | WAVE |
| Pinch the right thumb and index fingertips together | LASER |
| Extend only the right index finger | No longer triggers Laser |

## Changed Files

- game_controls.py: left-fist stop takes priority over distance checks; fists are excluded from neutral calibration; brief unknown states after stopping do not resume movement. Shield returns to the right hand with a stillness check. Laser changes from POINT to PINCH, with aiming coordinates changed to pinch_center. Wave excludes pinches and pinch transitions.
- gesture_recognition.py: updates command help for left/right hand roles. The existing debug interface automatically displays the new results.
- test_game_controls.py / test_recognition_fixes.py: updates action mapping tests for the new requirements, retaining head, Fire, two-hand routing, and JSON regressions.
- test_stop_shield_pinch.py: adds 14 regression tests for stopping, invalidating distance, resuming after reopening the hand, stationary shielding, pinch aiming, release termination, and pointing without activation.
- README.md / VALIDATION.txt / sample_packet.json: synchronizes current controls and sample output.

JSON version remains 3, retaining existing event and state fields. left_hand=STOP, left_hand_actions=[STOP], movement=Stop, and move_axis=0 represent left-fist stop. STOP is excluded from held_actions to avoid treating it as sustained movement input; only still-valid right-hand abilities and head actions remain there.

At this stage, only camera recognition and JSON output were present, without game integration. The code outputs a stop command rather than directly modifying character speed.

## Retained Fixes from the Previous Update

Models infer on unmirrored frames; selfie mirroring is for preview only. Handedness follows the player's own left/right, independent of image position or result order.
Head angles use the pose matrix, with H to calibrate center, smoothing, hysteresis, a dead zone, and immediate NONE on face loss.
Fire remains fist-then-push, with brief NONE tolerance, scale confirmation, and no repeated firing while held.
Existing PINCH geometry classification and reference-landmark regressions remain, with no new models or dependencies.
