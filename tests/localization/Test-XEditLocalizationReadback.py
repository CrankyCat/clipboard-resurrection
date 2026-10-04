#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare actual xEdit GetEditValue/Check output to every mapped locale field."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location('clipboard_localization_esp', Path(__file__).resolve().parents[2] / 'tools/localization/ClipboardLocalizationEsp.py')
esp_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(esp_tool)

LOCALES = ('en', 'ru', 'de', 'es', 'esmx', 'fr', 'it', 'ja', 'pl', 'ptbr', 'zhhans', 'zhhant')
ALIASES = {'cn': 'zhhant'}


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def expected_nonempty_fields(mapping: dict, language: str):
    if language not in (*LOCALES, *ALIASES):
        raise ValueError('Unsupported readback language')
    catalog_locale = ALIASES.get(language, language)
    english = esp_tool.validated_english_fields(mapping, root=esp_tool.ROOT)
    translated = {}
    catalog_path = None
    if catalog_locale != 'en':
        catalog_path = esp_tool.ROOT / 'localization/translations' / f'{catalog_locale}.json'
        for row in esp_tool.catalog.read_json(catalog_path):
            if row['key'] in translated:
                raise ValueError(f"Duplicate translated key: {row['key']}")
            translated[row['key']] = row
    expected = {}
    for row in mapping['fields']:
        identity = (row['form_id'], row['record_type'], row['field'], row['occurrence'])
        if identity in expected:
            raise ValueError(f'Duplicate mapped field: {identity}')
        source = english[row['key']]
        text = source['text']
        if catalog_locale != 'en':
            translation = translated[row['key']]
            if translation['source_sha256'] != esp_tool.catalog.text_hash(text):
                raise ValueError(f"Stale translated ESP source: {row['key']}")
            esp_tool.catalog.validate_translation(source, translation['text'])
            text = translation['text']
        expected[identity] = text
    return expected, catalog_locale, catalog_path


def expected_legacy_fields(mapping: dict, launch: dict):
    """Recheck historical reports against their original, hash-bound inputs."""
    language = launch['language']
    if language not in ('en', 'ru'):
        raise ValueError('Historical schema-1 readback supports only the recorded EN/RU runs')
    tables = {}
    if language != 'en':
        directory = Path(launch['esp_path']).parent / 'Strings'
        for kind in esp_tool.KINDS:
            path = directory / f'Clipboard_{language}.{kind}'
            matches = [row for row in launch['tables'] if Path(row['path']).resolve() == path.resolve()]
            if len(matches) != 1 or sha(path) != matches[0]['sha256']:
                raise ValueError(f'Legacy locale table lacks its unique original hash: {path}')
            tables[kind] = esp_tool.read_strings(path)
    expected = {}
    for row in mapping['fields']:
        identity = (row['form_id'], row['record_type'], row['field'], row['occurrence'])
        if identity in expected:
            raise ValueError(f'Duplicate mapped field: {identity}')
        expected[identity] = row['text'] if language == 'en' else tables[row['table']][row['string_id']]
    return expected, language, None


