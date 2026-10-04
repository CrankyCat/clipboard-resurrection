# SPDX-License-Identifier: GPL-3.0-or-later
"""Destructive checks are confined to newly created disposable checkout fixtures."""
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
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/maintenance'))
from maintenance_paths import (LockedFile, as_relative, contained, digest, no_reparse, read_json, require, snapshot, workspace)
from cleanup_repository import apply_plan, create_plan
from verified_publication import verified_publication

PWSH = os.environ.get('CLIPBOARD_TEST_PWSH', 'pwsh')
BASE = ROOT / 'build/validation/cleanup-tests'
BASE.mkdir(parents=True, exist_ok=True)


class Fixture(unittest.TestCase):
    def setUp(self):
        self.root = workspace(tempfile.mkdtemp(prefix='case-', dir=BASE))
        self.audit = '.local/cleanup-audit/fixture'
        self.runtime = ['outputs/one/offline-root/Data/F4SE/Plugins/f4rd-runtime.bin',
                        'outputs/two/offline-root/Data/F4SE/Plugins/f4rd-runtime.bin']
        self.objects = ['outputs/pre-relocation-build/build/a.obj', 'outputs/pre-relocation-build/build/b.pch']
        self.payload = b'D' * (1024 * 1024 + 1)
        for path in self.runtime:
            self.write(path, self.payload)
        for path in self.objects:
            self.write(path, b'object')
        self.write('config/Clipboard.InternalBuild.json', {'internalBuild': 128})
        self.write('config/Clipboard.PublishedBuild.json', {'internalBuild': 128})
        self.write('vcpkg.json', {'name': 'clipboard', 'version-string': '3.0.0'})
        self.write(self.audit + '/runtime-database-duplicates.json', [dict(
            sha256=digest(self.payload), bytesPerFile=len(self.payload), copies=2, paths=self.runtime),
            dict(sha256=digest(b'fixture'), bytesPerFile=7, copies=4, paths=['stubs'])])
        self.write(self.audit + '/pre-relocation-build-review.json', dict(
            narrowIntermediateCandidates=[dict(path=p) for p in self.objects]))
        self.write(self.audit + '/preserved-archives.json', [])
        self.write(self.audit + '/preserved-records.json', [dict(
            path='config/Clipboard.InternalBuild.json',
            sha256=snapshot(self.root, 'config/Clipboard.InternalBuild.json')['sha256'])])
        self.plan_path = '.local/cleanup-plans/review.json'
        self.links = []

    def tearDown(self):
        for path in self.links:
            if os.path.lexists(path):
                os.rmdir(path)
        # Resolve and check the precise disposable directory before recursive cleanup.
        target = self.root.resolve()
        require(target.parent == BASE.resolve() and target.name.startswith('case-'), 'Unsafe fixture teardown')
        no_reparse(target)
        shutil.rmtree(target)

    def write(self, path, value):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
        return target

    def plan(self):
        result = create_plan(self.root, self.audit, self.plan_path)
        self.assertEqual(result['selectedFiles'], 0)
        return read_json(self.root / self.plan_path)

    def select(self, plan, paths):
        for item in plan['candidates']:
            item['selected'] = item['path'] in paths
            if item['selected']:
                item['review'] = 'Disposable test fixture; no live consumer or historical evidence.'
        self.write(self.plan_path, plan)
        return digest((self.root / self.plan_path).read_bytes())

    def apply(self, plan, paths):
        return apply_plan(self.root, self.plan_path, self.select(plan, paths))

    def rejected(self, plan, paths):
        with self.assertRaises((ValueError, OSError)):
            self.apply(plan, paths)
        for path in paths:
            self.assertTrue((self.root / path).exists())

    def publication(self, number=128):
        base = f'dist/build-{number}'
        stage = base + '/Clipboard Resurrection - OG NG AE'
        payloads = {'F4SE/Plugins/clipboard.dll': b'dll', 'Scripts/Clipboard.pex': b'pex'}
        for name, data in payloads.items():
            self.write(stage + '/' + name, data)
        archive = self.root / base / 'package.zip'
        with zipfile.ZipFile(archive, 'w') as zipped:
            for name, data in payloads.items():
                zipped.writestr(name, data)
        manifest = self.write(base + '/package.manifest.sha256', ''.join(
            f'{digest(data)} *{name}\n' for name, data in payloads.items()).encode())
        record = dict(result='success', internalBuild=number, productVersion='3.0.0',
                      fileCount=2, stage=str(self.root / stage), archive=str(archive), manifest=str(manifest),
                      archiveSha256=digest(archive.read_bytes()), manifestSha256=digest(manifest.read_bytes()),
                      archiveVerification=dict(result='passed', fileCount=2, archiveSha256=digest(archive.read_bytes())))
        self.write(base + '/package.build.json', record)
        return record

    def junction(self, link, target):
        script = self.write('junction.ps1', b'param($Link,$Target)\nNew-Item -ItemType Junction -Path $Link -Target $Target | Out-Null\n')
        subprocess.run([PWSH, '-NoProfile', '-File', str(script), str(link), str(target)], check=True, capture_output=True)
        self.links.append(link)


