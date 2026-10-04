# SPDX-License-Identifier: GPL-3.0-or-later
"""Lossless output retention and failure-path checks in disposable Windows fixtures."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/maintenance'))
import outputs_retention as retention
from maintenance_paths import contained, digest, read_json, snapshot, workspace


class RetentionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'checkout'
        self.root.mkdir(exist_ok=True)
        self.root = workspace(self.root)
        self.rules = read_json(ROOT / 'config/outputs-retention.json')
        self.write(retention.POLICY, self.rules)
        self.case = 'outputs/finished-case'
        self.payload = b'raw evidence with repeated pages\n' * 8192
        self.write(self.case + '/capture.dmp', self.payload)
        self.write(self.case + '/same-copy.bin', self.payload)
        self.write(self.case + '/REPORT.md', b'Concise findings that remain accessible.\n')
        self.write(self.case + '/verification.json', dict(result='passed'))

    def write(self, path, value):
        target = contained(self.root, path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
        return target

    def status(self, status='archive'):
        retention.set_state(self.root, self.rules, self.case, status, 'Explicit fixture disposition')
        return retention.states(self.root, self.rules)

    def item(self):
        return retention.report(self.root, self.rules, retention.states(self.root, self.rules))['cases'][0]

    def apply(self):
        return retention.apply(self.root, self.rules, retention.states(self.root, self.rules))

    def test_unknown_passed_build_is_not_assumed_resolved(self):
        self.assertEqual(self.item()['action'], 'keep')
        self.assertIn('Unresolved', self.item()['reason'])
        self.assertFalse((self.root / '.local').exists())

    def test_completed_case_cools_down_and_active_case_stays(self):
        self.status('completed')
        self.assertEqual(self.item()['action'], 'keep')
        old = time.time() - 15 * 86400
        for path in (self.root / self.case).iterdir():
            os.utime(path, (old, old))
        self.assertEqual(self.item()['action'], 'archive')
        self.status('active')
        self.assertEqual(self.item()['action'], 'keep')

    def test_protected_fixture_overrides_archive_request(self):
        self.rules['protected'][self.case] = 'Active fixture'
        self.status()
        self.assertEqual(self.item()['reason'], 'Active fixture')
        self.assertEqual(self.apply()['removedFiles'], 0)

    def test_configured_input_inside_case_is_protected(self):
        self.status()
        self.write('.local/build-paths.json', dict(schemaVersion=1, RuntimeDatabasePath=self.case + '/capture.dmp'))
        self.assertIn('configured', self.item()['reason'])
        self.assertEqual(self.apply()['removedFiles'], 0)

    def test_archive_deduplicates_keeps_reports_and_restores_all_bytes(self):
        original = {path.relative_to(self.root).as_posix(): path.read_bytes() for path in (self.root / self.case).iterdir()}
        self.status()
        result = self.apply()
        self.assertEqual(result['removedFiles'], 2)
        self.assertTrue((self.root / self.case / 'REPORT.md').exists())
        manifest, _ = retention.load_archive(self.root, self.rules, self.case)
        self.assertEqual(manifest['deduplicatedBytes'], len(self.payload))
        with zipfile.ZipFile(self.root / manifest['archive']['path']) as archive:
            self.assertEqual(len(archive.namelist()), 3)
        self.assertEqual(self.item()['action'], 'archived')
        self.assertEqual(self.apply()['removedFiles'], 0)
        restored = retention.restore(self.root, self.rules, self.case)
        self.assertEqual(restored['files'], 2)
        for path, data in original.items():
            self.assertEqual((self.root / path).read_bytes(), data)
        self.assertEqual(self.item()['action'], 'keep')
        # Re-closing a restored case reuses its archive despite new file IDs.
        self.status()
        self.assertEqual(self.apply()['removedFiles'], 2)

    def test_changed_source_blocks_every_deletion(self):
        self.status()
        retention.prepare_archive(self.root, self.rules, self.case)
        self.write(self.case + '/same-copy.bin', b'new investigation')
        with self.assertRaisesRegex(ValueError, 'changed after archival'):
            self.apply()
        self.assertTrue((self.root / self.case / 'capture.dmp').exists())

    def test_new_source_file_blocks_every_deletion(self):
        self.status()
        retention.prepare_archive(self.root, self.rules, self.case)
        self.write(self.case + '/new.log', b'new evidence')
        with self.assertRaisesRegex(ValueError, 'New files'):
            self.apply()
        self.assertTrue((self.root / self.case / 'capture.dmp').exists())

    def test_corrupt_archive_blocks_delete_and_restore(self):
        self.status()
        manifest = retention.prepare_archive(self.root, self.rules, self.case)
        (self.root / manifest['archive']['path']).write_bytes(b'broken')
        for operation in (self.apply, lambda: retention.restore(self.root, self.rules, self.case)):
            with self.assertRaises(ValueError):
                operation()
        self.assertTrue((self.root / self.case / 'capture.dmp').exists())

    def test_changed_manifest_is_rejected(self):
        self.status()
        retention.prepare_archive(self.root, self.rules, self.case)
        _, path = retention.load_archive(self.root, self.rules, self.case)
        with path.open('ab') as stream:
            stream.write(b'\n')
        with self.assertRaisesRegex(ValueError, 'manifest changed'):
            self.apply()

    def test_restore_never_overwrites_new_work(self):
        self.status()
        self.apply()
        self.write(self.case + '/capture.dmp', b'new dump')
        with self.assertRaisesRegex(ValueError, 'overwrite changed file'):
            retention.restore(self.root, self.rules, self.case)
        self.assertEqual((self.root / self.case / 'capture.dmp').read_bytes(), b'new dump')
        self.assertFalse((self.root / self.case / 'same-copy.bin').exists())

    def test_interrupted_prune_resumes_existing_verified_archive(self):
        self.status()
        original_delete = retention.LockedFile.delete
        deleted = []
        def interrupt(source):
            if deleted:
                raise OSError('injected interruption')
            deleted.append(source.path)
            original_delete(source)
        with patch.object(retention.LockedFile, 'delete', interrupt), self.assertRaises(OSError):
            self.apply()
        self.assertEqual(len(list((self.root / self.rules['archiveRoot']).glob('*/case.zip'))), 1)
        self.assertEqual(self.apply()['removedFiles'], 1)
        retention.restore(self.root, self.rules, self.case)
        self.assertEqual((self.root / self.case / 'capture.dmp').read_bytes(), self.payload)

    def test_archive_reused_after_pointer_write_failure(self):
        self.status()
        with patch.object(retention, 'publish_pointer', side_effect=OSError('injected permission failure')):
            with self.assertRaises(OSError):
                self.apply()
        self.assertTrue((self.root / self.case / 'capture.dmp').exists())
        self.assertEqual(self.apply()['removedFiles'], 2)
        self.assertEqual(len(list((self.root / self.rules['archiveRoot']).glob('*/case.zip'))), 1)

    def test_status_change_while_archiving_preserves_originals(self):
        self.status()
        prepare = retention.prepare_archive
        def change_status(*args):
            result = prepare(*args)
            self.status('active')
            return result
        with patch.object(retention, 'prepare_archive', change_status):
            with self.assertRaisesRegex(ValueError, 'decisions changed'):
                self.apply()
        self.assertTrue((self.root / self.case / 'capture.dmp').exists())
        self.assertEqual(self.item()['action'], 'keep')

    def test_junction_in_case_is_rejected_without_following_it(self):
        target = self.root / 'outside-case'
        target.mkdir()
        (target / 'keep.txt').write_bytes(b'must remain')
        link = self.root / self.case / 'linked'
        script = self.write('junction.ps1', b'param($Link,$Target)\nNew-Item -ItemType Junction -Path $Link -Target $Target | Out-Null\n')
        subprocess.run([os.environ.get('CLIPBOARD_TEST_PWSH', 'pwsh'), '-NoProfile', '-File',
                        str(script), str(link), str(target)], check=True, capture_output=True)
        self.addCleanup(os.rmdir, link)  # Remove only this junction before fixture teardown.
        self.status()
        with self.assertRaisesRegex(ValueError, 'Reparse'):
            self.apply()
        self.assertEqual((target / 'keep.txt').read_bytes(), b'must remain')

    def test_no_savings_keeps_originals(self):
        for path in (self.root / self.case).iterdir():
            path.unlink()
        self.write(self.case + '/small.bin', b'123')
        self.status()
        self.assertEqual(self.apply()['removedFiles'], 0)
        self.assertTrue((self.root / self.case / 'small.bin').exists())
        self.assertIn('not save space', self.item()['reason'])

    def test_unsafe_paths_and_hardlinks_are_protected(self):
        for path in ('outputs/../src', 'outputs/a:stream', 'outputs', 'C:/outside', 'outputs/NUL'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                retention.case_path(self.root, path)
        os.link(self.root / self.case / 'capture.dmp', self.root / 'outside-case.dmp')
        self.status()
        self.assertIn('Hardlinked', self.item()['reason'])
        self.assertEqual(self.apply()['removedFiles'], 0)

    def test_collections_split_cases_and_keep_loose_files(self):
        self.write('outputs/import-diagnostics/active-one/raw.log', b'keep')
        self.write('outputs/import-diagnostics/index.json', dict(active='active-one'))
        report = retention.report(self.root, self.rules, retention.states(self.root, self.rules))
        self.assertIn('outputs/import-diagnostics/active-one', [row['case'] for row in report['cases']])
        self.assertEqual(report['retainedLooseFiles'][0]['path'], 'outputs/import-diagnostics/index.json')


if __name__ == '__main__':
    unittest.main(verbosity=2)
