"""Player-left movement/fist stop; player-right Shield/Fire/Wave/pinch Laser."""
from collections import deque
from math import atan2, degrees, dist, exp, hypot, isfinite, sqrt
from statistics import median
from gesture_logic import GestureState

HOLD_ACTIONS = ('MoveForward','MoveBackward','Shield','Laser','TurnLeft','TurnRight')
PULSE_ACTIONS = ('Fire','Wave')
COMBAT_ACTIONS = ('Shield','Fire','Wave','Laser')
DEFAULTS = dict(calibration_ms=900, pose_hold_ms=160, move_enter_forward=1.16,
    move_exit_forward=1.07, move_enter_backward=.86, move_exit_backward=.94,
    move_debounce_ms=120, head_deadzone_deg=8., head_exit_deadzone_deg=4.,
    head_full_turn_deg=28., head_smoothing_ms=90, head_debounce_ms=100,
    head_calibration_tolerance_deg=3.5,
    fire_push_ratio=1.15, fire_window_ms=750, fire_cooldown_ms=500,
    fire_pose_grace_ms=120, fire_confirm_ms=35,
    wave_distance=.16, wave_window_ms=450, wave_cooldown_ms=700,
    action_suppress_ms=240, event_retention_ms=500)


def route_hands(hands, swap=False, minimum_confidence=.60):
    """Tasks labels on UNMIRRORED input are player-relative, never screen sides.

    Only the highest-confidence result for a side is accepted. A missing/uncertain
    side stays missing: never fill it with the other hand or rely on result order.
    Display mirroring must not be passed into this function.
    """
    sides = {}
    for hand in hands:
        raw = hand.get('raw_handedness', hand.get('handedness', 'Unknown'))
        side = {'Left': 'Right', 'Right': 'Left'}.get(raw, 'Unknown') if swap else raw
        hand['raw_handedness'] = raw
        hand['handedness'] = side
        hand['accepted'] = False
        score = hand.get('handedness_score', 0.)
        if side not in ('Left', 'Right') or not isfinite(score) or score < minimum_confidence:
            continue
        old = sides.get(side)
        if old is None or (score, hand.get('area', 0)) > (old['handedness_score'], old.get('area', 0)):
            sides[side] = hand
    for hand in sides.values():
        hand['accepted'] = True
    return sides


def head_yaw_degrees(matrix):
    """Yaw from MediaPipe's canonical-to-camera face rotation on unmirrored input.

    R = Rz(roll) Ry(yaw) Rx(pitch); remove uniform scale. Canonical +Z faces
    the viewer. A player's right turn is negative camera Y rotation, so negate
    conventional yaw. Output: LEFT < 0, RIGHT > 0, in estimated degrees.
    """
    try:
        r = [[float(matrix[i][j]) for j in range(3)] for i in range(3)]
        if not all(isfinite(v) for row in r for v in row):
            return None
        norm = sqrt(sum(r[i][0] ** 2 for i in range(3)))
        if norm < 1e-6:
            return None
        determinant = (r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])
                       - r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])
                       + r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]))
        if determinant <= 0:
            return None
        return degrees(atan2(r[2][0]/norm, hypot(r[0][0], r[1][0])/norm))
    except (TypeError, IndexError, ValueError):
        return None


def hand_confidence(hand):
    """Expose actual model scores, not invented probabilities for motion rules."""
    if hand is None:
        return {'handedness': None, 'pose': None, 'method': 'missing',
                'model_label': None, 'model_score': None}
    method = hand.get('method', 'rule')
    score = hand.get('model_score')
    return {'handedness': hand.get('handedness_score'),
            'pose': score if method == 'model' else None, 'method': method,
            'model_label': hand.get('model_label'), 'model_score': score}


def palm_measurement(points, image_size):
    """Use MCP/wrist geometry rather than finger spread or fake absolute world depth."""
    w,h=image_size
    xy=[(p[0],p[1]*h/w) for p in points]
    scale=(dist(xy[5],xy[17])+dist(xy[0],xy[9]))/2
    return scale


def head_feature(points, image_size=(1,1), mirrored=True):
    """Roll/translation/scale-normalized nose offset; a yaw proxy, NOT true degrees."""
    if len(points)<264:return None
    w,h=image_size
    a,b=sorted([(points[k][0]*w,points[k][1]*h) for k in (33,263)])
    n=(points[1][0]*w,points[1][1]*h)
    dx,dy=b[0]-a[0],b[1]-a[1];den=dx*dx+dy*dy
    if den<1e-6:return None
    value=((n[0]-(a[0]+b[0])/2)*dx+(n[1]-(a[1]+b[1])/2)*dy)/den
    value=value if mirrored else -value
    return value if isfinite(value) and abs(value)<1 else None


