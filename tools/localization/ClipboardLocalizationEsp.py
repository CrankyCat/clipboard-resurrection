#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read/validate xEdit's ESP migration and generate language data, never edit ESPs."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
# Immutable inputs to the original xEdit localization migration; maintained ESP lives in assets/.
BASE = ROOT / 'tests/fixtures/esp/Clipboard-inline.esp'
SOURCE = ROOT / 'localization/source/esp.en.json'
MAPPING = ROOT / 'localization/metadata/esp-map.json'
KINDS = ('STRINGS', 'DLSTRINGS', 'ILSTRINGS')
_spec = importlib.util.spec_from_file_location('clipboard_esp_catalog', Path(__file__).with_name('Build-Localization.py'))
catalog = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(catalog)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def read_esp(path: Path):
    data = path.read_bytes()
    records, groups = [], []

    def walk(begin: int, end: int, ancestry: tuple = ()):
        p = begin
        while p < end:
            if p + 24 > end:
                raise ValueError(f'Truncated record at {p}')
            header = data[p:p + 24]
            tag, size, flags, fid = struct.unpack_from('<4sIII', header)
            if tag == b'GRUP':
                if size < 24 or p + size > end:
                    raise ValueError('Invalid group size')
                identity = (header[8:12].hex(), fid)
                groups.append({'identity': identity, 'header_tail': header[16:].hex()})
                walk(p + 24, p + size, ancestry + (identity,))
                p += size
                continue
            if p + 24 + size > end:
                raise ValueError('Truncated record payload')
            raw = data[p + 24:p + 24 + size]
            p += 24 + size
            if flags & 0x40000:
                if len(raw) < 4:
                    raise ValueError('Truncated compressed record')
                expected = struct.unpack_from('<I', raw)[0]
                decoder = zlib.decompressobj()
                raw = decoder.decompress(raw[4:])
                if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
                    raise ValueError('Invalid compressed record boundary')
                if len(raw) != expected:
                    raise ValueError('Compressed record length mismatch')
            subs, pos, extended = [], 0, None
            while pos < len(raw):
                if pos + 6 > len(raw):
                    raise ValueError('Truncated subrecord header')
                subtag, length = struct.unpack_from('<4sH', raw, pos)
                pos += 6
                if subtag == b'XXXX':
                    if length != 4 or extended is not None or pos + 4 > len(raw):
                        raise ValueError('Malformed extended subrecord')
                    extended = struct.unpack_from('<I', raw, pos)[0]
                    pos += 4
                    continue
                if extended is not None:
                    length, extended = extended, None
                if pos + length > len(raw):
                    raise ValueError('Truncated subrecord')
                subs.append((subtag.decode('ascii'), raw[pos:pos + length]))
                pos += length
            if extended is not None:
                raise ValueError('Dangling extended subrecord')
            records.append({'type': tag.decode('ascii'), 'form_id': f'{fid:08X}',
                            'flags': flags, 'header_tail': header[16:].hex(),
                            'groups': ancestry, 'subrecords': subs})
        if p != end:
            raise ValueError('Invalid container boundary')

    walk(0, len(data))
    if not records or records[0]['type'] != 'TES4':
        raise ValueError('Not a plugin')
    return records, groups


def text_value(raw: bytes) -> str:
    if not raw.endswith(b'\0'):
        raise ValueError('Expected a terminated inline text field')
    raw = raw[:-1]
    if b'\0' in raw:
        raise ValueError('Embedded NUL in inline text')
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw.decode('cp1252')


def table_for(record_type: str, field: str):
    # Reviewed FO4 field definitions + wbLocalization.LocalizedValueDecider.
    if field == 'FULL':
        return 'STRINGS'
    if field == 'DESC':
        return 'STRINGS' if record_type == 'LSCR' else 'DLSTRINGS'
    if (record_type, field) in {('MESG', 'ITXT'), ('ACTI', 'ATTX'),
                                ('AMMO', 'ONAM'), ('QUST', 'NNAM'), ('MGEF', 'DNAM')}:
        return 'STRINGS'
    if record_type in {'QUST', 'BOOK'} and field == 'CNAM':
        return 'DLSTRINGS'
    return None


