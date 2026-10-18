"""Current mapping: left fist STOP; right stationary palm Shield and pinch Laser."""
import json
from pathlib import Path
import unittest

from game_controls import GameControls, packet
from gesture_logic import classify_hand


def hand(pose='OPEN_PALM',scale=.1,x=.5,y=.6):
    return {'gesture':pose,'scale':scale,'palm':[x,y],'index_tip':[x+.04,y-.18],
            'pinch_center':[x+.01,y-.12]}


class MappingTests(unittest.TestCase):
    def setUp(self):
        self.c=GameControls({'calibration_ms':210});self.t=0;self.rows=[]

    def feed(self,n=1,left=None,right=None):
        for _ in range(n):
            self.t+=30;self.out=self.c.update(left,right,0,self.t);self.rows.append(self.out)
        return self.out

    def test_left_fist_immediately_stops_forward_and_backward(self):
        self.feed(12,left=hand())
        for depth,axis in ((.13,1),(.07,-1)):
            self.assertEqual(self.feed(20,left=hand(scale=depth))['move_axis'],axis)
            out=self.feed(left=hand('FIST',scale=depth))
            self.assertEqual(out['left_hand'],'STOP');self.assertEqual(out['move_axis'],0)
            self.assertEqual(out['movement'],'Stop')
            self.assertNotIn('MoveForward',out['held_actions']);self.assertNotIn('MoveBackward',out['held_actions'])

    def test_fist_moving_in_depth_never_moves_character(self):
        self.feed(12,left=hand())
        for scale in (.15,.08,.2,.06,.12):
            out=self.feed(10,left=hand('FIST',scale))
            self.assertEqual(out['left_hand'],'STOP');self.assertEqual(out['move_axis'],0)
            self.assertEqual(out['triggered_actions'],[])

    def test_fist_does_not_calibrate_movement_neutral(self):
        out=self.feed(30,left=hand('FIST',.15))
        self.assertFalse(out['calibrated']['left_hand']);self.assertEqual(out['left_hand'],'STOP')
        out=self.feed(25,left=hand(scale=.1))
        self.assertTrue(out['calibrated']['left_hand']);self.assertEqual(out['left_hand'],'NONE')

    def test_brief_unknown_or_open_does_not_restart_from_fist(self):
        self.feed(12,left=hand());self.feed(left=hand('FIST',.13))
        self.assertEqual(self.feed(left=hand('NONE',.13))['left_hand'],'STOP')
        self.assertEqual(self.feed(left=hand('OPEN_PALM',.13))['move_axis'],0)
        self.assertEqual(self.feed(left=hand('FIST',.13))['move_axis'],0)
        self.assertEqual(self.feed(20,left=hand('OPEN_PALM',.13))['move_axis'],1)

    def test_missing_left_clears_stop_but_preserves_right_shield(self):
        self.feed(12,left=hand('FIST'),right=hand())
        out=self.feed(right=hand())
        self.assertEqual(out['left_hand'],'NONE');self.assertEqual(out['right_hand'],'SHIELD')
        self.assertEqual(out['move_axis'],0)

    def test_stationary_right_palm_shields_left_palm_does_not(self):
        self.assertNotIn('Shield',self.feed(12,left=hand())['held_actions'])
        out=self.feed(12,right=hand())
        self.assertEqual(out['right_hand'],'SHIELD');self.assertEqual(out['held_actions'],['Shield'])

    def test_fast_vertical_palm_is_not_stationary_shield(self):
        self.feed(12,right=hand(y=.3))
        for y in (.35,.4,.45,.5,.55):
            self.assertNotIn('Shield',self.feed(right=hand(y=y))['held_actions'])
        self.assertEqual(self.feed(12,right=hand(y=.55))['right_hand'],'SHIELD')

    def test_pinch_activates_laser_and_uses_pinch_center(self):
        right=hand('PINCH')
        out=self.feed(12,right=right)
        self.assertEqual(out['right_hand'],'LASER');self.assertEqual(out['pointer'],right['pinch_center'])
        self.assertNotEqual(out['pointer'],right['index_tip']);self.assertNotIn('Shield',out['held_actions'])

    def test_point_no_longer_activates_laser(self):
        out=self.feed(20,right=hand('POINT'))
        self.assertEqual(out['right_hand'],'NONE');self.assertNotIn('Laser',out['held_actions'])

    def test_releasing_pinch_or_losing_hand_releases_laser(self):
        self.feed(12,right=hand('PINCH'))
        self.assertNotIn('Laser',self.feed(right=hand('OPEN_PALM'))['held_actions'])
        self.assertEqual(self.feed(12,right=hand())['right_hand'],'SHIELD')
        self.feed(12,right=hand('PINCH'))
        self.assertEqual(self.feed()['right_hand'],'NONE');self.assertIsNone(self.out['pointer'])

    def test_pinch_horizontal_and_depth_motion_never_fire_or_wave(self):
        self.feed(12,right=hand('PINCH',x=.2))
        for x in (.24,.28,.32,.36,.4,.44,.48):self.feed(right=hand('PINCH',x=x))
        self.feed(12,right=hand('PINCH',scale=.14,x=.48))
        self.assertEqual(self.out['right_hand'],'LASER')
        self.assertFalse(any(r['triggered_actions'] for r in self.rows))

    def test_shield_to_wave_then_stationary_shield(self):
        self.feed(12,right=hand(x=.2))
        for x in (.24,.28,.32,.36,.4,.44):self.feed(right=hand(x=x))
        fired=[r for r in self.rows if 'Wave' in r['triggered_actions']]
        self.assertEqual(len(fired),1);self.assertNotIn('Shield',fired[0]['held_actions'])
        self.assertEqual(self.feed(15,right=hand(x=.44))['right_hand'],'SHIELD')

    def test_stop_plus_shield_and_stop_plus_laser_json(self):
        for pose,label in (('OPEN_PALM','SHIELD'),('PINCH','LASER')):
            out=self.feed(12,left=hand('FIST',.14),right=hand(pose))
            data=json.loads(json.dumps(packet('mapping-test',self.t,self.t,out),allow_nan=False))
            self.assertEqual(data['left_hand'],'STOP');self.assertEqual(data['left_hand_actions'],['STOP'])
            self.assertEqual(data['right_hand'],label);self.assertEqual(data['move_axis'],0)

    def test_reference_pinch_geometry_reaches_laser(self):
        refs=json.loads((Path(__file__).parent/'tests/reference_landmarks.json').read_text())
        ref=next(r for r in refs if r['expected']=='PINCH');h=ref['hands'][0]
        pose=classify_hand(h['world'],h['model'],h['score'],image=h['image'],image_size=ref['size'])[0]
        self.assertEqual(pose,'PINCH')
        self.assertEqual(self.feed(12,right=hand(pose))['right_hand'],'LASER')


if __name__=='__main__':unittest.main()
