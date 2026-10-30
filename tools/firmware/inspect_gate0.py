"""Offline Gate 0 inventory and ELF descriptor extraction. Never opens hardware."""
import argparse
import hashlib
import json
from pathlib import Path

from elftools.elf.elffile import ELFFile


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def symbol_bytes(elf, name):
    matches = elf.get_section_by_name('.symtab').get_symbol_by_name(name)
    if not matches or len(matches) != 1:
        raise ValueError(f'Expected one ELF symbol: {name}')
    symbol = matches[0]
    section = elf.get_section(symbol['st_shndx'])
    offset = symbol['st_value'] - section['sh_addr']
    data = section.data()[offset:offset + symbol['st_size']]
    if len(data) != symbol['st_size']:
        raise ValueError(f'Truncated symbol: {name}')
    return data


def parse_config(data):
    if len(data) < 9 or int.from_bytes(data[2:4], 'little') != len(data):
        raise ValueError('Invalid configuration descriptor length')
    interfaces = []
    current = None
    offset = 0
    while offset < len(data):
        size = data[offset]
        if size < 2 or offset + size > len(data):
            raise ValueError('Invalid descriptor record')
        row = data[offset:offset + size]
        if row[1] == 4:
            current = {'interface': row[2], 'class': row[5],
                       'declared_endpoints': row[4], 'endpoints': []}
            interfaces.append(current)
        elif row[1] == 5:
            if current is None:
                raise ValueError('Endpoint before interface')
            current['endpoints'].append({
                'address': f'0x{row[2]:02x}', 'attributes': row[3],
                'max_packet': int.from_bytes(row[4:6], 'little'),
                'interval': row[6]})
        offset += size
    for interface in interfaces:
        if len(interface['endpoints']) != interface['declared_endpoints']:
            raise ValueError('Endpoint count mismatch')
    return interfaces


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.project.resolve()
    files = ['CMakeLists.txt', 'main/CMakeLists.txt', 'main/idf_component.yml',
             'main/codex_micro_usb.c', 'main/keymap_default.h', 'sdkconfig',
             'sdkconfig.defaults', 'dependencies.lock',
             'build/codex_micro_gate0a.bin', 'build/codex_micro_gate0a.elf',
             'build/bootloader/bootloader.bin', 'build/partition_table/partition-table.bin']
    inventory = [{'path': name, 'bytes': (root / name).stat().st_size,
                  'sha256': sha256(root / name)} for name in files]
    binary = (root / 'build/codex_micro_gate0a.bin').read_bytes()
    descriptors = {}
    with (root / 'build/codex_micro_gate0a.elf').open('rb') as stream:
        elf = ELFFile(stream)
        for name in ('codex_device_descriptor', 'codex_configuration_descriptor',
                     'codex_report_descriptor'):
            data = symbol_bytes(elf, name)
            matches = binary.count(data)
            if matches != 1:
                raise ValueError(f'{name}: expected exactly one image match, got {matches}')
            descriptors[name] = {'length': len(data), 'hex': data.hex(),
                                 'image_offset': binary.index(data)}
    config = bytes.fromhex(descriptors['codex_configuration_descriptor']['hex'])
    interfaces = parse_config(config)
    if [x['interface'] for x in interfaces] != [0, 1, 2]:
        raise ValueError('Unexpected interface baseline')
    if [e['address'] for e in interfaces[0]['endpoints']] != ['0x81']:
        raise ValueError('Unexpected HID endpoint baseline')
    # esp_app_desc_t starts after first segment header in normal ESP-IDF app images.
    app_desc = binary[32:32 + 256]
    if int.from_bytes(app_desc[:4], 'little') != 0xABCD5432:
        raise ValueError('Unexpected esp_app_desc_t placement/magic')
    elf_hash = sha256(root / 'build/codex_micro_gate0a.elf')
    if app_desc[144:176].hex() != elf_hash:
        raise ValueError('Application image does not match ELF SHA256')
    result = {'source_project': str(root), 'hardware_accessed': False,
              'running_device_verified': False, 'inventory': inventory,
              'elf_image_hash_matches': True, 'descriptors': descriptors,
              'interfaces': interfaces,
              'conclusion': 'Existing BIN and ELF agree: HID has only EP81 IN. '
                            'Live device descriptors and source reproducibility are not verified.'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(result['conclusion'])


if __name__ == '__main__':
    main()
