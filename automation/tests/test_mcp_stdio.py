import base64
import io
import json
import unittest
import subprocess
import sys
from pathlib import Path
from automation.mcp_stdio import Adapter, Server
from automation.mcp_tools import TOOLS, validate
from automation.client import BridgeError


class Fake:
    instances = []
    def __init__(self, *args, **kwargs):
        self.error = None
        self.calls = []
        self.closed = False
        self.instances.append(self)
    def connect(self): return self
    def close(self): self.closed = True
    def request(self, name, **args):
        self.calls.append((name, args))
        if name == 'get_screen_frame':
            return {'mime':'image/jpeg', 'base64':base64.b64encode(b'jpeg-fixture').decode()}
        if name == 'tap': raise BridgeError('outcome unknown')
        return {'state':'completed'}


class StdioTests(unittest.TestCase):
    def test_full_catalog_and_local_disconnect_annotation(self):
        names = {entry['name'] for entry in TOOLS}
        self.assertEqual(len(names), 14)
        self.assertTrue({'get_performance_stats', 'double_tap', 'long_press', 'launch_app'} <= names)
        entry = next(entry for entry in TOOLS if entry['name'] == 'disconnect')
        self.assertFalse(entry['annotations']['destructiveHint'])

    def test_added_tools_validate_before_connecting(self):
        a, s = self.ready(True)
        for name, args in [('double_tap', {'x': 2, 'y': .5}),
                           ('long_press', {'x': .5, 'y': .5, 'duration_ms': 10}),
                           ('launch_app', {'bundle_id': ''}),
                           ('launch_app', {'bundle_id': 'x' * 256})]:
            self.assertTrue(self.call(s, name, args)['isError'])
        self.assertEqual(Fake.instances, [])
        self.assertFalse(self.call(s, 'long_press', {'x': .5, 'y': .5})['isError'])
        self.assertEqual(Fake.instances[0].calls[-1][1]['duration_ms'], 600)
        self.assertFalse(self.call(s, 'launch_app', {'bundle_id': 'com.example.fixture'})['isError'])

    def test_real_subprocess_handshake_needs_no_sdk_or_pairing(self):
        root = Path(__file__).resolve().parents[2]
        messages = [
            {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2025-11-25'}},
            {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
            {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
            {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call',
             'params': {'name': 'get_connection_status', 'arguments': {}}},
        ]
        result = subprocess.run([sys.executable, '-S', '-m', 'automation.mcp_stdio', '--config', 'absent.json'],
                                input='\n'.join(json.dumps(m) for m in messages) + '\n',
                                cwd=root, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(rows[1]['result']['tools']), 14)
        self.assertFalse(json.loads(rows[2]['result']['content'][0]['text'])['connected'])

    def test_sdk_and_stdlib_catalogs_match(self):
        try:
            from automation.mcp_server import create_server, DeviceAdapter
        except ImportError:
            self.skipTest('optional MCP SDK not installed')
        import asyncio
        async def check():
            from mcp import Client
            adapter = DeviceAdapter('unused', control=True, factory=Fake)
            async with Client(create_server(adapter)) as client:
                tools = await client.list_tools()
                self.assertEqual({entry.name for entry in tools.tools}, {entry['name'] for entry in TOOLS})
                for entry in tools.tools:
                    expected = next(spec for spec in TOOLS if spec['name'] == entry.name)
                    self.assertEqual(entry.input_schema, expected['inputSchema'])
                for name, args in [('tap', {'x': True, 'y': .5}),
                                   ('tap', {'x': .5, 'y': .5, 'extra': 1}),
                                   ('swipe', {'x': .5, 'y': .5, 'to_x': .5, 'to_y': .2, 'duration_ms': '400'})]:
                    result = await client.call_tool(name, args)
                    self.assertTrue(result.is_error)
                    self.assertIsNone(adapter.client)
                for spec in TOOLS:
                    if spec['name'] in ('disconnect', 'get_connection_status'):
                        continue
                    args = {name: ('com.example.fixture' if name == 'bundle_id' else 'sample')
                            if prop['type'] == 'string' else prop.get('default', .5)
                            for name, prop in spec['inputSchema']['properties'].items()}
                    result = await client.call_tool(spec['name'], args)
                    self.assertEqual(result.is_error, spec['name'] == 'tap', spec['name'])
                await client.call_tool('disconnect', {})
                self.assertIsNone(adapter.client)
        asyncio.run(check())

    def setUp(self): Fake.instances = []
    def ready(self, control=False):
        a=Adapter('unused', control, factory=Fake)
        s=Server(a)
        s.dispatch({'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2025-11-25'}})
        s.dispatch({'jsonrpc':'2.0','method':'notifications/initialized'})
        return a,s
    def call(self,s,name,args=None):
        return s.dispatch({'jsonrpc':'2.0','id':5,'method':'tools/call','params':{'name':name,'arguments':args or {}}})['result']
    def test_input_rejected_before_connect(self):
        a,s=self.ready()
        r=self.call(s,'tap',{'x':.5,'y':.5})
        self.assertTrue(r['isError'])
        self.assertIn('input disabled',r['content'][0]['text'])
        self.assertEqual(Fake.instances,[])
    def test_image_is_native_mcp_content(self):
        a,s=self.ready()
        r=self.call(s,'get_screen_frame')
        self.assertFalse(r['isError'])
        self.assertEqual(r['content'][0]['type'],'image')
        self.assertEqual(base64.b64decode(r['content'][0]['data']),b'jpeg-fixture')
    def test_invalid_arguments_do_not_reach_device(self):
        a,s=self.ready(True)
        for args in [{'x':-1,'y':.5},{'x':True,'y':.5},{'x':float('nan'),'y':.5},{'x':.5,'y':.5,'extra':1},{'x':.5}]:
            self.assertTrue(self.call(s,'tap',args)['isError'])
        self.assertTrue(self.call(s,'type_text',{'text':'中文'})['isError'])
        self.assertEqual(Fake.instances,[])
    def test_duration_default_and_integer_limit(self):
        a,s=self.ready(True)
        args={'x':.5,'y':.8,'to_x':.5,'to_y':.2}
        self.assertFalse(self.call(s,'swipe',args)['isError'])
        self.assertEqual(Fake.instances[0].calls[0][1]['duration_ms'],400)
        self.assertTrue(self.call(s,'swipe',dict(args,duration_ms=40.5))['isError'])
    def test_failed_input_is_not_replayed(self):
        a,s=self.ready(True)
        self.assertTrue(self.call(s,'tap',{'x':.5,'y':.5})['isError'])
        self.assertEqual(len(Fake.instances[0].calls),1)
    def test_literal_unicode_is_preserved_and_does_not_enable_keyboard(self):
        a,s=self.ready(True)
        text='AI 直接输入\n你好，世界 😀 café 👩🏽‍💻'
        self.assertFalse(self.call(s,'insert_text',{'text':text})['isError'])
        self.assertEqual(Fake.instances[0].calls,[('insert_text',{'text':text})])
    def test_unicode_boundaries_are_utf16_units_and_reject_unpaired_surrogates(self):
        a,s=self.ready(True)
        self.assertFalse(self.call(s,'insert_text',{'text':'😀'*512})['isError'])
        for text in ['😀'*513,'x'*1025,'\ud800','\udc00']:
            self.assertTrue(self.call(s,'insert_text',{'text':text})['isError'])
        self.assertEqual(len(Fake.instances[0].calls),1)
    def test_literal_input_readonly_gate_precedes_connection(self):
        a,s=self.ready()
        self.assertTrue(self.call(s,'insert_text',{'text':'中文😀'})['isError'])
        self.assertEqual(Fake.instances,[])
    def test_ambiguous_literal_input_does_not_fallback_or_replay(self):
        class Ambiguous(Fake):
            def request(self,name,**args):
                self.calls.append((name,args))
                raise RuntimeError('outcome unknown')
        adapter=Adapter('unused',True,factory=Ambiguous)
        with self.assertRaises(RuntimeError):adapter.call('insert_text',{'text':'中文😀'})
        self.assertEqual(adapter.client.calls,[('insert_text',{'text':'中文😀'})])
    def test_status_is_lazy_and_disconnect_releases(self):
        a,s=self.ready()
        self.assertFalse(json.loads(self.call(s,'get_connection_status')['content'][0]['text'])['connected'])
        self.assertEqual(Fake.instances,[])
        self.call(s,'get_device_info')
        self.call(s,'disconnect')
        self.assertTrue(Fake.instances[0].closed)
        self.assertIsNone(a.client)
    def test_initialization_and_unknown_method(self):
        s=Server(Adapter('unused',factory=Fake))
        request={'jsonrpc':'2.0','id':'before','method':'tools/list'}
        self.assertIn('error',s.dispatch(request))
        a,s=self.ready()
        self.assertEqual(s.dispatch({'jsonrpc':'2.0','id':8,'method':'unknown'})['error']['code'],-32601)
        self.assertIsNone(s.dispatch({'jsonrpc':'2.0','method':'notifications/unknown'}))
    def test_stdio_eof_closes_and_stdout_is_protocol_only(self):
        a,s=self.ready()
        lines=[{'jsonrpc':'2.0','id':9,'method':'tools/call','params':{'name':'get_device_info'}}]
        output=io.BytesIO()
        s.serve(io.BytesIO(b'\n'.join(json.dumps(x).encode() for x in lines)+b'\n'),output)
        result=[json.loads(x) for x in output.getvalue().splitlines()]
        self.assertEqual(len(result),1)
        self.assertFalse(result[0]['result']['isError'])
        self.assertTrue(Fake.instances[0].closed)
    def test_parse_error_and_line_limit_close(self):
        a,s=self.ready()
        output=io.BytesIO()
        s.serve(io.BytesIO(b'{bad\n'+b'x'*65537+b'\n'),output)
        result=[json.loads(x) for x in output.getvalue().splitlines()]
        self.assertEqual([x['error']['code'] for x in result],[-32700,-32600])


if __name__=='__main__':unittest.main()
