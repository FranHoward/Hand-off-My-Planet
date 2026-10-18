from __future__ import annotations

import json
import re

import pytest

from companion.game_state import GameState
from companion.grok_client import GrokFunctionCallingClient
from companion.session import CompanionSession

ABILITIES = ("shield", "fire", "wave", "laser")
EVENTS = (
    "game_started", "movement_completed", "shield_activated", "fire_activated",
    "wave_activated", "laser_activated", "enemy_scanned", "boss_encountered", "tutorial_completed",
)


@pytest.mark.parametrize("ability", ABILITIES)
def test_game_state_tracks_confirmed_use_separately_from_availability(ability):
    game = GameState(oxygen=73, mission_progress=0.25, star_core_energy=42)
    original = game.to_dict()
    assert original["abilities"] == dict.fromkeys(ABILITIES, True)
    assert original["ability_confirmations"] == dict.fromkeys(ABILITIES, False)
    result = game.apply_event(ability + "_activated")
    assert result["ability_confirmations"] == {name: name == ability for name in ABILITIES}
    for key in ("planet", "player_health", "current_mission", "current_target", "boss_health",
                "oxygen", "mission_progress", "star_core_energy", "abilities"):
        assert result[key] == original[key]
    assert result["shield_up"] is (ability == "shield")
    assert result["fire_cast"] is (ability == "fire")
    result["abilities"][ability] = False
    assert game.abilities[ability] is True


def test_unprovided_environment_values_are_unknown():
    game = GameState().to_dict()
    assert game["oxygen"] is None
    assert game["mission_progress"] is None
    assert game["star_core_energy"] is None


@pytest.mark.parametrize("ability", ABILITIES)
def test_usage_help_and_spoken_claims_do_not_confirm_an_ability(ability):
    session = CompanionSession()
    before = session.game_state.to_dict()
    for question in (f"How do I use {ability}?", f"I already activated {ability}"):
        response = session.handle_player_speech(question)
        assert ability in response["line"].lower()
        assert session.tutorial.control_instruction(ability) in response["line"]
        assert response["advanced"] is False
        assert session.tutorial.get_status().current_stage_id == "welcome"
        assert session.game_state.to_dict() == before
        assert "get_game_state" in response["tools_used"]


@pytest.mark.parametrize("ability", ABILITIES)
def test_ai_respects_current_ability_availability(ability):
    game = GameState()
    game.abilities[ability] = False
    session = CompanionSession(game_state=game)
    response = session.handle_player_speech(f"How do I use {ability}?")
    assert "currently unavailable" in response["line"]
    assert "activate" not in response["line"]


@pytest.mark.parametrize("question", [
    "How do I cast ice magic?", "Can I use frost?", "Can I freeze the boss?",
    "Show me freezing abilities", "Repeat the ice spell instructions",
    "Can I activate ice on this planet?", "How do I cast lightning?",
    "Why is ice_spell_cast not working?",
    "Can I freeze the boss on this planet?",
])
def test_unsupported_ability_requests_are_declined(question):
    session = CompanionSession()
    response = session.handle_player_speech(question)
    assert "not currently available" in response["line"]
    assert "Shield, Fire, Wave, and Laser" in response["line"]
    assert not response["advanced"]
    assert not any(session.game_state.ability_confirmations.values())


def test_environmental_description_is_not_misidentified_as_a_combat_skill():
    session = CompanionSession()
    response = session.handle_player_speech("There is ice on the planet's surface")
    assert "not currently available" not in response["line"]


@pytest.mark.parametrize("question", [
    "How much damage does Fire do?", "What is the Wave cooldown?",
    "Does Laser pierce armor?", "How long does Shield last?",
])
def test_unconfirmed_combat_mechanics_are_not_invented(question):
    response = CompanionSession().handle_player_speech(question)
    assert "have not been provided by the gameplay team" in response["line"]
    assert not re.search(r"\d", response["line"])


def test_mission_and_combat_context_use_actual_state_values():
    game = GameState(current_mission="Inspect the landing site", current_target="Observed target", player_health=61, boss_health=84)
    game.abilities["laser"] = False
    session = CompanionSession(game_state=game)
    assert "Inspect the landing site" in session.handle_player_speech("What is my mission?")["line"]
    danger = session.ingest_game_event("player_in_danger")
    assert danger["animation_event"]["state"] == "warning"
    response = session.handle_player_speech("Help me with the boss")
    for fact in ("Observed target", "61", "84", "reports danger", "Shield, Fire, Wave"):
        assert fact in response["line"]
    assert "Laser" not in response["line"]
    assert session.tutorial.get_status().current_stage_id == "welcome"


def test_function_calling_reports_the_live_four_ability_state_and_steps():
    session = CompanionSession()
    client = GrokFunctionCallingClient(session.voice, session.functions)
    assert {tool["function"]["name"] for tool in client.tool_schemas()} == {
        "get_game_state", "get_tutorial_status", "get_current_tutorial_step", "get_available_tutorial_hint",
    }
    assert "Shield, Fire, Wave, and Laser" in client.system_prompt
    assert "Never invent" in client.system_prompt
    for event in EVENTS:
        status = client.execute_tool("get_tutorial_status")
        step = client.execute_tool("get_current_tutorial_step")
        hint = client.execute_tool("get_available_tutorial_hint")
        game = client.execute_tool("get_game_state")
        assert status["expected_event"] == step["expected_event"] == event
        assert set(game["abilities"]) == set(ABILITIES)
        assert not re.search(r"\b(?:ice|frost|freeze|freezing)\b|ice_cast|fire_spell_cast", json.dumps([status, step, hint, game]), re.I)
        if event.endswith("_activated"):
            ability = event.removesuffix("_activated")
            assert step["ability"] == ability
            assert game["ability_confirmations"][ability] is False
        session.ingest_game_event(event)
        if event.endswith("_activated"):
            assert client.execute_tool("get_game_state")["ability_confirmations"][ability] is True


def test_out_of_order_gameplay_is_recorded_but_never_reused_to_skip_a_later_stage():
    session = CompanionSession()
    response = session.ingest_game_event("laser_activated")
    assert response["game_state"]["ability_confirmations"]["laser"] is True
    assert response["tutorial"]["advanced"] is False
    for event in EVENTS[:5]:
        session.ingest_game_event(event)
    assert session.tutorial.get_status().current_stage_id == "laser"
    assert session.ingest_game_event("laser_activated")["tutorial"]["next_stage_id"] == "scan_enemy"
