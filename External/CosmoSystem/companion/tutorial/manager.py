from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from companion.game_state import ABILITY_EVENTS, SUPPORTED_ABILITIES, TUTORIAL_EVENTS
from companion.tutorial.models import (
    TutorialDefinition,
    TutorialEventResult,
    TutorialStage,
    TutorialStatus,
)

DEFAULT_STAGES_PATH = (
    Path(__file__).resolve().parents[2] / "data" / "tutorial_stages.json"
)


def load_tutorial_definition(path: str | Path | None = None) -> TutorialDefinition:
    source = Path(path) if path else DEFAULT_STAGES_PATH
    payload = json.loads(source.read_text(encoding="utf-8"))
    stages = tuple(
        TutorialStage.from_dict(item)
        for item in sorted(payload["stages"], key=lambda row: int(row["order"]))
    )
    if not stages:
        raise ValueError("Tutorial definition must include at least one stage")
    if tuple(stage.expected_event for stage in stages) != TUTORIAL_EVENTS:
        raise ValueError("Tutorial events must follow the confirmed nine-stage sequence")
    if len({stage.id for stage in stages}) != len(stages) or len({stage.order for stage in stages}) != len(stages):
        raise ValueError("Tutorial stage IDs and orders must be unique")
    for stage in stages:
        if stage.ability != ABILITY_EVENTS.get(stage.expected_event):
            raise ValueError("Ability stages must match the confirmed ability event")
    controls = payload.get("ability_controls") or {}
    if not isinstance(controls, dict) or set(controls) - set(SUPPORTED_ABILITIES):
        raise ValueError("Controls may only configure Shield, Fire, Wave, and Laser")
    if any(value is not None and not isinstance(value, str) for value in controls.values()):
        raise ValueError("Each ability control must be confirmed instruction text or null")
    return TutorialDefinition(
        id=payload["id"],
        title=payload.get("title", payload["id"]),
        description=payload.get("description", ""),
        stages=stages,
        ability_controls={name: controls.get(name) for name in SUPPORTED_ABILITIES},
    )


