# COSMO System (Python mock)

Python stand-in for the Unreal + Grok voice guide COSMO. The Unreal project is not available here, so gameplay events are mocked and COSMO runs locally.

This repo includes:

- **Game state** — authoritative flags updated only by gameplay events
- **Grok function calling** — tools the voice loop may invoke
- **Voice guidance** — short, friendly tutorial lines
- **Dynamic AI Tutorial Guide** — JSON-configured, event-driven onboarding

## Dynamic AI Tutorial Guide

`TutorialManager` owns tutorial state in Python. Grok prompts do **not** encode the stage graph.

Tracked state:

- current stage
- completed stages
- expected player action (gameplay event name)
- available hints
- completion flag

Configured stages live in [`data/tutorial_stages.json`](data/tutorial_stages.json):

1. Welcome to the Unknown Planet — `game_started`
2. Basic Movement — `movement_completed`
3. Shield Tutorial — `shield_activated`
4. Fire Tutorial — `fire_activated`
5. Wave Tutorial — `wave_activated`
6. Laser Tutorial — `laser_activated`
7. Enemy Scanning — `enemy_scanned`
8. Boss Battle Preparation — `boss_encountered`
9. Tutorial Complete — `tutorial_completed`

The manager advances **only** when the expected event arrives. Player speech such as “I already used my shield” never completes a stage.

### Function calling

Bound to the existing registry (`companion/function_calling.py`):

- `get_game_state()`
- `get_tutorial_status()`
- `get_current_tutorial_step()`
- `get_available_tutorial_hint()`

### COSMO animation states

Structured `companion_animation` events (for later Unreal hooks):

| State | When |
| --- | --- |
| `speaking` | Giving an instruction |
| `listening` | Player is talking |
| `scanning` | Enemy analysis step |
| `celebrating` | Stage completed |
| `warning` | Player in danger |


### Confirmed combat abilities and controls

The only supported combat abilities are **Shield, Fire, Wave, and Laser**.
Movement and enemy scanning are separate tutorial activities. Each stage waits
for its own gameplay event; duplicate, unrelated, and unsupported events cannot
skip stages. Unsupported event names leave Game State unchanged.

`data/tutorial_stages.json` contains `ability_controls` with one key for each
ability. This UE project configures those values from its confirmed GestureBridge
bindings. If a gameplay control changes, replace only that ability's value with
the exact new instruction and restart the server. Instructions,
transition lines, hints, and repeated guidance all use the same configured text.
COSMO does not implement gesture recognition or guess hand poses.

`abilities` in Game State describes availability. `ability_confirmations`
separately records whether a real/mock gameplay event confirmed each ability's
use. Availability does not mean success. An out-of-order activation can be
recorded by Game State, but it does not satisfy a later tutorial stage; a new
confirmation is needed when that stage is current. The existing `shield_up`
and `fire_cast` flags remain as compatibility fields updated by the canonical
activation events. They do not establish ongoing effects or duration.

The mock defaults for planet, health, mission, target, and boss health follow
the supplied game-state example. Oxygen, mission progress, and Star Core energy
are optional and default to `null` (unknown); gameplay can supply their values.
No damage, cooldown, effect, weakness, cost, or other combat rule is invented.

`data/companion_instructions.txt` contains the personality and behavior rules
for the future Grok connection. `GrokFunctionCallingClient.system_prompt` loads
these instructions, and `tool_schemas()` exposes the bound tools, including
`get_game_state`. Pass that prompt and those schemas to the real transport when
integrating Grok. The local mock enforces matching response rules without an API
key: unsupported ability questions are declined, and mission/combat context is
read from the current Game State.

### Browser Voice Settings

The existing tester includes browser voice selection, Test Voice, Stop Speaking,
and rate/pitch/volume sliders. Preferences persist in the browser; automatic
voice replies can still be toggled off. These controls apply only to browser TTS
for local testing and do not change Grok Realtime Voice API voices.

## Quick start

```bash
cd cosmo-system
python3 -m pip install -e ".[dev]"
python -m pytest -v
```

### CLI mock events

```bash
python3 -m companion open
python3 -m companion event game_started
python3 -m companion event movement_completed
python3 -m companion say "Repeat that"
python3 -m companion say "How do I use my shield?"
python3 -m companion event shield_activated
python3 -m companion status
```

Use HTTP so session state persists (each CLI command is a new process):

```bash
PYTHONPATH=. python3 -m companion serve --port 8765
```

Open the tester (microphone + event buttons): [http://127.0.0.1:8765/](http://127.0.0.1:8765/)

| What | URL |
| --- | --- |
| Voice + event UI | http://127.0.0.1:8765/ |
| Health | http://127.0.0.1:8765/health |
| Tutorial status | http://127.0.0.1:8765/tutorial/status |
| Current step | http://127.0.0.1:8765/tutorial/step |
| Next hint | http://127.0.0.1:8765/tutorial/hint |
| Full snapshot | http://127.0.0.1:8765/snapshot |

```bash
curl -s http://127.0.0.1:8765/tutorial/status
curl -s -X POST http://127.0.0.1:8765/events \
  -H 'Content-Type: application/json' \
  -d '{"event":"shield_activated"}'
curl -s -X POST http://127.0.0.1:8765/player/utterance \
  -H 'Content-Type: application/json' \
  -d '{"text":"What should I do?"}'
```

### Microphone (browser)

1. Use Chrome or Edge on `http://127.0.0.1:8765/` (Safari often works too).
2. Allow microphone access when the browser asks.
3. Hold **Hold to talk**, say e.g. “What should I do?” or “How do I use my shield?”, then release.
4. COSMO replies in text and (unless muted) spoken audio via the browser’s speech synthesis.
5. Click gameplay event buttons to advance the tutorial. Saying “I already moved” does **not** complete movement.

There is no xAI API key in this environment, so spoken replies come from the local Grok function-calling mock (same tools: status / step / hint). Plug in a real Grok client later without changing `TutorialManager`.

Unreal integration notes: [`docs/unreal-integration.md`](docs/unreal-integration.md).
