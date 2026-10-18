"""Regression tests from real reference landmarks; no camera or ML dependency."""
import json
from pathlib import Path
import unittest
from gesture_logic import GestureState, classify_hand, choose_gesture

REFS=json.loads((Path(__file__).parent/'tests/reference_landmarks.json').read_text())


def classify(h,size,world=None,image=None):
    return classify_hand(h['world'] if world is None else world,h['model'],h['score'],
        image=h['image'] if image is None else image,image_size=size)[0]


class RulesTest(unittest.TestCase):
    def test_six_reference_poses(self):
        for ref in REFS:
            with self.subTest(pose=ref['expected']):
                hands=[{'gesture':classify(h,ref['size']),'area':1,'palm':[.5,.5]} for h in ref['hands']]
                self.assertEqual(choose_gesture(hands)[0],ref['expected'])

    def test_low_score_open_palms_have_geometry_fallback(self):
        for ref in (REFS[0],REFS[5]):
            for h in ref['hands']:
                out=classify_hand(h['world'],'None',0,image=h['image'],image_size=ref['size'])
                self.assertEqual(out[0],'OPEN_PALM');self.assertEqual(out[2],'geometry')

    def test_side_pinch_with_curled_outer_fingers(self):
        h=REFS[2]['hands'][0]
        out=classify_hand(h['world'],'None',0,image=h['image'],image_size=REFS[2]['size'])
        self.assertEqual(out[0],'PINCH')
        self.assertGreater(out[1],.30)  # proves projected fallback handles noisy 3D

    def test_rotation_scale_and_horizontal_mirror(self):
        for ref in REFS:
            for h in ref['hands']:
                original=classify(h,ref['size'])
                world=[(-y*5+2,x*5-1,z*5+3) for x,y,z in h['world']]
                image=[(1-x,y,z) for x,y,z in h['image']]
                self.assertEqual(classify(h,ref['size'],world,image),original)

    def test_projected_fist_tips_do_not_become_pinch(self):
        h=REFS[1]['hands'][0];image=[p[:] for p in h['image']];image[4]=image[8][:]
        for score in (0,.95):
            self.assertEqual(classify_hand(h['world'],'Closed_Fist',score,
                image=image,image_size=REFS[1]['size'])[0],'FIST')

    def test_unknown_and_low_scores_remain_unknown(self):
        self.assertEqual(classify_hand([],'Thumb_Up',.99)[0],'NONE')
        self.assertEqual(classify_hand([],'Open_Palm',.3)[0],'NONE')
        self.assertEqual(classify_hand([(0,0,float('nan'))]*21,'None',0)[0],'NONE')

    def test_double_palm_priority_and_primary_hand(self):
        hands=[{'gesture':'OPEN_PALM','area':.1,'palm':[.2,.5]},
               {'gesture':'OPEN_PALM','area':.2,'palm':[.8,.5]}]
        self.assertEqual(choose_gesture(hands),('DOUBLE_PALM',[.5,.5]))
        hands[1]['gesture']='NONE'
        self.assertEqual(choose_gesture(hands)[0],'NONE')
        self.assertEqual(choose_gesture([]),('NONE',None))


class StateTest(unittest.TestCase):
    def test_jitter_and_one_event_per_entry(self):
        s=GestureState()
        for t,p in [(0,'FIST'),(100,'NONE'),(150,'FIST'),(300,'FIST')]:
            self.assertFalse(s.update(p,t)['triggered'])
        self.assertTrue(s.update('FIST',350)['triggered'])
        self.assertFalse(s.update('FIST',500)['triggered'])
        self.assertFalse(s.update('FIST',800)['triggered']);self.assertEqual(s.event_id,1)

    def test_tracking_loss_releases_immediately(self):
        s=GestureState();s.update('PINCH',0);s.update('PINCH',200)
        out=s.update('NONE',250,has_hands=False)
        self.assertEqual(out['gesture'],'NONE');self.assertFalse(out['confirmed'])

    def test_brief_unknown_keeps_hold_but_long_unknown_releases(self):
        s=GestureState();s.update('POINT',0);s.update('POINT',200)
        self.assertEqual(s.update('NONE',250)['gesture'],'POINT')
        self.assertEqual(s.update('POINT',300)['gesture'],'POINT');self.assertEqual(s.event_id,1)
        s.update('NONE',350);out=s.update('NONE',550)
        self.assertEqual(out['gesture'],'NONE');self.assertFalse(out['confirmed'])

    def test_transition_keeps_previous_stable_pose_until_confirmed(self):
        s=GestureState();s.update('POINT',0);s.update('POINT',200)
        out=s.update('FIST',250)
        self.assertEqual(out['gesture'],'POINT');self.assertTrue(out['confirmed'])
        out=s.update('FIST',450)
        self.assertEqual(out['gesture'],'FIST');self.assertTrue(out['triggered'])
        self.assertEqual(out['event_id'],2)

    def test_reentry_has_event_without_global_cooldown(self):
        s=GestureState();s.update('FIST',0);s.update('FIST',200)
        s.update('NONE',250,False);s.update('FIST',300)
        self.assertTrue(s.update('FIST',500)['triggered']);self.assertEqual(s.event_id,2)

    def test_stall_requires_new_confirmation(self):
        s=GestureState();s.update('FIST',0);s.update('FIST',200)
        self.assertEqual(s.update('FIST',1200)['gesture'],'NONE')
        self.assertTrue(s.update('FIST',1400)['triggered'])

    def test_backwards_timestamp_is_rejected(self):
        s=GestureState();s.update('FIST',100)
        with self.assertRaises(ValueError):s.update('FIST',99)


if __name__=='__main__':unittest.main()
