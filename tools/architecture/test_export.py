"""Verify the exporter preserves graph meaning across source formats."""
import unittest
from export import call_dot, mermaid_dot


class ExportTests(unittest.TestCase):
    def test_mermaid_keeps_isolated_module(self):
        result = mermaid_dot('flowchart\nsubgraph C_1[kernel]\nend\nsubgraph C_2[drivers]\nend\nsubgraph C_3[include]\nend\nC_1 -.-> C_3\n')
        self.assertIn('"C_2" [label="drivers"]', result)
        self.assertIn('"C_1" -> "C_3" [style=dashed]', result)

    def test_include_paths_keep_isolated_file_without_extra_container(self):
        result = mermaid_dot('flowchart\nsubgraph C_1[kernel]\nC_2[orphan.c]\nend\n')
        self.assertIn('"C_2" [label="kernel/orphan.c"]', result)
        self.assertNotIn('"C_1" [label=', result)

    def test_focus_keeps_incoming_and_outgoing_but_not_second_hop(self):
        graph = {'functions': [{'id': name, 'name': name, 'file': 'fixture.c', 'line': 1}
                               for name in ('caller', 'focus', 'leaf', 'unrelated')],
                 'calls': [{'from': 'caller', 'to': 'focus'}, {'from': 'focus', 'to': 'leaf'},
                           {'from': 'leaf', 'to': 'unrelated'}]}
        result = call_dot(graph, 'focus')
        self.assertIn('"caller" -> "focus"', result)
        self.assertIn('"focus" -> "leaf"', result)
        self.assertNotIn('unrelated', result)

    def test_unknown_mermaid_syntax_is_rejected(self):
        with self.assertRaises(ValueError):
            mermaid_dot('flowchart\nunknown syntax\n')


if __name__ == '__main__':
    unittest.main()
