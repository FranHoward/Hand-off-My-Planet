from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from companion.game_state import GameState
from companion.tutorial.manager import TutorialManager

# OpenAI / xAI compatible tool schemas. Grok never owns tutorial state.
TUTORIAL_TOOLS: list[dict[str, Any]] = [
    {
        "type": "function",
        "function": {
            "name": "get_tutorial_status",
            "description": "Return current tutorial stage, completed stages, expected gameplay event, and completion flag.",
            "parameters": {"type": "object", "properties": {}, "additionalProperties": False},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_current_tutorial_step",
            "description": "Return the current tutorial instruction and whether it was already spoken.",
            "parameters": {"type": "object", "properties": {}, "additionalProperties": False},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_available_tutorial_hint",
            "description": "Return the next unused contextual hint for the current tutorial stage.",
            "parameters": {"type": "object", "properties": {}, "additionalProperties": False},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "get_game_state",
            "description": "Return authoritative game state, Shield/Fire/Wave/Laser availability, and separately confirmed activations. Availability does not imply successful use.",
            "parameters": {"type": "object", "properties": {}, "additionalProperties": False},
        },
    },
]


@dataclass
class ToolCall:
    name: str
    arguments: dict[str, Any]
    result: Any


class FunctionRegistry:
    def __init__(self) -> None:
        self._handlers: dict[str, Callable[..., Any]] = {}
        self.schemas: list[dict[str, Any]] = []

    def register(self, schema: dict[str, Any], handler: Callable[..., Any]) -> None:
        name = schema["function"]["name"]
        self._handlers[name] = handler
        self.schemas.append(schema)

    def call(self, name: str, arguments: dict[str, Any] | None = None) -> ToolCall:
        if name not in self._handlers:
            raise KeyError(f"Unknown function: {name}")
        result = self._handlers[name](**(arguments or {}))
        return ToolCall(name=name, arguments=arguments or {}, result=result)

    def names(self) -> list[str]:
        return list(self._handlers)


def bind_tutorial_tools(
    registry: FunctionRegistry, manager: TutorialManager, game_state: GameState | None = None
) -> FunctionRegistry:
    game = game_state if game_state is not None else GameState()
    registry.register(TUTORIAL_TOOLS[0], lambda: manager.get_status().to_dict())
    registry.register(TUTORIAL_TOOLS[1], lambda: manager.get_current_step())
    registry.register(TUTORIAL_TOOLS[2], lambda: manager.get_available_hint())
    registry.register(TUTORIAL_TOOLS[3], game.to_dict)
    return registry
