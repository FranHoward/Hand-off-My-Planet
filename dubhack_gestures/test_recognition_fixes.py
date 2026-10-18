"""Player-side, yaw, motion and JSON regressions; no webcam or MediaPipe import."""
import contextlib
import copy
import io
import json
import math
from pathlib import Path
import tempfile
from types import SimpleNamespace as NS
import unittest

from game_controls import GameControls, DEFAULTS, route_hands, head_yaw_degrees, packet, release_packet
from gesture_recognition import display_coordinates, make_hands, load_settings, parse_args, format_debug_lines
from gesture_logic import classify_hand


def hand(side='Right', pose='FIST', scale=.1, x=.25, score=.98):
    return dict(handedness=side, raw_handedness=side, handedness_score=score,
                gesture=pose, scale=scale, palm=[x,.6], index_tip=[x,.4],
                area=scale*scale, model_label='Closed_Fist', model_score=.83, method='model')


def rotation(player_yaw, pitch=0, roll=0, scale=1):
    yaw=-math.radians(player_yaw);pitch=math.radians(pitch);roll=math.radians(roll)
    cy,sy=math.cos(yaw),math.sin(yaw);cp,sp=math.cos(pitch),math.sin(pitch);cr,sr=math.cos(roll),math.sin(roll)
    r=[[cr*cy,cr*sy*sp-sr*cp,cr*sy*cp+sr*sp],
       [sr*cy,sr*sy*sp+cr*cp,sr*sy*cp-cr*sp],[-sy,cy*sp,cy*cp]]
    return [[v*scale for v in row]+[10] for row in r]+[[0,0,0,1]]


class SideTests(unittest.TestCase):
    def test_player_labels_not_screen_sides_or_result_order(self):
        for x in (.15,.85):
            for reverse in (False,True):
                hands=[hand('Left',x=x),hand('Right',x=1-x)]
                if reverse:hands.reverse()
                sides=route_hands(hands)
                self.assertEqual(sides['Left']['palm'][0],x)
                self.assertEqual(sides['Right']['palm'][0],1-x)

    def test_mirrored_preview_does_not_change_player_labels(self):
        for mirrored in (True,False):
            hands=[hand('Left',x=.8),hand('Right',x=.2)]
            sides=route_hands(hands);display_coordinates(hands,mirrored)
            self.assertEqual(sides['Left']['handedness'],'Left')
            self.assertAlmostEqual(sides['Left']['palm'][0],.2 if mirrored else .8)

    def test_only_one_hand_never_fills_other_side(self):
        for side in ('Left','Right'):
            self.assertEqual(set(route_hands([hand(side)])),{side})

    def test_duplicate_side_is_not_reassigned(self):
        hands=[hand('Left',score=.7),hand('Left',score=.95)]
        sides=route_hands(hands)
        self.assertEqual(set(sides),{'Left'});self.assertIs(sides['Left'],hands[1])
        self.assertFalse(hands[0]['accepted'])

    def test_low_confidence_side_is_none(self):
        self.assertEqual(route_hands([hand(score=.59)]),{})

    def test_manual_swap_applied_once_even_when_rerouted(self):
        hands=[hand('Left')]
        self.assertEqual(set(route_hands(hands,True)),{'Right'})
        self.assertEqual(set(route_hands(hands,True)),{'Right'})
        self.assertEqual(set(route_hands(hands,False)),{'Left'})

    def test_make_hands_pairs_same_result_index_and_best_category(self):
        refs=json.loads((Path(__file__).parent/'tests/reference_landmarks.json').read_text())
        left,right=refs[0]['hands'][0],refs[1]['hands'][0]
        to_points=lambda pts:[NS(x=x,y=y,z=z) for x,y,z in pts]
        result=NS(hand_landmarks=[to_points(left['image']),to_points(right['image'])],
                  hand_world_landmarks=[to_points(left['world']),to_points(right['world'])],
                  gestures=[[NS(category_name='Open_Palm',score=.9)],[NS(category_name='Closed_Fist',score=.9)]],
                  handedness=[[NS(category_name='Right',score=.1),NS(category_name='Left',score=.9)],
                              [NS(category_name='Right',score=.95)]])
        sides=route_hands(make_hands(result,NS(score=.65,pinch=.30),refs[0]['size']))
        self.assertEqual(sides['Left']['gesture'],'OPEN_PALM')
        self.assertEqual(sides['Right']['gesture'],'FIST')


