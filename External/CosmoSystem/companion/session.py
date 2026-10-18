from __future__ import annotations

from typing import Any

from companion.companion_behavior import CompanionBehavior
from companion.function_calling import FunctionRegistry, bind_tutorial_tools
from companion.game_state import GameState
from companion.tutorial.manager import TutorialManager
from companion.voice import GrokVoiceGuide, VoiceResponse
from companion.grok_client import create_client, XaiFunctionCallingClient


class CompanionSession:
    """In-memory session Unreal (or the mock CLI/HTTP layer) talks to."""

    def __init__(self, tutorial: TutorialManager | None = None, game_state: GameState | None = None):
        self.game_state = game_state if game_state is not None else GameState()
        self.tutorial = tutorial or TutorialManager()
        self.behavior = CompanionBehavior()
        self.functions = bind_tutorial_tools(FunctionRegistry(), self.tutorial, self.game_state)
        self.voice = GrokVoiceGuide(self.tutorial, self.functions, self.behavior)
        self.ai_client = create_client(self.voice, self.functions)
        self._opened = False

    def open(self) -> VoiceResponse:
        """Speak the first instruction without treating player words as completion."""
        self._opened = True
        return self.voice.speak_current_instruction(force=True)

    def ingest_game_event(self, event_name: str) -> dict[str, Any]:
        event_name = (event_name or "").strip()
        game = self.game_state.apply_event(event_name)
        if event_name in {"player_in_danger", "player_damaged"}:
            line = "The game reports danger. Check your current objective and confirmed game information."
            animation = self.behavior.warning(line)
            return {
                "game_state": game,
                "tutorial": {
                    "accepted": False,
                    "advanced": False,
                    "ignored_reason": "non_tutorial_event",
                    "is_complete": self.tutorial.get_status().is_complete,
                    "spoken_line": line,
                },
                "status": self.tutorial.get_status().to_dict(),
                "animation_event": animation,
                "spoken_line": line,
            }
        if event_name == "danger_cleared":
            line = "You're clear. Back to the lesson."
            animation = self.behavior.speaking(line, reason="danger_cleared")
            return {
                "game_state": game,
                "tutorial": self.tutorial.get_status().to_dict(),
                "animation_event": animation,
                "spoken_line": line,
            }

        result = self.tutorial.handle_game_event(event_name)
        animation = result.animation_event
        if result.advanced and result.spoken_line:
            animation = self.behavior.celebrating(
                result.spoken_line, stage_id=result.completed_stage_id
            )
            if result.next_stage_id:
                nxt = self.tutorial.definition.stage_by_id(result.next_stage_id)
                if nxt.companion_state == "scanning":
                    animation = self.behavior.scanning(result.spoken_line)
                elif nxt.companion_state == "warning":
                    animation = self.behavior.warning(result.spoken_line)
        return {
            "game_state": game,
            "tutorial": result.to_dict(),
            "status": self.tutorial.get_status().to_dict(),
            "animation_event": animation,
            "spoken_line": result.spoken_line,
        }

    def handle_player_speech(self, text: str) -> dict[str, Any]:
        """Voice only. Never advances tutorial stages."""
        if not self._opened:
            self._opened = True
        response = self.ai_client.reply_to_player(text)
        return {
            "player_text": text,
            "spoken_line": response["line"],
            "provider": "local_fallback" if "provider_error" in response else self.snapshot()["provider"],
            "advanced": False,
            "status": self.tutorial.get_status().to_dict(),
            **response,
        }

    def snapshot(self) -> dict[str, Any]:
        return {
            "provider": "xai" if isinstance(self.ai_client, XaiFunctionCallingClient) else "local",
            "model": getattr(self.ai_client, "model", None),
            "game_state": self.game_state.to_dict(),
            "tutorial": self.tutorial.get_status().to_dict(),
            "companion": self.behavior.snapshot(),
            "functions": self.functions.names(),
        }
