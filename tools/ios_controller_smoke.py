"""Run the actual Objective-C controller in iOS Simulator against loopback TLS.
Only newly generated test credentials and a synthetic grayscale H.264 fixture are used.
"""
import json,socket,ssl,subprocess,sys,tempfile,threading,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT))
from tools.provision import provision
from automation import protocol as wire
BUNDLE='io.github.justintunsday.iosremotebridge.controller'
SIMULATOR=sys.argv[1]
APP=next((ROOT/'build/controller-simulator').rglob('iOSRemoteBridge.app'))
LOGS=ROOT/'build-logs'
FIXTURE=(ROOT/'client/ios/Tests/gray-full.h264').read_bytes()
def sim(*args,check=True,timeout=60):
    print('simctl '+args[0],flush=True)
    completed=subprocess.run(['xcrun','simctl',*args],check=False,capture_output=True,text=True,timeout=timeout)
    if check and completed.returncode:
        raise RuntimeError(f'simctl {args[0]} failed: {completed.stderr[:3000]}')
    return completed
def warmup():
    sim('install',SIMULATOR,str(APP))
    container=Path(sim('get_app_container',SIMULATOR,BUNDLE,'data').stdout.strip())
    ready=container/'Library/Caches/irb-warmup-ready'
    if ready.exists():ready.unlink()
    # A fresh hosted Simulator can take minutes on its first app launch. Keep
    # that cost outside the short-lived TLS server's handshake/credit timeouts.
    sim('launch',SIMULATOR,BUNDLE,'--irb-warmup',timeout=240)
    deadline=time.monotonic()+30
    while not ready.exists() and time.monotonic()<deadline:time.sleep(.25)
    assert ready.exists(),'first launch did not finish controller initialization'
    sim('terminate',SIMULATOR,BUNDLE)
    print('Cold simulator launch completed',flush=True)

def diagnostics():
    for arguments,name in [
        (('io',SIMULATOR,'screenshot',str(LOGS/'controller-failure.png')),'controller-failure-screenshot.log'),
        (('spawn',SIMULATOR,'log','show','--info','--last','3m','--predicate',f'eventMessage CONTAINS "{BUNDLE}" OR process == "iOSRemoteBridge"'),'controller-failure-system.log')]:
        try:
            output=sim(*arguments,check=False,timeout=25)
            (LOGS/name).write_text((output.stdout+'\n'+output.stderr)[-200000:])
        except subprocess.TimeoutExpired:(LOGS/name).write_text('Simulator diagnostic command timed out')
