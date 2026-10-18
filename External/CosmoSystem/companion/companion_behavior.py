from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


class CompanionState:
    IDLE = "idle"
    LISTENING = "listening"
    SPEAKING = "speaking"
    SCANNING = "scanning"
    CELEBRATING = "celebrating"
    WARNING = "warning"


ANIMATION_FOR_STATE = {
    CompanionState.IDLE: "idle_loop",
    CompanionState.LISTENING: "listen_lean_in",
    CompanionState.SPEAKING: "talk_gesture",
    CompanionState.SCANNING: "scan_pulse",
    CompanionState.CELEBRATING: "celebrate",
    CompanionState.WARNING: "warn_point",
}


@dataclass
class CompanionBehavior:
    """Structured companion states for later Unreal animation hooks."""

    state: str = CompanionState.IDLE
    events: list[dict[str, Any]] = field(default_factory=list)

    def set_state(
        self,
        state: str,
        *,
        reason: str,
        line: str | None = None,
        extra: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        if state not in ANIMATION_FOR_STATE:
            raise ValueError(f"Unknown companion state: {state}")
        self.state = state
        payload = {
            "event_type": "companion_animation",
            "state": state,
            "animation": ANIMATION_FOR_STATE[state],
            "reason": reason,
            "line": line,
        }
        if extra:
            payload.update(extra)
        self.events.append(payload)
        return payload

    def speaking(self, line: str, reason: str = "instruction") -> dict[str, Any]:
        return self.set_state(CompanionState.SPEAKING, reason=reason, line=line)

    def celebrating(self, line: str, stage_id: str | None = None) -> dict[str, Any]:
        return self.set_state(
            CompanionState.CELEBRATING,
            reason="stage_complete",
            line=line,
            extra={"stage_id": stage_id},
        )

    def scanning(self, line: str | None = None) -> dict[str, Any]:
        return self.set_state(CompanionState.SCANNING, reason="enemy_analysis", line=line)

    def warning(self, line: str) -> dict[str, Any]:
        return self.set_state(CompanionState.WARNING, reason="player_in_danger", line=line)

    def listening(self) -> dict[str, Any]:
        return self.set_state(CompanionState.LISTENING, reason="player_speech")

    def snapshot(self) -> dict[str, Any]:
        return {
            "state": self.state,
            "animation": ANIMATION_FOR_STATE[self.state],
            "event_count": len(self.events),
            "last_event": self.events[-1] if self.events else None,
        }
