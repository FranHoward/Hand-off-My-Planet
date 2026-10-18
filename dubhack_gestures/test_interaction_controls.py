from interaction_controls import InteractionControls


def hand(pose, x=.5):
    return dict(gesture=pose, scale=.1, palm=[x,.5], index_tip=[x,.4])


def test_scan_requires_dwell_and_release_and_never_attacks():
    controls = InteractionControls()
    pulses = []
    for t in range(0, 2001, 100):
        state = controls.update(None, hand('POINT', .3 + t/10000), 0., t)
        pulses.extend(state['triggered_actions'])
        assert not state['held_actions']
        assert state['tracking']['right_hand']
    assert pulses == ['Scan']
    assert state['events'] == []  # Retained events expire, even for a held pose.
    controls.update(None, None, 0., 2100)
    for t in range(2200, 2901, 100):
        state = controls.update(None, hand('POINT'), 0., t)
    assert state['events'][0]['action'] == 'Scan'
    assert state['events'][0]['id'] == 2


def test_hint_and_repeat_share_event_ids():
    controls = InteractionControls()
    for t in range(0, 1101, 100):
        state = controls.update(None, hand('TWO_FINGERS'), 0., t)
    assert [e['action'] for e in state['events']] == ['Hint']
    for t in range(1200, 2301, 100):
        state = controls.update(hand('TWO_FINGERS'), None, 0., t)
        assert state['move_axis'] == 0.
    assert state['events'][0]['action'] == 'Repeat'
    assert state['events'][0]['id'] == 2


def test_both_v_calibrates_once_without_hint_repeat_or_combat():
    controls = InteractionControls()
    pulses, calibrations = [], 0
    for t in range(0, 3001, 100):
        state = controls.update(hand('TWO_FINGERS'), hand('TWO_FINGERS'), 20., t)
        pulses.extend(state['triggered_actions'])
        calibrations += state['recalibrated']
        assert state['move_axis'] == state['turn_axis'] == 0.
        assert state['held_actions'] == []
    assert pulses == []
    assert calibrations == 1


def test_both_palms_emit_single_retry_without_losing_shield():
    controls = InteractionControls()
    pulses = []
    for t in range(0, 3001, 100):
        state = controls.update(hand('OPEN_PALM'), hand('OPEN_PALM'), 0., t)
        pulses.extend(state['triggered_actions'])
    assert pulses == ['Retry']
    assert 'Shield' in state['held_actions']


def test_tracking_gap_restarts_utility_dwell():
    controls = InteractionControls()
    controls.update(None, hand('POINT'), 0., 0)
    state = controls.update(None, hand('POINT'), 0., 1000)
    assert state['triggered_actions'] == []
    for t in range(1100, 1601, 100):
        state = controls.update(None, hand('POINT'), 0., t)
    assert state['triggered_actions'] == ['Scan']
