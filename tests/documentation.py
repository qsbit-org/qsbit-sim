"""Regression tests for documentation drift detection, without build dependencies."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('check_docs', Path(__file__).resolve().parents[1] / 'tools/check_docs.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class DocumentationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'docs/modules').mkdir(parents=True)
        (self.root / 'api.hpp').write_text('struct Input {};\n// end\n')
        self.page = self.root / 'docs/modules/module.md'
        self.page.write_text('''# Module

[API](../../api.hpp) [Behavior](#behavior)

## Behavior

**CTest:** `core.example`.

<!-- source: {"path": "api.hpp", "start": "struct Input", "end": "// end"} -->
```cpp
struct Input {};
```
<!-- /source -->
''')

    def check(self, names=('core.example',), write=False):
        return checker.check(self.root, set(names), write)

    def test_current_contract(self):
        self.assertEqual(self.check(), [])

    def test_source_change_requires_refresh(self):
        (self.root / 'api.hpp').write_text('struct Input { int value; };\n// end\n')
        self.assertTrue(any('stale source' in e for e in self.check()))
        self.assertEqual(self.check(write=True), [])
        self.assertIn('int value', self.page.read_text())
        self.assertEqual(self.check(), [])

    def test_empty_excerpt_refresh_is_idempotent(self):
        self.page.write_text(self.page.read_text().replace('```cpp\nstruct Input {};\n```\n', ''))
        self.assertEqual(self.check(write=True), [])
        once = self.page.read_text()
        self.assertEqual(self.check(), [])
        self.assertEqual(self.check(write=True), [])
        self.assertEqual(self.page.read_text(), once)

    def test_removed_test(self):
        self.assertTrue(any('unregistered CTest' in e for e in self.check(names=())))

    def test_missing_link_and_heading(self):
        self.page.write_text(self.page.read_text() + '\n[Missing](gone.md) [Heading](#gone)\n')
        errors = self.check()
        self.assertTrue(any('missing link target' in e for e in errors))
        self.assertTrue(any('missing heading' in e for e in errors))

    def test_missing_module_evidence(self):
        self.page.write_text(self.page.read_text().replace('**CTest:** `core.example`.', ''))
        self.assertTrue(any('missing CTest evidence' in e for e in self.check()))

    def test_invalid_excerpt_boundaries(self):
        (self.root / 'api.hpp').write_text('struct Other {};\n// end\n')
        self.assertTrue(any('non-unique source boundaries' in e for e in self.check(write=True)))

    def test_malformed_source_marker(self):
        self.page.write_text(self.page.read_text().replace('<!-- /source -->', ''))
        self.assertTrue(any('malformed source marker' in e for e in self.check()))

    def test_duplicate_heading_and_code_fence(self):
        self.assertEqual(checker.anchors('# A\n## A\n```cpp\n#define X\n```\n'), {'a', 'a-1'})


if __name__ == '__main__':
    unittest.main()
