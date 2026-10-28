"""Small deterministic integration tests against the actual libclang parser."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import analyze


class ArchitectureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='tinyos-architecture-test-')
        self.root = Path(self.temporary.name)
        self.destination = self.root / 'output'
        self.destination.mkdir()
        self.root_patch = patch.object(analyze, 'ROOT', self.root)
        self.root_patch.start()
        self.cache_patch = patch.object(analyze, 'CACHE', self.root / 'cache')
        self.cache_patch.start()

    def tearDown(self):
        self.cache_patch.stop()
        self.root_patch.stop()
        self.temporary.cleanup()

    def entry(self, name, content, standard='c11'):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return {'directory': str(self.root), 'file': str(path),
                'arguments': ['clang', '-std=' + standard, '-I' + str(self.root), str(path)]}

    def extract(self, entries):
        with contextlib.redirect_stdout(io.StringIO()):
            analyze.libclang(entries, self.destination)
        return json.loads((self.destination / 'graph.json').read_text())

    def test_direct_call_and_header_dependency(self):
        (self.root / 'api.h').write_text('int leaf(void);\n')
        entries = [self.entry('a.c', '#include "api.h"\nint caller(void) { return leaf(); }\n'),
                   self.entry('b.c', 'int leaf(void) { return 42; }\n')]
        graph = self.extract(entries)
        names = {node['id']: node['name'] for node in graph['functions']}
        edges = {(names.get(call['from']), names.get(call['to'])) for call in graph['calls']}
        self.assertIn(('caller', 'leaf'), edges)
        self.assertTrue(any(edge['from'] == 'a.c' and edge['to'].endswith('api.h') for edge in graph['includes']))
        self.assertFalse(graph['diagnostics'])

    def test_error_in_first_translation_unit_is_not_hidden_by_last(self):
        entries = [self.entry('bad.c', 'int broken( {\n'), self.entry('good.c', 'int good(void) { return 0; }\n')]
        with self.assertRaisesRegex(RuntimeError, 'graph is partial'):
            self.extract(entries)
        graph = json.loads((self.destination / 'graph.json').read_text())
        self.assertTrue(graph['diagnostics'])

    def test_cpp_template_has_a_node(self):
        graph = self.extract([self.entry('template.cpp',
            'int leaf() { return 1; }\ntemplate<class T> int helper(T) { return leaf(); }\n', 'c++20')])
        self.assertIn('helper', {node['name'] for node in graph['functions']})

    def test_kernel_scope_includes_library_sources(self):
        sources = ['kernel/boot/main.c', 'drivers/uart.c', 'lib/string.c', 'user/main.c']
        commands = '\n'.join('riscv64-linux-gnu-gcc -Iinclude -c ' + source + ' -o output.o'
                             for source in sources)
        with patch.object(analyze, 'run', return_value=subprocess.CompletedProcess([], 0, stdout=commands)), \
             patch.object(analyze.subprocess, 'check_output', return_value='/clang/resource\n'):
            entries, _ = analyze.prepare('kernel')
        selected = {str(Path(entry['file']).relative_to(self.root)) for entry in entries}
        self.assertEqual(selected, {'kernel/boot/main.c', 'drivers/uart.c', 'lib/string.c'})

    def test_uml_diagrams_allow_library_paths(self):
        entries = [self.entry('lib/string.c', 'int library_function(void) { return 0; }\n')]
        with patch.object(analyze, 'prepare', return_value=(entries, self.destination)), \
             patch.object(analyze, 'run'), \
             patch('sys.argv', ['analyze.py', 'uml']):
            analyze.main()
        config = (self.destination / '.clang-uml').read_text()
        allowed_paths = []
        for line in config.splitlines():
            if line.strip().startswith('paths: '):
                items = line.split(':', 1)[1].strip()[1:-1].split(',')
                allowed_paths.append([item.strip().strip('"') for item in items])
        self.assertEqual(len(allowed_paths), 2)
        for paths in allowed_paths:
            self.assertIn('lib', paths)
        self.assertIn(str(self.root / 'lib/string.c'), config)

    def test_joern_snapshot_contains_grouped_and_local_headers(self):
        entries = [self.entry('kernel/boot/main.c', '#include "mm/page.h"\nint boot(void) { return 0; }\n')]
        headers = {
            'include/mm/page.h': '#define PAGE_SIZE 4096\n',
            'include/arch/riscv/context.h': 'struct context { unsigned long pc; };\n',
            'kernel/boot/local.h': 'int boot(void);\n',
        }
        for name, content in headers.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)

        def inspect_snapshot(arguments, **options):
            staging = Path(arguments[1])
            for name, content in headers.items():
                self.assertEqual((staging / name).read_text(), content)
            self.assertTrue((staging / 'kernel/boot/main.c').is_file())
            database = json.loads((staging / 'compile_commands.json').read_text())
            self.assertEqual(database[0]['file'], str(staging / 'kernel/boot/main.c'))
            self.assertEqual(database[0]['directory'], str(staging))

        with patch.object(analyze, 'prepare', return_value=(entries, self.destination)), \
             patch.object(analyze, 'run', side_effect=inspect_snapshot) as runner, \
             patch('sys.argv', ['analyze.py', 'joern']):
            analyze.main()
        runner.assert_called_once()


if __name__ == '__main__':
    unittest.main()
