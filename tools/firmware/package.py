"""Create a self-contained USB/BLE development flash bundle; NEVER access hardware."""
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys


FLASH_SCRIPT = r'''param(
    [Parameter(Mandatory=$true)][string]$Port,
    [string]$IdfPython = "C:\Users\zengzhi\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe"
)
$ErrorActionPreference = 'Stop'
if ($Port -notmatch '^COM[0-9]+$') { throw 'Specify an explicit Windows COM port, for example COM7.' }
if (!(Test-Path -LiteralPath $IdfPython -PathType Leaf)) { throw 'ESP-IDF Python not found; supply -IdfPython.' }
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest.images) {
    $path = Join-Path $PSScriptRoot $entry.file
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Image checksum mismatch: $($entry.file)"
    }
}
$arguments = @('-m','esptool','--chip','esp32s3','--port',$Port,'--baud','460800',
    '--before','default_reset','--after','hard_reset','write_flash',
    '--flash_mode','dio','--flash_size','16MB','--flash_freq','80m')
foreach ($entry in $manifest.images) {
    $arguments += @($entry.offset, (Join-Path $PSScriptRoot $entry.file))
}
& $IdfPython @arguments
if ($LASTEXITCODE -ne 0) { throw "esptool failed: $LASTEXITCODE" }
'''


