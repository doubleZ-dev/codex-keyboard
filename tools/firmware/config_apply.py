"""Validate input files locally; --apply explicitly writes both files over USB."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
from upgrade import Connection


def apply_files(call, files):
    transaction = call('fs.txbegin')['tx']
    for name, data in files.items():
        for offset in range(0, len(data), 512):
            chunk = data[offset:offset+512]
            result = call('fs.writebin', dict(file=name, data=base64.b64encode(chunk).decode(),
                                             offset=offset,tx=transaction,append=True,
                                             completed=offset+len(chunk) == len(data)))
            if result.get('data_written') != len(chunk):
                raise RuntimeError('Unexpected write acknowledgement; do not retry the chunk')
    call('fs.txcommit', dict(tx=transaction))
    for name, expected in files.items():
        actual = bytearray()
        while len(actual) < len(expected):
            result = call('fs.readbin', dict(file=name, offset=len(actual), len=384))
            chunk = base64.b64decode(result['data'], validate=True)
            if result['total_size'] != len(expected) or not chunk or len(chunk)>384:
                raise RuntimeError('Unexpected readback size')
            actual.extend(chunk)
        if bytes(actual) != expected:
            raise RuntimeError('Readback mismatch: ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keymap', required=True, type=Path)
    parser.add_argument('--smart', required=True, type=Path)
    parser.add_argument('--serial')
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    files = {'keymap.json': args.keymap.read_bytes(), 'smart_actions.json': args.smart.read_bytes()}
    for (name, data), limit in zip(files.items(), (131072, 32768)):
        if not data or len(data)>limit or b'\0' in data or not isinstance(json.loads(data), dict):
            parser.error('Invalid object or file size: ' + name)
        print(name, len(data), 'bytes; SHA256', hashlib.sha256(data).hexdigest())
    if not args.apply:
        print('Local syntax/size check only; device schema validation happens at commit. No device accessed.')
        return
    connection = Connection(args.serial)
    try:
        apply_files(connection.call, files)
    finally:
        connection.close()
    print('Both files committed and read back byte-for-byte. Host action execution is a separate test.')


if __name__ == '__main__':
    main()
