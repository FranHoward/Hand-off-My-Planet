"""Gesture-only utility interactions sharing the combat stream's event IDs."""
from game_controls import GameControls, hand_confidence


class InteractionControls(GameControls):
    def __init__(self, settings=None):
        super().__init__(settings)
        self.utility_holds = {}
        self.utility_time = None

    def dwell(self, action, active, timestamp, duration):
        if not active:
            self.utility_holds.pop(action, None)
            return False
        since, fired = self.utility_holds.setdefault(action, (timestamp, False))
        if not fired and timestamp - since >= duration:
            self.utility_holds[action] = (since, True)
            return True
        return False

    def update(self, left, right, head, timestamp):
        if self.utility_time is not None and timestamp - self.utility_time > 400:
            self.utility_holds.clear()
        self.utility_time = timestamp
        lp = left['gesture'] if left else 'NONE'
        rp = right['gesture'] if right else 'NONE'
        calibration = lp == rp == 'TWO_FINGERS'
        commands = []
        for action, active, duration in (
            ('Scan', rp == 'POINT' and not calibration, 600),
            ('Hint', rp == 'TWO_FINGERS' and not calibration, 1000),
            ('Repeat', lp == 'TWO_FINGERS' and not calibration, 1000),
            ('Retry', lp == rp == 'OPEN_PALM', 1500),
        ):
            if self.dwell(action, active, timestamp, duration):
                commands.append(action)
        recalibrated = self.dwell('Calibrate', calibration, timestamp, 1500)
        if recalibrated:
            self.recalibrate()
        # Utility poses cannot also fire a swipe or reuse a previous combat pose.
        state = super().update(None if lp == 'TWO_FINGERS' else left,
                               None if rp in ('POINT', 'TWO_FINGERS') else right,
                               None if calibration else head, timestamp)
        for action in commands:
            self._emit(action, timestamp)
        state['events'] = list(self.events)
        state['triggered_actions'].extend(commands)
        state['utility_actions'] = commands
        state['recalibrated'] = recalibrated
        state['tracking']['left_hand'] = left is not None
        state['tracking']['right_hand'] = right is not None
        state['recognition_confidence']['left_hand'] = hand_confidence(left)
        state['recognition_confidence']['right_hand'] = hand_confidence(right)
        if rp in ('POINT', 'TWO_FINGERS'):
            state['right_hand'] = 'SCAN' if rp == 'POINT' else 'HINT'
        if calibration:
            state['held_actions'] = []
            state['move_axis'] = state['turn_axis'] = 0.
            state['movement'] = 'Stop'
            state['look'] = 'Center'
        return state
