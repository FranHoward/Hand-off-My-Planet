from __future__ import annotations

import json

import pytest

from companion.game_state import KNOWN_EVENTS, TUTORIAL_EVENTS
from companion.function_calling import FunctionRegistry, bind_tutorial_tools
from companion.session import CompanionSession
from companion.tutorial.manager import TutorialManager

PROGRESSION = [
    "game_started",
    "movement_completed",
    "shield_activated",
    "fire_activated",
    "wave_activated",
    "laser_activated",
    "enemy_scanned",
    "boss_encountered",
    "tutorial_completed",
]


def test_stage_progression_follows_expected_events() -> None:
    manager = TutorialManager()
    assert list(TUTORIAL_EVENTS) == PROGRESSION
    assert [stage.name for stage in manager.definition.stages] == [
        "Welcome to the Unknown Planet", "Basic Movement", "Shield Tutorial",
        "Fire Tutorial", "Wave Tutorial", "Laser Tutorial", "Enemy Scanning",
        "Boss Battle Preparation", "Tutorial Complete",
    ]
    expected_ids = [
        "welcome",
        "movement",
        "shield",
        "fire",
        "wave",
        "laser",
        "scan_enemy",
        "boss_prep",
        "complete",
    ]
    assert len(PROGRESSION) == len(expected_ids)
    for event, stage_id in zip(PROGRESSION, expected_ids):
        status = manager.get_status()
        assert status.current_stage_id == stage_id
        assert status.expected_event == event
        result = manager.handle_game_event(event)
        assert result.accepted
        assert result.advanced
        assert result.completed_stage_id == stage_id
    assert manager.get_status().is_complete
    assert manager.get_status().completed_stage_ids == expected_ids


def test_unrelated_events_are_ignored() -> None:
    manager = TutorialManager()
    result = manager.handle_game_event("shield_activated")
    assert not result.accepted
    assert not result.advanced
    assert result.ignored_reason == "unexpected_event"
    assert manager.get_status().current_stage_id == "welcome"


def test_duplicate_completion_is_prevented() -> None:
    manager = TutorialManager()
    first = manager.handle_game_event("game_started")
    assert first.advanced
    # Same event is no longer expected.
    second = manager.handle_game_event("game_started")
    assert not second.advanced
    assert second.ignored_reason == "unexpected_event"
    assert manager.get_status().completed_stage_ids.count("welcome") == 1
    assert manager.get_status().current_stage_id == "movement"


def test_player_speech_never_advances_stages() -> None:
    session = CompanionSession()
    session.open()
    before = session.tutorial.get_status().current_stage_id
    spoken = session.handle_player_speech("I already moved and raised my shield!")
    assert spoken["advanced"] is False
    assert session.tutorial.get_status().current_stage_id == before
    assert session.tutorial.get_status().completed_stage_ids == []


def test_repeat_instruction() -> None:
    session = CompanionSession()
    first = session.open()
    repeated = session.handle_player_speech("Repeat that")
    assert "repeat" in repeated["tools_used"] or "get_current_tutorial_step" in repeated["tools_used"]
    assert repeated["line"] == first.line
    assert repeated["used_existing_instruction"] is True


def test_avoids_repeating_instruction_without_repeat_request() -> None:
    session = CompanionSession()
    first = session.open()
    again = session.handle_player_speech("What should I do?")
    assert again["line"] != first.line
    assert "hint" in again["animation_event"]["reason"] or "get_available_tutorial_hint" in again["tools_used"]


def test_contextual_hints_rotate_and_match_stage() -> None:
    manager = TutorialManager()
    manager.handle_game_event("game_started")
    assert manager.get_status().current_stage_id == "movement"
    first = manager.get_available_hint()
    second = manager.get_available_hint()
    assert first["hint"] != second["hint"]
    assert "left hand" in first["hint"].lower() and "push" in first["hint"].lower()
    shield_help = TutorialManager()
    shield_help.handle_game_event("game_started")
    shield_help.handle_game_event("movement_completed")
    line = shield_help.topic_help("shield")
    assert line is not None
    assert "shield" in line.lower()


def test_how_do_i_use_my_shield_is_contextual() -> None:
    session = CompanionSession()
    session.ingest_game_event("game_started")
    session.ingest_game_event("movement_completed")
    reply = session.handle_player_speech("How do I use my shield?")
    assert "shield" in reply["line"].lower()
    assert session.tutorial.get_status().current_stage_id == "shield"


def test_function_calling_tools_are_bound() -> None:
    manager = TutorialManager()
    registry = bind_tutorial_tools(FunctionRegistry(), manager)
    status = registry.call("get_tutorial_status").result
    step = registry.call("get_current_tutorial_step").result
    hint = registry.call("get_available_tutorial_hint").result
    assert status["current_stage_id"] == "welcome"
    assert step["expected_event"] == "game_started"
    assert hint["hint"]


def test_tutorial_completion_and_late_events() -> None:
    manager = TutorialManager()
    for event in PROGRESSION:
        manager.handle_game_event(event)
    assert manager.get_status().is_complete
    late = manager.handle_game_event("movement_completed")
    assert not late.advanced
    assert late.ignored_reason == "tutorial_already_complete"