class YawTests(unittest.TestCase):
    def test_matrix_player_left_center_right_degrees(self):
        for angle in (-35,-15,0,15,35):
            self.assertAlmostEqual(head_yaw_degrees(rotation(angle)),angle)

    def test_matrix_pitch_roll_translation_scale_do_not_shift_yaw(self):
        for angle in (-25,0,25):
            self.assertAlmostEqual(head_yaw_degrees(rotation(angle,20,-15,2)),angle)

    def test_invalid_matrix_is_missing(self):
        for matrix in (None,[],[[0]*4]*4,[[float('nan')]*4]*4):
            self.assertIsNone(head_yaw_degrees(matrix))

    def test_face_pose_into_direction_pipeline(self):
        control=GameControls({'calibration_ms':210});t=0
        for angle,expected in ((5,'NONE'),(-15,'LEFT'),(5,'NONE'),(25,'RIGHT'),(5,'NONE')):
            for _ in range(25):
                t+=30;out=control.update(None,None,head_yaw_degrees(rotation(angle)),t)
            self.assertEqual(out['head_direction'],expected)


class IndependentControlTests(unittest.TestCase):
    def setUp(self):
        self.c=GameControls({'calibration_ms':210});self.t=0;self.records=[]

    def feed(self,n=1,left=None,right=None,yaw=0):
        for _ in range(n):
            self.t+=30;self.out=self.c.update(left,right,yaw,self.t);self.records.append(copy.deepcopy(self.out))
        return self.out

    def punch(self,left=None,blur=False,peak=.12):
        self.feed(10,left,right=hand())
        for i,s in enumerate((.104,.108,.112,.116,peak,peak,peak)):
            self.feed(left=left,right=hand(pose='NONE' if blur and i in (2,3) else 'FIST',scale=s))
        return self.out

    def events(self):return [e for r in self.records for e in r['triggered_actions']]

    def test_shield_is_right_only(self):
        self.assertNotIn('Shield',self.feed(12,left=hand('Left','OPEN_PALM'))['held_actions'])
        out=self.feed(12,right=hand('Right','OPEN_PALM'))
        self.assertEqual(out['left_hand'],'NONE');self.assertIn('Shield',out['held_actions'])
        self.assertEqual(out['right_hand'],'SHIELD')

    def test_left_stop_and_right_fire_simultaneous(self):
        self.punch(hand('Left','FIST'))
        frames=[r for r in self.records if 'Fire' in r['triggered_actions']]
        self.assertEqual(len(frames),1)
        self.assertEqual(frames[0]['left_hand'],'STOP');self.assertEqual(frames[0]['right_hand'],'FIRE')
        self.assertEqual(frames[0]['move_axis'],0);self.assertNotIn('Shield',frames[0]['held_actions'])

    def test_left_stop_and_right_pinch_laser_simultaneous(self):
        out=self.feed(12,left=hand('Left','FIST'),right=hand('Right','PINCH'))
        self.assertEqual(set(out['held_actions']),{'Laser'})
        self.assertEqual(out['left_hand'],'STOP');self.assertEqual(out['right_hand'],'LASER')

    def test_left_forward_and_right_wave_simultaneous(self):
        self.feed(12,left=hand('Left','OPEN_PALM'),right=hand('Right','OPEN_PALM'))
        self.feed(12,left=hand('Left','OPEN_PALM',scale=.13),right=hand('Right','OPEN_PALM'))
        for x in (.29,.33,.37,.41,.45,.49):
            self.feed(left=hand('Left','OPEN_PALM',scale=.13),right=hand('Right','OPEN_PALM',x=x))
        frames=[r for r in self.records if 'Wave' in r['triggered_actions']]
        self.assertEqual(len(frames),1)
        self.assertEqual(frames[0]['left_hand'],'FORWARD');self.assertEqual(frames[0]['right_hand'],'WAVE')

    def test_missing_left_plus_right_laser(self):
        out=self.feed(12,right=hand('Right','PINCH'))
        self.assertEqual(out['left_hand'],'NONE');self.assertEqual(out['right_hand'],'LASER')

    def test_left_punch_never_becomes_attack(self):
        self.feed(12,left=hand('Left'))
        self.feed(12,left=hand('Left',scale=.14))
        self.assertEqual(self.events(),[]);self.assertEqual(self.out['right_hand'],'NONE')

    def test_short_unknown_fist_frames_do_not_cancel_fire(self):
        self.punch(blur=True);self.assertEqual(self.events(),['Fire'])

    def test_static_fist_and_small_scale_jitter_do_not_fire(self):
        for s in [.10,.105,.099,.107]*20:self.feed(right=hand(scale=s))
        self.assertEqual(self.events(),[])

    def test_single_scale_spike_does_not_fire(self):
        self.feed(12,right=hand());self.feed(right=hand(scale=.14));self.feed(12,right=hand())
        self.assertEqual(self.events(),[])

    def test_rearmed_fist_still_requires_push_confirmation(self):
        self.punch();self.feed(25,right=hand(scale=.12));self.feed(12,right=hand())
        self.feed(right=hand(scale=.14));self.feed(12,right=hand())
        self.assertEqual(self.events(),['Fire'])

    def test_fire_keeps_original_fist_requirement(self):
        for pose in ('OPEN_PALM','POINT','TWO_FINGERS','PINCH','NONE'):
            self.c.recalibrate();self.records=[]
            self.feed(12,right=hand(pose=pose));self.feed(12,right=hand(pose=pose,scale=.14))
            self.assertNotIn('Fire',self.events())

    def test_opening_fist_cancels_fire_preparation(self):
        self.feed(12,right=hand());self.feed(right=hand(pose='OPEN_PALM'))
        self.feed(12,right=hand(scale=.14));self.assertEqual(self.events(),[])

    def test_long_unknown_does_not_reuse_old_fist(self):
        self.feed(12,right=hand());self.feed(8,right=hand(pose='NONE'))
        self.feed(12,right=hand(scale=.14));self.assertEqual(self.events(),[])

    def test_actual_hand_loss_clears_fire_and_history_immediately(self):
        self.punch();out=self.feed(yaw=None)
        self.assertEqual(out['right_hand'],'NONE');self.assertFalse(out['debug']['fire_armed'])
        self.feed(12,right=hand(scale=.14));self.assertEqual(self.events(),['Fire'])

    def test_each_hand_loss_preserves_only_other_hand(self):
        left=hand('Left','FIST');right=hand('Right','PINCH')
        self.feed(12,left,right)
        out=self.feed(left=left);self.assertEqual(out['left_hand'],'STOP');self.assertEqual(out['right_hand'],'NONE')
        self.feed(12,left,right)
        out=self.feed(right=right);self.assertEqual(out['left_hand'],'NONE');self.assertEqual(out['right_hand'],'LASER')

    def test_offset_center_calibration_left_center_right(self):
        self.feed(12,yaw=11)
        for yaw,expected in ((-9,'LEFT'),(11,'NONE'),(31,'RIGHT'),(11,'NONE')):
            out=self.feed(25,yaw=yaw)
            self.assertEqual(out['head_direction'],expected)
            self.assertAlmostEqual(out['raw_yaw'],yaw)
            self.assertAlmostEqual(out['calibrated_yaw'],yaw-11)

    def test_neutral_noise_and_hysteresis(self):
        self.feed(12)
        for yaw in (-3,2,0,-1,3)*10:
            self.assertEqual(self.feed(yaw=yaw)['head_direction'],'NONE')
        self.assertEqual(self.feed(20,yaw=15)['head_direction'],'RIGHT')
        self.assertEqual(self.feed(20,yaw=6)['head_direction'],'RIGHT')
        self.assertEqual(self.feed(20,yaw=0)['head_direction'],'NONE')
        self.assertEqual(self.feed(20,yaw=-15)['head_direction'],'LEFT')
        self.assertEqual(self.feed(20,yaw=-6)['head_direction'],'LEFT')
        self.assertEqual(self.feed(20,yaw=0)['head_direction'],'NONE')

    def test_one_frame_head_spike_does_not_turn(self):
        self.feed(12);self.assertEqual(self.feed(yaw=30)['head_direction'],'NONE')
        self.assertEqual(self.feed(15)['head_direction'],'NONE')

    def test_exit_threshold_jitter_does_not_bypass_debounce(self):
        self.c.cfg['head_smoothing_ms']=1
        self.feed(12);self.feed(20,yaw=15)
        for yaw in (3.8,4.2)*15:
            self.assertEqual(self.feed(yaw=yaw)['head_direction'],'RIGHT')
        self.assertEqual(self.feed(10,yaw=0)['head_direction'],'NONE')

    def test_face_loss_clears_direction_and_yaw_immediately(self):
        self.feed(12);self.feed(25,yaw=25)
        out=self.feed(yaw=None)
        self.assertEqual(out['head_direction'],'NONE');self.assertEqual(out['turn_axis'],0)
        self.assertIsNone(out['raw_yaw']);self.assertIsNone(out['calibrated_yaw'])
        self.assertEqual(self.feed()['head_direction'],'NONE')

    def test_recenter_head_does_not_reset_left_control(self):
        left=hand('Left','FIST');self.feed(12,left,yaw=0)
        self.feed(20,left,yaw=20);self.c.recalibrate_head()
        out=self.feed(12,left,yaw=20)
        self.assertEqual(out['head_direction'],'NONE');self.assertEqual(out['left_hand'],'STOP')
        self.assertEqual(out['calibrated_yaw'],0)

    def test_json_backward_fields_and_new_channels(self):
        self.punch(hand('Left','FIST'))
        row=next(r for r in self.records if r['triggered_actions'])
        out=json.loads(json.dumps(packet('s',10,500,row),allow_nan=False))
        for key in ('version','session_id','frame_id','timestamp_ms','held_actions','events','triggered_actions','move_axis','turn_axis','pointer','movement','look','calibrated','tracking','debug'):
            self.assertIn(key,out)
        self.assertEqual(out['version'],3);self.assertEqual(out['right_hand'],'FIRE');self.assertEqual(out['left_hand'],'STOP')
        self.assertEqual(out['events'][0]['action'],'Fire')

    def test_debug_has_requested_fields_and_no_fake_rule_confidence(self):
        left=hand('Left','OPEN_PALM');left['method']='geometry'
        out=self.feed(12,left=left)
        lines='\n'.join(format_debug_lines(out,30))
        for label in ('Left Hand Gesture','Right Hand Gesture','Head Direction','Raw Yaw','Calibrated Yaw','Recognition Confidence'):
            self.assertIn(label,lines)
        self.assertIsNone(out['recognition_confidence']['left_hand']['pose'])
        self.assertIsNone(out['recognition_confidence']['head'])

    def test_shutdown_new_channels_are_none(self):
        out=release_packet('s',12,400)
        for key in ('left_hand','right_hand','head_direction'):self.assertEqual(out[key],'NONE')
        self.assertIsNone(out['raw_yaw'])