class CleanupTests(Fixture):
    def test_default_plan_is_non_destructive_and_excludes_stubs(self):
        plan = self.plan()
        self.assertEqual(len(plan['candidates']), 4)
        self.assertFalse(any(i['selected'] for i in plan['candidates']))
        self.assertEqual(plan['totals']['runtimeRedundantCopies'], 1)
        for path in self.runtime + self.objects:
            self.assertTrue((self.root / path).exists())
        with self.assertRaises(FileExistsError):
            create_plan(self.root, self.audit, self.plan_path)

    def test_requires_selection_digest_and_review(self):
        plan = self.plan()
        with self.assertRaises(ValueError):
            apply_plan(self.root, self.plan_path, '0' * 64)
        self.rejected(plan, [])
        sha = self.select(plan, self.objects)
        edited = read_json(self.root / self.plan_path)
        edited['candidates'][0]['reason'] += ' changed'
        self.write(self.plan_path, edited)
        with self.assertRaises(ValueError):
            apply_plan(self.root, self.plan_path, sha)
        for item in edited['candidates']:
            item['review'] = ''
        self.write(self.plan_path, edited)
        with self.assertRaises(ValueError):
            apply_plan(self.root, self.plan_path, digest((self.root / self.plan_path).read_bytes()))

    def test_intermediate_apply_preserves_every_other_file(self):
        self.write('outputs/pre-relocation-build/build/clipboard.pdb', b'unique symbols')
        self.write('outputs/pre-relocation-build/build/clipboard.dll', b'unique binary')
        result = self.apply(self.plan(), self.objects)
        self.assertEqual(result['removedFiles'], 2)
        self.assertTrue(all(not (self.root / p).exists() for p in self.objects))
        self.assertTrue((self.root / self.runtime[0]).exists())
        self.assertTrue((self.root / 'outputs/pre-relocation-build/build/clipboard.pdb').exists())
        events = [json.loads(s)['event'] for s in Path(result['journal']).read_text().splitlines()]
        self.assertEqual(events, ['preflight-passed', 'delete-intent', 'deleted', 'delete-intent', 'deleted', 'complete'])

    def test_runtime_retained_with_full_original_index_before_removal(self):
        plan = self.plan()
        result = self.apply(plan, self.runtime)
        self.assertEqual(result['removedFiles'], 2)
        self.assertEqual((self.root / plan['runtimeRetention']['blob']).read_bytes(), self.payload)
        index = read_json(next((self.root / '.local/cleanup-retained/indexes').glob('*.json')))
        self.assertEqual({i['path'] for i in index['originalFiles']}, set(self.runtime))
        self.assertTrue(all(not (self.root / p).exists() for p in self.runtime))
        self.assertTrue(all((self.root / p).exists() for p in self.objects))

    def test_completed_audit_reports_clean_without_writing_another_plan(self):
        self.apply(self.plan(), self.runtime + self.objects)
        # Source/config changes after cleanup do not invalidate historical
        # completion; current consumer guards remain mandatory only for removal.
        self.write('tools/new-consumer.ps1', b'new code after cleanup')
        self.write('config/Clipboard.InternalBuild.json', {'internalBuild': 129})
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        for _ in range(2):
            result = create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')
            self.assertEqual(result['result'], 'already-clean')
            self.assertEqual(result['previouslyRemovedFiles'], 4)
            self.assertEqual(result['removedFiles'], 0)
            self.assertEqual(result['remainingCandidates'], 0)
        after = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_missing_files_without_completion_are_not_clean(self):
        self.plan()
        for path in self.runtime + self.objects:
            contained(self.root, path).unlink()
        with self.assertRaisesRegex(ValueError, 'without a verified completed cleanup'):
            create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')

    def test_partial_cleanup_needs_fresh_audit(self):
        self.apply(self.plan(), self.runtime)
        with self.assertRaisesRegex(ValueError, 'stale or partly applied'):
            create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')
        self.assertTrue(all((self.root / path).exists() for path in self.objects))

    def test_restored_candidate_is_not_silently_cleaned(self):
        self.apply(self.plan(), self.runtime + self.objects)
        self.write(self.objects[0], b'restored evidence')
        with self.assertRaisesRegex(ValueError, 'stale or partly applied'):
            create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')
        self.assertEqual((self.root / self.objects[0]).read_bytes(), b'restored evidence')

    def test_incomplete_and_incorrect_journals_are_not_clean(self):
        result = self.apply(self.plan(), self.runtime + self.objects)
        journal = Path(result['journal'])
        events = [json.loads(line) for line in journal.read_text().splitlines()]
        cases = [events[:-1], events[:1] + events[3:], copy.deepcopy(events)]
        cases[-1][1]['sha256'] = '0' * 64
        for rows in cases:
            with self.subTest(events=len(rows)):
                journal.write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8')
                with self.assertRaises(ValueError):
                    create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')

    def test_changed_reviewed_plan_is_not_clean(self):
        self.apply(self.plan(), self.runtime + self.objects)
        with (self.root / self.plan_path).open('ab') as stream:
            stream.write(b'\n')
        with self.assertRaisesRegex(ValueError, 'Exact reviewed plan'):
            create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')

    def test_corrupt_completed_retention_is_not_clean(self):
        plan = self.plan()
        self.apply(plan, self.runtime + self.objects)
        self.write(plan['runtimeRetention']['blob'], b'broken retention')
        with self.assertRaisesRegex(ValueError, 'Retained runtime database'):
            create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')

    def test_changed_completed_index_is_not_clean(self):
        self.apply(self.plan(), self.runtime + self.objects)
        index_path = next((self.root / '.local/cleanup-retained/indexes').glob('*.json'))
        index = read_json(index_path)
        index['selectedOriginalPaths'].pop()
        self.write(index_path.relative_to(self.root).as_posix(), index)
        with self.assertRaisesRegex(ValueError, 'Retained runtime index'):
            create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')

    def test_changed_completed_audit_is_not_clean(self):
        self.apply(self.plan(), self.runtime + self.objects)
        path = self.audit + '/pre-relocation-build-review.json'
        audit = read_json(self.root / path)
        audit['changedAfterApply'] = True
        self.write(path, audit)
        with self.assertRaisesRegex(ValueError, 'Completed audit changed'):
            create_plan(self.root, self.audit, '.local/cleanup-plans/second.json')

    def test_corrupt_retained_copy_blocks_all_deletions(self):
        plan = self.plan()
        self.write(plan['runtimeRetention']['blob'], b'broken')
        self.rejected(plan, self.runtime + self.objects)

    def test_changed_hash_even_same_length_and_mtime_aborts_whole_batch(self):
        plan = self.plan()
        path = self.root / self.objects[-1]
        info = path.stat()
        path.write_bytes(b'edited')
        os.utime(path, ns=(info.st_atime_ns, info.st_mtime_ns))
        self.rejected(plan, self.objects)

    def test_missing_later_candidate_preserves_earlier_candidate(self):
        plan = self.plan()
        path = contained(self.root, self.objects[-1])
        path.unlink()
        with self.assertRaises(OSError):
            self.apply(plan, self.objects)
        self.assertTrue((self.root / self.objects[0]).exists())

    def test_modified_guard_or_new_consumer_aborts(self):
        plan = self.plan()
        self.write('tools/new-consumer.ps1', b'new input consumer')
        self.rejected(plan, self.objects)

    def test_preserved_counter_change_aborts(self):
        plan = self.plan()
        self.write('config/Clipboard.InternalBuild.json', {'internalBuild': 129})
        self.rejected(plan, self.objects)

    def test_changed_workspace_rejected(self):
        plan = self.plan()
        plan['workspace'] += '-other'
        self.rejected(plan, self.objects)

    def test_active_configured_input_blocks_plan(self):
        self.write('CMakeUserPresets.json', dict(cacheVariables=dict(DB=str(self.root / self.runtime[0]))))
        with self.assertRaisesRegex(ValueError, 'configured input'):
            self.plan()

    def test_environment_change_blocks_apply(self):
        plan = self.plan()
        old = os.environ.get('CLIPBOARD_GAME_ROOT')
        try:
            os.environ['CLIPBOARD_GAME_ROOT'] = str(self.root / 'outputs')
            self.rejected(plan, self.objects)
        finally:
            if old is None:
                os.environ.pop('CLIPBOARD_GAME_ROOT', None)
            else:
                os.environ['CLIPBOARD_GAME_ROOT'] = old

    def test_persistent_local_build_input_is_protected(self):
        self.write('.local/build-paths.json', dict(schemaVersion=1, RuntimeDatabasePath=self.runtime[0]))
        with self.assertRaisesRegex(ValueError, 'configured input'):
            self.plan()

    def test_changed_persistent_local_build_configuration_blocks_apply(self):
        plan = self.plan()
        self.write('.local/build-paths.json', dict(schemaVersion=1, GameRoot='.local/toolchains/fallout4-papyrus'))
        self.rejected(plan, self.objects)

    def test_all_protected_categories_rejected_even_with_matching_hash(self):
        protected = ['dist/build-128/a.obj', 'Deployments/a.obj', 'build/papyrus/game-fixture-20260911/a.obj',
                     'build/native/a.obj', 'src/a.obj', 'assets/a.obj', 'localization/a.obj', 'config/a.obj',
                     'docs/a.obj', 'tests/a.obj', 'external/a.obj', 'Original Project Files/a.obj',
                     'legacy/a.obj', '.local/codex/docs/a.obj', '.local/rollback/a.obj',
                     'outputs/crash/a.dmp', 'outputs/case/a.pdb', 'outputs/case/a.fos',
                     'outputs/localization-implementation/xedit/Data/Fallout4.esm']
        for path in protected:
            self.write(path, b'protected')
        plan = self.plan()
        for path in protected:
            with self.subTest(path=path):
                edited = copy.deepcopy(plan)
                item = dict(snapshot(self.root, path), action='remove-intermediate', selected=False, review='')
                edited['candidates'].append(item)
                self.rejected(edited, [path])

    def test_unaudited_intermediate_duplicate_and_incomplete_plan_rejected(self):
        self.write('outputs/pre-relocation-build/build/unreviewed.obj', b'keep')
        plan = self.plan()
        edited = copy.deepcopy(plan)
        edited['candidates'].append(dict(snapshot(self.root, 'outputs/pre-relocation-build/build/unreviewed.obj'),
                                         action='remove-intermediate', selected=False, review=''))
        self.rejected(edited, self.objects)
        edited = copy.deepcopy(plan)
        edited['candidates'].append(copy.deepcopy(edited['candidates'][0]))
        self.rejected(edited, self.objects)
        plan['candidates'].pop()
        self.rejected(plan, self.objects)

    def test_containment_traversal_ads_and_windows_aliases(self):
        for path in ('../outside', '/absolute', 'D:/outside', 'outputs/../a.obj', 'outputs/a.obj:stream',
                     'outputs/a.obj.', 'outputs/a.obj ', 'outputs/NUL.obj', 'outputs//a.obj', 'outputs\\a.obj'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                contained(self.root, path)
        with self.assertRaises(ValueError):
            as_relative(self.root, str(self.root) + '-sibling/a.obj')

    def test_hardlinked_candidate_rejected(self):
        os.link(self.root / self.objects[0], self.root / 'alias.obj')
        with self.assertRaisesRegex(ValueError, 'Hardlinked'):
            self.plan()

    def test_open_file_blocks_entire_removal_batch(self):
        plan = self.plan()
        with LockedFile(self.root, snapshot(self.root, self.objects[-1])):
            self.rejected(plan, self.objects)

    def test_retention_reparse_point_blocks_all_removals(self):
        plan = self.plan()
        target = self.root / 'retained-elsewhere'
        target.mkdir()
        self.junction(self.root / '.local/cleanup-retained', target)
        self.rejected(plan, self.runtime + self.objects)
        self.assertEqual(list(target.iterdir()), [])

    def test_reparse_parent_rejected_after_plan(self):
        plan = self.plan()
        original = self.root / 'outputs/pre-relocation-build/build'
        renamed = self.root / 'outputs/pre-relocation-build/retained'
        original.rename(renamed)
        self.junction(original, renamed)
        with self.assertRaisesRegex(ValueError, 'Reparse'):
            self.apply(plan, self.objects)
        self.assertTrue((renamed / 'a.obj').exists())

    def test_foreign_powershell_build_lock_blocks_apply(self):
        plan = self.plan()
        script = self.write('hold-lock.ps1', b'''param($Root,$Helper,$Ready,$Release)
. $Helper
$lock = Enter-ClipboardBuildLock $Root
try { [IO.File]::WriteAllText($Ready, 'ready'); while (-not (Test-Path -LiteralPath $Release)) { Start-Sleep -Milliseconds 30 } }
finally { $lock.ReleaseMutex(); $lock.Dispose() }
''')
        ready, release = self.root / 'ready', self.root / 'release'
        process = subprocess.Popen([PWSH, '-NoProfile', '-File', str(script), str(self.root),
                                    str(ROOT / 'tools/build/ClipboardBuildIdentity.ps1'), str(ready), str(release)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 15
            while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(.03)
            self.assertTrue(ready.exists(), 'Separate PowerShell process failed to acquire build lock')
            with self.assertRaisesRegex(ValueError, 'build/publication/cleanup'):
                self.apply(plan, self.objects)
            self.assertTrue(all((self.root / p).exists() for p in self.objects))
        finally:
            release.write_text('release')
            process.communicate(timeout=10)


class CleanupCommandTests(Fixture):
    def setUp(self):
        super().setUp()
        self.apply(self.plan(), self.runtime + self.objects)
        for path in ('tools/maintenance/Clean-ClipboardRepository.ps1',
                     'tools/maintenance/Clean-ClipboardBuild.ps1',
                     'tools/maintenance/cleanup_repository.py',
                     'tools/maintenance/maintenance_paths.py',
                     'tools/maintenance/ArchiveBuildTree.py',
                     'tools/maintenance/outputs_retention.py',
                     'config/outputs-retention.json',
                     'tools/build/ClipboardBuildIdentity.ps1',
                     'tools/build/Get-ClipboardPython.ps1'):
            self.write(path, (ROOT / path).read_bytes())
        python = sys.executable.replace("'", "''")
        self.write('Build.ps1', (f"param([switch] $CheckOnly)\n"
                                f"[pscustomobject]@{{result='passed';PythonPath='{python}'}}\n").encode())
        self.write('build/logs/keep.log', b'unpublished diagnostic evidence')

    def command(self, *args):
        return subprocess.run([PWSH, '-NoProfile', '-File',
                               str(self.root / 'tools/maintenance/Clean-ClipboardRepository.ps1'),
                               '-AuditPath', self.audit, '-PythonPath', sys.executable, *args],
                              capture_output=True, text=True)

    def test_report_does_not_clean_build_or_require_manual_file_moves(self):
        run = self.command()
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads(run.stdout)
        self.assertEqual(report['result'], 'dry-run')
        self.assertEqual(report['historical']['result'], 'already-clean')
        self.assertEqual(report['build']['result'], 'dry-run')
        self.assertTrue((self.root / 'build/logs/keep.log').exists())
        self.assertFalse((self.root / '.local/build-history').exists())

    def test_apply_automatically_archives_build_and_is_repeatable(self):
        run = self.command('-Apply')
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['result'], 'cleaned')
        self.assertFalse((self.root / 'build').exists())
        archive = next((self.root / '.local/build-history').glob('*/build.zip'))
        with zipfile.ZipFile(archive) as saved:
            self.assertEqual(saved.read('build/logs/keep.log'), b'unpublished diagnostic evidence')
        run = self.command('-Apply')
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['result'], 'already-clean')
        self.assertEqual(len(list((self.root / '.local/build-history').glob('*/build.zip'))), 1)
        self.assertEqual(read_json(self.root / 'config/Clipboard.InternalBuild.json')['internalBuild'], 128)

    def test_explicit_historical_plan_still_requires_its_reviewed_hash(self):
        for args in (('-PlanPath', self.plan_path), ('-ReviewedPlanSha256', '')):
            with self.subTest(args=args):
                run = self.command('-Apply', *args)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn('ReviewedPlanSha256', run.stderr)
                self.assertTrue((self.root / 'build/logs/keep.log').exists())

    def test_output_case_commands_and_routine_apply_restore(self):
        case = 'outputs/finished-case'
        payload = b'original diagnostic contents\n' * 8192
        self.write(case + '/capture.bin', payload)
        self.write(case + '/REPORT.md', b'Preserved concise report')
        run = self.command('-CompleteOutputCase', case)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['status'], 'completed')
        for path in (self.root / case).iterdir():
            os.utime(path, (1_600_000_000, 1_600_000_000))
        run = self.command('-Apply')
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['outputs']['removedFiles'], 1)
        self.assertFalse((self.root / case / 'capture.bin').exists())
        run = self.command('-RestoreOutputCase', case)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['status'], 'active')
        self.assertEqual((self.root / case / 'capture.bin').read_bytes(), payload)
        run = self.command('-KeepOutputCase', case)
        self.assertEqual(run.returncode, 0, run.stderr)
        run = self.command()
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['result'], 'retained')
        self.assertEqual(json.loads(run.stdout)['outputs']['eligibleCases'], 0)

    def test_case_actions_cannot_mix_with_cleanup_or_each_other(self):
        for args in (('-Apply', '-CompleteOutputCase', 'outputs/case'),
                     ('-KeepOutputCase', 'outputs/case', '-RestoreOutputCase', 'outputs/case')):
            with self.subTest(args=args):
                run = self.command(*args)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn('one nonempty output-case action', run.stderr)
                self.assertTrue((self.root / 'build/logs/keep.log').exists())