def main():
    root = Path(__file__).resolve().parents[2]
    version = re.search(r'set\(PROJECT_VER "([^"]+)"\)',
                        (root / 'firmware/codex-micro/CMakeLists.txt').read_text()).group(1)
    subprocess.run([sys.executable, str(root / 'tools/firmware/verify_build.py')], check=True)
    test_evidence=root / 'docs/firmware/evidence' / ('candidate-'+version)
    for name in ('host-tests.json','official-sdk-contract.json'):
        report=json.loads((test_evidence / name).read_text(encoding='utf-8'))
        if report.get('passed') is not True or report.get('hardware_accessed') is not False:
            raise RuntimeError('Missing successful offline evidence: '+name)
        for relative,expected in report['source_sha256'].items():
            if hashlib.sha256((root / 'firmware/codex-micro' / relative).read_bytes()).hexdigest()!=expected:
                raise RuntimeError('Tests stale after source change: '+relative)
    output = root / 'firmware/codex-micro/build-output'
    manifest = json.loads((output / 'build-input.json').read_text())
    app_hash = manifest['artifact_sha256']['codex_micro_gate0a.bin']
    runtime = None
    runtime_path = root / 'docs/firmware/evidence/usb-microphone-runtime-verification.json'
    if runtime_path.exists():
        candidate = json.loads(runtime_path.read_text(encoding='utf-8'))
        if candidate.get('app_sha256') == app_hash:
            runtime = dict(app_sha256=app_hash, hardware_accessed=candidate['hardware_accessed'],
                full_hardware_acceptance=False, scope=candidate['scope'],
                measured_benchmarks=candidate['measured_benchmarks'], limitations=candidate['limitations'])
    evidence = json.loads((root / 'docs/firmware/evidence/offline-build-verification.json').read_text())
    bringup = evidence['firmware_mode'] in ('mainboard_bringup','mainboard_services')
    services = evidence['firmware_mode'] == 'mainboard_services'
    bundle = root / 'firmware/releases' / (('mainboard-dev-' if bringup else 'dev-') + app_hash[:12])
    bundle.mkdir(parents=True, exist_ok=True)
    images = []
    # Initial/recovery install selects ota_0; preserve Gate0 NVS offset and content.
    for role, offset, source, name in [
        ('bootloader', '0x0', 'bootloader/bootloader.bin', 'bootloader.bin'),
        ('partition-table', '0x8000', 'partition_table/partition-table.bin', 'partition-table.bin'),
        ('app', '0x10000', 'codex_micro_gate0a.bin', 'firmware.bin'),
        ('ota-selection', '0x610000', 'ota_data_initial.bin', 'ota_data_initial.bin')]:
        data = (output / source).read_bytes()
        (bundle / name).write_bytes(data)
        images.append(dict(role=role, offset=offset, file=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    evidence = json.loads((root / 'docs/firmware/evidence/offline-build-verification.json').read_text())
    (bundle / 'manifest.json').write_text(json.dumps(dict(
        release_class=('Mainboard-only bring-up; physical input reporting/BLE/lights disabled' if bringup else
                       'Full USB/BLE development candidate; PCB/Host acceptance pending'),
        firmware_mode=evidence['firmware_mode'],
        chip='esp32s3', flash_size='16MB', hardware_tested=bool(runtime),
        full_hardware_acceptance=False, runtime_validation=runtime, images=images,
        build_source_sha256=manifest['source_sha256'], verification=evidence), indent=2)+'\n', encoding='utf-8')
    (bundle / 'flash.ps1').write_text(FLASH_SCRIPT, encoding='utf-8-sig')
    shutil.copy2(root / 'tools/firmware/usb_probe.py', bundle / 'usb_probe.py')
    shutil.copy2(root / 'tools/firmware/upgrade.py', bundle / 'upgrade.py')
    shutil.copy2(root / 'tools/firmware/maintenance.py', bundle / 'maintenance.py')
    shutil.copy2(root / 'tools/firmware/config_apply.py', bundle / 'config_apply.py')
    shutil.copy2(root / 'tools/firmware/backup.py', bundle / 'backup.py')
    shutil.copy2(root / 'tools/firmware/microphone_probe.py', bundle / 'microphone_probe.py')
    shutil.copy2(root / 'tools/firmware/microphone_level.py', bundle / 'microphone_level.py')
    shutil.copy2(root / 'firmware/codex-micro/README.md', bundle / 'Firmware_Details.md')
    shutil.copytree(root / 'firmware/codex-micro/tests/fixtures', bundle / 'examples', dirs_exist_ok=True)
    for name in ('host-tests.json','official-sdk-contract.json'):
        shutil.copy2(test_evidence / name,bundle / name)
    (bundle / 'README.md').write_text(f'''# Codex Micro {version} 候选固件包

模式：{evidence['firmware_mode']}。本包面向 H1 / ESP32-S3-WROOM-1-N16R8 / ESP-IDF 5.5.3。
功能、协议证据、容量和实机项目见 Firmware_Details.md。候选包不代表整机验收通过。

## 安装与数据保留

1. 安装前备份 keymap.json、smart_actions.json，核对三个 BLE Bond。
2. 本版布局3/config schema3；配置分区在原0x620000扩展为512KB。
   从0.8.1及更旧布局首次迁移须完整ROM安装四镜像，不能只写应用或走旧OTA。
3. BOOT按住、短按EN、松开BOOT，重新检测实际ROM端口。
   执行 `./flash.ps1 -Port COM45`；COM45须替换为本次检测结果。
   脚本校验四镜像SHA256，不全片擦除，保留旧NVS/config及BLE绑定。
4. 正常复位，摇杆松手至少2秒。检查版本/运行ELF哈希、PSRAM、配置generation、
   输入释放/归中、BLE绑定、音频枚举和灯光诊断。

## 后续同布局在线升级

`python upgrade.py --transport auto --install firmware.bin` 先握手CDC维护通路，
旧固件可回退HID。需要本地pyserial和hidapi，关闭占用COM口的日志监视器，
停止录音、释放输入并完成配置写入。超时后须对账持久offset，禁止盲重发。
CDC项目维护帧与官方Vendor Report6分离，采用CRC32和有界COBS分帧；
仍使用同一升级写入、哈希、激活、试启动和回滚状态机。

## 验证与边界

不自动应用examples。config_apply.py不带--apply只检查本地文件；带--apply
事务写入两文件并逐字节读回。Smart Actions在Input主机执行，权限由Input控制。
USB麦克风INMP441左声道，48kHz mono PCM16，不提供BLE音频，不改电脑默认录音。
低功耗取消；电池实测暂缓。只提供电压与低电提示，不伪造百分比/在位/充电。
RGB上限64/255是软件限亮，不是电流测量。KI_FP未知语义不作为已支持功能。
官方GUI、无线重连、全键与灯光回归另存实测证据。个人录音不打入包。
''', encoding='utf-8')
    if runtime:
        (bundle / 'Runtime_Validation.json').write_text(json.dumps(runtime,indent=2)+'\n',encoding='utf-8')
        with (bundle / 'README.md').open('a',encoding='utf-8') as readme:
            readme.write('''

## 本镜像已完成的局部实机验证

对应BIN哈希已匹配实机证据：USB请求/CDC、板载INMP441的5秒与10秒录音、
60.66秒持续采集及97次并行RPC、10次开关流、静音及单端点音量调整已检查。
稳态无新增采样丢弃，启停边界有少量样本丢弃；USB完整升级已成功，但上传存在
需要持久偏移对账恢复的超时。详情见Runtime_Validation.json。
这不是完整键盘/BLE/灯光或Desktop/Input交互验收；打包操作本身不执行烧录。
个人录音未复制到固件发布包。
''')
    print('Bundle created (packaging performed no device writes):', bundle)


if __name__ == '__main__':
    main()
