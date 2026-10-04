# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('retention', ROOT/'tools/build/PreserveBuildDiagnostics.py')
retention = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retention)


class RetentionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dll = self.put('build/native/clipboard.dll', b'dll')
        self.pdb = self.put('build/native/clipboard.pdb', b'symbols')
        native = dict(internalBuild=128, result='success',
                      dll=dict(path=str(self.dll), sha256=retention.sha(self.dll)),
                      pdb=dict(path=str(self.pdb), sha256=retention.sha(self.pdb)))
        self.native = self.put('build/metadata/native/native-build.json', json.dumps(native).encode())
        outputs = []
        for i in range(16):
            path = self.put(f'build/papyrus/current/Scripts/Script{i}.pex', b'compiled')
            outputs.append(dict(file=path.name, sha256=retention.sha(path)))
        self.papyrus = self.put('build/papyrus/current/papyrus-build.json', json.dumps(dict(internalBuild=128, result='success', outputs=outputs)).encode())
        record = dict(internalBuild=128, result='success',
                      nativeBuild=dict(metadata=str(self.native), metadataSha256=retention.sha(self.native), dllSha256=retention.sha(self.dll)),
                      papyrusBuild=dict(metadata=str(self.papyrus), metadataSha256=retention.sha(self.papyrus)))
        self.record = self.put('dist/build-128/package.build.json', json.dumps(record).encode())

    def put(self, relative, content):
        path = self.root/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def test_survives_build_removal(self):
        result = retention.preserve(self.root, self.record)
        self.assertEqual(result, retention.preserve(self.root, self.record))
        # Temporary test tree only, never the real project build folder.
        import shutil
        shutil.rmtree(self.root/'build')
        manifest = Path(result['manifest'])
        for entry in retention.read(manifest)['files']:
            self.assertEqual(retention.sha(manifest.parent/entry['path']), entry['sha256'])

    def test_changed_pdb_refused(self):
        self.pdb.write_bytes(b'different symbols')
        with self.assertRaisesRegex(ValueError, 'pdb hash'):
            retention.preserve(self.root, self.record)

    def test_changed_metadata_refused(self):
        self.native.write_bytes(b'{}')
        with self.assertRaisesRegex(ValueError, 'metadata hash'):
            retention.preserve(self.root, self.record)

    def test_changed_script_refused(self):
        (self.root/'build/papyrus/current/Scripts/Script0.pex').write_bytes(b'other build')
        with self.assertRaisesRegex(ValueError, 'Papyrus output hash'):
            retention.preserve(self.root, self.record)

    def test_existing_snapshot_corruption_refused(self):
        saved = Path(retention.preserve(self.root, self.record)['manifest']).parent
        (saved/'build/native/clipboard.pdb').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'failed verification'):
            retention.preserve(self.root, self.record)

    def test_outside_workspace_refused(self):
        with self.assertRaises(ValueError):
            retention.safe(self.root, self.root/'../elsewhere')


if __name__ == '__main__':
    unittest.main()
