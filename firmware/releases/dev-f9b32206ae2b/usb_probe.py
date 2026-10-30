"""Read-only firmware RPC probe. --selftest needs no device or hidapi."""
import argparse
import json
import time


def reports(request):
    payload = json.dumps(request, separators=(',', ':'), ensure_ascii=False).encode('utf-8') + b'\n'
    for offset in range(0, len(payload), 61):
        chunk = payload[offset:offset+61]
        yield bytes([6, 2, len(chunk)]) + chunk + bytes(61-len(chunk))


class Responses:
    def __init__(self):
        self.pending = bytearray()

    def feed(self, report):
        report = bytes(report)
        if len(report) != 64 or report[0] != 6 or report[1] != 2 or report[2] > 61:
            raise ValueError('Unexpected vendor HID report')
        self.pending.extend(report[3:3+report[2]])
        if len(self.pending) > 16384:
            raise ValueError('Response exceeds probe limit')
        messages = []
        while b'\n' in self.pending:
            line, _, self.pending = self.pending.partition(b'\n')
            if line.strip():
                messages.append(json.loads(line))
        return messages


def request(device, decoder, method, request_id, params=None, timeout=5):
    message = dict(id=request_id, method=method)
    if params is not None:
        message['params'] = params
    for report in reports(message):
        if device.write(report) != len(report):
            raise RuntimeError('Short HID write')
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        report = device.read(64, 100)
        if report:
            for response in decoder.feed(report):
                if response.get('id') == request_id:
                    return response
    raise TimeoutError(method + ': no correlated response')


def selftest():
    for size in (0, 1, 61, 62, 384, 4096):
        obj = dict(id=1, result=('中文"' if size < 1000 else 'x') * size)
        decoder = Responses()
        received = []
        for report in reports(obj):
            assert len(report) == 64
            received += decoder.feed(report)
        assert received == [obj]
    print('USB probe offline framing self-test passed; no hardware accessed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--selftest', action='store_true')
    parser.add_argument('--serial')
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return
    if not args.list and not args.probe:
        parser.error('Choose --list, --probe or --selftest')
    import hid
    devices = [d for d in hid.enumerate(0x303A, 0x8360) if d.get('usage_page') == 0xFF00]
    if args.serial:
        devices = [d for d in devices if d.get('serial_number') == args.serial]
    if args.list:
        for d in devices:
            print({k: d.get(k) for k in ('product_string', 'serial_number', 'interface_number', 'path')})
    if args.probe:
        if len(devices) != 1:
            raise RuntimeError(f'Expected exactly one matching vendor interface, found {len(devices)}')
        device = hid.device()
        try:
            device.open_path(devices[0]['path'])
            decoder = Responses()
            for index, method in enumerate(('sys.version', 'device.status', 'diag.inputs', 'sys.selftest'), 1):
                print(json.dumps(request(device, decoder, method, index), ensure_ascii=False))
        finally:
            device.close()


if __name__ == '__main__':
    main()