def inventory(path=BASE):
    records, _ = read_esp(path)
    rows = []
    for record in records:
        edid = next((text_value(raw) for tag, raw in record['subrecords'] if tag == 'EDID'), record['form_id'])
        occurrences = Counter()
        for tag, raw in record['subrecords']:
            index = occurrences[tag]
            occurrences[tag] += 1
            table = table_for(record['type'], tag)
            if table is None:
                continue
            text = text_value(raw)
            if not text:
                continue
            key = '$Clipboard_ESP_' + re.sub('[^A-Za-z0-9_]', '_', edid) + '_' + tag + '_' + str(index)
            rows.append({'key': key, 'text': text,
                         'context': f"{record['type']} {edid} ({record['form_id']}), {tag} occurrence {index}. Preserve alias tags and numeric format tokens, and button meaning/order.",
                         'record_type': record['type'], 'form_id': record['form_id'],
                         'field': tag, 'occurrence': index, 'table': table})
    if len({r['key'] for r in rows}) != len(rows):
        raise ValueError('Duplicate source key')
    return rows


def read_strings(path: Path):
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f'Truncated string table: {path}')
    count, size = struct.unpack_from('<II', data)
    start = 8 + count * 8
    if start + size != len(data):
        raise ValueError(f'Invalid table length: {path}')
    rows = {}
    for index in range(count):
        string_id, offset = struct.unpack_from('<II', data, 8 + index * 8)
        if string_id == 0 or string_id in rows or offset >= size:
            raise ValueError('Invalid string ID/offset')
        pos = start + offset
        if path.suffix.upper() == '.STRINGS':
            end = data.find(b'\0', pos)
            if end == -1:
                raise ValueError('Unterminated string')
            raw = data[pos:end]
        else:
            if pos + 4 > len(data):
                raise ValueError('Truncated sized string header')
            length = struct.unpack_from('<I', data, pos)[0]
            if length < 1 or pos + 4 + length > len(data) or data[pos + 3 + length] != 0:
                raise ValueError('Invalid sized string')
            raw = data[pos + 4:pos + 3 + length]
        if b'\0' in raw:
            raise ValueError('Embedded string NUL')
        # FO4/xEdit's English table codepage is 1252; all other FO4 locales
        # use UTF-8. Trying UTF-8 first would conceal mojibake in English.
        rows[string_id] = raw.decode('cp1252' if '_en.' in path.name.lower() else 'utf-8')
    return rows


def _validated_english_fields(mapping: dict, root: Path, sources: dict):
    fields = {field['key']: field for field in mapping['fields']}
    english = {key: row for key, row in sources.items() if row['surface'] == 'esp'}
    if len(fields) != len(mapping['fields']) or fields.keys() != english.keys():
        raise ValueError('Captured ESP map does not cover every source key exactly once')
    override_path = root / 'localization/metadata/esp-display-overrides.json'
    overrides = {}
    if override_path.is_file():
        data = catalog.read_json(override_path)
        expected_properties = {'schema', 'source_esp_sha256', 'localized_esp_sha256', 'captured_map_sha256', 'overrides'}
        if not isinstance(data, dict) or set(data) != expected_properties or type(data['schema']) is not int or data['schema'] != 1:
            raise ValueError('Invalid ESP display override schema')
        captured_path = root / 'localization/metadata/esp-map.json'
        if (data['source_esp_sha256'] != mapping['source_esp_sha256'] or
                data['localized_esp_sha256'] != mapping['localized_esp_sha256'] or
                data['captured_map_sha256'] != sha(captured_path) or
                mapping != catalog.read_json(captured_path)):
            raise ValueError('ESP display override targets a different captured migration')
        if not isinstance(data['overrides'], list):
            raise ValueError('ESP display overrides must be an array')
        identity = ('key', 'record_type', 'form_id', 'field', 'occurrence', 'table', 'string_id')
        entry_properties = set(identity) | {'captured_text_sha256', 'replacement_text', 'replacement_text_sha256', 'reason'}
        for entry in data['overrides']:
            if not isinstance(entry, dict) or set(entry) != entry_properties:
                raise ValueError('ESP display override lacks its exact identity, hashes, replacement or reason')
            key = entry['key']
            if not isinstance(key, str) or key in overrides or key not in fields:
                raise ValueError('Duplicate or unknown ESP display override key')
            field, source = fields[key], english[key]
            if any(type(entry[name]) is not type(field[name]) or entry[name] != field[name] for name in identity):
                raise ValueError('ESP display override field or string ID differs from the capture')
            if (entry['captured_text_sha256'] != catalog.text_hash(field['text']) or
                    entry['replacement_text'] != source['text'] or
                    entry['replacement_text_sha256'] != catalog.text_hash(source['text'])):
                raise ValueError('ESP display override original or replacement text hash is stale')
            if field['text'] == source['text'] or not isinstance(entry['reason'], str) or not entry['reason'].strip():
                raise ValueError('ESP display override must explain an actual text correction')
            overrides[key] = entry
    for key, field in fields.items():
        if field['text'] != english[key]['text'] and key not in overrides:
            raise ValueError('Captured ESP field differs from its current English source without a reviewed override')
    return english, overrides