class PublicationTests(Fixture):
    def test_latest_and_explicit_number_are_verified(self):
        self.publication(127)
        self.publication(128)
        self.assertEqual(verified_publication(self.root, '3.0.0')['internalBuild'], 128)
        self.assertEqual(verified_publication(self.root, '3.0.0', 127)['internalBuild'], 127)
        with self.assertRaises(ValueError):
            verified_publication(self.root, '3.0.0', 129)

    def test_bad_latest_does_not_fall_back(self):
        self.publication(127)
        self.publication(128)
        self.write('dist/build-128/package.build.json', {'result': 'failed'})
        with self.assertRaises((ValueError, KeyError)):
            verified_publication(self.root, '3.0.0')

    def test_ambiguous_record_rejected(self):
        record = self.publication()
        self.write('dist/build-128/extra.build.json', record)
        with self.assertRaises(ValueError):
            verified_publication(self.root, '3.0.0')

    def test_version_and_build_identity_rejected(self):
        record = self.publication()
        with self.assertRaises(ValueError):
            verified_publication(self.root, '3.1.0')
        record['internalBuild'] = 127
        self.write('dist/build-128/package.build.json', record)
        with self.assertRaises(ValueError):
            verified_publication(self.root, '3.0.0')

    def test_archive_or_manifest_hash_change_rejected(self):
        record = self.publication()
        Path(record['archive']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'archive hash'):
            verified_publication(self.root, '3.0.0')
        record = self.publication()
        Path(record['manifest']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'manifest hash'):
            verified_publication(self.root, '3.0.0')

    def test_missing_changed_or_extra_stage_files_rejected(self):
        record = self.publication()
        stage = Path(record['stage'])
        (stage / 'Scripts/Clipboard.pex').write_bytes(b'changed')
        with self.assertRaises(ValueError):
            verified_publication(self.root, '3.0.0')
        record = self.publication()
        (stage / 'Scripts/Clipboard.pex').unlink()
        with self.assertRaises(ValueError):
            verified_publication(self.root, '3.0.0')
        self.publication()
        (stage / 'extra.txt').write_bytes(b'extra')
        with self.assertRaises(ValueError):
            verified_publication(self.root, '3.0.0')

    def test_archive_manifest_disagreement_rejected(self):
        record = self.publication()
        with zipfile.ZipFile(record['archive'], 'w') as zipped:
            zipped.writestr('bad.txt', b'bad')
        sha = digest(Path(record['archive']).read_bytes())
        record['archiveSha256'] = sha
        record['archiveVerification']['archiveSha256'] = sha
        self.write('dist/build-128/package.build.json', record)
        with self.assertRaisesRegex(ValueError, 'Archive paths'):
            verified_publication(self.root, '3.0.0')

    def test_stage_escape_rejected(self):
        record = self.publication()
        record['stage'] = str(self.root / 'dist/build-128-other/stage')
        self.write('dist/build-128/package.build.json', record)
        with self.assertRaisesRegex(ValueError, 'stage path'):
            verified_publication(self.root, '3.0.0')

    def test_publication_reparse_point_rejected(self):
        record = self.publication()
        stage = Path(record['stage'])
        target = stage.with_name('moved-stage')
        stage.rename(target)
        self.junction(stage, target)
        with self.assertRaisesRegex(ValueError, 'Reparse'):
            verified_publication(self.root, '3.0.0')

    def test_manifest_traversal_rejected_even_with_updated_hash(self):
        record = self.publication()
        manifest = Path(record['manifest'])
        manifest.write_text('0' * 64 + ' *../outside\n')
        record['manifestSha256'] = digest(manifest.read_bytes())
        self.write('dist/build-128/package.build.json', record)
        with self.assertRaises(ValueError):
            verified_publication(self.root, '3.0.0')