class ConfigAndClassifierTests(unittest.TestCase):
    def test_high_confidence_fist_not_overwritten_by_noisy_point_geometry(self):
        refs=json.loads((Path(__file__).parent/'tests/reference_landmarks.json').read_text())
        ref=next(r for r in refs if r['expected']=='POINT');h=ref['hands'][0]
        self.assertEqual(classify_hand(h['world'],'Closed_Fist',.9,image=h['image'],image_size=ref['size'])[0],'FIST')

    def test_old_config_loads_with_degree_defaults(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'settings.json';path.write_text(json.dumps({'head_deadzone':.055,'head_full_turn':.2,'fire_cooldown_ms':800}))
            with contextlib.redirect_stderr(io.StringIO()):cfg=load_settings(path)
        self.assertEqual(cfg['head_deadzone_deg'],DEFAULTS['head_deadzone_deg']);self.assertEqual(cfg['fire_cooldown_ms'],800)

    def test_default_runs_without_network_and_no_mirror_only_changes_preview(self):
        args=parse_args([]);self.assertEqual(args.udp_port,0);self.assertFalse(args.swap_hands);self.assertFalse(args.input_mirrored)
        self.assertFalse(parse_args(['--no-mirror']).swap_hands)


if __name__=='__main__':unittest.main()
