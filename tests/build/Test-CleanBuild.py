# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class CleanBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)/'checkout [literal]'
        for relative in ('tools/maintenance/ArchiveBuildTree.py', 'tools/maintenance/maintenance_paths.py',
                         'tools/maintenance/Clean-ClipboardBuild.ps1', 'tools/build/ClipboardBuildIdentity.ps1'):
            target = self.root/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT/relative, target)
        python = sys.executable.replace("'", "''")
        (self.root/'Build.ps1').write_text(f"param([switch] $CheckOnly)\n[pscustomobject]@{{result='passed';PythonPath='{python}'}}\n")
        (self.root/'build/subfolder').mkdir(parents=True)
        (self.root/'build/subfolder/evidence [1].log').write_bytes(b'keep original evidence')
        (self.root/'source.txt').write_bytes(b'untouched')

    def run_clean(self, apply=False):
        command = [shutil.which('pwsh'), '-NoProfile', '-File', str(self.root/'tools/maintenance/Clean-ClipboardBuild.ps1')]
        if apply:
            command.append('-Apply')
        return subprocess.run(command, capture_output=True, text=True)

    def test_default_is_report_only(self):
        result = self.run_clean()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root/'build/subfolder/evidence [1].log').is_file())
        self.assertFalse((self.root/'.local').exists())

    def test_apply_archives_then_removes_exact_build(self):
        result = self.run_clean(True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root/'build').exists())
        self.assertEqual((self.root/'source.txt').read_bytes(), b'untouched')
        manifest = next((self.root/'.local/build-history').glob('*/manifest.json'))
        data = json.loads(manifest.read_text())
        with zipfile.ZipFile(self.root/data['archive']['path']) as z:
            self.assertEqual(z.read('build/subfolder/evidence [1].log'), b'keep original evidence')

    def test_missing_persistent_inputs_refuse_delete(self):
        (self.root/'Build.ps1').write_text("throw 'Missing persistent toolchain'\n")
        result = self.run_clean(True)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.root/'build/subfolder/evidence [1].log').is_file())

    def test_empty_build_report_and_apply(self):
        (self.root/'build/subfolder/evidence [1].log').unlink()
        result = self.run_clean()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root/'build/subfolder').is_dir())
        result = self.run_clean(True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root/'build').exists())
        manifest = next((self.root/'.local/build-history').glob('*/manifest.json'))
        data = json.loads(manifest.read_text())
        self.assertEqual(data['files'], [])
        self.assertIn('build/subfolder', data['directories'])


if __name__ == '__main__':
    unittest.main()
