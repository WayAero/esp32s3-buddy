"""Package existing ESP-IDF output. Never flashes, publishes, or erases a device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def flash_layout(build):
    build = build.resolve()
    data = json.loads((build / 'flasher_args.json').read_text(encoding='utf-8'))
    files = []
    for address, name in data['flash_files'].items():
        offset = int(address, 0)
        path = (build / name).resolve()
        if not path.is_relative_to(build) or not path.is_file() or offset < 0:
            raise ValueError(f'Invalid flash input: {address}, {name}')
        files.append((offset, path))
    files.sort()
    if not files or files[0][0] != 0:
        raise ValueError('Full image must start at 0x0 for this product')
    for previous, current in zip(files, files[1:]):
        if previous[0] + previous[1].stat().st_size > current[0]:
            raise ValueError('Flash images overlap')
    return data, files


def sha256(path):
    with path.open('rb') as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
        return digest.hexdigest()


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def source_manifest(output, build):
    names = git('-c', 'core.quotepath=false', 'ls-files', '-z').split('\0')
    names += git('-c', 'core.quotepath=false', 'ls-files', '--others', '--exclude-standard', '-z').split('\0')
    return {name: sha256(ROOT / name) for name in sorted(set(names))
            if name and (ROOT / name).is_file() and
            not (ROOT / name).resolve().is_relative_to(output.resolve()) and
            not (ROOT / name).resolve().is_relative_to(build.resolve())}


def check_build_source(description, build):
    if Path(description['project_path']).resolve() != ROOT.resolve() or Path(description['build_dir']).resolve() != build.resolve():
        raise ValueError('Build belongs to a different source checkout or directory')


def check_directories(build, output_root):
    # fullclean 和源码排除规则不得覆盖项目本身。
    for directory in (build, output_root):
        if ROOT.resolve().is_relative_to(directory.resolve()):
            raise ValueError('Build/output directory must not contain the source checkout')
    if output_root.resolve().is_relative_to(build.resolve()):
        raise ValueError('Release output must be outside the build directory')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'artifacts/release')
    parser.add_argument('--jobs', type=int, help='Limit clean build parallelism (positive integer)')
    args = parser.parse_args()
    if args.jobs is not None and args.jobs < 1:
        parser.error('--jobs must be a positive integer')
    build = args.build_dir.resolve()
    check_directories(build, args.output_dir)
    description = json.loads((build / 'project_description.json').read_text(encoding='utf-8'))
    check_build_source(description, build)
    version = description['project_version']
    expected = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (ROOT / 'CMakeLists.txt').read_text())[1]
    if version != expected or not re.fullmatch(r'[A-Za-z0-9._-]+', version):
        raise ValueError('Build version differs from source; rebuild before packaging')
    output = args.output_dir.resolve() / version
    normalize_command = [sys.executable, str(ROOT / 'tools/normalize_dependency_locks.py')]
    subprocess.run(normalize_command, check=True)
    # 在固定源码快照上干净构建，避免同版本旧镜像被错误关联到当前源码。
    manifest = source_manifest(args.output_dir.resolve(), build)
    if not manifest:
        raise ValueError('Source snapshot is empty')
    commit = git('rev-parse', 'HEAD')
    dirty = bool(git('status', '--porcelain'))
    idf_py = Path(description['idf_path']) / 'tools/idf.py'
    idf_command = [sys.executable, str(idf_py), '-C', str(ROOT), '-B', str(build)]
    if args.jobs is None:
        subprocess.run(idf_command + ['fullclean', 'build'], check=True)
    else:
        # IDF 的 Ninja 入口没有并行数选项；保留完整清理和配置，再由 CMake 限制构建并行数。
        subprocess.run(idf_command + ['fullclean', 'reconfigure'], check=True)
        subprocess.run(['cmake', '--build', str(build), '--parallel', str(args.jobs)], check=True)
    # Windows 配置会写回反斜杠；只恢复路径表示后仍严格比较全部源码摘要。
    subprocess.run(normalize_command, check=True)
    if source_manifest(args.output_dir.resolve(), build) != manifest or git('rev-parse', 'HEAD') != commit:
        raise ValueError('Source changed during release build; retry from a stable checkout')
    description = json.loads((build / 'project_description.json').read_text(encoding='utf-8'))
    check_build_source(description, build)
    if description['project_version'] != version:
        raise ValueError('Version changed during release build')
    data, files = flash_layout(build)
    chip = data['extra_esptool_args']['chip']
    if chip != 'esp32s3':
        raise ValueError('This release supports esp32s3 only')
    output.mkdir(parents=True, exist_ok=True)
    prefix = f'esp32s3-buddy-{version}'
    full, app = output / (prefix + '-full.bin'), output / (prefix + '-app.bin')
    command = [sys.executable, '-m', 'esptool', '--chip', chip, 'merge-bin',
               '--output', str(full), *data['write_flash_args']]
    for offset, path in files:
        command.extend([hex(offset), str(path)])
    subprocess.run(command, check=True)
    app_path = (build / data['app']['file']).resolve()
    if not app_path.is_relative_to(build) or app_path not in [path for _, path in files]:
        raise ValueError('App does not match flash inputs')
    if (int(data['app']['offset'], 0), app_path) not in files:
        raise ValueError('App offset differs from flash mapping')
    shutil.copyfile(app_path, app)
    # 未提交构建用源码文件摘要追溯，不能只把 HEAD 声称为镜像源码。
    metadata = {
        'version': version, 'source_commit': commit, 'source_dirty': dirty,
        'source_manifest_sha256': hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest(),
        'idf_version': description['git_revision'], 'target': chip,
        'hardware': 'ESP32-S3 N16R8 / ST7789V / XPT2046',
        'plugin_compatibility': 'Buddy JSONL status / Folder Push V2',
        'full_address': '0x0', 'app_address': data['app']['offset'],
        'flash_settings': data['flash_settings'],
        'full_span': {'start': 0, 'end_exclusive': full.stat().st_size},
        'data_effect': 'full resets NVS/bond/calibration/storage; app preserves partitions and migrates settings',
        'images': {path.name: {'bytes': path.stat().st_size, 'sha256': sha256(path)} for path in (full, app)},
        'flash_inputs': [{'address': hex(offset), 'file': path.relative_to(build).as_posix(),
                          'bytes': path.stat().st_size, 'sha256': sha256(path)} for offset, path in files],
    }
    (output / 'release.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (output / 'SHA256SUMS.txt').write_text(''.join(f'{sha256(path)}  {path.name}\n' for path in (full, app)), encoding='ascii')
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        shutil.copyfile(ROOT / name, output / name)
    license_index = []
    license_roots = [ROOT / 'components/third_party'] + [Path(p) for p in description['build_component_paths'] if p]
    sdk = Path(description['idf_path'])
    license_candidates = {p.resolve() for directory in license_roots if directory.is_dir()
                          for p in directory.rglob('*') if p.is_file() and
                          re.search(r'license|licence|copying|notice|ofl', p.name, re.I) and p.stat().st_size < 1024 * 1024}
    license_candidates.update(p.resolve() for p in sdk.glob('*') if p.is_file() and re.search(r'license|notice', p.name, re.I))
    with zipfile.ZipFile(output / (prefix + '-debug.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for offset, path in files:
            archive.write(path, 'build/' + path.relative_to(build).as_posix())
        for name in (description['app_elf'], description['project_name'] + '.map', 'flasher_args.json', 'project_description.json'):
            archive.write(build / name, 'build/' + name)
        archive.write(Path(description['config_file']), 'build/sdkconfig')
        for name in ('dependencies.lock', 'sdkconfig.defaults', 'partitions.csv', 'LICENSE', 'THIRD_PARTY_NOTICES.md'):
            archive.write(ROOT / name, name)
        archive.writestr('source-manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2))
        for path in sorted(license_candidates):
            if path.is_relative_to(ROOT):
                relative = 'project/' + path.relative_to(ROOT).as_posix()
            elif path.is_relative_to(sdk):
                relative = 'esp-idf/' + path.relative_to(sdk).as_posix()
            else:
                raise ValueError(f'Unexpected license location: {path}')
            archive.write(path, 'licenses/' + relative)
            license_index.append({'file': relative, 'sha256': sha256(path)})
        archive.writestr('license-index.json', json.dumps(license_index, indent=2))
    with zipfile.ZipFile(output / (prefix + '-source.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, expected_hash in manifest.items():
            content = (ROOT / name).read_bytes()
            if hashlib.sha256(content).hexdigest() != expected_hash:
                raise ValueError(f'Source changed while archiving: {name}')
            archive.writestr(name, content)
    print(f'Packaged {version}: {output}; app={metadata["app_address"]}; {len(license_index)} license files')


if __name__ == '__main__':
    main()
