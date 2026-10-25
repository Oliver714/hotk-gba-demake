"""Check release version selection and failure behavior before publishing."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from release_notes import extract_entry, render_notes


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, text):
        path = self.root / name
        path.write_text(text, encoding='utf-8')
        return path

    def test_both_languages_select_only_the_matching_version(self):
        self.write('CHANGELOG.md', '# 更新记录\n## v0.3.0\n- 新版本\n## v0.2.0\n- 中文更新\n### 修复\n- 内容\n## v0.1.0\n- 旧版本\n')
        self.write('CHANGELOG.en.md', '# Changelog\n## v0.2.0\n- English changes\n## v0.1.0\n- Old changes\n')
        self.assertEqual(render_notes(self.root, 'v0.2.0'),
                         '## 简体中文\n\n- 中文更新\n### 修复\n- 内容\n\n## English\n\n- English changes\n')

    def test_brackets_date_bom_and_fenced_headings(self):
        path = self.write('CHANGELOG.md', '\ufeff# Changelog\n## [v0.2.0] - 2026-10-25\n- Changes\n```md\n## v0.1.0\n```\nMore text\n## v0.1.0\n- Old\n')
        self.assertEqual(extract_entry(path, 'v0.2.0'), '- Changes\n```md\n## v0.1.0\n```\nMore text')

    def test_prefix_match_does_not_select_another_version(self):
        path = self.write('CHANGELOG.md', '## v0.2.01\n- Wrong version\n')
        with self.assertRaisesRegex(ValueError, 'found 0'):
            extract_entry(path, 'v0.2.0')

    def test_duplicate_and_empty_entries_fail(self):
        for text in ('## v0.2.0\n- First\n## v0.2.0\n- Second\n',
                     '## v0.2.0\n\n', '## v0.2.0\n<!-- TODO -->\n'):
            with self.subTest(text=text):
                path = self.write('CHANGELOG.md', text)
                with self.assertRaises(ValueError):
                    extract_entry(path, 'v0.2.0')

    def test_missing_english_entry_fails(self):
        self.write('CHANGELOG.md', '## v0.2.0\n- 中文更新\n')
        self.write('CHANGELOG.en.md', '## v0.1.0\n- Old changes\n')
        with self.assertRaisesRegex(ValueError, 'CHANGELOG.en.md'):
            render_notes(self.root, 'v0.2.0')

    def test_invalid_tag_fails(self):
        for tag in ('', 'main', 'v', 'v0.2.0\n', '../v0.2.0'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                render_notes(self.root, tag)

    def test_cli_does_not_replace_output_when_version_is_missing(self):
        output = self.write('notes.md', 'Previous notes')
        result = subprocess.run([sys.executable, str(ROOT / 'tools/release_notes.py'),
                                 '--tag', 'v9999.9999.9999', '--output', str(output)],
                                capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'CHANGELOG.md', result.stderr)
        self.assertEqual(output.read_text(encoding='utf-8'), 'Previous notes')


if __name__ == '__main__':
    unittest.main()