class TutorialManager:
    """State-driven tutorial. Advances only on matching gameplay events."""

    def __init__(self, definition: TutorialDefinition | None = None, path: str | Path | None = None):
        self.definition = definition or load_tutorial_definition(path)
        self._index = 0
        self._completed: list[str] = []
        self._complete = False
        self._hint_index = 0
        self._last_instruction: str | None = None
        self._last_hint: str | None = None
        self._instruction_spoken_for_stage: str | None = None

    @property
    def current_stage(self) -> TutorialStage | None:
        if self._complete:
            return None
        return self.definition.stages[self._index]

    def control_instruction(self, ability: str) -> str:
        if ability not in SUPPORTED_ABILITIES:
            raise KeyError(ability)
        configured = self.definition.ability_controls.get(ability)
        if configured and configured.strip():
            return configured.strip()
        return (
            f"Use the game's assigned {ability.title()} control to activate {ability.title()} once. "
            "The exact gesture is not configured yet."
        )

    def _render(self, text: str) -> str:
        for ability in SUPPORTED_ABILITIES:
            text = text.replace("{" + ability + "_control}", self.control_instruction(ability))
        return text

    def get_status(self) -> TutorialStatus:
        stage = self.current_stage
        return TutorialStatus(
            tutorial_id=self.definition.id,
            current_stage_id=None if stage is None else stage.id,
            current_stage_name=None if stage is None else stage.name,
            completed_stage_ids=list(self._completed),
            expected_event=None if stage is None else stage.expected_event,
            available_hints=[] if stage is None else [self._render(hint) for hint in stage.hints],
            is_complete=self._complete,
            last_instruction=self._last_instruction,
            last_hint=self._last_hint,
        )

    def get_current_step(self) -> dict[str, Any]:
        stage = self.current_stage
        if stage is None:
            return {
                "is_complete": True,
                "stage_id": None,
                "name": None,
                "instruction": "Tutorial complete. You're cleared for the unknown, explorer!",
                "expected_event": None,
                "already_spoken": True,
            }
        return {
            "is_complete": False,
            "stage_id": stage.id,
            "name": stage.name,
            "instruction": self._render(stage.instruction),
            "expected_event": stage.expected_event,
            "already_spoken": self._instruction_spoken_for_stage == stage.id,
            "companion_state": stage.companion_state,
            "ability": stage.ability,
            "control_instruction": self.control_instruction(stage.ability) if stage.ability else None,
            "control_configured": bool((self.definition.ability_controls.get(stage.ability) or "").strip()),
        }

    def get_available_hint(self) -> dict[str, Any]:
        stage = self.current_stage
        if stage is None:
            hint = "You're done with the lesson. Explore freely."
            self._last_hint = hint
            return {"hint": hint, "stage_id": None, "exhausted": True}
        if not stage.hints:
            hint = self._render(stage.instruction)
            self._last_hint = hint
            return {"hint": hint, "stage_id": stage.id, "exhausted": True}
        hint = self._render(stage.hints[self._hint_index % len(stage.hints)])
        self._hint_index += 1
        self._last_hint = hint
        return {
            "hint": hint,
            "stage_id": stage.id,
            "exhausted": self._hint_index >= len(stage.hints),
        }

    def topic_help(self, topic: str) -> str | None:
        key = topic.lower().strip()
        # Ability help remains available at every stage, including after completion.
        if key in SUPPORTED_ABILITIES:
            for ability_stage in self.definition.stages:
                if ability_stage.ability == key:
                    return self._render(ability_stage.help_topics.get(key, ability_stage.instruction))
        stage = self.current_stage
        if stage is None:
            return None
        if key in stage.help_topics:
            return self._render(stage.help_topics[key])
        for stored, text in stage.help_topics.items():
            if stored in key or key in stored:
                return self._render(text)
        return None

    def mark_instruction_spoken(self, line: str | None = None) -> None:
        stage = self.current_stage
        spoken = line or (self._render(stage.instruction) if stage else None)
        if spoken:
            self._last_instruction = spoken
        if stage is not None:
            self._instruction_spoken_for_stage = stage.id

    def repeat_instruction(self) -> str:
        stage = self.current_stage
        if stage is None:
            line = "Tutorial complete. You're cleared for the unknown, explorer!"
            self._last_instruction = line
            return line
        instruction = self._render(stage.instruction)
        self.mark_instruction_spoken(instruction)
        return instruction

    def handle_game_event(self, event_name: str) -> TutorialEventResult:
        event = (event_name or "").strip()
        stage = self.current_stage
        if self._complete or stage is None:
            return TutorialEventResult(
                accepted=False,
                advanced=False,
                ignored_reason="tutorial_already_complete",
                completed_stage_id=None,
                next_stage_id=None,
                is_complete=True,
                companion_state="idle",
                spoken_line=None,
                animation_event=None,
            )
        if event not in TUTORIAL_EVENTS or event != stage.expected_event:
            return TutorialEventResult(
                accepted=False,
                advanced=False,
                ignored_reason="unexpected_event",
                completed_stage_id=None,
                next_stage_id=stage.id,
                is_complete=False,
                companion_state=stage.companion_state,
                spoken_line=None,
                animation_event=None,
            )
        if stage.id in self._completed:
            return TutorialEventResult(
                accepted=False,
                advanced=False,
                ignored_reason="duplicate_completion",
                completed_stage_id=stage.id,
                next_stage_id=stage.id,
                is_complete=False,
                companion_state=stage.companion_state,
                spoken_line=None,
                animation_event=None,
            )

        self._completed.append(stage.id)
        spoken = self._render(stage.completion_line)
        completed_id = stage.id
        next_index = self._index + 1
        self._hint_index = 0
        self._last_hint = None
        self._instruction_spoken_for_stage = None

        if next_index >= len(self.definition.stages):
            self._complete = True
            self._last_instruction = spoken
            return TutorialEventResult(
                accepted=True,
                advanced=True,
                ignored_reason=None,
                completed_stage_id=completed_id,
                next_stage_id=None,
                is_complete=True,
                companion_state="celebrating",
                spoken_line=spoken,
                animation_event=_animation("celebrating", "celebrate_tutorial_complete", completed_id),
            )

        self._index = next_index
        nxt = self.definition.stages[self._index]
        # Announce only the next action using the same configurable instruction
        # returned by tools and repeats, rather than duplicating it in JSON.
        spoken = f"{spoken} {self._render(nxt.instruction)}"
        self._last_instruction = spoken
        self._instruction_spoken_for_stage = nxt.id
        return TutorialEventResult(
            accepted=True,
            advanced=True,
            ignored_reason=None,
            completed_stage_id=completed_id,
            next_stage_id=nxt.id,
            is_complete=False,
            companion_state="celebrating",
            spoken_line=spoken,
            animation_event=_animation("celebrating", "celebrate_stage_complete", completed_id),
        )


def _animation(state: str, animation: str, stage_id: str) -> dict[str, Any]:
    return {
        "event_type": "companion_animation",
        "state": state,
        "animation": animation,
        "stage_id": stage_id,
    }