class CaptureTests(Fixture):
    def install_capture(self):
        record = self.publication()
        for relative in ('tools/diagnostics/Capture-ClipboardImportDiagnostic.ps1',
                         'tools/build/ClipboardBuildPaths.ps1', 'tools/build/Get-ClipboardPublication.ps1',
                         'tools/build/Get-ClipboardVersion.ps1', 'tools/build/Get-ClipboardPython.ps1',
                         'tools/maintenance/maintenance_paths.py', 'tools/maintenance/verified_publication.py'):
            self.write(relative, (ROOT / relative).read_bytes())
        self.write('src/papyrus/Clipboard.psc', b'Scriptname Clipboard')
        self.write('game/F4SE/Clipboard.log', b'test log')
        return record

    def run_capture(self, record, *extra):
        return subprocess.run([PWSH, '-NoProfile', '-File', str(self.root / 'tools/diagnostics/Capture-ClipboardImportDiagnostic.ps1'),
            '-RunName', 'fixture', '-Phase', 'Before', '-InstalledMod', record['stage'],
            '-GameUserRoot', str(self.root / 'game'), '-PythonPath', sys.executable, *extra],
            capture_output=True, text=True, timeout=30)

    def test_capture_preflight_leaves_no_output_for_bad_explicit_input(self):
        record = self.install_capture()
        result = self.run_capture(record, '-PatternPath', str(self.root / 'missing.ini'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('PatternPath', result.stderr)
        self.assertFalse((self.root / 'outputs/import-diagnostics').exists())

    def test_capture_preflight_leaves_no_output_for_bad_publication(self):
        record = self.install_capture()
        Path(record['archive']).write_bytes(b'bad')
        result = self.run_capture(record)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('archive hash mismatch', result.stderr)
        self.assertFalse((self.root / 'outputs/import-diagnostics').exists())

    def test_capture_records_verified_build_and_identities(self):
        record = self.install_capture()
        result = self.run_capture(record, '-InternalBuild', '128')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        capture = read_json(next((self.root / 'outputs/import-diagnostics').glob('*/capture.json')))
        self.assertEqual(capture['publication']['internalBuild'], 128)
        self.assertTrue(all(i['matchesStage'] for i in capture['identities']))
        self.assertTrue(any(f['status'] == 'missing' for f in capture['files']))


if __name__ == '__main__':
    unittest.main(verbosity=2)
