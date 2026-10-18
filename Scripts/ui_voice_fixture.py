"""Local synthetic audio for UI sequencing QA. Does not call Grok or use a microphone."""
import asyncio,base64,json,struct
from websockets.asyncio.server import serve
async def handle(ws):
    first=True
    feedback_failure=True
    pending=""
    async def send(kind,**kw): await ws.send(json.dumps({'type':kind,**kw}))
    await asyncio.sleep(.3)
    await send('cosmo.ready',source='grok',sample_rate=24000,tutorial_priority=True,model='offline-ui-fixture')
    async for raw in ws:
        e=json.loads(raw)
        if e.get('type')=='cosmo.tutorial_played' and e.get('utterance_id')==pending:
            pending=''
        if e.get('type')=='cosmo.say':
            if pending:
                await send('cosmo.error',message='Previous utterance was not acknowledged')
                continue
            pending=e['utterance_id']
            fields={'cosmo_channel':'tutorial','utterance_id':e['utterance_id']}
            if first or (feedback_failure and e.get("text")=="Shield activated!"):
                if first: first=False
                else: feedback_failure=False
                await send('cosmo.tutorial.error',utterance_id=e['utterance_id'])
                continue
            await send('response.created',response={'id':e['utterance_id']},**fields)
            await send('response.output_audio.delta',delta=base64.b64encode(struct.pack('<h',0)*24000).decode(),**fields)
            # Completion arrives before a full second of queued audio is consumed.
            await send('response.done',response={'status':'completed'},**fields)
            print('UI_FIXTURE_SENT',e['utterance_id'],flush=True)
async def main():
    async with serve(handle,'127.0.0.1',18766):
        print('UI_FIXTURE_READY synthetic=true grok=false',flush=True)
        await asyncio.Event().wait()
asyncio.run(main())