def scenario(wrong_pin,preview=None):
    name='wrong-pin' if wrong_pin else 'stream-and-input'+('-'+preview if preview else '')
    print('Starting '+name,flush=True)
    with tempfile.TemporaryDirectory() as temporary:
        pairing=Path(temporary)/'pairing';provision('127.0.0.1',pairing)
        config=json.loads((pairing/'client-config.json').read_text())
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(45)
        config['port']=listener.getsockname()[1]
        if wrong_pin: config['certificate_sha256']='0'*64
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(pairing/'server.crt',pairing/'server.key')
        observed={'auth':False,'credits':[],'operations':[],'error':None}
        all_credits=threading.Event()
        def server():
            try:
                raw,_=listener.accept()
                with context.wrap_socket(raw,server_side=True) as sock:
                    sock.settimeout(45)
                    h=wire.unpack(wire.recv_exact(sock,24));auth=json.loads(wire.recv_exact(sock,h.length))
                    if h.kind!=wire.AUTH or auth.get('token')!=config['token']: raise ValueError('incorrect authentication')
                    observed['auth']=True
                    if auth.get('video_window')!=3 or auth.get('fps')!=60 or auth.get('control') is not True or auth.get('lockscreen_view') is not True: raise ValueError('interactive negotiation mismatch')
                    sequence=0;rx=h.sequence
                    def send(kind,payload,pts=0):
                        nonlocal sequence
                        sequence+=1;body=json.dumps(payload).encode() if isinstance(payload,dict) else payload
                        packet=wire.Header(kind,1 if kind==wire.VIDEO else 0,len(body),sequence,pts).pack()+body
                        for offset in range(0,len(packet),19): sock.sendall(packet[offset:offset+19])
                    send(wire.RESPONSE,{'event':'authenticated'})
                    lock_mode=preview=='lockscreen';locked_sent=False
                    device={'model':'SYNTHETIC_TEST','width':192,'height':64}
                    if lock_mode: device.update(lockscreen_view=True,locked=False,display_on=True,input_allowed=True,input_epoch=1,min_visible_pts=0,wake_supported=True)
                    send(wire.RESPONSE,{'event':'authorized','device':device})
                    for pts in (100,101,102): send(wire.VIDEO,FIXTURE,pts)
                    while True:
                        h=wire.unpack(wire.recv_exact(sock,24));body=wire.recv_exact(sock,h.length);rx+=1
                        if h.sequence!=rx: raise ValueError('client sequence mismatch')
                        if h.kind==wire.PING:send(wire.PONG,body,h.pts_us)
                        elif h.kind==wire.CREDIT:
                            observed['credits'].append(h.pts_us)
                            if len(observed['credits'])>=(6 if lock_mode else 3):all_credits.set()
                        elif h.kind==wire.CONTROL:
                            request=json.loads(body);observed['operations'].append({k:v for k,v in request.items() if k!='text'})
                            if lock_mode and request['op'] in ('touch','press_home','type_text') and request.get('input_epoch')!=device['input_epoch']: raise ValueError('input not bound to current epoch')
                            send(wire.RESPONSE,{'id':request['id'],'op':request['op'],'ok':True,'result':{'state':'submitted'}})
                            if lock_mode and request['op']=='wake_screen':
                                device.update(locked=False,display_on=True,input_allowed=True,input_epoch=3,min_visible_pts=300)
                                send(wire.RESPONSE,{'event':'device_state','device':device})
                                send(wire.VIDEO,FIXTURE,250);send(wire.VIDEO,FIXTURE,300)
                        else:raise ValueError('unexpected client frame')
                        if lock_mode and not locked_sent and len(observed['operations'])==6 and len(observed['credits'])>=3:
                            locked_sent=True;device.update(locked=True,display_on=False,input_allowed=False,input_epoch=2,min_visible_pts=200)
                            send(wire.RESPONSE,{'event':'device_state','device':device});send(wire.VIDEO,FIXTURE,150)
            except (ssl.SSLError,EOFError,OSError,ValueError) as error:observed['error']=type(error).__name__
        thread=threading.Thread(target=server,daemon=True);thread.start()
        container=Path(sim('get_app_container',SIMULATOR,BUNDLE,'data').stdout.strip());cache=container/'Library/Caches';cache.mkdir(parents=True,exist_ok=True)
        result_path=cache/'irb-smoke-result.json'
        if result_path.exists():result_path.unlink()
        (cache/'irb-smoke-profile.json').write_text(json.dumps(config))
        (cache/'irb-smoke-options.json').write_text(json.dumps({'pin_failure':wrong_pin,'ui_preview':preview}))
        sim('launch',SIMULATOR,BUNDLE,'--irb-smoke')
        deadline=time.monotonic()+55
        while not result_path.exists() and time.monotonic()<deadline:time.sleep(.25)
        if not result_path.exists():
            sim('io',SIMULATOR,'screenshot',str(LOGS/f'controller-{name}-timeout.png'),check=False)
            diagnostics=sim('spawn',SIMULATOR,'log','show','--last','2m','--predicate',f'process == "iOSRemoteBridge"',check=False)
            (LOGS/f'controller-{name}-timeout.log').write_text(diagnostics.stdout+'\n'+diagnostics.stderr)
            raise AssertionError(f'{name}: no simulator test result')
        result=json.loads(result_path.read_text());(LOGS/f'controller-{name}.json').write_text(json.dumps(result,indent=2))
        time.sleep(.5) # Let SwiftUI sheet/fullscreen transitions settle for visual QA.
        sim('io',SIMULATOR,'screenshot',str(LOGS/f'controller-{name}.png'),check=False)
        if result['passed'] and not wrong_pin:all_credits.wait(timeout=5)
        sim('terminate',SIMULATOR,BUNDLE,check=False);listener.close();thread.join(timeout=3)
        assert result['passed'],result
        if wrong_pin:assert not observed['auth'],'credentials were sent before certificate verification'
        else:
            assert observed['auth'] and observed['credits']==([100,101,102,150,250,300] if preview=='lockscreen' else [100,101,102]),observed
            operations=observed['operations'];assert [x['op'] for x in operations]==['configure','touch','touch','touch','press_home','type_text']+(['wake_screen','press_home'] if preview=='lockscreen' else []),observed
            assert operations[0]['fps']==60 and operations[0]['scale']==1 and operations[0]['bitrate']==8000000,observed
            assert [x['phase'] for x in operations[1:4]]==[0,1,2],observed
        print(name+': '+json.dumps(result,ensure_ascii=False),flush=True)
try:
    warmup()
    scenario(False)
    scenario(False,'settings')
    scenario(False,'fullscreen')
    scenario(False,'lockscreen')
    scenario(True)
    print('Actual iOS Simulator TLS, certificate-pin, decoded grayscale, coordinate and control checks passed')
except Exception:
    diagnostics()
    raise
