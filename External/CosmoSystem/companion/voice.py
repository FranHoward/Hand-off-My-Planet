from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any

from companion.companion_behavior import CompanionBehavior
from companion.function_calling import FunctionRegistry, ToolCall
from companion.game_state import SUPPORTED_ABILITIES
from companion.tutorial.manager import TutorialManager

REPEAT_PATTERNS = (
    r"\brepeat\b",
    r"\bsay that again\b",
    r"\bone more time\b",
    r"\bwhat did you say\b",
)
STATUS_PATTERNS = (
    r"what should i do",
    r"what'?s next",
    r"current (step|task|objective)",
    r"where (am i|are we)",
)
HINT_PATTERNS = (
    r"\bhint\b",
    r"\bconfused\b",
    r"\bstuck\b",
    r"\bhelp\b",
    r"how do i",
    r"i don'?t (know|understand)",
)
TOPIC_ALIASES = {
    "shield": "shield",
    "defend": "shield",
    "fire": "fire",
    "wave": "wave",
    "laser": "laser",
    "scan": "scan",
    "enemy": "enemy",
    "boss": "boss",
    "move": "move",
    "walk": "move",
    "danger": "danger",
}

# Negative recognition only: these terms are never offered as supported skills.
REMOVED_ABILITY_PATTERN = r"(?<![a-z])(?:ice|frost|freeze|freezing|frozen)(?![a-z])"
COMBAT_QUERY_PATTERN = r"\b(?:abilities|ability|skills?|spells?|magic|cast|combat|attacks?|gestures?|damage|cooldowns?)\b"
UNCONFIRMED_MECHANICS_PATTERN = r"\b(?:damage|cooldowns?|effects?|weakness(?:es)?|range|duration|cost|meter|stronger|strongest|best|does|burn|slow|knockback|pierce)\b"


@dataclass
class VoiceResponse:
    line: str
    tools_used: list[str]
    companion_state: str
    animation_event: dict[str, Any] | None
    used_existing_instruction: bool

    def to_dict(self) -> dict[str, Any]:
        return {
            "line": self.line,
            "tools_used": list(self.tools_used),
            "companion_state": self.companion_state,
            "animation_event": self.animation_event,
            "used_existing_instruction": self.used_existing_instruction,
        }


