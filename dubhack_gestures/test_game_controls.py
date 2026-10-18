import unittest
from game_controls import GameControls,head_feature,COMBAT_ACTIONS,release_packet


def hand(pose='OPEN_PALM',scale=.1,x=.5,y=.6):
 return {'gesture':pose,'scale':scale,'palm':[x,y],'index_tip':[x,y-.15]}


class ControlTests(unittest.TestCase):
 def setUp(self):self.c=GameControls({'calibration_ms':210});self.t=0;self.fired=[]
 def feed(self,n=1,left=.1,right=None,head=0):
  for _ in range(n):
   self.t+=30;self.out=self.c.update(hand(scale=left) if left is not None else None,right,head,self.t)
   self.fired.extend(self.out['triggered_actions'])
  return self.out
 def test_neutral_and_calibration(self):
  self.assertEqual(self.feed(5,left=.13)['move_axis'],0)
  o=self.feed(10,left=.13);self.assertTrue(o['calibrated']['left_hand']);self.assertEqual(o['movement'],'Stop')
 def test_forward_backward_and_neutral(self):
  self.feed(10)
  self.assertEqual(self.feed(15,left=.13)['move_axis'],1)
  self.assertEqual(self.feed(15,left=.1)['move_axis'],0)
  self.assertEqual(self.feed(15,left=.07)['move_axis'],-1)
 def test_simultaneous_movement_shield_and_look(self):
  self.feed(10,right=hand())
  o=self.feed(15,left=.13,right=hand(),head=-20)
  self.assertEqual(set(o['held_actions']),{'MoveForward','Shield','TurnLeft'})
  self.assertLess(o['turn_axis'],0)
 def test_fist_alone_does_not_fire(self):
  self.feed(60,right=hand('FIST'));self.assertEqual(self.fired,[])
 def punch(self):
  self.feed(8,right=hand('FIST'))
  for s in (.105,.11,.115,.12,.125,.13):self.feed(right=hand('FIST',s))
 def test_fist_then_push_once_and_retract_rearms(self):
  self.punch();self.assertEqual(self.fired,['Fire'])
  self.feed(35,right=hand('FIST',.13));self.assertEqual(self.fired,['Fire'])
  self.punch();self.assertEqual(self.fired,['Fire','Fire'])
 def test_wave_horizontal_not_vertical(self):
  self.feed(10,right=hand(x=.3))
  for x in (.34,.38,.42,.46,.5,.54):self.feed(right=hand(x=x))
  self.assertEqual(self.fired,['Wave'])
  self.assertNotIn('Shield',self.out['held_actions'])  # right wave replaces right shield
  self.assertEqual(self.out['right_hand'],'WAVE')
  self.feed(30,right=hand(x=.54));self.assertEqual(self.fired,['Wave'])
  self.c.recalibrate();self.fired=[]
  for y in (.2,.25,.3,.35,.4,.45,.5,.55):self.feed(right=hand(y=y))
  self.assertEqual(self.fired,[])
 def test_pinch_aims_without_wave(self):
  self.feed(10,right=hand('PINCH'))
  for x in (.45,.4,.35,.3,.25,.2):self.feed(right=hand('PINCH',x=x))
  self.assertIn('Laser',self.out['held_actions']);self.assertAlmostEqual(self.out['pointer'][0],.2);self.assertAlmostEqual(self.out['pointer'][1],.45)
  self.assertEqual(self.fired,[])
 def test_fist_sideways_is_not_wave_or_fire(self):
  for x in (.2,.24,.28,.32,.36,.4,.44,.48,.52):self.feed(right=hand('FIST',x=x))
  self.assertEqual(self.fired,[])
 def test_losses_release_each_channel(self):
  self.feed(10,right=hand('PINCH'));self.feed(15,left=.13,right=hand('PINCH'),head=20)
  o=self.feed(left=None,right=hand('PINCH'),head=None)
  self.assertEqual(o['move_axis'],0);self.assertEqual(o['turn_axis'],0);self.assertIn('Laser',o['held_actions'])
  self.assertNotIn('Shield',o['held_actions'])
  self.assertEqual(self.feed(left=None,right=None,head=None)['held_actions'],[])
 def test_recalibration_resets_neutral(self):
  self.feed(10);self.feed(15,left=.13,head=20);self.c.recalibrate()
  self.assertEqual(self.feed(left=.13,head=20)['move_axis'],0)
  o=self.feed(10,left=.13,head=20);self.assertEqual(o['move_axis'],0);self.assertEqual(o['turn_axis'],0)
 def test_event_queue_expires(self):
  self.punch();self.assertEqual(len(self.out['events']),1)
  self.feed(25,right=None);self.assertEqual(self.out['events'],[])
 def test_tracking_jump_does_not_create_wave(self):
  self.feed(10,right=hand(x=.1));self.feed(right=hand(x=.7));self.assertEqual(self.fired,[])
 def test_long_frame_gap_does_not_create_punch(self):
  self.feed(10,right=hand('FIST'));self.t+=1000
  self.feed(right=hand('FIST',.15));self.assertEqual(self.fired,[])
 def test_exactly_four_combat_abilities(self):
  self.assertEqual(set(COMBAT_ACTIONS),{'Shield','Fire','Wave','Laser'})
 def test_head_feature_translation_scale_and_mirror(self):
  p=[(.5,.5,0)]*478;p[33]=(.3,.4,0);p[263]=(.7,.4,0);p[1]=(.56,.5,0)
  a=head_feature(p);self.assertAlmostEqual(a,.15)
  self.assertAlmostEqual(head_feature([(x*2+3,y*2-1,z) for x,y,z in p]),a)
  self.assertAlmostEqual(head_feature([(1-x,y,z) for x,y,z in p],mirrored=False),a)
 def test_shutdown_packet_is_all_released(self):
  p=release_packet('session',99,1234)
  self.assertEqual(p['version'],3);self.assertEqual(p['held_actions'],[]);self.assertEqual(p['events'],[])
  self.assertEqual(p['move_axis'],0);self.assertEqual(p['turn_axis'],0)

if __name__=='__main__':unittest.main()
