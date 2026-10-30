"""Project-local USB firmware update. Only --install writes a connected device."""
import argparse
import base64
import hashlib
import json
import struct
from pathlib import Path
import time
from usb_probe import request, Responses
from maintenance import CDCConnection


def upload(call, image, chunk_size=1024, on_timeout=None):
    """Stop-and-wait. Offset is checked after every durable write acknowledgement."""
    if not 1 <= chunk_size <= 1024:
        raise ValueError('Chunk size must be between 1 and 1024 bytes')
    status = call('upgrade.status')
    if len(image)>=320 and image[288:292]==struct.pack('<I',0x434d4831):
        if status.get('image_contract')!=list(struct.unpack_from('<8I',image,288)):
            raise RuntimeError('Device board/layout/schema contract differs; a complete ROM installation is required before this image can use online updates')
    if not status['available'] or status['pending_verify'] or status['busy']:
        raise RuntimeError('Device unavailable, busy or trial boot unconfirmed')
    if status['state'] not in ('idle', 'failed'):
        raise RuntimeError('An update already exists; inspect its status before proceeding')
    if len(image) > status['capacity']:
        raise ValueError('Image exceeds inactive partition')
    old_address = status['running_address']
    status = call('upgrade.begin', dict(size=len(image), sha256=hashlib.sha256(image).hexdigest()))
    size = min(chunk_size, status['chunk_max'])
    if size < 1 or status['state'] != 'writing' or status['offset'] != 0:
        raise RuntimeError('Unexpected begin response')
    for offset in range(0, len(image), size):
        chunk = image[offset:offset + size]
        params=dict(offset=offset,data=base64.b64encode(chunk).decode('ascii'))
        try:
            status = call('upgrade.write', params)
        except TimeoutError:
            # A timeout does not establish whether a write happened. Reconcile
            # against the device's durable, ordered offset before any resend.
            status=call('upgrade.status')
            if on_timeout is not None:
                on_timeout(offset, status)
            if (status.get('state')!='writing' or status.get('busy') or
                    status.get('size')!=len(image)):
                raise RuntimeError('Write timeout: device state cannot be reconciled')
            if status['offset']==offset:
                print('Write timeout; device confirms no committed progress at',offset,
                      '- resending this exact block once',flush=True)
                status=call('upgrade.write',params)
            elif status['offset']==offset+len(chunk):
                print('Write response lost; durable offset confirms',status['offset'],flush=True)
            else:
                raise RuntimeError('Write timeout: unexpected durable offset; stopping')
        if status['state'] != 'writing' or status['offset'] != offset + len(chunk):
            raise RuntimeError('Unexpected committed offset; stopping without activation')
    status = call('upgrade.finish')
    if status['state'] != 'ready':
        raise RuntimeError('Image validation did not produce ready state')
    return old_address


class Connection:
    def __init__(self, serial):
        import hid
        devices = [d for d in hid.enumerate(0x303A, 0x8360)
                   if d.get('usage_page') == 0xFF00 and
                   (serial is None or d.get('serial_number') == serial)]
        if len(devices) != 1:
            raise RuntimeError(f'Expected one Vendor HID interface, found {len(devices)}')
        self.device = hid.device()
        self.device.open_path(devices[0]['path'])
        self.decoder = Responses()
        self.sequence = 0

    def call(self, method, params=None):
        self.sequence += 1
        reply = request(self.device, self.decoder, method, self.sequence, params, timeout=15)
        if 'error' in reply:
            raise RuntimeError(f'{method}: {reply["error"]}')
        return reply['result']

    def close(self):
        self.device.close()


def connect(serial_number, transport):
    if transport == 'hid':
        return Connection(serial_number)
    connection = None
    try:
        connection = CDCConnection(serial_number)
        connection.call('upgrade.status')  # Read-only capability handshake.
        return connection
    except (OSError, RuntimeError, TimeoutError, ImportError):
        if connection:
            connection.close()
        if transport == 'cdc':
            raise
        return Connection(serial_number)  # Older firmware has no CDC decoder.


