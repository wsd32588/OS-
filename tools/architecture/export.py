#!/usr/bin/env python3
"""Export existing local analysis results as DOT/SVG and an offline HTML gallery."""
import argparse
import html
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from analyze import CACHE, ROOT


def quoted(value):
    return json.dumps(value, ensure_ascii=False)


def call_dot(graph, focus=None):
    nodes = {node['id']: node for node in graph['functions']}
    edges = {(call['from'], call['to']) for call in graph['calls'] if call['from'] in nodes and call['to'] in nodes}
    if focus:
        selected = {key for key, node in nodes.items() if node['name'] == focus}
        if not selected:
            return None
        neighbors = selected | {endpoint for edge in edges if selected.intersection(edge) for endpoint in edge}
        nodes = {key: node for key, node in nodes.items() if key in neighbors}
        edges = {edge for edge in edges if selected.intersection(edge)}
    lines = ['digraph calls {', 'rankdir=LR;', 'node [shape=box, fontname="sans-serif"];', 'edge [color="#64748b"];']
    for key, node in nodes.items():
        label = f"{node['name']}\n{node['file']}:{node['line']}"
        lines.append(f'{quoted(key)} [label={quoted(label)}];')
    lines.extend(f'{quoted(source)} -> {quoted(target)};' for source, target in sorted(edges))
    return '\n'.join(lines + ['}'])


def mermaid_dot(source):
    """Convert only the flowchart subset emitted by our clang-uml configs."""
    nodes = {}
    edges = []
    groups = []
    group_identifiers = []
    nonempty_groups = set()
    for line in source.splitlines():
        line = line.strip()
        if line == 'end':
            if groups:
                groups.pop()
                group_identifiers.pop()
            continue
        match = re.fullmatch(r'(subgraph\s+)?(C_\d+)\[(.*)\]', line)
        if match:
            is_group, identifier, label = match.groups()
            if group_identifiers:
                nonempty_groups.add(group_identifiers[-1])
            if is_group:
                group_identifiers.append(identifier)
                groups.append(label)
                nodes[identifier] = '/'.join(groups)
            else:
                nodes[identifier] = '/'.join([*groups, label])
            continue
        match = re.fullmatch(r'(C_\d+)\s+(-->|-\.->)\s+(C_\d+)', line)
        if match:
            edges.append(match.groups())
            continue
        if line and not line.startswith(('flowchart', '%%')):
            raise ValueError(f'Unsupported Mermaid syntax: {line}')
    # Flatten nonempty containers, preserving leaves and empty directory modules.
    connected = {endpoint for source, _, target in edges for endpoint in (source, target)}
    lines = ['digraph dependencies {', 'rankdir=LR;', 'node [shape=box, fontname="sans-serif"];']
    lines.extend(f'{quoted(key)} [label={quoted(label)}];' for key, label in nodes.items() if key in connected or key not in nonempty_groups)
    lines.extend(f'{quoted(source)} -> {quoted(target)} [style={"dashed" if arrow == "-.->" else "solid"}];'
                 for source, arrow, target in edges)
    return '\n'.join(lines + ['}'])


def export(scope):
    directory = CACHE / scope
    graph = json.loads((directory / 'graph.json').read_text())
    if any(' error:' in item or ' fatal error:' in item for item in graph['diagnostics']):
        raise RuntimeError('Existing libclang graph contains errors; regenerate analysis first')
    output = directory / 'export'
    output.mkdir(exist_ok=True)
    environment = os.environ.copy()
    cached_dot = CACHE / 'graphviz/usr/bin/dot'
    dot = shutil.which('dot') or str(cached_dot)
    if not shutil.which('dot'):
        libraries = CACHE / 'graphviz/usr/lib/x86_64-linux-gnu'
        environment['LD_LIBRARY_PATH'] = str(libraries) + ':' + environment.get('LD_LIBRARY_PATH', '')
        environment['GVBINDIR'] = str(libraries / 'graphviz')
        subprocess.run([dot, '-c'], env=environment, check=True)
    panels = []

    def render(name, label, content):
        dot_file = output / (name + '.dot')
        svg_file = output / (name + '.svg')
        dot_file.write_text(content)
        subprocess.run([dot, '-Tsvg', str(dot_file), '-o', str(svg_file)], env=environment, check=True)
        panels.append({'name': name, 'label': label})

    for filename, label in [('includes', '头文件依赖 · clang-uml'), ('modules', '目录依赖 · clang-uml')]:
        render(filename, label, mermaid_dot((directory / (filename + '.mmd')).read_text()))
    for function in ('kernel_main', 'syscall_handle', 'user_memory_release', 'user_copy_from', 'sched_reschedule', 'main'):
        content = call_dot(graph, function)
        if content:
            render('calls-' + function, function + ' · 直接调用与调用者', content)
    render('calls', '完整函数调用图 · libclang', call_dot(graph))

    cpg_file = directory / 'cpg.bin'
    if cpg_file.exists():
        java = CACHE / 'java/usr/lib/jvm/java-25-openjdk-amd64'
        if java.exists():
            environment['JAVA_HOME'] = str(java)
        executable = shutil.which('joern-export') or str(CACHE / 'joern/joern-cli/joern-export')
        with tempfile.TemporaryDirectory(prefix='cfg-export-', dir=directory) as temporary:
            cfg_directory = Path(temporary) / 'cfg'
            subprocess.run([executable, str(cpg_file), '--repr', 'cfg', '--format', 'dot', '--out', str(cfg_directory)],
                           env=environment, check=True)
            for source in sorted(cfg_directory.glob('*.dot')):
                content = source.read_text()
                match = re.search(r'^digraph\s+"([^"]*)"', content)
                title = match.group(1) if match else source.stem
                render('joern-' + source.stem, title + ' · Joern CFG', content)
    template = Path(__file__).with_name('viewer.html').read_text()
    options = '\n'.join(f'<option value="{html.escape(panel["name"])}">{html.escape(panel["label"])}</option>' for panel in panels)
    page = template.replace('__OPTIONS__', options).replace('__SCOPE__', html.escape(scope))
    (output / 'index.html').write_text(page)
    print(f'{len(panels)} SVG diagrams exported: {output / "index.html"}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scope', choices=['kernel', 'user', 'host'], default='kernel')
    try:
        export(parser.parse_args().scope)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'architecture export: {error}\n')