class Calibration:
    def __init__(self,duration,relative=False,tolerance=.045):
        self.duration=duration;self.relative=relative;self.tolerance=tolerance;self.samples=deque();self.value=None
    def update(self,value,t):
        if value is None:self.samples.clear();return self.value
        if self.value is not None:return self.value
        self.samples.append((t,value))
        while len(self.samples)>1 and t-self.samples[0][0]>self.duration+150:self.samples.popleft()
        values=[x[1] for x in self.samples]
        if len(values)>=8 and t-self.samples[0][0]>=self.duration:
            mid=median(values);spread=max(values)-min(values)
            if spread <= (.12*mid if self.relative else self.tolerance):self.value=mid
        return self.value


class GameControls:
    def __init__(self,settings=None):
        self.cfg=dict(DEFAULTS);self.cfg.update(settings or {})
        self.event_id=0;self.events=deque();self.last_time=None
        self.recalibrate()
    def recalibrate(self):
        c=self.cfg
        self.left_cal=Calibration(c['calibration_ms'],True)
        self.recalibrate_head()
        self.left_ema=None;self.head_ema=None;self.move=0;self.move_candidate=0;self.move_since=0
        self.left_state=GestureState(hold_ms=c['pose_hold_ms'])
        self.left_stop=False
        self.right_state=GestureState(hold_ms=c['pose_hold_ms'])
        self.history=deque();self.fist_since=None;self.fire_locked=False;self.fire_base=0
        self.last_fire=-100000;self.last_wave=-100000;self.suppress_until=0
        self.last_right=None;self.last_fist=None;self.fire_candidate=None;self.last_attack='NONE';self.events.clear()
    def recalibrate_head(self):
        self.head_cal=Calibration(self.cfg['calibration_ms'],tolerance=self.cfg['head_calibration_tolerance_deg'])
        self.head_ema=None;self.head_direction=0;self.head_candidate=0;self.head_since=0
    def _emit(self,action,t):
        self.event_id+=1;self.events.append({'id':self.event_id,'action':action,'timestamp_ms':t})
    def update(self,left,right,head,t):
        if self.last_time is not None and t<self.last_time:raise ValueError('timestamps must increase')
        gap=self.last_time is not None and t-self.last_time>400
        dt=33 if self.last_time is None else max(1,t-self.last_time)
        self.last_time=t;c=self.cfg;alpha=1-exp(-dt/70)
        if gap:
            self.history.clear();self.fist_since=None;self.last_right=None
            self.last_fist=None;self.fire_candidate=None;self.fire_locked=False
            self.left_ema=None;self.head_ema=None;self.move=0;self.move_candidate=0
            self.head_direction=0;self.head_candidate=0
            self.left_state=GestureState(hold_ms=c['pose_hold_ms'])
            self.right_state=GestureState(hold_ms=c['pose_hold_ms'])
        held=[];new=[]
        left_raw=left['gesture'] if left else 'NONE'
        left_pose=self.left_state.update(left_raw,t,left is not None)['gesture']
        if not left:
            self.left_stop=False
        elif left_raw=='FIST':
            # Stop on the FIRST recognized fist frame, regardless of hand depth.
            self.left_stop=True
        elif (self.left_stop and left_raw!='NONE' and left_pose==left_raw
              and t-self.left_state.candidate_since>=c['pose_hold_ms']):
            # Require a confirmed non-fist pose to resume. Brief classification
            # gaps must not restart movement while the player is holding a fist.
            self.left_stop=False
        scale=left.get('scale') if left else None
        if scale is not None and (not isfinite(scale) or scale<=0):scale=None
        neutral=self.left_cal.update(None if self.left_stop else scale,t)
        ratio=None
        if scale is None or self.left_stop:
            self.left_ema=None;self.move=0;self.move_candidate=0;self.move_since=t
        elif neutral:
            self.left_ema=scale if self.left_ema is None else self.left_ema+alpha*(scale-self.left_ema)
            ratio=self.left_ema/neutral
            target=(1 if ratio>=c['move_enter_forward'] else -1 if ratio<=c['move_enter_backward'] else 0)
            if self.move==1 and ratio>c['move_exit_forward']:target=1
            if self.move==-1 and ratio<c['move_exit_backward']:target=-1
            if target!=self.move_candidate:self.move_candidate=target;self.move_since=t
            if t-self.move_since>=c['move_debounce_ms']:self.move=target
        if self.move:held.append('MoveForward' if self.move>0 else 'MoveBackward')
        head=head if head is not None and isfinite(head) else None
        head_neutral=self.head_cal.update(head,t);turn=0.;head_delta=None;calibrated_yaw=None
        if head is None:
            self.head_ema=None;self.head_direction=0;self.head_candidate=0;self.head_since=t
        elif head_neutral is not None:
            head_alpha=1-exp(-dt/c['head_smoothing_ms'])
            self.head_ema=head if self.head_ema is None else self.head_ema+head_alpha*(head-self.head_ema)
            calibrated_yaw=head-head_neutral
            head_delta=self.head_ema-head_neutral
            target=1 if head_delta>=c['head_deadzone_deg'] else -1 if head_delta<=-c['head_deadzone_deg'] else 0
            if self.head_direction==1 and head_delta>c['head_exit_deadzone_deg']:target=1
            if self.head_direction==-1 and head_delta<-c['head_exit_deadzone_deg']:target=-1
            if target!=self.head_candidate:self.head_candidate=target;self.head_since=t
            if t-self.head_since>=c['head_debounce_ms']:self.head_direction=target
            if self.head_direction:
                amount=(head_delta*self.head_direction-c['head_exit_deadzone_deg'])/(c['head_full_turn_deg']-c['head_exit_deadzone_deg'])
                # Keep the same direction through the short exit debounce. A zero
                # axis here would bypass hysteresis and make the displayed label
                # flicker NONE/RIGHT at the exit boundary.
                turn=min(1.,max(.05,amount))*self.head_direction
        if turn:held.append('TurnRight' if turn>0 else 'TurnLeft')
        pointer=None;stable='NONE';fire_ratio=None;fire_anchor=None
        if not right:
            self.right_state.update('NONE',t,False);self.history.clear();self.fist_since=None
            self.last_right=None;self.fire_locked=False
            self.last_fist=None;self.fire_candidate=None;self.last_attack='NONE';self.suppress_until=0
        else:
            pose=right['gesture'];s=right['scale'];x,y=right['palm']
            if self.last_right is not None and dist((x,y),self.last_right)>.30:
                self.history.clear();self.fist_since=None;self.fire_locked=False
                self.last_fist=None;self.fire_candidate=None;self.last_attack='NONE';self.suppress_until=0
                self.right_state=GestureState(hold_ms=c['pose_hold_ms'])
            self.last_right=(x,y)
            stable=self.right_state.update(pose,t,True)['gesture']
            self.history.append((t,x,y,s,pose))
            while self.history and t-self.history[0][0]>max(c['fire_window_ms'],c['wave_window_ms']):self.history.popleft()
            if pose=='FIST':
                if self.fist_since is None:self.fist_since=t
                self.last_fist=t
                if self.fire_locked and s<=self.fire_base*1.08 and t-self.last_fire>=c['fire_cooldown_ms']:
                    self.fire_locked=False;self.fist_since=t;self.history.clear();self.fire_candidate=None
                eligible=[h for h in self.history if h[4]=='FIST' and 80<=t-h[0]<=c['fire_window_ms']]
                if not self.fire_locked and t-self.fist_since>=c['pose_hold_ms'] and eligible:
                    base=median(h[3] for h in eligible[:3])
                    fire_anchor=base;fire_ratio=s/base if base>0 else None
                    if base>0 and s/base>=c['fire_push_ratio'] and t-self.last_fire>=c['fire_cooldown_ms']:
                        if self.fire_candidate is None:self.fire_candidate=t
                        if t-self.fire_candidate>=c['fire_confirm_ms']:
                            self._emit('Fire',t);new.append('Fire');self.last_fire=t
                            self.fire_locked=True;self.fire_base=base;self.suppress_until=t+c['action_suppress_ms'];self.last_attack='FIRE'
                    else:self.fire_candidate=None
            else:
                fist_grace=pose=='NONE' and self.last_fist is not None and t-self.last_fist<=c['fire_pose_grace_ms']
                if not fist_grace:
                    self.fist_since=None;self.fire_locked=False;self.last_fist=None;self.fire_candidate=None
                    # An explicit new pose must not reuse the previous fist's anchor.
                    self.history=deque(h for h in self.history if h[4]!='FIST')
                if not fist_grace and pose not in ('POINT','PINCH') and stable!='PINCH' and t-self.last_wave>=c['wave_cooldown_ms']:
                    eligible=[h for h in self.history if 100<=t-h[0]<=c['wave_window_ms'] and h[4] not in ('POINT','FIST','PINCH')]
                    for h in eligible:
                        dx,dy=x-h[1],y-h[2]
                        if abs(dx)>=c['wave_distance'] and abs(dx)>1.8*abs(dy) and .8<s/max(h[3],1e-6)<1.2:
                            self._emit('Wave',t);new.append('Wave');self.last_wave=t
                            self.suppress_until=t+c['action_suppress_ms'];self.last_attack='WAVE';self.history.clear();break
            if t>=self.suppress_until:
                if stable=='PINCH' and pose in ('PINCH','NONE'):
                    held.append('Laser');pointer=right.get('pinch_center',right.get('index_tip'))
                elif stable=='OPEN_PALM' and pose in ('OPEN_PALM','NONE'):
                    recent=[h for h in self.history if 70<=t-h[0]<=160]
                    stationary=bool(recent) and all(
                        dist((x,y),(h[1],h[2]))<=.035 and .9<=s/max(h[3],1e-6)<=1.1
                        for h in recent)
                    if stationary:held.append('Shield')
        left_actions=[]
        if self.left_stop:left_actions.append('STOP')
        elif self.move:left_actions.append('FORWARD' if self.move>0 else 'BACKWARD')
        right_gesture=('LASER' if 'Laser' in held else 'SHIELD' if 'Shield' in held
                       else self.last_attack if right and t<self.suppress_until else 'NONE')
        while self.events and t-self.events[0]['timestamp_ms']>c['event_retention_ms']:self.events.popleft()
        return {'held_actions':held,'events':list(self.events),'triggered_actions':new,
                'left_hand':left_actions[0] if left_actions else 'NONE','left_hand_actions':left_actions,
                'right_hand':right_gesture,'head_direction':'RIGHT' if turn>0 else 'LEFT' if turn<0 else 'NONE',
                'raw_yaw':head,'calibrated_yaw':calibrated_yaw,'smoothed_yaw':head_delta,
                'yaw_unit':'degrees','head_neutral_yaw':head_neutral,
                'recognition_confidence':{'left_hand':hand_confidence(left),'right_hand':hand_confidence(right),'head':None},
                'move_axis':float(self.move),'turn_axis':round(turn,4),'pointer':pointer,
                'movement':'MoveForward' if self.move>0 else 'MoveBackward' if self.move<0 else 'Stop',
                'look':'TurnRight' if turn>0 else 'TurnLeft' if turn<0 else 'Center',
                'calibrated':{'left_hand':self.left_cal.value is not None,'head':self.head_cal.value is not None},
                'tracking':{'left_hand':left is not None,'right_hand':right is not None,'face':head is not None},
                'debug':{'left_scale_ratio':ratio,'left_stop':self.left_stop,'head_offset':head_delta,'left_pose':left_pose,'right_pose':stable,
                         'fire_armed':self.fist_since is not None and t-self.fist_since>=c['pose_hold_ms'] and not self.fire_locked,
                         'fire_locked':self.fire_locked,'fire_scale':right.get('scale') if right else None,
                         'fire_baseline':self.fire_base if self.fire_locked else fire_anchor,
                         'fire_push_ratio':fire_ratio,'fire_required_ratio':c['fire_push_ratio']}}


def packet(session,frame,t,result):
    return dict(version=3,session_id=session,frame_id=frame,timestamp_ms=t,**result)


def release_packet(session,frame,t):
    return packet(session,frame,t,{'held_actions':[],'events':[],'triggered_actions':[],
        'left_hand':'NONE','left_hand_actions':[],'right_hand':'NONE','head_direction':'NONE',
        'raw_yaw':None,'calibrated_yaw':None,'smoothed_yaw':None,'head_neutral_yaw':None,'yaw_unit':'degrees',
        'recognition_confidence':{'left_hand':hand_confidence(None),'right_hand':hand_confidence(None),'head':None},
        'move_axis':0.,'turn_axis':0.,'pointer':None,'movement':'Stop','look':'Center',
        'calibrated':{'left_hand':False,'head':False},
        'tracking':{'left_hand':False,'right_hand':False,'face':False}})