def validated_english_fields(mapping: dict, root: Path = ROOT) -> dict:
    """Current English ESP rows, with only explicit captured-map display overrides."""
    sources, _ = catalog.load_sources(root)
    return _validated_english_fields(mapping, root, sources)[0]


def validate_revision(localized: Path):
    """Accept only the reviewed menu-name override removal, preserving all else."""
    revision = catalog.read_json(ROOT / 'localization/metadata/esp-revision.json')
    baseline = ROOT / 'tests/fixtures/esp/Clipboard.esp'
    mapping = catalog.read_json(ROOT / 'localization/metadata/esp-map.json')
    if (revision['baseline_sha256'] != mapping['localized_esp_sha256'] or
            sha(baseline) != revision['baseline_sha256'] or
            sha(localized) != revision['edited_sha256']):
        raise ValueError('ESP revision identities differ from the reviewed menu override removal')
    old, old_groups = read_esp(baseline)
    new, new_groups = read_esp(localized)
    old_ids, new_ids = ({r['form_id']: r for r in rows} for rows in (old, new))
    removed = revision['removed_form_id']
    if (removed != '0011FBD3' or len(old_ids) != len(old) or len(new_ids) != len(new) or
            old_ids.keys() - new_ids.keys() != {removed} or new_ids.keys() - old_ids.keys() or
            old_groups != new_groups or old_ids[removed]['type'] != 'KYWD'):
        raise ValueError('Unexpected record/group change in edited ESP')
    if [r['form_id'] for r in old if r['form_id'] != removed] != [r['form_id'] for r in new]:
        raise ValueError('Unexpected record reordering in edited ESP')
    for after in new:
        before = old_ids[after['form_id']]
        if after['type'] != 'TES4':
            if after != before:
                raise ValueError(f'Unexpected gameplay edit: {after["form_id"]}')
            continue
        expected = dict(before)
        expected['subrecords'] = []
        for tag, raw in before['subrecords']:
            if tag == 'HEDR':
                if len(raw) != 12:
                    raise ValueError('Unexpected HEDR size')
                raw = raw[:4] + struct.pack('<I', struct.unpack_from('<I', raw, 4)[0] - 1) + raw[8:]
            expected['subrecords'].append((tag, raw))
        if after != expected:
            raise ValueError('Edited ESP header differs beyond the one-record count decrement')
    return {'removed_form_id': removed, 'record_count': len(new), 'edited_sha256': sha(localized)}


