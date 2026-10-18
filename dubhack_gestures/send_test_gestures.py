#!/usr/bin/env python3
"""Send JSON v3 actions to UE, without camera or ML. Ctrl+C releases all actions."""
import argparse,json,socket,time,uuid
from game_controls import HOLD_ACTIONS,PULSE_ACTIONS,packet,release_packet


def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--action',choices=HOLD_ACTIONS+PULSE_ACTIONS)
 p.add_argument('--combo',action='store_true',help='MoveForward + TurnRight + Shield simultaneously')
 p.add_argument('--host',default='127.0.0.1');p.add_argument('--port',type=int,default=5052)
 p.add_argument('--seconds',type=float,default=2)
 a=p.parse_args()
 if not 1<=a.port<=65535 or not .1<=a.seconds<=60:p.error('Invalid port or seconds')
 sequences=[['MoveForward','TurnRight','Shield']] if a.combo else [[a.action]] if a.action else [[x] for x in HOLD_ACTIONS+PULSE_ACTIONS]
 sid=uuid.uuid4().hex;start=time.monotonic();frame=0;eid=0
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
  def send(actions,events):
   nonlocal frame
   now=int((time.monotonic()-start)*1000)
   held=[x for x in actions if x in HOLD_ACTIONS]
   data={'held_actions':held,'events':[e for e in events if now-e['timestamp_ms']<=500],
    'move_axis':1. if 'MoveForward' in held else -1. if 'MoveBackward' in held else 0.,
    'turn_axis':.7 if 'TurnRight' in held else -.7 if 'TurnLeft' in held else 0.,
    'pointer':[.5,.5] if 'Laser' in held else None}
   sock.sendto(json.dumps(packet(sid,frame,now,data)).encode(),(a.host,a.port));frame+=1
  try:
   for actions in sequences:
    print(' + '.join(actions),flush=True);events=[]
    for action in actions:
     if action in PULSE_ACTIONS:
      eid+=1;events.append(dict(id=eid,action=action,timestamp_ms=int((time.monotonic()-start)*1000)))
    end=time.monotonic()+a.seconds
    while time.monotonic()<end:send(actions,events);time.sleep(1/30)
    for _ in range(15):send([],[]);time.sleep(1/30)
  except KeyboardInterrupt:pass
  finally:
   sock.sendto(json.dumps(release_packet(sid,frame,int((time.monotonic()-start)*1000))).encode(),(a.host,a.port))

if __name__=='__main__':main()
