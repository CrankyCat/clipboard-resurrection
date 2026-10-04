"""Package assembly failure tests using disposable inputs and independently made ZIPs."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import warnings
from zipfile import ZipFile

spec = importlib.util.spec_from_file_location('package_plan', Path(__file__).resolve().parents[2] / 'tools/build/PackagePlan.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'assets').mkdir()
        (self.root / 'assets/a.txt').write_bytes(b'authored')
        (self.root / 'notices/dependency').mkdir(parents=True)
        (self.root / 'notices/dependency/copyright').write_bytes(b'license')
        self.recipe = dict(schema=1, trees=[dict(source='assets', destination='Data', files=['a.txt'])], files=[],
                           dependencyNotices=dict(source='notices', destination='Licenses', packages=['dependency']),
                           patternSlots=dict(destination='Slots', first=1, last=2, filename='PLACEHOLDER'))
        self.recipe_path = self.root / 'package.json'

    def plan(self):
        self.recipe_path.write_text(json.dumps(self.recipe), encoding='utf-8')
        return package.plan(self.root, self.recipe_path)

    def test_sources_and_empty_slots(self):
        records = {row['relativePath']: row for row in self.plan()['files']}
        self.assertEqual(set(records), {'Data/a.txt', 'Licenses/vcpkg-dependency.txt', 'Slots/1/PLACEHOLDER', 'Slots/2/PLACEHOLDER'})
        self.assertIsNone(records['Slots/1/PLACEHOLDER']['source'])
        self.assertEqual(records['Slots/1/PLACEHOLDER']['sha256'], 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855')

    def test_unlisted_asset(self):
        (self.root / 'assets/unlisted.txt').write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError, 'inventory'):
            self.plan()

    def test_missing_license(self):
        (self.root / 'notices/dependency/copyright').unlink()
        with self.assertRaisesRegex(ValueError, 'license inventory'):
            self.plan()

    def test_collisions_including_windows_case_and_directory(self):
        for target in ('Data/a.txt', 'data/A.TXT', 'Data/a.txt/child', 'Data'):
            with self.subTest(target=target):
                self.recipe['files'] = [dict(source='assets/a.txt', destination=target)]
                with self.assertRaisesRegex(ValueError, 'collision'):
                    self.plan()

    def test_unsafe_paths(self):
        for target in ('../escape', 'C:/escape', '/escape', 'Data/../escape', 'Data\\escape', 'Data/x:stream', 'NUL.txt', 'Data/x.'):
            with self.subTest(target=target):
                self.recipe['files'] = [dict(source='assets/a.txt', destination=target)]
                with self.assertRaises(ValueError):
                    self.plan()

    def test_player_pattern_rejected(self):
        self.recipe['files'] = [dict(source='assets/a.txt', destination='Slots/1/Pattern.INI')]
        with self.assertRaisesRegex(ValueError, 'patterns'):
            self.plan()

    def test_archive_exactness(self):
        records = self.plan()['files']
        payload = {'Data/a.txt': b'authored', 'Licenses/vcpkg-dependency.txt': b'license',
                   'Slots/1/PLACEHOLDER': b'', 'Slots/2/PLACEHOLDER': b''}
        archive = self.root / 'package.zip'
        for corruption in ('none', 'changed', 'missing', 'extra', 'case', 'duplicate'):
            with self.subTest(corruption=corruption):
                changed = dict(payload)
                if corruption == 'changed':
                    changed['Data/a.txt'] = b'tampered'
                elif corruption == 'missing':
                    del changed['Slots/2/PLACEHOLDER']
                elif corruption == 'extra':
                    changed['extra'] = b'extra'
                elif corruption == 'case':
                    changed['data/a.txt'] = changed.pop('Data/a.txt')
                with ZipFile(archive, 'w') as zipped:
                    for name, data in changed.items():
                        zipped.writestr(name, data)
                    if corruption == 'duplicate':
                        with warnings.catch_warnings():
                            warnings.simplefilter('ignore')
                            zipped.writestr('Data/a.txt', b'authored')
                if corruption == 'none':
                    self.assertEqual(package.verify_archive(archive, records)['fileCount'], 4)
                else:
                    with self.assertRaises(ValueError):
                        package.verify_archive(archive, records)


if __name__ == '__main__':
    unittest.main()