def verify(run: Path):
    launch = json.loads((run / 'launch.json').read_text(encoding='utf-8'))
    schema = launch.get('schema_version', 1)
    if type(schema) is not int or schema not in (1, 2):
        raise ValueError('Unsupported launch evidence schema')
    if launch['language'] not in (*LOCALES, *ALIASES):
        raise ValueError('Unsupported readback language')
    report = run / 'readback.json'
    data = json.loads(report.read_text(encoding='utf-8'))
    if not launch['launched'] or data.get('format') != 'clipboard-xedit-readback-v2' or data.get('complete') is not True:
        raise ValueError('Readback did not complete in a launched xEdit process')
    for path_key, hash_key in [('executable', 'executable_sha256'), ('esp_path', 'esp_sha256_before'), ('script_path', 'script_sha256')]:
        if sha(Path(launch[path_key])) != launch[hash_key]:
            raise ValueError(f'Input changed during xEdit verification: {path_key}')
    for table in launch['tables']:
        if sha(Path(table['path'])) != table['sha256']:
            raise ValueError(f"Table changed: {table['path']}")
        if 'source' in table and sha(Path(table['source'])) != table['sha256']:
            raise ValueError(f"Package source table changed: {table['source']}")
    for fixture in launch.get('fixture_inputs', []):
        if sha(Path(fixture['path'])) != fixture['sha256']:
            raise ValueError(f"Fixture input changed: {fixture['path']}")
    mapping = json.loads(esp_tool.MAPPING.read_text(encoding='utf-8'))
    if launch['esp_sha256_before'] != mapping['localized_esp_sha256']:
        raise ValueError('Readback is for a different localized ESP')
    english_source_path = esp_tool.ROOT / 'localization/source/esp.en.json'
    overrides_path = esp_tool.ROOT / 'localization/metadata/esp-display-overrides.json'
    if schema == 2:
        if (Path(launch['english_source_path']).resolve() != english_source_path.resolve() or
                sha(english_source_path) != launch['english_source_sha256']):
            raise ValueError('English ESP source changed during readback')
        if overrides_path.is_file():
            if (not launch.get('display_overrides_path') or
                    Path(launch['display_overrides_path']).resolve() != overrides_path.resolve() or
                    sha(overrides_path) != launch['display_overrides_sha256']):
                raise ValueError('Reviewed ESP display overrides changed during readback')
        elif launch.get('display_overrides_path') is not None or launch.get('display_overrides_sha256') is not None:
            raise ValueError('Reviewed ESP display overrides disappeared during readback')
        expected, catalog_locale, catalog_path = expected_nonempty_fields(mapping, launch['language'])
        if launch['catalog_locale'] != catalog_locale:
            raise ValueError('Launch selected an incorrect source catalog')
        if Path(launch['mapping_path']).resolve() != esp_tool.MAPPING.resolve() or sha(esp_tool.MAPPING) != launch['mapping_sha256']:
            raise ValueError('Captured ESP mapping changed during readback')
        if catalog_path and (Path(launch['catalog_path']).resolve() != catalog_path.resolve() or sha(catalog_path) != launch['catalog_sha256']):
            raise ValueError('Translation source catalog changed during readback')
        table_directory = Path(launch['esp_path']).parent / 'Strings'
        expected_paths = {str((table_directory / f'Clipboard_{locale}.{kind}').resolve()).casefold()
                          for locale in (*LOCALES, *ALIASES) for kind in esp_tool.KINDS}
        actual_paths = [str(Path(table['path']).resolve()).casefold() for table in launch['tables']]
        if len(actual_paths) != len(expected_paths) or set(actual_paths) != expected_paths:
            raise ValueError('Launch does not capture all 39 expected locale tables exactly once')
        if f"-l:{launch['language']}" not in launch['arguments']:
            raise ValueError('Launch arguments do not select the recorded readback language')
    else:
        expected, catalog_locale, catalog_path = expected_legacy_fields(mapping, launch)
    # Empty localized ID-zero fields were deliberately omitted from the translation corpus.
    records, _ = esp_tool.read_esp(Path(launch['esp_path']))
    for record in records:
        occurrences = Counter()
        for field, value in record['subrecords']:
            occurrence = occurrences[field]
            occurrences[field] += 1
            if esp_tool.table_for(record['type'], field) and value == struct.pack('<I', 0):
                expected[(record['form_id'], record['type'], field, occurrence)] = ''
    actual = {}
    for row in data['fields']:
        identity = (row['form_id'], row['record_type'], row['field'], row['occurrence'])
        if identity in actual:
            raise ValueError(f'Duplicate xEdit field: {identity}')
        if type(row['occurrence']) is not int or not isinstance(row['text'], str) or not isinstance(row['path'], str):
            raise ValueError(f'Invalid xEdit field data: {identity}')
        actual[identity] = row['text']
    errors = data['check_errors']
    for error in errors:
        if not all(isinstance(error.get(key), str) for key in ('form_id', 'path', 'error')):
            raise ValueError('Invalid xEdit Check error data')
    summary = tuple(data['summary'][key] for key in ('records', 'fields', 'nonempty_fields', 'check_errors'))
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    differences = [{'field': identity, 'expected': expected[identity], 'actual': actual[identity]}
                   for identity in expected.keys() & actual.keys() if expected[identity] != actual[identity]]
    expected_summary = (len(records), len(actual), sum(bool(v) for v in actual.values()), len(errors))
    if summary != expected_summary:
        raise ValueError(f'Inconsistent xEdit summary: {summary}, expected {expected_summary}')
    result = {'schema_version': 2, 'language': launch['language'], 'catalog_locale': catalog_locale,
              'launch_schema_version': schema,
              'expected_source': ('validated_current_catalogs' if schema == 2 else
                                  'captured_map' if launch['language'] == 'en' else 'frozen_locale_tables'),
              'current_catalog_attested': schema == 2,
              'catalog_sha256': sha(catalog_path) if catalog_path else None,
              'english_source_sha256': sha(english_source_path) if schema == 2 else None,
              'display_overrides_sha256': sha(overrides_path) if schema == 2 and overrides_path.is_file() else None,
              'mapping_sha256': sha(esp_tool.MAPPING), 'captured_table_count': len(launch['tables']),
              'passed': not (missing or extra or differences or errors),
              'esp_sha256': mapping['localized_esp_sha256'], 'executable_sha256': launch['executable_sha256'],
              'script_sha256': launch['script_sha256'], 'readback_sha256': sha(report),
              'record_count': len(records), 'mapped_nonempty_fields': len(mapping['fields']),
              'xedit_localized_fields': len(actual), 'xedit_check_errors': errors,
              'missing_fields': missing, 'extra_fields': extra, 'text_differences': differences,
              'plugin_and_table_hashes_unchanged': True,
              'scope': 'Actual xEdit field resolution and Check; no in-game font, layout, or runtime claim.'}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = verify(args.run)
    text = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        args.output.write_text(text, encoding='utf-8')
    # Windows consoles may use CP1252; keep diagnostics printable without
    # changing the exact Unicode comparison or the UTF-8 evidence file.
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
