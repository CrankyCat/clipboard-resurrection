#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host fixtures for readback language selection; these do not execute xEdit."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('readback', Path(__file__).with_name('Test-XEditLocalizationReadback.py'))
readback = importlib.util.module_from_spec(spec)
spec.loader.exec_module(readback)


class ReadbackLocales(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'localization').mkdir()
        self.source = {'key': '$Clipboard_Test', 'text': 'Workshop {0}', 'form_id': '01000001',
                       'record_type': 'MESG', 'field': 'DESC', 'occurrence': 0,
                       'table': 'DLSTRINGS', 'string_id': 1, 'surface': 'esp'}
        self.mapping = {'fields': [self.source]}
        self.identity = ('01000001', 'MESG', 'DESC', 0)
        self.patcher = patch.object(readback.esp_tool, 'ROOT', self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)
        source_patcher = patch.object(readback.esp_tool, 'validated_english_fields',
                                      return_value={self.source['key']: self.source})
        self.current_source = source_patcher.start()
        self.addCleanup(source_patcher.stop)

    def write_catalog(self, locale, text, source_hash=None):
        row = {'key': self.source['key'], 'text': text,
               'source_sha256': source_hash or readback.esp_tool.catalog.text_hash(self.source['text'])}
        (self.root / 'localization' / f'{locale}.json').write_text(
            json.dumps([row], ensure_ascii=False), encoding='utf-8')

    def test_cn_reads_traditional_catalog_without_cn_source_file(self):
        self.write_catalog('zhhant', '工房選擇 {0}')
        self.write_catalog('zhhans', '工房选择 {0}')
        traditional, canonical, path = readback.expected_nonempty_fields(self.mapping, 'cn')
        simplified, _, _ = readback.expected_nonempty_fields(self.mapping, 'zhhans')
        self.assertEqual(traditional[self.identity], '工房選擇 {0}')
        self.assertEqual(simplified[self.identity], '工房选择 {0}')
        self.assertEqual(canonical, 'zhhant')
        self.assertEqual(path.name, 'zhhant.json')

    def test_every_canonical_locale_reads_its_own_catalog(self):
        for locale in readback.LOCALES[1:]:
            self.write_catalog(locale, locale + ' {0}')
        for locale in readback.LOCALES[1:]:
            with self.subTest(locale=locale):
                expected, canonical, path = readback.expected_nonempty_fields(self.mapping, locale)
                self.assertEqual(expected[self.identity], locale + ' {0}')
                self.assertEqual(canonical, locale)
                self.assertEqual(path.name, locale + '.json')

    def test_english_uses_validated_source_and_unknown_language_fails(self):
        expected, canonical, path = readback.expected_nonempty_fields(self.mapping, 'en')
        self.assertEqual(expected[self.identity], self.source['text'])
        self.assertEqual(canonical, 'en')
        self.assertIsNone(path)
        with self.assertRaisesRegex(ValueError, 'Unsupported readback language'):
            readback.expected_nonempty_fields(self.mapping, '../ru')

    def test_current_display_override_drives_english_and_translation_source_hash(self):
        revised = {**self.source, 'text': 'Corrected workshop {0}'}
        self.current_source.return_value = {self.source['key']: revised}
        expected, _, _ = readback.expected_nonempty_fields(self.mapping, 'en')
        self.assertEqual(expected[self.identity], revised['text'])
        self.write_catalog('ru', 'Мастерская {0}')
        with self.assertRaisesRegex(ValueError, 'Stale translated ESP source'):
            readback.expected_nonempty_fields(self.mapping, 'ru')
        self.write_catalog('ru', 'Мастерская {0}', readback.esp_tool.catalog.text_hash(revised['text']))
        translated, _, _ = readback.expected_nonempty_fields(self.mapping, 'ru')
        self.assertEqual(translated[self.identity], 'Мастерская {0}')

    def legacy_launch(self):
        directory = self.root / 'Data/Strings'
        directory.mkdir(parents=True)
        tables = []
        for kind in readback.esp_tool.KINDS:
            path = directory / f'Clipboard_ru.{kind}'
            path.write_bytes(readback.esp_tool.table_bytes(kind, {1: 'Исходная мастерская {0}'}))
            tables.append({'path': str(path), 'sha256': readback.sha(path)})
        return {'language': 'ru', 'esp_path': str(self.root / 'Data/Clipboard.esp'), 'tables': tables}

    def test_legacy_report_ignores_current_catalog_and_english_override(self):
        launch = self.legacy_launch()
        self.write_catalog('ru', 'Changed current translation {0}')
        self.current_source.side_effect = AssertionError('Legacy evidence must not load current English')
        expected, _, path = readback.expected_legacy_fields(self.mapping, launch)
        self.assertEqual(expected[self.identity], 'Исходная мастерская {0}')
        self.assertIsNone(path)
        expected_en, _, _ = readback.expected_legacy_fields(self.mapping, {**launch, 'language': 'en'})
        self.assertEqual(expected_en[self.identity], self.source['text'])

    def test_legacy_table_hash_changes_and_duplicates_are_rejected(self):
        launch = self.legacy_launch()
        launch['tables'].append(launch['tables'][0])
        with self.assertRaisesRegex(ValueError, 'unique original hash'):
            readback.expected_legacy_fields(self.mapping, launch)
        launch['tables'].pop()
        Path(launch['tables'][0]['path']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'unique original hash'):
            readback.expected_legacy_fields(self.mapping, launch)

    def test_stale_source_and_changed_placeholder_are_rejected(self):
        self.write_catalog('de', 'Werkstatt {0}', '0' * 64)
        with self.assertRaisesRegex(ValueError, 'Stale translated ESP source'):
            readback.expected_nonempty_fields(self.mapping, 'de')
        self.write_catalog('de', 'Werkstatt {1}')
        with self.assertRaisesRegex(ValueError, 'Placeholder mismatch'):
            readback.expected_nonempty_fields(self.mapping, 'de')


if __name__ == '__main__':
    unittest.main()
