from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any
import math


SUPPORTED_ABILITIES = ("shield", "fire", "wave", "laser")
ABILITY_EVENTS = {f"{ability}_activated": ability for ability in SUPPORTED_ABILITIES}

TUTORIAL_EVENTS = (
    "game_started",
    "movement_completed",
    "shield_activated",
    "fire_activated",
    "wave_activated",
    "laser_activated",
    "enemy_scanned",
    "boss_encountered",
    "tutorial_completed",
)

KNOWN_EVENTS = TUTORIAL_EVENTS + (
    "player_damaged",
    "player_in_danger",
    "danger_cleared",
)


@dataclass
class GameState:
    """Authoritative mock of Unreal gameplay flags."""

    started: bool = False
    moved: bool = False
    shield_up: bool = False
    fire_cast: bool = False
    enemy_scanned: bool = False
    boss_encountered: bool = False
    tutorial_finished: bool = False
    in_danger: bool = False
    event_log: list[str] = field(default_factory=list)
    planet: str = "Kepler-X"
    player_health: float = 100
    current_mission: str = "Find the Star Core"
    abilities: dict[str, bool] = field(default_factory=lambda: dict.fromkeys(SUPPORTED_ABILITIES, True))
    ability_confirmations: dict[str, bool] = field(default_factory=lambda: dict.fromkeys(SUPPORTED_ABILITIES, False))
    current_target: str | None = "Ancient Guardian"
    boss_health: float | None = 100
    # No values/scales for these fields have been confirmed by the gameplay team.
    oxygen: float | None = None
    mission_progress: float | None = None
    star_core_energy: float | None = None

    def apply_event(self, event_name: str) -> dict[str, Any]:
        event = (event_name or "").strip()
        if event not in KNOWN_EVENTS:
            return self.to_dict()
        self.event_log.append(event)
        if event in ABILITY_EVENTS:
            self.ability_confirmations[ABILITY_EVENTS[event]] = True
        mapping = {
            "game_started": ("started", True),
            "movement_completed": ("moved", True),
            "shield_activated": ("shield_up", True),
            "fire_activated": ("fire_cast", True),
            "enemy_scanned": ("enemy_scanned", True),
            "boss_encountered": ("boss_encountered", True),
            "tutorial_completed": ("tutorial_finished", True),
            "player_in_danger": ("in_danger", True),
            "player_damaged": ("in_danger", True),
            "danger_cleared": ("in_danger", False),
        }
        if event in mapping:
            attr, value = mapping[event]
            setattr(self, attr, value)
        return self.to_dict()

    def to_dict(self) -> dict[str, Any]:
        return {
            "started": self.started,
            "moved": self.moved,
            "shield_up": self.shield_up,
            "fire_cast": self.fire_cast,
            "enemy_scanned": self.enemy_scanned,
            "boss_encountered": self.boss_encountered,
            "tutorial_finished": self.tutorial_finished,
            "in_danger": self.in_danger,
            "last_event": self.event_log[-1] if self.event_log else None,
            "planet": self.planet,
            "player_health": self.player_health,
            "current_mission": self.current_mission,
            # Availability is distinct from gameplay-confirmed use.
            "abilities": {name: bool(self.abilities.get(name, False)) for name in SUPPORTED_ABILITIES},
            "ability_confirmations": {name: bool(self.ability_confirmations.get(name, False)) for name in SUPPORTED_ABILITIES},
            "current_target": self.current_target,
            "boss_health": self.boss_health,
            "oxygen": self.oxygen,
            "mission_progress": self.mission_progress,
            "star_core_energy": self.star_core_energy,
        }

    def update_live_state(self, payload: dict[str, Any]) -> dict[str, Any]:
        """UE telemetry updates facts, never tutorial flags or confirmed-use history."""
        boolean_fields = {"shield_up", "in_danger"}
        number_fields = {"player_health", "boss_health", "oxygen", "mission_progress", "star_core_energy"}
        string_fields = {"planet", "current_mission", "current_target"}
        allowed = boolean_fields | number_fields | string_fields | {"abilities"}
        if not isinstance(payload, dict) or set(payload) - allowed:
            raise ValueError("Unsupported live game state fields")
        for name, value in payload.items():
            if name in boolean_fields and not isinstance(value, bool):
                raise ValueError(f"{name} must be boolean")
            if name in number_fields:
                if value is None and name != "player_health":
                    continue
                if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                    raise ValueError(f"{name} must be a finite non-negative number")
            if name in string_fields and not (isinstance(value, str) or (name == "current_target" and value is None)):
                raise ValueError(f"{name} must be text")
            if name == "abilities":
                if not isinstance(value, dict) or set(value) - set(SUPPORTED_ABILITIES) or not all(isinstance(v, bool) for v in value.values()):
                    raise ValueError("abilities must contain supported ability booleans")
        # Validate the whole payload before mutation.
        for name, value in payload.items():
            if name == "abilities":
                self.abilities.update(value)
            else:
                setattr(self, name, value)
        return self.to_dict()