def compare_migration(localized: Path, strings: Path | None = None):
    requested = localized
    revision = None
    if ((ROOT / 'localization/metadata/esp-revision.json').is_file() and
            sha(localized) != catalog.read_json(ROOT / 'localization/metadata/esp-map.json')['localized_esp_sha256']):
        revision = validate_revision(localized)
        localized = ROOT / 'tests/fixtures/esp/Clipboard.esp'
    old, old_groups = read_esp(BASE)
    new, new_groups = read_esp(localized)
    if len(old) != len(new) or old_groups != new_groups:
        raise ValueError('Record count or group identity/header changed')
    if new[0]['flags'] != old[0]['flags'] | 0x80:
        raise ValueError('Localized flag migration mismatch')
    sources = inventory()
    by_field = {(r['form_id'], r['field'], r['occurrence']): r for r in sources}
    overrides, current_english = {}, {}
    if (ROOT / 'localization/metadata/esp-display-overrides.json').is_file():
        captured = catalog.read_json(ROOT / 'localization/metadata/esp-map.json')
        if sha(BASE) != captured['source_esp_sha256'] or sha(localized) != captured['localized_esp_sha256']:
            raise ValueError('ESP display overrides require the unchanged captured plugin identities')
        current_sources, _ = catalog.load_sources(ROOT)
        current_english, overrides = _validated_english_fields(captured, ROOT, current_sources)
    tables = {kind: read_strings((strings or requested.parent / 'Strings') / f'Clipboard_en.{kind}') for kind in KINDS}
    old_ids = {r['form_id']: r for r in old}
    new_ids = {r['form_id']: r for r in new}
    if len(old_ids) != len(old) or len(new_ids) != len(new) or old_ids.keys() != new_ids.keys():
        raise ValueError('Duplicate, added, or removed FormID')
    reordered = sum(a['form_id'] != b['form_id'] for a, b in zip(old, new))
    if reordered:
        # xEdit saves modified record groups in ascending FormID order. Record
        # ancestry and every ordered subrecord (buttons, conditions, VMAD)
        # remain strict below; pattern-file row order is unrelated.
        group_ids, original_group_ids = {}, {}
        for record in old:
            original_group_ids.setdefault(record['groups'], []).append(record['form_id'])
        for record in new:
            group_ids.setdefault(record['groups'], []).append(record['form_id'])
        if any(ids != original_group_ids.get(group) and ids != sorted(original_group_ids.get(group, []))
               for group, ids in group_ids.items()):
            raise ValueError('Record order changed outside xEdit canonical FormID sorting')
    mapped, empty_count, normalizations, applied_overrides = [], 0, [], []
    for before in old:
        after = new_ids[before['form_id']]
        for attr in ('type', 'form_id', 'header_tail', 'groups'):
            if before[attr] != after[attr]:
                raise ValueError(f'Record {before["form_id"]} changed {attr}')
        if before['type'] != 'TES4' and before['flags'] != after['flags']:
            raise ValueError('Gameplay record flags changed')
        after_subrecords = after['subrecords']
        # Observed xEdit 4.1.5q save normalization: explicit zero internal-cell
        # count in TES4. Allow only this exact appended field/value.
        if (before['type'] == 'TES4' and
                after_subrecords == before['subrecords'] + [('INCC', b'\0' * 4)]):
            after_subrecords = after_subrecords[:-1]
            normalizations.append({'form_id': '00000000', 'field': 'INCC',
                                   'before_hex': None, 'after_hex': '00000000'})
        if [s[0] for s in before['subrecords']] != [s[0] for s in after_subrecords]:
            raise ValueError(f'Subrecord topology changed: {before["form_id"]}')
        occurrences = Counter()
        for (tag, old_raw), (_, new_raw) in zip(before['subrecords'], after_subrecords):
            index = occurrences[tag]
            occurrences[tag] += 1
            table = table_for(before['type'], tag)
            if table is None:
                if old_raw != new_raw:
                    raise ValueError(f'Non-text data changed: {before["form_id"]}/{tag}/{index}')
                continue
            if len(new_raw) != 4:
                raise ValueError(f'Localizable field not externalized: {before["form_id"]}/{tag}')
            string_id = struct.unpack('<I', new_raw)[0]
            english = text_value(old_raw)
            if not english:
                if string_id != 0:
                    raise ValueError('Empty text did not map to ID 0')
                empty_count += 1
                continue
            row = dict(by_field[(before['form_id'], tag, index)])
            row['string_id'] = string_id
            expected_english = english
            if row['key'] in overrides:
                override = overrides[row['key']]
                identity = ('record_type', 'form_id', 'field', 'occurrence', 'table', 'string_id')
                if (any(row[name] != override[name] for name in identity) or
                        catalog.text_hash(english) != override['captured_text_sha256']):
                    raise ValueError('Actual ESP field does not match the reviewed display override')
                expected_english = current_english[row['key']]['text']
                applied_overrides.append(dict(override))
            if tables[table].get(string_id) != expected_english:
                raise ValueError(f'English text changed outside its reviewed display value: {before["form_id"]}/{tag}/{index}')
            # Preserve original inline English in the returned migration map.
            mapped.append(row)
    if len(mapped) != len(sources):
        raise ValueError('Incomplete localization coverage')
    if {row['key'] for row in applied_overrides} != overrides.keys():
        raise ValueError('An ESP display override was not found in the actual plugin')
    return {'schema_version': 1, 'source_esp_sha256': sha(BASE),
            'localized_esp_sha256': sha(requested), 'record_count': revision['record_count'] if revision else len(new),
            'baseline_localized_esp_sha256': sha(localized), 'reviewed_revision': revision,
            'empty_fields': empty_count, 'fields': mapped,
            'reordered_record_positions': reordered,
            'xedit_zero_value_normalizations': normalizations,
            'english_display_overrides': applied_overrides,
            'table_counts': {k: len(v) for k, v in tables.items()}}


def table_bytes(kind: str, rows: dict[int, str], encoding: str = 'utf-8'):
    entries, payload = bytearray(), bytearray()
    for string_id, text in sorted(rows.items()):
        if not string_id or '\0' in text:
            raise ValueError('Invalid generated string')
        entries += struct.pack('<II', string_id, len(payload))
        encoded = text.encode(encoding) + b'\0'
        if kind != 'STRINGS':
            payload += struct.pack('<I', len(encoded))
        payload += encoded
    return bytes(struct.pack('<II', len(rows), len(payload)) + entries + payload)


