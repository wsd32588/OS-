#!/usr/bin/env python3
"""Install optional analysis tools in the ignored project cache (Ubuntu 26.04 x86-64)."""
import argparse
import hashlib
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / '.cache/architecture'
DOWNLOADS = CACHE / 'downloads'
UML_VERSION = '0.6.3'
JOERN_VERSION = 'v4.0.652'
UML_SHA256 = '5eba5b39b3417259974e1561b7f7765c303cea7435ae77403203454d2753d1bb'
JOERN_SHA512 = '27183505b6a0412ba3a5ffee0800e403a4e571e7b92c6ba69fd2adec8eeb150e8f9a92204da6a36d6eda7c50fe5292598f408515dfa5cb4303bd1a572e7bf34a'


def run(arguments, **options):
    subprocess.run(arguments, check=True, **options)


def download(url, name, digest=None, algorithm='sha256'):
    target = DOWNLOADS / name
    if not target.exists():
        partial = target.with_suffix(target.suffix + '.part')
        run(['curl', '-fLsS', '--retry', '2', url, '-o', str(partial)])
        partial.replace(target)
    if digest:
        with target.open('rb') as stream:
            actual = hashlib.file_digest(stream, algorithm).hexdigest()
        if actual != digest:
            raise RuntimeError(f'Checksum mismatch: {target}; inspect/remove this cached download before retrying')
    return target


def packages(names, destination):
    missing = [name for name in names if not list(DOWNLOADS.glob(name + '_*.deb'))]
    if missing:
        run(['apt-get', 'download', *missing], cwd=DOWNLOADS)
    for name in names:
        candidates = sorted(DOWNLOADS.glob(name + '_*.deb'))
        if len(candidates) != 1:
            raise RuntimeError(f'Expected one cached package for {name}, found {len(candidates)}')
        run(['dpkg-deb', '-x', str(candidates[0]), str(destination)])


def install(backend):
    if platform.machine() != 'x86_64' or 'VERSION_ID="26.04"' not in Path('/etc/os-release').read_text():
        raise RuntimeError('This cache installer supports Ubuntu 26.04 x86-64. For other systems install the tools externally; see README.')
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    if backend in ('all', 'libclang'):
        if not Path('/usr/lib/x86_64-linux-gnu/libclang-21.so.21').exists() or not shutil.which('clang'):
            raise RuntimeError('System Clang/libclang 21 is required. Install clang-21 and libclang1-21 first.')
        environment = CACHE / 'venv'
        if not (environment / 'bin/python').exists():
            run([sys.executable, '-m', 'venv', str(environment)])
        run([str(environment / 'bin/pip'), 'install', '-r', str(Path(__file__).with_name('requirements.txt'))])
    if backend in ('all', 'uml'):
        name = f'clang-uml_{UML_VERSION}-0ubuntu1ppa1.questing_amd64.deb'
        package = download(f'https://github.com/bkryza/clang-uml/releases/download/{UML_VERSION}/{name}',
                           'clang-uml-questing.deb', UML_SHA256)
        run(['dpkg-deb', '-x', str(package), str(CACHE / 'clang-uml')])
        packages(['libyaml-cpp0.8', 'libclang-cpp20', 'libllvm20', 'libz3-4'], CACHE / 'clang-uml')
    if backend in ('all', 'export'):
        packages(['graphviz', 'libgvc7', 'libcgraph8', 'libcdt6', 'libpathplan4',
                  'libgvpr2', 'libgvplugin-pango8', 'libxdot4', 'libltdl7'], CACHE / 'graphviz')
    if backend in ('all', 'joern'):
        archive = download(f'https://github.com/joernio/joern/releases/download/{JOERN_VERSION}/joern-cli-linux-x86_64.zip',
                           'joern.zip', JOERN_SHA512, 'sha512')
        destination = CACHE / 'joern'
        if not (destination / 'joern-cli/joern-parse').exists():
            with zipfile.ZipFile(archive) as package:
                for entry in package.infolist():
                    target = (destination / entry.filename).resolve()
                    if not target.is_relative_to(destination.resolve()):
                        raise RuntimeError('Unsafe ZIP member path')
                package.extractall(destination)
                for entry in package.infolist():
                    mode = (entry.external_attr >> 16) & 0o777
                    if mode:
                        (destination / entry.filename).chmod(mode)
        packages(['openjdk-25-jre-headless'], CACHE / 'java')
        # Debian Java configuration uses absolute /etc links; relocate inside the cache.
        java_root = (CACHE / 'java').resolve()
        for path in java_root.rglob('*'):
            if path.is_symlink():
                target = path.readlink()
                relocated = java_root / str(target).lstrip('/')
                if target.is_absolute() and relocated.exists():
                    path.unlink()
                    path.symlink_to(relocated)
    licenses = CACHE / 'licenses'
    licenses.mkdir(exist_ok=True)
    for name, url in {
        'clang-uml-LICENSE.md': f'https://raw.githubusercontent.com/bkryza/clang-uml/{UML_VERSION}/LICENSE.md',
        'joern-LICENSE': f'https://raw.githubusercontent.com/joernio/joern/{JOERN_VERSION}/LICENSE',
        'LLVM-LICENSE.TXT': 'https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-21.1.7/LICENSE.TXT',
    }.items():
        cached = download(url, name)
        shutil.copyfile(cached, licenses / name)
    print(f'Installed {backend} in {CACHE}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend', choices=['all', 'libclang', 'uml', 'joern', 'export'], default='all')
    try:
        install(parser.parse_args().backend)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'architecture install: {error}', file=sys.stderr)
        sys.exit(1)
