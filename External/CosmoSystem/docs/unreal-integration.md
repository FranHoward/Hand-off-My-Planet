# Unreal Engine integration

The Python COSMO service is the mock host until the Unreal project is available. Keep tutorial **state and progression in Python** (later: the same API on a game server or Unreal subsystem). Grok only **explains** the current step.

## Event contract

Unreal should POST gameplay confirmations. Do not infer completion from voice STT.

`POST /events`

```json
{ "event": "shield_activated" }
```

Supported tutorial events:

| Event | Completes stage |
| --- | --- |
| `game_started` | Welcome |
| `movement_completed` | Basic movement |
| `shield_activated` | Shield |
| `fire_activated` | Fire |
| `wave_activated` | Wave |
| `laser_activated` | Laser |
| `enemy_scanned` | Scan |
| `boss_encountered` | Boss prep |
| `tutorial_completed` | Tutorial complete |

Extra COSMO events (do not advance the tutorial):

- `player_in_danger` / `player_damaged` → warning animation + spoken line
- `danger_cleared` → return to speaking

## Voice

`POST /player/utterance`

```json
{ "text": "How do I use my shield?" }
```

The voice layer may call:

- `get_game_state` (live availability and confirmed activations)
- `get_tutorial_status`
- `get_current_tutorial_step`
- `get_available_tutorial_hint`

It must **not** call any “complete stage” function based on the transcript.

When you wire real Grok, use `GrokFunctionCallingClient.system_prompt` from
`data/companion_instructions.txt` as the system instructions and register the schemas in `companion/function_calling.py` (`TUTORIAL_TOOLS`) on the xAI / OpenAI-compatible tools list. Tool handlers should hit this same `TutorialManager` (or an HTTP facade in front of it).

## Ability and control contract

There are exactly four combat abilities: Shield, Fire, Wave, and Laser.
Use the canonical `*_activated` event names in the table. Unsupported event
names are ignored with `accepted: false` and `advanced: false`; they do not
mutate Game State. The existing `/game/event` and `/voice` aliases remain valid.

`GET /snapshot` includes the Game State also returned by `get_game_state`:

- `abilities`: availability for the four supported abilities.
- `ability_confirmations`: historical confirmed use, initially false for each.
- Existing gameplay flags, current mission/target, health, and environmental fields.

Confirmed use is distinct from availability and tutorial progress. An event
received at the wrong stage does not advance or pre-complete a future stage.
Send an activation event only after gameplay actually confirms activation;
neither a recognized phrase nor an attempted gesture is sufficient by itself.
No damage, cooldown, weakness, or combat effect is assigned by this mock.

The top-level `ability_controls` object in `data/tutorial_stages.json` stores
confirmed control instruction text for `shield`, `fire`, `wave`, and `laser`.
This UE integration fills them from the project's GestureBridge configuration.
Update them only when the gesture/gameplay team changes a control, then restart the Python host. The current-step
tool exposes `ability`, `control_instruction`, and `control_configured`.
Transitions, repeats, hints, and ability help use those same instructions.

Browser Voice Settings remain local test controls; actual Grok Realtime voice
selection and playback belong to the future Grok transport integration.

## Animation hook

Every spoken reply and accepted stage completion includes:

```json
{
  "event_type": "companion_animation",
  "state": "celebrating",
  "animation": "celebrate",
  "reason": "stage_complete",
  "line": "Movement confirmed. Use the game's assigned Shield control to activate Shield once. The exact gesture is not configured yet. I'll wait for gameplay confirmation.",
  "stage_id": "movement"
}
```

Map `state` / `animation` onto the COSMO AnimBP:

- `speaking` → `talk_gesture`
- `listening` → `listen_lean_in`
- `scanning` → `scan_pulse`
- `celebrating` → `celebrate`
- `warning` → `warn_point`

## Suggested Unreal wiring

1. Gameplay ability / input confirms locally (movement distance, shield tag, ability activate).
2. Blueprint or C++ calls a thin HTTP (or UDP) client: `POST /events`.
3. Read `spoken_line` + `animation_event` from the response and play Grok TTS / anim notify.
4. Push microphone transcripts to `POST /player/utterance` **without** mutating tutorial completion.
5. Poll `GET /tutorial/status` if UI widgets need the current objective.

Until Unreal is connected, use `python3 -m companion serve` or the CLI `event` command as the game.