def generate_tables(output: Path, check: bool = False):
    if not output.resolve().is_relative_to(ROOT.resolve()) or output.resolve().is_relative_to((ROOT / 'Original Project Files').resolve()):
        raise ValueError('String-table output must remain in the maintained project')
    mapping = catalog.read_json(MAPPING)
    localized = ROOT / 'assets/Clipboard.esp'
    if sha(BASE) != mapping['source_esp_sha256']:
        raise ValueError('ESP identity differs from captured supported migration')
    if sha(localized) != mapping['localized_esp_sha256']:
        if not (ROOT / 'localization/metadata/esp-revision.json').is_file():
            raise ValueError('ESP identity differs from captured supported migration')
        validate_revision(localized)
    sources, inputs = catalog.load_sources(ROOT)
    english, overrides = _validated_english_fields(mapping, ROOT, sources)
    override_path = ROOT / 'localization/metadata/esp-display-overrides.json'
    if override_path.is_file():
        inputs[override_path.relative_to(ROOT).as_posix()] = sha(override_path)
    translations, translation_inputs = catalog.load_locale_translations(ROOT, sources)
    inputs.update(translation_inputs)
    generated, payloads, expected_tables = {}, {}, {}
    for locale in catalog.LOCALES:
        tables = {kind: {} for kind in KINDS}
        for field in mapping['fields']:
            value = english[field['key']]['text'] if locale == 'en' else translations[locale][field['key']]['text']
            rows = tables[field['table']]
            string_id = field['string_id']
            if string_id in rows and rows[string_id] != value:
                raise ValueError('One xEdit string ID maps to conflicting translations')
            rows[string_id] = value
        for kind, rows in tables.items():
            relative = f'Strings/Clipboard_{locale}.{kind}'
            target = output / relative
            data = table_bytes(kind, rows, 'cp1252' if locale == 'en' else 'utf-8')
            payloads[relative] = data
            expected_tables[relative] = rows
            if check:
                if not target.is_file() or target.read_bytes() != data:
                    raise ValueError(f'String table is missing or stale: {target}')
            generated[relative] = {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}
    for alias, locale in catalog.LOCALE_ALIASES.items():
        for kind in KINDS:
            original = f'Strings/Clipboard_{locale}.{kind}'
            relative = f'Strings/Clipboard_{alias}.{kind}'
            payloads[relative] = payloads[original]
            expected_tables[relative] = expected_tables[original]
            generated[relative] = dict(generated[original])
            target = output / relative
            if check and (not target.is_file() or target.read_bytes() != payloads[relative]):
                raise ValueError(f'String table alias is missing or stale: {target}')
    extras = [p.name for p in (output / 'Strings').glob('Clipboard_*') if 'Strings/' + p.name not in generated]
    if extras:
        raise ValueError(f'Unexpected Clipboard locale tables: {extras}')
    for relative, data in payloads.items():
        target = output / relative
        if not check:
            catalog.write_atomic(target, data)
        if read_strings(target) != expected_tables[relative]:
            raise ValueError('Generated table round-trip mismatch')
    return {'generated_locales': (*catalog.LOCALES, *catalog.LOCALE_ALIASES),
            'locale_aliases': dict(catalog.LOCALE_ALIASES), 'mapped_fields': len(mapping['fields']),
            'localized_esp_sha256': sha(localized),
            'english_display_overrides': list(overrides.values()),
            'inputs': dict(sorted(inputs.items())), 'outputs': generated}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('extract', 'capture', 'generate', 'verify'))
    parser.add_argument('--esp', type=Path, default=ROOT / 'outputs/localization-implementation/xedit/Data/Clipboard.esp')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/generated/localization')
    parser.add_argument('--strings', type=Path, help='English string-table directory for an ESP stored separately from generated tables')
    parser.add_argument('--check', action='store_true', help='Validate existing generated tables without writing')
    args = parser.parse_args()
    if args.action == 'extract':
        rows = inventory()
        write_json(SOURCE, [{k: row[k] for k in ('key', 'text', 'context')} for row in rows])
        print(json.dumps({'source_fields': len(rows), 'tables': dict(Counter(r['table'] for r in rows))}))
    elif args.action in ('capture', 'verify'):
        result = compare_migration(args.esp, args.strings)
        if args.action == 'capture':
            if result['english_display_overrides']:
                raise ValueError('Display overrides cannot replace the original captured migration map')
            write_json(MAPPING, result)
        print(json.dumps({k: v for k, v in result.items() if k != 'fields'}))
    else:
        print(json.dumps(generate_tables(args.output, args.check)))


if __name__ == '__main__':
    main()
