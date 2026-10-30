"""Build the isolated work project with local ESP-IDF tools; no flash actions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf-path', type=Path, required=True)
    parser.add_argument('--idf-python', type=Path, required=True)
    parser.add_argument('--tools-root', type=Path, default=Path.home() / '.espressif/tools')
    parser.add_argument('--incremental', action='store_true', help='Reuse only the last generated ASCII temporary stage')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[2] / 'firmware/codex-micro'
    tools = args.tools_root.resolve()
    bins = [tools / 'xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin',
            tools / 'cmake/3.30.2/bin', tools / 'ninja/1.12.1']
    for path in [args.idf_path / 'tools/idf.py', args.idf_python, *bins]:
        if not path.exists():
            parser.error(f'Missing required local tool: {path}')
    env = os.environ.copy()
    env.update(IDF_PATH=str(args.idf_path.resolve()),
               IDF_PYTHON_ENV_PATH=str(args.idf_python.resolve().parents[1]),
               PYTHONUTF8='1', PYTHONIOENCODING='utf-8')
    rom_dir = tools / 'esp-rom-elfs/20241011'
    if rom_dir.is_dir():
        env['ESP_ROM_ELF_DIR'] = str(rom_dir)
    env['PATH'] = os.pathsep.join([str(args.idf_python.parent),
                                  *(str(p) for p in bins), env['PATH']])
    # Both IDF and CMake expand short names back to Unicode; this binutils
    # distribution strips non-ASCII path characters. Build an exact source copy.
    previous = project / 'build-output/build-input.json'
    if args.incremental and previous.exists():
        stage = Path(json.loads(previous.read_text(encoding='utf-8'))['stage']).resolve()
        temp_root = Path(tempfile.gettempdir()).resolve()
        if stage.parent != temp_root or not stage.name.startswith('codex-micro-build-') or not stage.is_dir():
            parser.error('Refusing to reuse a stage outside the generated TEMP directory')
    else:
        stage = Path(tempfile.mkdtemp(prefix='codex-micro-build-')).resolve()
    if not str(stage).isascii():
        parser.error('TEMP must be an ASCII path for the installed binutils')
    for name in ('main', 'components', 'managed_components'):
        target = (stage / name).resolve()
        if target.parent != stage:
            parser.error('Stage source directory resolved outside validated staging root')
        if target.exists():
            # Only these three source copies under the validated generated TEMP stage.
            shutil.rmtree(target)
        shutil.copytree(project / name, stage / name)
    for name in ('CMakeLists.txt', 'sdkconfig', 'sdkconfig.defaults', 'dependencies.lock', 'partitions.csv'):
        shutil.copy2(project / name, stage / name)
    inventory = {}
    for directory in ('main', 'components'):
        for path in sorted((stage / directory).rglob('*')):
            if path.is_file():
                inventory[str(path.relative_to(stage))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in ('CMakeLists.txt', 'sdkconfig', 'sdkconfig.defaults', 'dependencies.lock', 'partitions.csv'):
        inventory[name] = hashlib.sha256((stage / name).read_bytes()).hexdigest()
    source = str(stage)
    build = str(stage / 'build')
    logs = project / 'build-output'
    logs.mkdir(exist_ok=True)
    cmake = str(bins[1] / 'cmake.exe')
    commands = [[cmake, '-S', source, '-B', build, '-G', 'Ninja',
                 '-DPYTHON_DEPS_CHECKED=1', '-DPYTHON=' + str(args.idf_python),
                 '-DESP_PLATFORM=1', '-DCCACHE_ENABLE=0'],
                [cmake, '--build', build]]
    manifest = {'stage': source, 'source_sha256': inventory, 'hardware_accessed': False,
                'status': 'building'}
    def save_manifest():
        (logs / 'build-input.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    save_manifest()
    print('ASCII staging directory:', source, flush=True)
    with (logs / 'build-run.log').open('w', encoding='utf-8') as log:
        for command in commands:
            with subprocess.Popen(command, cwd=source, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True,
                                  encoding='utf-8', errors='replace') as process:
                for line in process.stdout:
                    log.write(line)
                    print(line, end='', flush=True)
                code = process.wait()
                if code:
                    manifest.update(status='failed', exit_code=code)
                    save_manifest()
                    return code
    artifacts = {}
    for name in ('codex_micro_gate0a.bin', 'codex_micro_gate0a.elf',
                 'codex_micro_gate0a.map', 'bootloader/bootloader.bin',
                 'partition_table/partition-table.bin', 'ota_data_initial.bin', 'flasher_args.json'):
        destination = logs / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(stage / 'build' / name, destination)
        artifacts[name] = hashlib.sha256(destination.read_bytes()).hexdigest()
    shutil.copy2(stage / 'sdkconfig', logs / 'sdkconfig.generated')
    artifacts['sdkconfig.generated'] = hashlib.sha256((logs / 'sdkconfig.generated').read_bytes()).hexdigest()
    manifest.update(status='complete', artifact_sha256=artifacts)
    save_manifest()
    print('Build artifacts:', logs, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
