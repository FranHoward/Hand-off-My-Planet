from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class TutorialStage:
    id: str
    name: str
    order: int
    expected_event: str
    instruction: str
    completion_line: str
    companion_state: str
    hints: tuple[str, ...] = ()
    help_topics: dict[str, str] = field(default_factory=dict)
    ability: str | None = None

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "TutorialStage":
        return cls(
            id=data["id"],
            name=data["name"],
            order=int(data["order"]),
            expected_event=data["expected_event"],
            instruction=data["instruction"],
            completion_line=data["completion_line"],
            companion_state=data.get("companion_state", "speaking"),
            hints=tuple(data.get("hints") or ()),
            help_topics=dict(data.get("help_topics") or {}),
            ability=data.get("ability"),
        )


@dataclass
class TutorialDefinition:
    id: str
    title: str
    description: str
    stages: tuple[TutorialStage, ...]
    ability_controls: dict[str, str | None] = field(default_factory=dict)

    def stage_by_id(self, stage_id: str) -> TutorialStage:
        for stage in self.stages:
            if stage.id == stage_id:
                return stage
        raise KeyError(stage_id)

    def first_stage(self) -> TutorialStage:
        return self.stages[0]


@dataclass
class TutorialStatus:
    tutorial_id: str
    current_stage_id: str | None
    current_stage_name: str | None
    completed_stage_ids: list[str]
    expected_event: str | None
    available_hints: list[str]
    is_complete: bool
    last_instruction: str | None
    last_hint: str | None

    def to_dict(self) -> dict[str, Any]:
        return {
            "tutorial_id": self.tutorial_id,
            "current_stage_id": self.current_stage_id,
            "current_stage_name": self.current_stage_name,
            "completed_stage_ids": list(self.completed_stage_ids),
            "expected_event": self.expected_event,
            "available_hints": list(self.available_hints),
            "is_complete": self.is_complete,
            "last_instruction": self.last_instruction,
            "last_hint": self.last_hint,
        }


@dataclass
class TutorialEventResult:
    accepted: bool
    advanced: bool
    ignored_reason: str | None
    completed_stage_id: str | None
    next_stage_id: str | None
    is_complete: bool
    companion_state: str
    spoken_line: str | None
    animation_event: dict[str, Any] | None

    def to_dict(self) -> dict[str, Any]:
        return {
            "accepted": self.accepted,
            "advanced": self.advanced,
            "ignored_reason": self.ignored_reason,
            "completed_stage_id": self.completed_stage_id,
            "next_stage_id": self.next_stage_id,
            "is_complete": self.is_complete,
            "companion_state": self.companion_state,
            "spoken_line": self.spoken_line,
            "animation_event": self.animation_event,
        }
