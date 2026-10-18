#!/usr/bin/env python3
"""Webcam -> left-hand movement, right-hand abilities, head turn -> JSON v3.

Run: python gesture_recognition.py
Uses MediaPipe Tasks (not the older mp.solutions API).
"""
import argparse
import json
import socket
import ssl
import sys
import time
import uuid
import urllib.request
from pathlib import Path

from gesture_logic import classify_hand, geometry_features
from game_controls import GameControls, DEFAULTS, palm_measurement, head_yaw_degrees, route_hands, packet, release_packet
from contextlib import ExitStack
from interaction_controls import InteractionControls

MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/gesture_recognizer/"
    "gesture_recognizer/float16/1/gesture_recognizer.task"
)
ROOT = Path(__file__).resolve().parent
CONNECTIONS = [(0,1),(1,2),(2,3),(3,4),(0,5),(5,6),(6,7),(7,8),
               (5,9),(9,10),(10,11),(11,12),(9,13),(13,14),(14,15),
               (15,16),(13,17),(0,17),(17,18),(18,19),(19,20)]


def ensure_model(path, url=MODEL_URL, allow_download=True):
    if path.is_file():
        return
    if not allow_download:
        raise FileNotFoundError(f"Bundled model missing: {path.name}. Repair the game runtime installation.")
    import certifi
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".download")
    print("Downloading the official MediaPipe model (first run only)...", file=sys.stderr)
    try:
        context = ssl.create_default_context(cafile=certifi.where())
        with urllib.request.urlopen(url, context=context, timeout=60) as response:
            with tmp.open("wb") as out:
                while chunk := response.read(1024*1024):
                    out.write(chunk)
        if tmp.stat().st_size < 1_000_000:
            raise RuntimeError("Model download is incomplete.")
        tmp.replace(path)
    except Exception as exc:
        tmp.unlink(missing_ok=True)
        raise RuntimeError(
            f"Cannot download model. Download {url} manually and save it to {path}."
        ) from exc


def make_hands(result, args, image_size=(1,1)):
    hands = []
    for i, landmarks in enumerate(result.hand_landmarks):
        world = [(p.x, p.y, p.z) for p in result.hand_world_landmarks[i]]
        points = [(p.x, p.y, p.z) for p in landmarks]
        categories = result.gestures[i]
        top = max(categories, key=lambda c: c.score) if categories else None
        model_label, model_score = (top.category_name, top.score) if top else ("None", 0.0)
        gesture, ratio, method = classify_hand(
            world, model_label, model_score, args.score, args.pinch, points, image_size
        )
        palm = [sum(getattr(landmarks[k], axis) for k in (0,5,9,13,17))/5
                for axis in ("x", "y")]
        xs, ys = [p.x for p in landmarks], [p.y for p in landmarks]
        handed = result.handedness[i]
        handed_top = max(handed, key=lambda c: c.score) if handed else None
        hands.append({
            "gesture": gesture, "handedness": handed_top.category_name if handed_top else "Unknown",
            "raw_handedness": handed_top.category_name if handed_top else "Unknown",
            "handedness_score": float(handed_top.score) if handed_top else 0.0,
            "scale": palm_measurement(points,image_size),
            "method": method, "model_label": model_label,
            "model_score": round(float(model_score), 4),
            "pinch_ratio": None if ratio is None else round(ratio, 4),
            "palm": palm, "index_tip": [landmarks[8].x, landmarks[8].y],
            "pinch_center": [(getattr(landmarks[4], axis)+getattr(landmarks[8], axis))/2
                             for axis in ("x", "y")],
            "area": (max(xs)-min(xs))*(max(ys)-min(ys)),
            "features": geometry_features(world, points, image_size),
        })
    return hands


FACE_MODEL_URL='https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task'


def display_coordinates(hands, mirrored):
    """Transform screen coordinates only; anatomical labels never depend on preview."""
    if mirrored:
        for hand in hands:
            for key in ('palm','index_tip','pinch_center'):
                if key in hand:hand[key]=[1-hand[key][0],hand[key][1]]