class GrokVoiceGuide:
    """Mock Grok voice loop: function calls first, then a short spoken line.

    Player speech never advances tutorial stages.
    """

    def __init__(
        self,
        tutorial: TutorialManager,
        registry: FunctionRegistry,
        behavior: CompanionBehavior,
    ):
        self.tutorial = tutorial
        self.registry = registry
        self.behavior = behavior

    def handle_utterance(self, text: str) -> VoiceResponse:
        self.behavior.listening()
        utterance = (text or "").strip().lower()
        calls = self._plan_tool_calls(utterance)
        results = [self.registry.call(name) for name in calls]
        return self._speak(utterance, results)

    def speak_current_instruction(self, *, force: bool = False) -> VoiceResponse:
        step = self.registry.call("get_current_tutorial_step").result
        if not force and step.get("already_spoken") and not step.get("is_complete"):
            hint = self.registry.call("get_available_tutorial_hint").result
            line = hint["hint"]
            event = self.behavior.speaking(line, reason="hint_instead_of_repeat")
            return VoiceResponse(line, ["get_current_tutorial_step", "get_available_tutorial_hint"], "speaking", event, True)
        line = step["instruction"]
        self.tutorial.mark_instruction_spoken(line)
        event = self._behavior_for_step(step, line, reason="instruction")
        return VoiceResponse(line, ["get_current_tutorial_step"], event["state"], event, False)

    def _plan_tool_calls(self, utterance: str) -> list[str]:
        if _matches(utterance, REPEAT_PATTERNS):
            return ["get_game_state", "get_current_tutorial_step"]
        if _matches(utterance, HINT_PATTERNS):
            return ["get_game_state", "get_tutorial_status", "get_available_tutorial_hint"]
        if _matches(utterance, STATUS_PATTERNS):
            return ["get_game_state", "get_tutorial_status", "get_current_tutorial_step"]
        return ["get_game_state", "get_tutorial_status", "get_current_tutorial_step"]

    def _speak(self, utterance: str, results: list[ToolCall]) -> VoiceResponse:
        by_name = {call.name: call.result for call in results}
        tools_used = [call.name for call in results]
        step = by_name.get("get_current_tutorial_step") or self.tutorial.get_current_step()
        status = by_name.get("get_tutorial_status") or self.tutorial.get_status().to_dict()
        game = by_name["get_game_state"]
        topic = _detect_topic(utterance)
        available = [name.title() for name in SUPPORTED_ABILITIES if game["abilities"].get(name)]
        catalog = "The supported combat abilities are Shield, Fire, Wave, and Laser."

        def reply(line: str, reason: str) -> VoiceResponse:
            event = self.behavior.speaking(line, reason=reason)
            return VoiceResponse(line, tools_used, "speaking", event, False)

        if _asks_about_removed_ability(utterance):
            return reply("That combat ability is not currently available. " + catalog, "unsupported_ability")

        if topic in SUPPORTED_ABILITIES:
            if not game["abilities"].get(topic):
                return reply(f"{topic.title()} is supported, but it is currently unavailable according to game state.", "ability_unavailable")
            if re.search(UNCONFIRMED_MECHANICS_PATTERN, utterance):
                return reply(
                    f"{topic.title()} is available. Its effects, damage, cooldowns, and combat rules have not been provided by the gameplay team.",
                    "unconfirmed_mechanics",
                )

        if topic not in SUPPORTED_ABILITIES and re.search(COMBAT_QUERY_PATTERN, utterance):
            return reply(
                catalog + " Other combat abilities are not currently available. "
                + "Currently available: " + (", ".join(available) or "none") + ". "
                + "Combat effects and control details must come from confirmed game configuration.",
                "ability_catalog",
            )

        if re.search(r"\b(?:mission|star core)\b", utterance):
            return reply(f"Your current mission is: {game['current_mission']}.", "mission")

        if topic in {"boss", "enemy", "danger"} or (game["in_danger"] and _matches(utterance, HINT_PATTERNS)):
            facts = [f"Current target: {game['current_target'] or 'not confirmed'}."]
            if game["player_health"] is not None:
                facts.append(f"Your health: {game['player_health']}.")
            if game["boss_health"] is not None:
                facts.append(f"Boss health: {game['boss_health']}.")
            if game["in_danger"]:
                facts.append("The game reports danger.")
            facts.append("Available abilities: " + (", ".join(available) or "none") + ".")
            return reply(" ".join(facts), "game_state_context")

        if _matches(utterance, REPEAT_PATTERNS):
            line = self.tutorial.repeat_instruction()
            event = self.behavior.speaking(line, reason="repeat")
            return VoiceResponse(line, tools_used, "speaking", event, True)

        if topic:
            help_line = self.tutorial.topic_help(topic)
            if help_line:
                event = self.behavior.speaking(help_line, reason=f"help:{topic}")
                return VoiceResponse(help_line, tools_used, "speaking", event, False)

        if "get_available_tutorial_hint" in by_name:
            line = by_name["get_available_tutorial_hint"]["hint"]
            event = self.behavior.speaking(line, reason="hint")
            return VoiceResponse(line, tools_used, "speaking", event, False)

        if status.get("is_complete"):
            line = step["instruction"]
            event = self.behavior.celebrating(line)
            return VoiceResponse(line, tools_used, "celebrating", event, True)

        already = bool(step.get("already_spoken"))
        if already and not _matches(utterance, REPEAT_PATTERNS):
            hint = self.registry.call("get_available_tutorial_hint").result
            tools_used.append("get_available_tutorial_hint")
            line = hint["hint"]
            event = self.behavior.speaking(line, reason="hint_instead_of_repeat")
            return VoiceResponse(line, tools_used, "speaking", event, True)

        line = step["instruction"]
        self.tutorial.mark_instruction_spoken(line)
        event = self._behavior_for_step(step, line, reason="instruction")
        return VoiceResponse(line, tools_used, event["state"], event, False)

    def _behavior_for_step(self, step: dict[str, Any], line: str, reason: str) -> dict[str, Any]:
        state = step.get("companion_state") or "speaking"
        if state == "scanning":
            return self.behavior.scanning(line)
        if state == "warning":
            return self.behavior.warning(line)
        if state == "celebrating":
            return self.behavior.celebrating(line, stage_id=step.get("stage_id"))
        return self.behavior.speaking(line, reason=reason)


def _matches(text: str, patterns: tuple[str, ...]) -> bool:
    return any(re.search(pattern, text) for pattern in patterns)


def _detect_topic(text: str) -> str | None:
    for word, topic in TOPIC_ALIASES.items():
        if re.search(rf"\b{word}\b", text):
            return topic
    return None


def _asks_about_removed_ability(text: str) -> bool:
    if not re.search(REMOVED_ABILITY_PATTERN, text):
        return False
    # Environmental descriptions are not combat requests.
    environment = re.search(r"\b(?:weather|terrain|environment|landscape|lake|surface|temperature|ground|planet)\b", text)
    combat = re.search(COMBAT_QUERY_PATTERN + r"|\b(?:use|activate|control)\b|\b(?:can|could|how do|how can) i\b", text)
    return bool(combat or not environment)
