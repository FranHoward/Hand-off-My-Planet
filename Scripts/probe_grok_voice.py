"""Explicit live probe of the bundled Grok relay. Never prints credentials."""
import argparse,asyncio,base64,json,os,socket,subprocess,time,urllib.request,wave
from pathlib import Path

def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));return s.getsockname()[1]

async def receive(voice_port,output):
    from websockets.asyncio.client import connect
    pcm=bytearray();ready=False;identifier='grok-connection-probe'
    async with connect(f'ws://127.0.0.1:{voice_port}',open_timeout=10,max_size=2*1024*1024) as ws:
        async with asyncio.timeout(60):
            async for raw in ws:
                e=json.loads(raw);kind=e.get('type')
                if kind=='cosmo.error' or kind=='cosmo.tutorial.error':
                    raise RuntimeError(str(e.get('message','Voice relay error')))
                if kind=='cosmo.ready':
                    if e.get('source')!='grok' or e.get('sample_rate')!=24000:raise RuntimeError('Unexpected voice source or format')
                    ready=True;print('GROK_SESSION_READY source=grok sample_rate=24000',flush=True)
                    await ws.send(json.dumps({'type':'cosmo.say','utterance_id':identifier,'text':'Hello, partner. Grok voice is connected and ready.'}))
                if e.get('utterance_id')!=identifier:continue
                if kind in ('response.output_audio.delta','response.audio.delta'):
                    pcm.extend(base64.b64decode(e.get('delta',''),validate=True))
                if kind=='response.done':
                    if e.get('response',{}).get('status')!='completed':raise RuntimeError('Grok did not complete speech')
                    if not ready or len(pcm)<2400 or len(pcm)%2:raise RuntimeError('No valid speech audio returned')
                    output.parent.mkdir(parents=True,exist_ok=True)
                    with wave.open(str(output),'wb') as w:
                        w.setnchannels(1);w.setsampwidth(2);w.setframerate(24000);w.writeframes(pcm)
                    await ws.send(json.dumps({'type':'cosmo.tutorial_played','utterance_id':identifier,'keep_priority':False}))
                    print(f'GROK_VOICE_PROBE PASS audio_bytes={len(pcm)} duration_seconds={len(pcm)/48000:.2f}',flush=True)
                    return
    raise RuntimeError('Voice relay closed without speech')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--live',action='store_true');args=parser.parse_args()
    if not args.live:raise SystemExit('Pass --live to explicitly authorize a real Grok voice request.')
    root=Path.cwd();runtime=root/'Plugins/CosmoBridge/Resources/Runtime/Win64'
    http_port,voice_port=free_port(),free_port()
    while voice_port==http_port:voice_port=free_port()
    env=dict(os.environ,COSMO_AI_PROVIDER='auto')
    child=subprocess.Popen([str(runtime/'python/python.exe'),'-B',str(runtime/'app/cosmo_service.py'),'--parent-pid',str(os.getpid()),'--log-dir',str(root/'Saved/Logs/GrokLiveProbe'),'--port',str(http_port),'--voice-port',str(voice_port)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        for _ in range(100):
            if child.poll() is not None:raise RuntimeError('Bundled service exited during startup')
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{http_port}/health',timeout=.5) as r:health=json.load(r)
                if health.get('ok'):break
            except OSError:time.sleep(.15)
        else:raise RuntimeError('Bundled service health timed out')
        realtime=health.get('realtime',{})
        if realtime.get('status')!='listening' or not realtime.get('key_configured'):raise RuntimeError('Bundled service cannot read voice credentials or start the relay')
        print('BUNDLED_SERVICE_READY voice_credentials_present=True',flush=True)
        asyncio.run(receive(voice_port,root/'Saved/Audio/Grok_Connection_Test.wav'))
    except Exception as exc:
        from companion.realtime_voice import public_error
        import winreg
        secret=os.environ.get('XAI_API_KEY','')
        if not secret:
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER,'Environment') as k:secret,_=winreg.QueryValueEx(k,'XAI_API_KEY')
            except OSError:pass
        print('GROK_VOICE_PROBE FAIL '+public_error(str(exc),secret),flush=True)
        return 1
    finally:
        if child.poll() is None:child.terminate()
        child.wait(timeout=10)
    return 0
if __name__=='__main__':raise SystemExit(main())