def test_companion_states_for_scan_celebrate_and_warning() -> None:
    session = CompanionSession()
    session.open()
    assert session.behavior.state == "speaking"
    for event in PROGRESSION[:5]:
        session.ingest_game_event(event)
    assert session.tutorial.get_status().current_stage_id == "laser"
    laser = session.ingest_game_event("laser_activated")
    assert laser["animation_event"]["state"] in {"celebrating", "scanning"}
    session.ingest_game_event("enemy_scanned")
    danger = session.ingest_game_event("player_in_danger")
    assert danger["animation_event"]["state"] == "warning"
    assert session.tutorial.get_status().current_stage_id == "boss_prep"


@pytest.mark.parametrize("event,next_stage", [
    ("shield_activated", "fire"), ("fire_activated", "wave"),
    ("wave_activated", "laser"), ("laser_activated", "scan_enemy"),
])
def test_each_ability_advances_to_exactly_the_next_stage(event, next_stage):
    manager = TutorialManager()
    for previous in PROGRESSION[:PROGRESSION.index(event)]:
        manager.handle_game_event(previous)
    result = manager.handle_game_event(event)
    assert result.accepted and result.advanced
    assert result.next_stage_id == next_stage
    assert manager.get_status().current_stage_id == next_stage
    assert manager.get_current_step()["instruction"] in result.spoken_line


def test_all_unrelated_and_duplicate_events_leave_each_stage_unchanged():
    manager = TutorialManager()
    for expected in PROGRESSION:
        before = manager.get_status().to_dict()
        for other in KNOWN_EVENTS:
            if other != expected:
                result = manager.handle_game_event(other)
                assert not result.accepted and not result.advanced
                assert manager.get_status().to_dict() == before
        assert manager.handle_game_event(expected).advanced
        after = manager.get_status().to_dict()
        assert not manager.handle_game_event(expected).advanced
        assert manager.get_status().to_dict() == after


# Retired names remain only as negative regression inputs, never supported events.
@pytest.mark.parametrize("retired", [
    "ice_spell_cast", "ice_activated", "freeze_cast", "freeze_activated",
    "frost_activated", "frost_spell_cast", "fire_spell_cast", "lightning_activated",
])
def test_retired_events_are_rejected_at_every_stage_without_state_mutation(retired):
    session = CompanionSession()
    assert retired not in KNOWN_EVENTS
    for expected in PROGRESSION:
        before = session.snapshot()
        response = session.ingest_game_event(retired)
        assert not response["tutorial"]["accepted"]
        assert not response["tutorial"]["advanced"]
        assert response["spoken_line"] is None
        assert session.snapshot() == before
        session.ingest_game_event(expected)


def test_controls_are_configurable_across_instructions_hints_transitions_and_help(tmp_path):
    from companion.tutorial.manager import DEFAULT_STAGES_PATH
    payload = json.loads(DEFAULT_STAGES_PATH.read_text())
    confirmed = "Use the teammate-confirmed control FIRE_INPUT_01."
    payload["ability_controls"]["fire"] = confirmed
    source = tmp_path / "tutorial_stages.json"
    source.write_text(json.dumps(payload))
    manager = TutorialManager(path=source)
    for event in PROGRESSION[:3]:
        result = manager.handle_game_event(event)
    step = manager.get_current_step()
    assert confirmed in result.spoken_line
    assert confirmed in step["instruction"]
    assert step["control_instruction"] == confirmed
    assert step["control_configured"] is True
    assert confirmed in manager.get_available_hint()["hint"]
    assert confirmed in manager.get_status().available_hints[0]
    assert confirmed in manager.repeat_instruction()
    assert confirmed in manager.topic_help("fire")


def test_project_ability_controls_are_configured_and_help_survives_completion():
    manager = TutorialManager()
    for event in PROGRESSION:
        step = manager.get_current_step()
        if step.get("ability"):
            assert step["control_configured"] is True
            assert step["control_instruction"] in step["instruction"]
        manager.handle_game_event(event)
    for ability in ("shield", "fire", "wave", "laser"):
        assert ability in manager.topic_help(ability).lower()


@pytest.mark.parametrize("change", ["sequence", "duplicate_id", "ability", "controls", "control_type"])
def test_invalid_configuration_is_rejected(tmp_path, change):
    from companion.tutorial.manager import DEFAULT_STAGES_PATH
    payload = json.loads(DEFAULT_STAGES_PATH.read_text())
    if change == "sequence":
        payload["stages"][3]["expected_event"] = "wave_activated"
    elif change == "duplicate_id":
        payload["stages"][1]["id"] = "welcome"
    elif change == "ability":
        payload["stages"][3]["ability"] = "wave"
    elif change == "controls":
        payload["ability_controls"]["unsupported"] = "Unconfirmed control"
    else:
        payload["ability_controls"]["fire"] = 123
    source = tmp_path / "invalid.json"
    source.write_text(json.dumps(payload))
    with pytest.raises(ValueError):
        TutorialManager(path=source)