def format_debug_lines(control,fps,head_enabled=True):
    def number(value):return 'N/A' if value is None else f'{value:+.1f} deg'
    def score(value):return 'N/A' if value is None else f'{value:.2f}'
    cal=control['calibrated'];conf=control['recognition_confidence']
    left=conf['left_hand'];right=conf['right_hand']
    return [
        'Left Hand Gesture: '+(' + '.join(control['left_hand_actions']) or 'NONE'),
        'Right Hand Gesture: '+control['right_hand']+' | Head Direction: '+control['head_direction'],
        'Raw Yaw: '+number(control['raw_yaw'])+' | Calibrated Yaw: '+number(control['calibrated_yaw']),
        'Smoothed Yaw: '+number(control['smoothed_yaw'])+' | Neutral: '+number(control['head_neutral_yaw']),
        'Recognition Confidence (side/pose): L '+score(left['handedness'])+'/'+score(left['pose'])+
        ' R '+score(right['handedness'])+'/'+score(right['pose'])+' | Head: N/A',
        f"Pose: L {control['debug']['left_pose']} ({left['method']}) R {control['debug']['right_pose']} ({right['method']})",
        f"Neutral: left={'OK' if cal['left_hand'] else 'HOLD STILL'} head={('OK' if cal['head'] else 'FACE FRONT, HOLD') if head_enabled else 'OFF'} | {fps:.0f} FPS",
        f"Fire: {'ARMED' if control['debug']['fire_armed'] else 'LOCKED' if control['debug']['fire_locked'] else 'make fist, then push'}",
        'Point: scan | Right V: hint | Left V: repeat | Both V 1.5s: calibrate']


def draw(cv2,frame,result,hands,control,fps,head_enabled=True,mirrored=True):
    h,w=frame.shape[:2]
    for landmarks,hand in zip(result.hand_landmarks,hands):
        pts=[(int((1-p.x if mirrored else p.x)*w),int(p.y*h)) for p in landmarks]
        color=(240,180,50) if hand['handedness']=='Left' else (40,180,255)
        for a,b in CONNECTIONS:cv2.line(frame,pts[a],pts[b],color,2)
        for p in pts:cv2.circle(frame,p,3,color,-1)
        cv2.putText(frame,hand['handedness']+': '+hand['gesture']+('' if hand['accepted'] else ' (ignored)'),
            (max(0,min(pts[0][0],w-280)),max(15,min(h-15,pts[0][1]+25))),
            cv2.FONT_HERSHEY_SIMPLEX,.55,color,2)
    # Put diagnostics below the camera so the face/hands are never hidden by text.
    panel=cv2.copyMakeBorder(frame,0,254,0,max(0,960-w),cv2.BORDER_CONSTANT,value=(20,20,20))
    for i,line in enumerate(format_debug_lines(control,fps,head_enabled)):
        cv2.putText(panel,line,(12,h+24+27*i),cv2.FONT_HERSHEY_SIMPLEX,.52,(100,240,180),1)
    return panel


def parse_args(argv=None):
    p=argparse.ArgumentParser(description='Player left: move/fist STOP; right: open-palm Shield, fist-push Fire, Wave, pinch Laser; head: LEFT/NONE/RIGHT. JSON v3.')
    p.add_argument('--require-local-models',action='store_true',help='Never download models at runtime')
    p.add_argument('--camera',type=int,default=0)
    p.add_argument('--video',type=Path)
    p.add_argument('--image',type=Path,help='Static pose inspection only; motion cannot be inferred from one image')
    p.add_argument('--model',type=Path,default=ROOT/'models/gesture_recognizer.task')
    p.add_argument('--face-model',type=Path,default=ROOT/'models/face_landmarker.task')
    p.add_argument('--controls',type=Path,default=ROOT/'recognition_settings.json')
    p.add_argument('--score',type=float,default=.65)
    p.add_argument('--pinch',type=float,default=.30)
    p.add_argument('--udp-host',default='127.0.0.1')
    p.add_argument('--udp-port',type=int,default=0,help='Optional existing UDP output; 0 disables networking (default)')
    p.add_argument('--swap-hands',action='store_true')
    p.add_argument('--invert-turn',action='store_true')
    p.add_argument('--no-head',action='store_true',help='Disable head detection; always output turn_axis=0')
    p.add_argument('--no-mirror',action='store_true',help='Disable preview mirror only; does not swap player hands')
    p.add_argument('--input-mirrored',action='store_true',help='Undo mirroring already applied by a virtual camera/video source')
    p.add_argument('--headless',action='store_true')
    p.add_argument('--preview-port',type=int,default=0,help='Loopback UE camera preview; 0 disables streaming')
    p.add_argument('--max-frames',type=int,default=0)
    p.add_argument('--jsonl',type=Path)
    p.add_argument('--runtime-log',type=Path,help='Log recognizer diagnostics when launched by Unreal')
    a=p.parse_args(argv)
    if a.image and a.video:p.error('Choose --image or --video')
    if not 0<=a.udp_port<=65535 or not 0<=a.preview_port<=65535 or a.max_frames<0:p.error('Invalid port/frame count')
    if not 0<=a.score<=1 or not 0<a.pinch<2:p.error('Invalid score/pinch')
    return a


