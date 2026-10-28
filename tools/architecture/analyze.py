#!/usr/bin/env python3
"""Local architecture entry points; never invokes a model or uploads source."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import os
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / '.cache/architecture'


def run(arguments, **options):
    return subprocess.run(arguments, cwd=ROOT, check=True, **options)


def prepare(scope):
    entries = []
    if scope == 'host':
        run(['cmake', '-S', 'test', '-B', 'test/build/cmake', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        entries = json.loads((ROOT / 'test/build/cmake/compile_commands.json').read_text())
    else:
        output = run(['make', '-Bn', 'all'], capture_output=True, text=True).stdout
        source_prefixes = ('kernel/', 'drivers/', 'lib/') if scope == 'kernel' else (scope + '/',)
        for line in output.splitlines():
            args = shlex.split(line)
            if not args or not args[0].endswith('gcc') or '-c' not in args:
                continue
            source = args[args.index('-c') + 1]
            if not source.endswith('.c') or not source.startswith(source_prefixes):
                continue
            entries.append({'directory': str(ROOT), 'file': str(ROOT / source), 'arguments': args})
    if not entries:
        raise RuntimeError('No compilation entries found')
    result = []
    for entry in entries:
        source = Path(entry['file'])
        if not source.is_absolute():
            source = Path(entry['directory']) / source
        args = entry.get('arguments') or shlex.split(entry['command'])
        flags = []
        skip = False
        for argument in args[1:]:
            if skip:
                skip = False
                continue
            if argument in ('-o', '-MF', '-MT', '-MQ'):
                skip = True
                continue
            if argument in ('-c', '-MMD', '-MP') or argument == entry['file'] or argument == str(source.relative_to(ROOT)):
                continue
            flags.append(argument)
        resource = subprocess.check_output(['clang', '-print-resource-dir'], text=True).strip()
        flags += ['-isystem', str(Path(resource) / 'include')]
        if scope != 'host':
            flags.insert(0, '--target=riscv64-unknown-linux-gnu')
        result.append({'directory': entry['directory'], 'file': str(source), 'arguments': ['clang++' if source.suffix == '.cpp' else 'clang', *flags, str(source)]})
    destination = CACHE / scope
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'compile_commands.json').write_text(json.dumps(result, indent=2) + '\n')
    return result, destination


def libclang(entries, destination):
    from clang import cindex
    # Match the system Clang headers when a system libclang is available.
    system_library = Path('/usr/lib/x86_64-linux-gnu/libclang-21.so.21')
    if system_library.exists() and not cindex.Config.loaded:
        cindex.Config.set_library_file(str(system_library))
    index = cindex.Index.create()
    nodes = {}
    calls = []
    includes = []
    diagnostics = []
    has_errors = False

    def location(cursor):
        if not cursor.location.file:
            return None
        path = Path(cursor.location.file.name).resolve()
        if not path.is_relative_to(ROOT) or '.cache' in path.parts:
            return None
        return {'file': str(path.relative_to(ROOT)), 'line': cursor.location.line}

    def visit(cursor, caller=None):
        where = location(cursor)
        if cursor.location.file and where is None:
            return
        if cursor.kind in (cindex.CursorKind.FUNCTION_DECL, cindex.CursorKind.CXX_METHOD,
                            cindex.CursorKind.FUNCTION_TEMPLATE, cindex.CursorKind.CONSTRUCTOR,
                            cindex.CursorKind.DESTRUCTOR) and where and cursor.is_definition():
            caller = cursor.get_usr()
            nodes[caller] = {'id': caller, 'name': cursor.spelling, **where}
        if cursor.kind == cindex.CursorKind.CALL_EXPR and caller and where:
            referenced = cursor.referenced
            calls.append({'from': caller, 'to': referenced.get_usr() if referenced else None,
                          'name': cursor.spelling, **where})
        for child in cursor.get_children():
            visit(child, caller)

    for entry in entries:
        flags = entry['arguments'][1:-1] + ['-working-directory=' + entry['directory']]
        unit = index.parse(entry['file'], args=flags)
        diagnostics.extend(str(item) for item in unit.diagnostics)
        has_errors |= any(item.severity >= cindex.Diagnostic.Error for item in unit.diagnostics)
        for inclusion in unit.get_includes():
            if inclusion.source and Path(inclusion.source.name).resolve().is_relative_to(ROOT):
                includes.append({'from': str(Path(inclusion.source.name).resolve().relative_to(ROOT)), 'to': inclusion.include.name})
        visit(unit.cursor)
    graph = {'scope': destination.name, 'functions': list(nodes.values()), 'calls': calls,
             'includes': includes, 'diagnostics': diagnostics,
             'limitations': ['Static direct calls only; indirect calls may be unresolved.', 'Assembly/trap/runtime edges require manual review.']}
    (destination / 'graph.json').write_text(json.dumps(graph, indent=2) + '\n')
    lines = ['flowchart LR']
    identifiers = {key: f'n{i}' for i, key in enumerate(nodes)}
    for key, node in nodes.items():
        label = node['name'] + ' · ' + node['file']
        lines.append(f'  {identifiers[key]}[{json.dumps(label)}]')
    for call in calls:
        if call['from'] in identifiers and call['to'] in identifiers:
            lines.append(f"  {identifiers[call['from']]} --> {identifiers[call['to']]}")
    (destination / 'calls.mmd').write_text('\n'.join(dict.fromkeys(lines)) + '\n')
    if has_errors:
        raise RuntimeError('Parsing errors; graph is partial. See graph.json diagnostics.')
    print(f'{len(nodes)} functions, {len(calls)} calls: {destination}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backend', choices=['prepare', 'libclang', 'uml', 'joern'])
    parser.add_argument('--scope', choices=['kernel', 'user', 'host'], default='kernel')
    options = parser.parse_args()
    entries, destination = prepare(options.scope)
    if options.backend == 'libclang':
        libclang(entries, destination)
    elif options.backend == 'uml':
        executable = shutil.which('clang-uml') or str(CACHE / 'clang-uml/usr/bin/clang-uml')
        config = destination / '.clang-uml'
        sources = '\n'.join('      - ' + json.dumps(entry['file']) for entry in entries)
        include_paths = json.dumps(['kernel', 'drivers', 'lib', 'include', 'user', 'test'])
        config.write_text(f'compilation_database_dir: {json.dumps(str(destination))}\noutput_directory: {json.dumps(str(destination))}\ndiagrams:\n  includes:\n    type: include\n    relative_to: {json.dumps(str(ROOT))}\n    include:\n      paths: {include_paths}\n    glob:\n{sources}\n  modules:\n    type: package\n    package_type: directory\n    relative_to: {json.dumps(str(ROOT))}\n    include:\n      paths: {include_paths}\n    glob:\n{sources}\n')
        environment = os.environ.copy()
        environment['LD_LIBRARY_PATH'] = str(CACHE / 'clang-uml/usr/lib/x86_64-linux-gnu') + ':' + environment.get('LD_LIBRARY_PATH', '')
        run([executable, '-c', str(config), '-g', 'mermaid'], env=environment)
    elif options.backend == 'joern':
        executable = shutil.which('joern-parse') or str(CACHE / 'joern/joern-cli/joern-parse')
        with tempfile.TemporaryDirectory(prefix='sources-', dir=destination) as temporary:
            staging = Path(temporary)
            sources = {Path(entry['file']) for entry in entries}
            header_directories = {source.parent for source in sources}
            for directory in header_directories:
                sources.update(source for source in directory.glob('*') if source.suffix in ('.h', '.hpp'))
            # Public headers now live in grouped subdirectories under include/.
            sources.update(source for source in (ROOT / 'include').rglob('*')
                           if source.is_file() and source.suffix in ('.h', '.hpp'))
            for source in sorted(sources):
                target = staging / source.relative_to(ROOT)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read_bytes())
            environment = os.environ.copy()
            java_home = CACHE / 'java/usr/lib/jvm/java-25-openjdk-amd64'
            if java_home.exists():
                environment['JAVA_HOME'] = str(java_home)
            database = []
            for entry in entries:
                rewritten = json.loads(json.dumps(entry).replace(str(ROOT), str(staging)))
                database.append(rewritten)
            compilation_database = staging / 'compile_commands.json'
            compilation_database.write_text(json.dumps(database, indent=2) + '\n')
            run([executable, str(staging), '--language', 'NEWC', '--output', str(destination / 'cpg.bin'),
                 '--frontend-args', '--compilation-database', str(compilation_database),
                 '--log-problems', '--enable-file-content'], env=environment)
    else:
        print(destination / 'compile_commands.json')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, ImportError) as error:
        print(f'architecture: {error}', file=sys.stderr)
        sys.exit(1)
