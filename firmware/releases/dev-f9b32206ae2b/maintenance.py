"""Project CDC maintenance framing; independent of official HID RPC."""
import json
import struct
import time
import zlib

MAX_JSON = 2048
MAX_FRAME = 2080


def encode(payload):
    if not 0 < len(payload) <= MAX_JSON:
        raise ValueError('Maintenance JSON exceeds frame limit')
    raw = b'CMUP\x01' + struct.pack('<H', len(payload)) + payload
    raw += struct.pack('<I', zlib.crc32(raw))
    result = bytearray(b'\0\0')
    code_pos, code = 1, 1
    for byte in raw:
        if byte == 0:
            result[code_pos] = code
            code_pos, code = len(result), 1
            result.append(0)
        else:
            result.append(byte)
            code += 1
            if code == 255:
                result[code_pos] = code
                code_pos, code = len(result), 1
                result.append(0)
    result[code_pos] = code
    result.append(0)
    return bytes(result)


def decode(frame):
    raw, offset = bytearray(), 0
    while offset < len(frame):
        code = frame[offset]
        offset += 1
        if not code or offset + code - 1 > len(frame):
            raise ValueError('Invalid COBS frame')
        raw.extend(frame[offset:offset + code - 1])
        offset += code - 1
        if code != 255 and offset < len(frame):
            raw.append(0)
    if len(raw) < 12 or raw[:5] != b'CMUP\x01':
        raise ValueError('Invalid maintenance header')
    size = struct.unpack_from('<H', raw, 5)[0]
    if not 0 < size <= MAX_JSON or len(raw) != size + 11:
        raise ValueError('Invalid maintenance length')
    if zlib.crc32(raw[:-4]) != struct.unpack_from('<I', raw, len(raw)-4)[0]:
        raise ValueError('Invalid maintenance CRC')
    return bytes(raw[7:-4])


class Decoder:
    def __init__(self):
        self.buffer = bytearray()
        self.collecting = self.overflow = False

    def feed(self, data):
        replies = []
        for byte in data:
            if byte == 0:
                if self.collecting and self.buffer and not self.overflow:
                    try:
                        replies.append(decode(self.buffer))
                    except ValueError:
                        pass  # Discard intervening ASCII logs and damaged frames.
                self.collecting, self.overflow = True, False
                self.buffer.clear()
            elif self.collecting and not self.overflow:
                if len(self.buffer) < MAX_FRAME:
                    self.buffer.append(byte)
                else:
                    self.overflow = True
        return replies


class CDCConnection:
    def __init__(self, serial_number):
        import serial
        from serial.tools import list_ports
        ports = [p for p in list_ports.comports()
                 if (p.vid, p.pid) == (0x303A, 0x8360)
                 and (serial_number is None or p.serial_number == serial_number)]
        if len(ports) != 1:
            raise RuntimeError(f'Expected one application CDC port, found {len(ports)}')
        # Windows grants exclusive serial ownership. Do not share a log monitor
        # during maintenance, and never open the ROM downloader by guessing COM.
        self.device = serial.Serial(ports[0].device, 115200, timeout=.1, write_timeout=3)
        self.decoder = Decoder()
        self.sequence = 0

    def call(self, method, params=None):
        self.sequence += 1
        request = dict(id=self.sequence, method=method, params=params or {})
        frame=encode(json.dumps(request, separators=(',', ':')).encode())
        if self.device.write(frame)!=len(frame):
            raise RuntimeError('Short CDC write; inspect durable device state before retrying')
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            # Drain available bytes; read(512) would add a full serial timeout
            # to every short response even when the complete frame arrived.
            for payload in self.decoder.feed(self.device.read(max(1,min(self.device.in_waiting,4096)))):
                try:
                    response = json.loads(payload)
                except (ValueError, UnicodeError):
                    continue
                if not isinstance(response, dict) or response.get('id') != self.sequence:
                    continue
                if 'error' in response:
                    raise RuntimeError(f'{method}: {response["error"]}')
                if 'result' not in response:
                    raise RuntimeError('Missing maintenance result')
                return response['result']
        raise TimeoutError(f'CDC {method} response timed out')

    def close(self):
        self.device.close()
