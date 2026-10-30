"""Offline host updater tests: no HID import, device access or flashing."""
import base64
import hashlib
import unittest
import struct
from upgrade import upload, software_health_ok
from config_apply import apply_files
from maintenance import CDCConnection,encode
import json


class UploadTests(unittest.TestCase):
    def test_cdc_real_decoder_correlates_and_checks_short_write(self):
        class Port:
            short=False
            data=bytearray()
            @property
            def in_waiting(self): return len(self.data)
            def read(self,n):
                result=bytes(self.data[:n]);del self.data[:n];return result
            def write(self,data):
                self.data=bytearray(b'logs\n'+encode(b'{"id":999,"result":{}}')+encode(b'{"id":1,"result":{"state":"idle"}}'))
                return len(data)-1 if self.short else len(data)
        from maintenance import Decoder
        c=object.__new__(CDCConnection);c.device=Port();c.sequence=0;c.decoder=Decoder()
        self.assertEqual(c.call('upgrade.status'),{'state':'idle'})
        c.device.short=True
        with self.assertRaisesRegex(RuntimeError,'Short CDC write'):
            c.call('upgrade.status')
    def test_layout_mismatch_rejected_before_begin(self):
        image=bytearray(512)
        struct.pack_into('<8I',image,288,0x434d4831,20261023,0x00160008,3,1,3,0,0)
        calls=[]
        def call(method,params=None):
            calls.append(method)
            return dict(available=True,pending_verify=False,busy=False,state='idle',capacity=4096,
                        image_contract=[0x434d4831,20261023,0x00160008,2,1,2,0,0])
        with self.assertRaisesRegex(RuntimeError,'ROM installation'):
            upload(call,image)
        self.assertEqual(calls,['upgrade.status'])
    def test_mainboard_health_requires_disabled_outputs(self):
        board=dict(board_definition_valid=True,firmware_mode='mainboard_bringup',
                   input_reporting_enabled=False,lighting_enabled=False,ble_required=False,
                   ble_initialized=False,lighting_faults=0)
        self.assertTrue(software_health_ok(board))
        for key in ('input_reporting_enabled','lighting_enabled','ble_required'):
            self.assertFalse(software_health_ok(dict(board,**{key:True})))
        self.assertFalse(software_health_ok(dict(board,firmware_mode='full')))
        self.assertTrue(software_health_ok(dict(board,firmware_mode='full',ble_initialized=True)))
        full=dict(board,firmware_mode='full',ble_initialized=True,usb_microphone_required=True)
        self.assertFalse(software_health_ok(full))
        self.assertFalse(software_health_ok(dict(full,microphone=dict(initialized=False))))
        self.assertTrue(software_health_ok(dict(full,microphone=dict(initialized=True),heap_psram_free=1000000)))
        self.assertFalse(software_health_ok(dict(full,microphone=dict(initialized=True),heap_psram_free=0)))
        self.assertFalse(software_health_ok(dict(board,firmware_mode='mainboard_services',ble_required=True)))
        self.assertTrue(software_health_ok(dict(board,firmware_mode='mainboard_services',ble_required=True,ble_initialized=True)))
    def test_transfer_and_no_early_activation(self):
        image = bytes(range(256)) * 9
        received = bytearray()
        calls = []

        def call(method, params=None):
            calls.append(method)
            if method == 'upgrade.status':
                return dict(available=True, pending_verify=False, busy=False, state='idle',
                            capacity=4096, running_address=0x10000)
            if method == 'upgrade.begin':
                self.assertEqual(params, dict(size=len(image), sha256=hashlib.sha256(image).hexdigest()))
                return dict(chunk_max=1024, state='writing', offset=0)
            if method == 'upgrade.write':
                self.assertEqual(params['offset'], len(received))
                received.extend(base64.b64decode(params['data'], validate=True))
                return dict(state='writing', offset=len(received))
            if method == 'upgrade.finish':
                self.assertEqual(bytes(received), image)
                return dict(state='ready')
            self.fail('Unexpected operation ' + method)

        self.assertEqual(upload(call, image), 0x10000)
        self.assertEqual(calls.count('upgrade.write'), 3)

    def test_offset_mismatch_stops_before_finish(self):
        calls = []

        def call(method, params=None):
            calls.append(method)
            if method == 'upgrade.status':
                return dict(available=True, pending_verify=False, busy=False, state='idle',
                            capacity=4096, running_address=0x10000)
            if method == 'upgrade.begin':
                return dict(chunk_max=1024, state='writing', offset=0)
            return dict(state='writing', offset=0)

        with self.assertRaises(RuntimeError):
            upload(call, bytes(500))
        self.assertNotIn('upgrade.finish', calls)
        self.assertNotIn('upgrade.activate', calls)

    def test_short_chunks_follow_acknowledged_offsets(self):
        image=bytes(1100)
        received=bytearray()
        def call(method, params=None):
            if method=='upgrade.status':
                return dict(available=True,pending_verify=False,busy=False,state='idle',capacity=4096,running_address=65536)
            if method=='upgrade.begin': return dict(chunk_max=1024,state='writing',offset=0)
            if method=='upgrade.write':
                self.assertEqual(params['offset'],len(received))
                data=base64.b64decode(params['data'],validate=True)
                self.assertLessEqual(len(data),256)
                received.extend(data)
                return dict(state='writing',offset=len(received))
            self.assertEqual(method,'upgrade.finish')
            self.assertEqual(received,image)
            return dict(state='ready')
        self.assertEqual(upload(call,image,256),65536)

    def test_timeout_reconciles_before_resend_or_accept(self):
        for committed in (False,True):
            with self.subTest(committed=committed):
                calls=[];offset=0;attempt=0;started=False
                def call(method,params=None):
                    nonlocal offset,attempt,started
                    calls.append(method)
                    if method=='upgrade.status':
                        return dict(available=True,pending_verify=False,busy=False,
                            state='writing' if started else 'idle',capacity=4096,
                            running_address=65536,size=256,offset=offset)
                    if method=='upgrade.begin':
                        started=True
                        return dict(chunk_max=1024,state='writing',offset=0)
                    if method=='upgrade.write':
                        attempt+=1
                        if attempt==1:
                            if committed: offset=256
                            raise TimeoutError('simulated lost reply')
                        self.assertEqual(calls[-2],'upgrade.status')
                        self.assertEqual(params['offset'],0)
                        offset=256
                        return dict(state='writing',offset=offset)
                    self.assertEqual(method,'upgrade.finish')
                    return dict(state='ready')
                self.assertEqual(upload(call,bytes(256),256),65536)
                self.assertEqual(attempt,1 if committed else 2)

    def test_timeout_busy_or_unexpected_offset_never_resends(self):
        for busy,offset in ((True,0),(False,17)):
            attempts=0;started=False
            def call(method,params=None):
                nonlocal attempts,started
                if method=='upgrade.status':
                    return dict(available=True,pending_verify=False,busy=busy if started else False,
                        state='writing' if started else 'idle',capacity=4096,running_address=65536,
                        size=256,offset=offset)
                if method=='upgrade.begin':
                    started=True
                    return dict(chunk_max=1024,state='writing',offset=0)
                self.assertEqual(method,'upgrade.write');attempts+=1
                raise TimeoutError()
            with self.assertRaises(RuntimeError):upload(call,bytes(256),256)
            self.assertEqual(attempts,1)


class ConfigurationTests(unittest.TestCase):
    def test_both_files_commit_then_exact_readback(self):
        files = {'keymap.json': b'x' * 131072, 'smart_actions.json': b'y' * 32768}
        staged = {name: bytearray() for name in files}
        committed = False

        def call(method, params=None):
            nonlocal committed
            if method == 'fs.txbegin':
                return {'tx': 42}
            if method == 'fs.writebin':
                self.assertFalse(committed)
                data = base64.b64decode(params['data'], validate=True)
                staged[params['file']].extend(data)
                self.assertEqual(params['completed'], len(staged[params['file']]) == len(files[params['file']]))
                return {'data_written': len(data)}
            if method == 'fs.txcommit':
                self.assertEqual(params['tx'], 42)
                self.assertEqual({k: bytes(v) for k, v in staged.items()}, files)
                committed = True
                return {'ok': True}
            if method == 'fs.readbin':
                self.assertTrue(committed)
                data = files[params['file']]
                chunk = data[params['offset']:params['offset'] + params['len']]
                return {'total_size': len(data), 'data': base64.b64encode(chunk).decode()}
            self.fail(method)

        apply_files(call, files)
        self.assertTrue(committed)


if __name__ == '__main__':
    unittest.main()
