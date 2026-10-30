"""Verify built image identity, source inventory and Gate0 USB descriptors offline."""
import hashlib
import json
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from inspect_gate0 import symbol_bytes, parse_config


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parents[2]
    project = root / 'firmware/codex-micro'
    output = project / 'build-output'
    manifest = json.loads((output / 'build-input.json').read_text())
    if manifest['status'] != 'complete':
        raise RuntimeError('Latest build did not complete; refusing stale artifacts')
    for name, expected in manifest['source_sha256'].items():
        if digest(project / name) != expected:
            raise RuntimeError(f'Source differs from build: {name}')
    for name, expected in manifest['artifact_sha256'].items():
        if digest(output / name) != expected:
            raise RuntimeError(f'Artifact differs from manifest: {name}')
    binary = (output / 'codex_micro_gate0a.bin').read_bytes()
    generated = (output / 'sdkconfig.generated').read_text(encoding='utf-8')
    bringup = 'CONFIG_CODEX_MAINBOARD_BRINGUP=y' in generated.splitlines()
    audio = 'CONFIG_CODEX_USB_MICROPHONE=y' in generated.splitlines()
    services = 'CONFIG_CODEX_BOARD_SERVICES=y' in generated.splitlines()
    for setting in ('CONFIG_BT_ENABLED=y', 'CONFIG_BT_NIMBLE_ENABLED=y',
                    'CONFIG_BT_NIMBLE_NVS_PERSIST=y', 'CONFIG_BT_NIMBLE_MAX_BONDS=3',
                    'CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1', 'CONFIG_BT_NIMBLE_MAX_CCCDS=16',
                    'CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=128',
                    'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y',
                    'CONFIG_SPIRAM=y','CONFIG_SPIRAM_MODE_OCT=y','CONFIG_SPIRAM_SPEED_80M=y',
                    'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y','CONFIG_RMT_TX_ISR_CACHE_SAFE=y',
                    '# CONFIG_PM_ENABLE is not set', '# CONFIG_BT_CTRL_MODEM_SLEEP is not set'):
        if setting not in generated.splitlines():
            raise RuntimeError('Unexpected generated SDK configuration: ' + setting)
    if binary[288:320] != struct.pack('<8I', 0x434d4831, 20261023, 0x00160008, 3, 1, 3, 0, 0):
        raise RuntimeError('Missing board/layout/updater compatibility descriptor')
    partitions = {}
    raw = (output / 'partition_table/partition-table.bin').read_bytes()
    for offset in range(0, len(raw), 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack('<HBBII16sI', raw[offset:offset+32])
        if magic != 0x50AA:
            break
        partitions[label.rstrip(b'\0').decode()] = (kind, subtype, address, size)
    expected_partitions = dict(nvs=(1,2,0x9000,0x6000), phy_init=(1,1,0xf000,0x1000),
        ota_0=(0,0x10,0x10000,0x300000), ota_1=(0,0x11,0x310000,0x300000),
        otadata=(1,0,0x610000,0x2000), config=(1,2,0x620000,0x80000))
    if partitions != expected_partitions:
        raise RuntimeError('Unexpected partition layout; refusing release')
    if (output / 'ota_data_initial.bin').read_bytes() != b'\xff' * 8192:
        raise RuntimeError('Initial OTA selection must be blank (boots ota_0)')
    if binary[3] >> 4 != 4 or len(binary) > 0x300000:
        raise RuntimeError('Expected 16MB image header and app fitting a 3MB slot')
    elf_hash = digest(output / 'codex_micro_gate0a.elf')
    if binary[176:208].hex() != elf_hash:
        raise RuntimeError('BIN embedded ELF hash mismatch')
    baseline = json.loads((root / 'docs/firmware/evidence/gate0-baseline.json').read_text(encoding='utf-8'))
    descriptors = {}
    with (output / 'codex_micro_gate0a.elf').open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {symbol.name for symbol in elf.get_section_by_name('.symtab').iter_symbols()}
        external=elf.get_section_by_name('.ext_ram.bss')
        if external is None or external['sh_size']>8*1024*1024:
            raise RuntimeError('Missing or oversized external configuration BSS')
        external_start=external['sh_addr'];external_end=external_start+external['sh_size']
        symbol_table={symbol.name:symbol for symbol in elf.get_section_by_name('.symtab').iter_symbols()}
        for name in ('current_json','current_smart','current_model','candidate_model','staging_keymap','staging_smart'):
            symbol=symbol_table.get(name)
            if symbol is None or not external_start<=symbol['st_value']<external_end:
                raise RuntimeError('Configuration buffer not located in PSRAM: '+name)
        standard=symbol_bytes(elf,'standard_report_descriptor')
        if not standard.endswith(bytes.fromhex('05010980a10185031500250119812983750195038102750595018103c0')):
            raise RuntimeError('System Control report descriptor differs from the fixed 1-byte contract')
        if bringup:
            forbidden = {'board_lights_start', 'action_runtime_tick', 'stick_stream_pump'}
            if not services: forbidden.add('codex_ble_init')
            remaining = forbidden & symbols
            if remaining:
                raise RuntimeError('Bring-up image still links active peripheral entrypoints: ' + str(remaining))
            if b'{"method":"v.oai.rad"' in binary or b'{"method":"v.oai.hid"' in binary:
                raise RuntimeError('Bring-up image contains physical Vendor notification format')
        else:
            required = {'board_lights_start', 'action_runtime_tick', 'stick_stream_pump', 'codex_ble_init',
                        'radial_runtime_tick','layer_navigation_hold','maintenance_feed'}
            missing = required - symbols
            if missing:
                raise RuntimeError('Full-board image lacks active peripheral entrypoints: ' + str(missing))
        for name, entry in baseline['descriptors'].items():
            actual = symbol_bytes(elf, name)
            expected = bytes.fromhex(entry['hex'])
            unchanged = actual == expected
            additive_standard_hid = False
            additive_audio = False
            audio_iad_device = False
            if audio and name == 'codex_device_descriptor':
                audio_iad_device = (actual[:4] == expected[:4] and actual[4:7] == bytes([0xef,2,1])
                                    and actual[7:] == expected[7:])
            if name == 'codex_configuration_descriptor' and not unchanged:
                interfaces = parse_config(actual)
                additive_standard_hid = (len(actual) == len(expected) + 25 and
                    actual[9:len(expected)] == expected[9:] and actual[4] == 4 and
                    int.from_bytes(actual[2:4], 'little') == len(actual) and
                    actual[:2] == expected[:2] and actual[5:9] == expected[5:9] and
                    [item['interface'] for item in interfaces] == [0, 1, 2, 3] and
                    [e['address'] for e in interfaces[3]['endpoints']] == ['0x84'])
                if audio:
                    # Verify interface 3 independently, including its report descriptor length.
                    # The first 100 bytes after the configuration header remain Gate0-exact.
                    cdc_without_notify = bytearray(expected)
                    if cdc_without_notify[70:77] != bytes([7,5,0x82,3,8,0,1]):
                        raise RuntimeError('Unexpected Gate0 CDC notification descriptor')
                    cdc_without_notify[46] = 0
                    del cdc_without_notify[70:77]
                    prefix_length=len(cdc_without_notify)
                    standard = actual[prefix_length:prefix_length+25]
                    active_in={endpoint['address'] for item in interfaces for endpoint in item['endpoints']
                               if int(endpoint['address'],16)&128}
                    if len(active_in)>4:
                        raise RuntimeError('ESP32-S3 permits only four data IN endpoints plus EP0')
                    additive_audio = (actual[9:prefix_length] == cdc_without_notify[9:] and actual[4] == 6
                        and actual[:2] == expected[:2] and actual[5:9] == expected[5:9]
                        and [item['interface'] for item in interfaces] == [0,1,2,3,4,5,5]
                        and [item['class'] for item in interfaces] == [3,2,10,3,1,1,1]
                        and standard[:9] == bytes([9,4,3,0,1,3,0,0,0])
                        and standard[9:16] == bytes([9,0x21,0x11,1,0,1,0x22])
                        and standard[16:18] == len(symbol_bytes(elf,'standard_report_descriptor')).to_bytes(2,'little')
                        and standard[18:] == bytes([7,5,0x84,3,16,0,5])
                        and len(actual) == 250
                        and active_in == {'0x81','0x82','0x83','0x84'}
                        and all(not item['endpoints'] for item in interfaces[4:6])
                        and interfaces[-1]['endpoints'] == [dict(address='0x82', attributes=5,
                                                               max_packet=98, interval=1)])
            if (not unchanged and not additive_standard_hid and not additive_audio and not audio_iad_device) or binary.count(actual) != 1:
                raise RuntimeError(f'USB descriptor regression: {name}')
            descriptors[name] = {'length': len(actual), 'matches_gate0': unchanged,
                                 'additive_standard_hid': additive_standard_hid,
                                 'additive_audio': additive_audio, 'audio_iad_device': audio_iad_device}
    original = Path(baseline['source_project'])
    for entry in baseline['inventory']:
        if digest(original / entry['path']) != entry['sha256']:
            raise RuntimeError(f'Original Gate0 changed: {entry["path"]}')
    evidence = dict(hardware_accessed=False, running_device_verified=False,
                    firmware_mode=('mainboard_services' if services else 'mainboard_bringup') if bringup else 'full',
                    business_input_and_lighting_disabled=bringup,
                    usb_microphone_enabled=audio,
                    build_status=manifest['status'], sources_match=True,
                    original_gate0_unchanged=True, elf_image_hash_matches=True,
                    descriptors=descriptors, bin_bytes=len(binary), partitions=partitions,
                    artifact_sha256=manifest['artifact_sha256'])
    path = root / 'docs/firmware/evidence/offline-build-verification.json'
    path.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