def load_settings(path):
    value=json.loads(path.read_text(encoding='utf-8'))
    legacy={'head_deadzone','head_full_turn'}
    if not isinstance(value,dict) or set(value)-set(DEFAULTS)-legacy:raise ValueError('Invalid recognition settings keys')
    if set(value)&legacy:
        print('Old nose-offset thresholds ignored; face forward for automatic calibration.',file=sys.stderr)
    value={**DEFAULTS,**{k:v for k,v in value.items() if k not in legacy}}
    if any(type(v) not in (float,int) or not 0<v<10000 for v in value.values()):raise ValueError('Invalid recognition settings values')
    if not (0<value['move_enter_backward']<value['move_exit_backward']<1<value['move_exit_forward']<value['move_enter_forward']):raise ValueError('Invalid movement thresholds')
    if not (0<value['head_exit_deadzone_deg']<value['head_deadzone_deg']<value['head_full_turn_deg']<90) or value['fire_push_ratio']<=1:raise ValueError('Invalid look/fire thresholds')
    return value


def run(args):
    import cv2
    import mediapipe as mp
    from mediapipe.tasks import python
    from mediapipe.tasks.python import vision
    ensure_model(args.model, allow_download=not args.require_local_models)
    if args.image:
        frame=cv2.imread(str(args.image))
        if frame is None:raise RuntimeError('Cannot read image')
        if args.input_mirrored:frame=cv2.flip(frame,1)
        options=vision.GestureRecognizerOptions(base_options=python.BaseOptions(model_asset_buffer=args.model.read_bytes()),num_hands=2)
        with vision.GestureRecognizer.create_from_options(options) as recognizer:
            result=recognizer.recognize(mp.Image(image_format=mp.ImageFormat.SRGB,data=cv2.cvtColor(frame,cv2.COLOR_BGR2RGB)))
        hands=make_hands(result,args,(frame.shape[1],frame.shape[0]))
        route_hands(hands,args.swap_hands)
        print(json.dumps({'mode':'pose_inspection_only','hands':hands},allow_nan=False));return
    controls=InteractionControls(load_settings(args.controls))
    if not args.no_head:ensure_model(args.face_model,FACE_MODEL_URL,allow_download=not args.require_local_models)
    cap=cv2.VideoCapture(str(args.video) if args.video else args.camera)
    if not cap.isOpened():cap.release();raise RuntimeError('Cannot open camera/video; check permission or --camera 1')
    if not args.video:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH,960);cap.set(cv2.CAP_PROP_FRAME_HEIGHT,540)
    fps=cap.get(cv2.CAP_PROP_FPS);fps=fps if 1<=fps<=240 else 30.
    sid=uuid.uuid4().hex;frame_id=0;stamp=-1;start=time.monotonic();last_print=None;last_print_ms=-1000;sock=None;log=None;ue_preview=None
    try:
        if args.preview_port:
            from camera_preview import CameraPreview
            try:ue_preview=CameraPreview(args.preview_port).start()
            except OSError:print('UE preview port unavailable; recognition continues without preview.',file=sys.stderr)
        if args.udp_port:sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        if args.jsonl:log=args.jsonl.open('w',encoding='utf-8')
        with ExitStack() as stack:
            hand_options=vision.GestureRecognizerOptions(base_options=python.BaseOptions(model_asset_buffer=args.model.read_bytes()),running_mode=vision.RunningMode.VIDEO,num_hands=2,min_hand_detection_confidence=.6,min_hand_presence_confidence=.6,min_tracking_confidence=.6)
            recognizer=stack.enter_context(vision.GestureRecognizer.create_from_options(hand_options))
            face=None
            if not args.no_head:
                face_options=vision.FaceLandmarkerOptions(base_options=python.BaseOptions(model_asset_buffer=args.face_model.read_bytes()),running_mode=vision.RunningMode.VIDEO,num_faces=1,output_facial_transformation_matrixes=True,min_face_detection_confidence=.6,min_face_presence_confidence=.6,min_tracking_confidence=.6)
                face=stack.enter_context(vision.FaceLandmarker.create_from_options(face_options))
            while True:
                ok,frame=cap.read()
                if not ok:
                    if not args.video:raise RuntimeError('Camera frame read failed')
                    break
                tick=time.monotonic();stamp=max(stamp+1,int(frame_id*1000/fps) if args.video else int((tick-start)*1000))
                # Inference always uses the physical, unmirrored image. Preview
                # mirroring happens only AFTER both models, and never swaps labels.
                if args.input_mirrored:frame=cv2.flip(frame,1)
                im=mp.Image(image_format=mp.ImageFormat.SRGB,data=cv2.cvtColor(frame,cv2.COLOR_BGR2RGB))
                result=recognizer.recognize_for_video(im,stamp)
                hands=make_hands(result,args,(frame.shape[1],frame.shape[0]))
                sides=route_hands(hands,args.swap_hands)
                display_coordinates(hands,not args.no_mirror)
                yaw=None
                if face:
                    faces=face.detect_for_video(im,stamp)
                    if faces.face_landmarks and len(faces.facial_transformation_matrixes):
                        yaw=head_yaw_degrees(faces.facial_transformation_matrixes[0])
                        if yaw is not None and args.invert_turn:yaw=-yaw
                state=controls.update(sides.get('Left'),sides.get('Right'),yaw,stamp)
                out=packet(sid,frame_id,stamp,state)
                out['hands']=[{k:h[k] for k in ('handedness','raw_handedness','handedness_score','accepted','gesture','method','model_label','model_score','palm','scale','index_tip')} for h in hands]
                payload=json.dumps(out,separators=(',',':'),allow_nan=False)
                if sock:sock.sendto(payload.encode('utf-8'),(args.udp_host,args.udp_port))
                if log:log.write(payload+'\n');log.flush()
                signature=(tuple(state['held_actions']),state['left_hand'],state['right_hand'],state['head_direction'],tuple(state['calibrated'].values()))
                if signature!=last_print or state['triggered_actions'] or stamp-last_print_ms>=500:
                    print(payload,flush=True);last_print=signature;last_print_ms=stamp
                if ue_preview or not args.headless:
                    preview=cv2.flip(frame,1) if not args.no_mirror else frame.copy()
                    preview=draw(cv2,preview,result,hands,state,1/max(time.monotonic()-tick,1e-6),not args.no_head,not args.no_mirror)
                    if ue_preview:ue_preview.publish(preview[:frame.shape[0],:frame.shape[1]])
                if not args.headless:
                    cv2.imshow('DubHacks - Gesture Controls',preview)
                    cv2.waitKey(1)  # Pump the preview window; keys do not control gameplay.
                    if cv2.getWindowProperty('DubHacks - Gesture Controls',cv2.WND_PROP_VISIBLE)<1:break
                frame_id+=1
                if args.max_frames and frame_id>=args.max_frames:break
    finally:
        final=json.dumps(release_packet(sid,frame_id+1,stamp+1))
        print(final,flush=True)
        if sock:
            try:sock.sendto(final.encode('utf-8'),(args.udp_host,args.udp_port))
            except OSError:pass
            sock.close()
        if log:log.write(final+'\n');log.close()
        cap.release()
        if ue_preview:ue_preview.stop()
        if not args.headless:cv2.destroyAllWindows()


if __name__=='__main__':
    args=parse_args()
    if args.runtime_log:
        args.runtime_log.parent.mkdir(parents=True,exist_ok=True)
        sys.stdout=sys.stderr=args.runtime_log.open('w',encoding='utf-8',buffering=1)
    try:run(args)
    except KeyboardInterrupt:pass
    except Exception as exc:print('Error: '+str(exc),file=sys.stderr);sys.exit(1)
