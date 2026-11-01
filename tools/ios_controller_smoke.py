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
def sim(*args,check=True):
    return subprocess.run(['xcrun','simctl',*args],check=check,capture_output=True,text=True)
sim('install',SIMULATOR,str(APP))
def scenario(wrong_pin):
    name='wrong-pin' if wrong_pin else 'stream-and-input'
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
                    if auth.get('video_window')!=3 or auth.get('fps')!=60 or auth.get('control') is not True: raise ValueError('interactive negotiation mismatch')
                    sequence=0;rx=h.sequence
                    def send(kind,payload,pts=0):
                        nonlocal sequence
                        sequence+=1;body=json.dumps(payload).encode() if isinstance(payload,dict) else payload
                        packet=wire.Header(kind,1 if kind==wire.VIDEO else 0,len(body),sequence,pts).pack()+body
                        for offset in range(0,len(packet),19): sock.sendall(packet[offset:offset+19])
                    send(wire.RESPONSE,{'event':'authenticated'})
                    send(wire.RESPONSE,{'event':'authorized','device':{'model':'SYNTHETIC_TEST','width':256,'height':16}})
                    for pts in (100,101,102): send(wire.VIDEO,FIXTURE,pts)
                    while True:
                        h=wire.unpack(wire.recv_exact(sock,24));body=wire.recv_exact(sock,h.length);rx+=1
                        if h.sequence!=rx: raise ValueError('client sequence mismatch')
                        if h.kind==wire.PING:send(wire.PONG,body,h.pts_us)
                        elif h.kind==wire.CREDIT:
                            observed['credits'].append(h.pts_us)
                            if len(observed['credits'])>=3:all_credits.set()
                        elif h.kind==wire.CONTROL:
                            request=json.loads(body);observed['operations'].append({k:v for k,v in request.items() if k!='text'})
                            send(wire.RESPONSE,{'id':request['id'],'op':request['op'],'ok':True,'result':{'state':'submitted'}})
                        else:raise ValueError('unexpected client frame')
            except (ssl.SSLError,EOFError,OSError,ValueError) as error:observed['error']=type(error).__name__
        thread=threading.Thread(target=server,daemon=True);thread.start()
        container=Path(sim('get_app_container',SIMULATOR,BUNDLE,'data').stdout.strip());cache=container/'Library/Caches';cache.mkdir(parents=True,exist_ok=True)
        result_path=cache/'irb-smoke-result.json'
        if result_path.exists():result_path.unlink()
        (cache/'irb-smoke-profile.json').write_text(json.dumps(config))
        (cache/'irb-smoke-options.json').write_text(json.dumps({'pin_failure':wrong_pin}))
        sim('launch',SIMULATOR,BUNDLE,'--irb-smoke')
        deadline=time.monotonic()+55
        while not result_path.exists() and time.monotonic()<deadline:time.sleep(.25)
        if not result_path.exists():
            sim('io',SIMULATOR,'screenshot',str(LOGS/f'controller-{name}-timeout.png'),check=False)
            diagnostics=sim('spawn',SIMULATOR,'log','show','--last','2m','--predicate',f'process == "iOSRemoteBridge"',check=False)
            (LOGS/f'controller-{name}-timeout.log').write_text(diagnostics.stdout+'\n'+diagnostics.stderr)
            raise AssertionError(f'{name}: no simulator test result')
        result=json.loads(result_path.read_text());(LOGS/f'controller-{name}.json').write_text(json.dumps(result,indent=2))
        sim('io',SIMULATOR,'screenshot',str(LOGS/f'controller-{name}.png'),check=False)
        if result['passed'] and not wrong_pin:all_credits.wait(timeout=5)
        sim('terminate',SIMULATOR,BUNDLE,check=False);listener.close();thread.join(timeout=3)
        assert result['passed'],result
        if wrong_pin:assert not observed['auth'],'credentials were sent before certificate verification'
        else:
            assert observed['auth'] and observed['credits']==[100,101,102],observed
            operations=observed['operations'];assert [x['op'] for x in operations]==['touch','touch','touch','press_home','type_text'],observed
            assert [x['phase'] for x in operations[:3]]==[0,1,2],observed
        print(name+': '+json.dumps(result,ensure_ascii=False))
scenario(False)
scenario(True)
print('Actual iOS Simulator TLS, certificate-pin, decoded grayscale, coordinate and control checks passed')