def software_health_ok(health):
    if not health.get('board_definition_valid') or health.get('lighting_faults'):
        return False
    if health.get('usb_microphone_required') and not health.get('microphone',{}).get('initialized'):
        return False
    if 'heap_psram_free' in health and health['heap_psram_free']<=0:
        return False
    if health.get('firmware_mode') in ('mainboard_bringup','mainboard_services'):
        outputs_off=health.get('input_reporting_enabled') is False and health.get('lighting_enabled') is False
        ble_ok=(health.get('ble_initialized') is True and not health.get('ble_error',0)
                if health.get('ble_required') else health.get('ble_required') is False)
        return outputs_off and ble_ok
    return health.get('ble_initialized') is not False and not health.get('ble_error', 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--status', action='store_true')
    group.add_argument('--install', type=Path, metavar='FIRMWARE_BIN')
    group.add_argument('--abort', action='store_true', help='Discard an unactivated transfer')
    parser.add_argument('--serial')
    parser.add_argument('--transport', choices=('auto','cdc','hid'), default='auto')
    parser.add_argument('--diagnostics', type=Path,
                        help='Save transport snapshots before activation and after write timeouts')
    parser.add_argument('--chunk-size', type=int, choices=(256,512,1024), default=1024,
                        help='Use 256 for shorter control-transfer fragment bursts on Windows')
    args = parser.parse_args()
    image = None
    if args.install:
        image = args.install.read_bytes()
        if not 256 <= len(image) <= 0x300000 or image[0] != 0xE9 or image[32:36] != b'\x32\x54\xcd\xab':
            parser.error('Expected an ESP-IDF application BIN, not merged flash or bootloader')
        if image[288:320] != struct.pack('<8I', 0x434d4831, 20261023, 0x00160008, 3, 1, 3, 0, 0):
            parser.error('Incompatible board, partition layout, updater ABI or configuration schema')
        expected_elf = image[176:208].hex()
        print('Installing', len(image), 'bytes; SHA256', hashlib.sha256(image).hexdigest(), flush=True)
    connection = connect(args.serial, args.transport)
    try:
        if args.status:
            print(json.dumps(connection.call('upgrade.status'), indent=2))
            return
        if args.abort:
            print(json.dumps(connection.call('upgrade.abort'), indent=2))
            return
        diagnostics = None
        if args.diagnostics:
            diagnostics = dict(before=connection.call('diag.transport'), timeout_events=[])
        def timeout_diagnostic(offset, status):
            diagnostics['timeout_events'].append(dict(offset=offset, status=status,
                                                       transport=connection.call('diag.transport')))
            args.diagnostics.parent.mkdir(parents=True, exist_ok=True)
            args.diagnostics.write_text(json.dumps(diagnostics, indent=2), encoding='utf-8')
        old_address = upload(connection.call, image, args.chunk_size,
                             timeout_diagnostic if diagnostics is not None else None)
        if diagnostics is not None:
            diagnostics['after_upload'] = connection.call('diag.transport')
            args.diagnostics.parent.mkdir(parents=True, exist_ok=True)
            args.diagnostics.write_text(json.dumps(diagnostics, indent=2), encoding='utf-8')
        activated = connection.call('upgrade.activate')
        if activated['state'] != 'activated':
            raise RuntimeError('Boot selection was not activated')
        # No automatic retries of mutating requests. A lost reply requires inspection.
        connection.call('upgrade.reboot')
    finally:
        connection.close()
    print('Waiting for trial image; will confirm only the expected ELF and new partition.', flush=True)
    time.sleep(3)
    deadline = time.monotonic() + 75
    last_error = None
    while time.monotonic() < deadline:
        connection = None
        try:
            connection = connect(args.serial, args.transport)
            status = connection.call('upgrade.status')
            if status['running_elf_sha256'] != expected_elf or status['running_address'] == old_address:
                raise RuntimeError('Expected trial image is not running; withholding confirmation')
            if not status['pending_verify']:
                print('Expected new image is already confirmed (possibly a previous confirmation ACK was lost).')
                return
            health = connection.call('sys.selftest')
            if not software_health_ok(health):
                raise RuntimeError('Software diagnostics failed; withholding confirmation')
            confirmed = connection.call('upgrade.confirm')
            if confirmed['pending_verify']:
                raise RuntimeError('Confirmation failed')
            print('Trial firmware confirmed. This is software health verification, not PCB acceptance.')
            return
        except (OSError, RuntimeError, TimeoutError) as error:
            last_error = error
        finally:
            if connection:
                connection.close()
        time.sleep(1)
    raise RuntimeError(f'No confirmed trial boot: {last_error}. An unconfirmed trial reboots for rollback after 120 seconds; inspect upgrade.status on reconnect.')


if __name__ == '__main__':
    main()
