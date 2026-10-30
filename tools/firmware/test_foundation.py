"""Compile actual foundation C into a host DLL with isolated Zig, then test it."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import hashlib
import json
import re
from datetime import datetime, timezone


def main():
    project = Path(__file__).resolve().parents[2] / 'firmware/codex-micro'
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path, default=project / '.host-tools/ziglang/zig.exe')
    parser.add_argument('--idf-path', type=Path,
                        default=Path('G:/ESP32/archive_vv5.5.3_windows-x64/v5.5.3/esp-idf'))
    args = parser.parse_args()
    if not args.zig.exists():
        parser.error('Install local test compiler: python -m pip install --target '
                     'firmware/codex-micro/.host-tools ziglang==0.13.0')
    output = project / '.host-tests'
    output.mkdir(exist_ok=True)
    env = os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR'] = str(output / 'zig-cache')
    subprocess.run([str(args.zig.resolve()), 'cc', '-shared', '-O2', '-Wall', '-Wextra',
                    '-Werror', '-I', 'components/fw_foundation/include',
                    'components/fw_foundation/matrix_debounce.c',
                    'components/fw_foundation/board_definition.c', 'tests/host_shim.c',
                    '-o', str(output / 'foundation.dll')], cwd=project, env=env, check=True)
    subprocess.run([sys.executable, 'tests/test_foundation.py',
                    str(output / 'foundation.dll'), '-v'], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/usb_rpc/include', 'components/usb_rpc/usb_rpc.c',
                    'tests/test_usb_rpc.c', '-o', str(output / 'test_usb_rpc.exe')],
                   cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_usb_rpc.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/usb_rpc/include', 'components/usb_rpc/maintenance_frame.c',
                    'tests/test_maintenance_frame.c', '-o', str(output / 'test_maintenance_frame.exe')],
                   cwd=project, env=env, check=True)
    vector=output / 'maintenance-vector.bin'
    subprocess.run([str(output / 'test_maintenance_frame.exe'), str(vector)], cwd=project, check=True)
    from maintenance import encode, Decoder
    payload=bytes([255])*2048
    assert vector.read_bytes()==encode(payload), 'C/Python CDC framing mismatch'
    decoder=Decoder()
    assert decoder.feed(b'ASCII logs\n'+vector.read_bytes())==[payload]
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/fw_foundation/include',
                    'components/fw_foundation/joystick_filter.c', 'tests/test_joystick_filter.c',
                    '-o', str(output / 'test_joystick_filter.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_joystick_filter.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/fw_foundation/include',
                    'components/fw_foundation/battery_monitor.c', 'tests/test_battery_monitor.c',
                    '-o', str(output / 'test_battery_monitor.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_battery_monitor.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/fw_foundation/include', '-I', 'components/device_services/include',
                    'components/device_services/light_demo.c', 'tests/test_light_demo.c',
                    '-o', str(output / 'test_light_demo.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_light_demo.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/fw_foundation/include',
                    'components/fw_foundation/default_actions.c', 'tests/test_default_actions.c',
                    '-o', str(output / 'test_default_actions.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_default_actions.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/fw_foundation/include', 'tests/test_firmware_policy.c',
                    '-o', str(output / 'test_firmware_policy.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_firmware_policy.exe')], cwd=project, check=True)
    source = (project / 'main/codex_micro_usb.c').read_text(encoding='utf-8')
    standard = source[source.index('static bool submit_standard('):]
    standard = standard[:standard.index('\n}')+2]
    (output / 'standard_submit.inc').write_text(standard, encoding='utf-8')
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(output), '-I', 'tests/mocks', '-I', 'components/ble_transport/include',
                    '-I', 'components/fw_foundation/include', 'tests/test_standard_submit.c',
                    '-o', str(output / 'test_standard_submit.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_standard_submit.exe')], cwd=project, check=True)
    vendor_notice = source[source.index('static bool enqueue_vendor_hid('):]
    vendor_notice = vendor_notice[:vendor_notice.index('\n}')+2]
    (output / 'vendor_hid_notice.inc').write_text(vendor_notice, encoding='utf-8')
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(output), '-I', 'components/usb_rpc/include',
                    'components/usb_rpc/usb_rpc.c', 'tests/test_codex_hid_notice.c',
                    '-o', str(output / 'test_codex_hid_notice.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_codex_hid_notice.exe')], cwd=project, check=True)
    callback = source[source.index('void tud_hid_set_report_cb('):]
    callback = callback[:callback.index('\n}')+2]
    (output / 'usb_receive_callback.inc').write_text(callback, encoding='utf-8')
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(output), '-I', 'components/usb_microphone/include',
                    'tests/test_usb_receive.c', '-o', str(output / 'test_usb_receive.exe')],
                   cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_usb_receive.exe')], cwd=project, check=True)
    cjson = args.idf_path / 'components/json/cJSON'
    internal=source[source.index('static bool internal_action('):]
    internal=internal[:internal.index('\n}')+2]
    (output / 'internal_action.inc').write_text(internal, encoding='utf-8')
    selection=source[source.index('static void transport_selection_step('):]
    selection=selection[:selection.index('\n}')+2]
    (output / 'transport_selection.inc').write_text(selection,encoding='utf-8')
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(output), '-I', 'tests/mocks', '-I', 'components/ble_transport/include',
                    '-I', 'components/usb_rpc/include', '-I', 'components/device_services/include',
                    '-I', 'components/fw_foundation/include', '-DCJSON_NESTING_LIMIT=32', '-I', str(cjson), str(cjson / 'cJSON.c'),
                    'components/device_services/keymap_model.c', 'components/device_services/key_codes.c',
                    'components/device_services/light_engine.c', 'components/device_services/layer_navigation.c',
                    'components/device_services/radial_runtime.c', 'components/usb_rpc/usb_rpc.c',
                    'components/fw_foundation/default_actions.c', 'components/device_services/action_runtime.c',
                    'tests/test_internal_actions.c', '-o', str(output / 'test_internal_actions.exe')],
                   cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_internal_actions.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/device_services/include', '-I', 'components/fw_foundation/include', '-DCJSON_NESTING_LIMIT=32', '-I', str(cjson), str(cjson / 'cJSON.c'),
                    'components/device_services/keymap_model.c', 'components/device_services/key_codes.c', 'components/device_services/light_engine.c',
                    'components/device_services/radial_runtime.c', 'components/device_services/layer_navigation.c',
                    'tests/test_radial_navigation.c', '-o', str(output / 'test_radial_navigation.exe')],
                   cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_radial_navigation.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/fw_foundation/include', '-I', 'components/device_services/include', '-I', 'components/fw_foundation/include',
                    '-DCJSON_NESTING_LIMIT=32', '-I', str(cjson), str(cjson / 'cJSON.c'),
                    'components/device_services/stick_stream.c',
                    'components/device_services/keymap_model.c', 'components/device_services/key_codes.c', 'components/device_services/light_engine.c', 'components/device_services/hid_state.c',
                    'components/device_services/action_runtime.c', 'components/device_services/smart_actions.c',
                    'tests/test_device_services.c', '-o', str(output / 'test_device_services.exe')],
                   cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_device_services.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/device_services/include', '-I', 'components/fw_foundation/include', '-I', 'components/ble_transport/include',
                    '-DCJSON_NESTING_LIMIT=32', '-I', str(cjson), str(cjson / 'cJSON.c'),
                    'components/device_services/keymap_model.c', 'components/device_services/key_codes.c', 'components/device_services/light_engine.c', 'components/device_services/action_runtime.c',
                    'components/device_services/smart_actions.c', 'components/device_services/hid_state.c', 'components/ble_transport/ble_slot_policy.c',
                    'tests/test_advanced_actions.c', '-o', str(output / 'test_advanced_actions.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_advanced_actions.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'tests', '-I', 'tests/mocks', '-I', 'components/ble_transport/include',
                    'components/ble_transport/ble_slot_policy.c', 'tests/test_ble_transport.c',
                    '-o', str(output / 'test_ble_transport.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_ble_transport.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'tests/mocks', '-I', 'components/config_store/include',
                    '-I', 'components/device_services/include', '-I', 'components/fw_foundation/include', '-DCJSON_NESTING_LIMIT=32', '-I', str(cjson), str(cjson / 'cJSON.c'),
                    'components/device_services/keymap_model.c', 'components/device_services/key_codes.c', 'components/device_services/light_engine.c', 'components/device_services/smart_actions.c', 'tests/test_config_store.c',
                    '-o', str(output / 'test_config_store.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_config_store.exe')], cwd=project, check=True)
    rpc_source = (project / 'components/config_store/config_rpc.c').read_text(encoding='utf-8')
    extracted = []
    for signature in ('static void error(', 'static int persist('):
        function = rpc_source[rpc_source.index(signature):]
        extracted.append(function[:function.index('\n}')+2])
    (output / 'config_rpc_envelope.inc').write_text('\n'.join(extracted), encoding='utf-8')
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(output), '-DCJSON_NESTING_LIMIT=32', '-I', str(cjson), str(cjson / 'cJSON.c'),
                    'tests/test_config_rpc_envelope.c', '-o', str(output / 'test_config_rpc_envelope.exe')],
                   cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_config_rpc_envelope.exe')], cwd=project, check=True)
    mbedtls=args.idf_path / 'components/mbedtls/mbedtls'
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-DMBEDTLS_CONFIG_FILE="mbedtls_test_config.h"',
                    '-I', 'tests', '-I', 'tests/mocks', '-I', str(mbedtls / 'include'),
                    '-I', 'components/config_store/include', '-I', 'components/device_services/include',
                    '-I', 'components/fw_foundation/include', '-DCJSON_NESTING_LIMIT=32', '-I', str(cjson), str(cjson / 'cJSON.c'),
                    str(mbedtls / 'library/base64.c'), str(mbedtls / 'library/sha1.c'),
                    str(mbedtls / 'library/platform.c'), str(mbedtls / 'library/platform_util.c'),
                    str(mbedtls / 'library/constant_time.c'),
                    'components/config_store/config_rpc.c', 'components/device_services/keymap_model.c',
                    'components/device_services/key_codes.c', 'components/device_services/light_engine.c',
                    'components/device_services/smart_actions.c', 'tests/test_config_rpc.c',
                    '-o', str(output / 'test_config_rpc.exe')], cwd=project, env=env, check=True)
    subprocess.run([str(output / 'test_config_rpc.exe')], cwd=project, check=True)
    subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', 'components/firmware_update/include',
                    'components/firmware_update/update_engine.c', 'tests/test_update_engine.c',
                    '-o', str(output / 'test_firmware_engine.exe')], cwd=project, env=env, check=True)
    result=subprocess.call([str(output / 'test_firmware_engine.exe')], cwd=project)
    if result==0:
        files=[]
        for directory in ('main','components','tests'):
            files.extend(p for p in (project / directory).rglob('*') if p.is_file() and p.suffix in ('.c','.h','.json','.txt','.yml'))
        files.extend(project / name for name in ('CMakeLists.txt','sdkconfig','sdkconfig.defaults','partitions.csv','dependencies.lock'))
        report=dict(passed=True,hardware_accessed=False,scope='Host C tests with actual sources; SDK/OS/Flash doubles are identified in test output',
                    timestamp_utc=datetime.now(timezone.utc).isoformat(),
                    source_sha256={p.relative_to(project).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)},
                    runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        version=re.search(r'set\(PROJECT_VER "([^"]+)"\)',(project / 'CMakeLists.txt').read_text(encoding='utf-8')).group(1)
        evidence=project.parents[1] / 'docs/firmware/evidence' / ('candidate-'+version)
        evidence.mkdir(parents=True,exist_ok=True)
        (evidence / 'host-tests.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return result


if __name__ == '__main__':
    raise SystemExit(main())
