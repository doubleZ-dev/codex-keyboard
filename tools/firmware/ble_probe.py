"""Scan only for this project's BLE advertisements; never pair implicitly."""
import argparse
import asyncio
import json
from pathlib import Path
import sys


async def scan(seconds):
    dependencies = Path(__file__).resolve().parents[2] / 'firmware/codex-micro/.host-tools/ble-validation'
    if dependencies.is_dir():
        sys.path.insert(0, str(dependencies))
    from bleak import BleakScanner
    devices = await BleakScanner.discover(timeout=seconds, return_adv=True)
    result = []
    for device, advertisement in devices.values():
        name = advertisement.local_name or device.name or ''
        if name.startswith('Codex Micro'):
            result.append(dict(name=name, address=device.address,
                               rssi=advertisement.rssi,
                               services=advertisement.service_uuids))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=10)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 30:
        parser.error('Scan duration must be 1..30 seconds')
    print(json.dumps(asyncio.run(scan(args.seconds)), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
