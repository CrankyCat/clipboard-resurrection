#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host checks for the read-only ESP migration verifier and string-table writer.

All plugin bytes here are synthetic temporary test fixtures. Production ESPs
are never written or modified by these tests.
"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("clipboard_localization_esp", ROOT / "tools/ClipboardLocalizationEsp.py")
esp = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(esp)


def subrecord(tag: str, raw: bytes) -> bytes:
    return struct.pack("<4sH", tag.encode("ascii"), len(raw)) + raw


def record(tag: str, fid: int, subs: list[tuple[str, bytes]], flags=0, trailing=b"") -> bytes:
    payload = b"".join(subrecord(tag, raw) for tag, raw in subs) + trailing
    if flags & 0x40000:
        payload = struct.pack("<I", len(payload)) + zlib.compress(payload)
    return struct.pack("<4sIIIHHHH", tag.encode("ascii"), len(payload), flags, fid, 0, 0, 131, 0) + payload


def group(payload: bytes, label=b"MESG") -> bytes:
    return struct.pack("<4sI4sIHHHH", b"GRUP", len(payload) + 24, label, 0, 0, 0, 0, 0) + payload


def independent_table(kind: str, rows: list[tuple[int, bytes]]) -> bytes:
    """Fixture construction follows byte-level Bethesda directory layout."""
    directory, payload = bytearray(), bytearray()
    for key, raw in rows:
        directory.extend(struct.pack("<II", key, len(payload)))
        if kind != "STRINGS":
            payload.extend(struct.pack("<I", len(raw) + 1))
        payload.extend(raw + b"\0")
    return struct.pack("<II", len(rows), len(payload)) + directory + payload


class LocalizationEspTests(unittest.TestCase):
    def setUp(self):
        test_root = ROOT / "build/tests/localization-esp"
        test_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="fixture-", dir=test_root)
        self.directory = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def file(self, name: str, data: bytes) -> Path:
        target = self.directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return target

    def fixture(self, *, new_subs=None, new_flags=0, localized=True, group_label=b"MESG"):
        header_subs = [("HEDR", struct.pack("<fII", 0.95, 1, 0x2000)),
                       ("MAST", b"Fallout4.esm\0"), ("DATA", bytes(8))]
        source_subs = [("EDID", b"FixtureMessage\0"), ("FULL", b"Fixture\0"),
                       ("DESC", b"Rotate <Alias=0>\0"), ("ITXT", b"1\xB0\0"),
                       ("ITXT", b"Back\0"), ("ITXT", b"\0"), ("VMAD", b"\x06\0\x02\0script\xFF")]
        target_subs = [(tag, struct.pack("<I", value) if isinstance(value, int) else value)
                       for tag, value in [("EDID", b"FixtureMessage\0"), ("FULL", 1),
                                          ("DESC", 2), ("ITXT", 3), ("ITXT", 4), ("ITXT", 0),
                                          ("VMAD", b"\x06\0\x02\0script\xFF")]]
        if new_subs is not None:
            target_subs = new_subs(target_subs)
        original = self.file("source.esp", record("TES4", 0, header_subs) +
                             group(record("MESG", 0x01001234, source_subs)))
        candidate = self.file("candidate/Clipboard.esp", record("TES4", 0, header_subs, 0x80 if localized else 0) +
                              group(record("MESG", 0x01001234, target_subs, new_flags), group_label))
        for kind, rows in {"STRINGS": [(1, b"Fixture"), (3, b"1\xB0"), (4, b"Back")],
                           "DLSTRINGS": [(2, b"Rotate <Alias=0>")], "ILSTRINGS": []}.items():
            self.file(f"candidate/Strings/Clipboard_en.{kind}", independent_table(kind, rows))
        return original, candidate

    def compare(self, original, candidate):
        inventory = esp.inventory
        with patch.object(esp, "ROOT", self.directory), patch.object(esp, "BASE", original), patch.object(esp, "inventory", lambda: inventory(original)):
            return esp.compare_migration(candidate)

    def generation_fixture(self):
        original, candidate = self.fixture()
        mapping = self.compare(original, candidate)
        self.file("package/base/Clipboard.esp", original.read_bytes())
        self.file("package/v240/Clipboard.esp", candidate.read_bytes())
        mapping_path = self.file("localization/esp-map.json", json.dumps(mapping).encode("utf-8"))
        sources = [{**row, "surface": "esp"} for row in mapping["fields"]]
        for surface in esp.catalog.SOURCES:
            key = "$Clipboard_Docs_ReadMe_001" if surface == "docs" else f"$Clipboard_{surface}"
            rows = mapping["fields"] if surface == "esp" else [{"key": key, "text": "Other", "context": "Synthetic fixture"}]
            self.file(f"localization/source/{surface}.en.json", json.dumps(rows).encode("utf-8"))
            if surface != "esp":
                sources.extend({**row, "surface": surface} for row in rows)
        self.file("localization/source/docs-layout.json", json.dumps({
            "schema": 2, "translation_status": "pending", "enabled_locales": [],
            "retained_translation_keys": {"ru": ["$Clipboard_Docs_ReadMe_001"]},
            "documents": [{"source": "package/v240/Docs/clipboard/Clipboard-ReadMe.txt",
                           "filename": "Clipboard-ReadMe.txt"}]}).encode("utf-8"))
        labels = dict(zip(esp.catalog.TRANSLATED_LOCALES,
                          ("Пример", "Beispiel", "Ejemplo", "México", "Exemple", "Esempio",
                           "見本", "Przykład", "Exemplo", "示例", "範例")))
        for locale, label in labels.items():
            rows = []
            for source in sources:
                if locale != "ru" and source["surface"] == "docs":
                    continue
                text = {"Fixture": label, "Rotate <Alias=0>": label + " <Alias=0>", "1°": "1°", "Back": label + " retour"}.get(source["text"], label)
                rows.append({"key": source["key"], "text": text, "source_sha256": esp.catalog.text_hash(source["text"])})
            self.file(f"localization/{locale}.json", json.dumps(rows, ensure_ascii=False).encode("utf-8"))
        return mapping_path, labels

    def generate(self, output, check=False):
        with patch.object(esp, "ROOT", self.directory), \
             patch.object(esp, "BASE", self.directory / "package/base/Clipboard.esp"), \
             patch.object(esp, "MAPPING", self.directory / "localization/esp-map.json"):
            return esp.generate_tables(output, check)

    def display_override_fixture(self):
        mapping_path, _ = self.generation_fixture()
        mapping = json.loads(mapping_path.read_bytes())
        field = mapping['fields'][0]
        replacement = 'Corrected fixture'
        source_path = self.directory / 'localization/source/esp.en.json'
        sources = json.loads(source_path.read_bytes())
        next(row for row in sources if row['key'] == field['key'])['text'] = replacement
        source_path.write_text(json.dumps(sources), encoding='utf-8')
        for locale in esp.catalog.TRANSLATED_LOCALES:
            path = self.directory / f'localization/{locale}.json'
            translations = json.loads(path.read_bytes())
            next(row for row in translations if row['key'] == field['key'])['source_sha256'] = esp.catalog.text_hash(replacement)
            path.write_text(json.dumps(translations), encoding='utf-8')
        entry = {name: field[name] for name in ('key', 'record_type', 'form_id', 'field', 'occurrence', 'table', 'string_id')}
        entry.update(captured_text_sha256=esp.catalog.text_hash(field['text']), replacement_text=replacement,
                     replacement_text_sha256=esp.catalog.text_hash(replacement), reason='Reviewed synthetic display-only correction.')
        data = {'schema': 1, 'source_esp_sha256': mapping['source_esp_sha256'],
                'localized_esp_sha256': mapping['localized_esp_sha256'], 'captured_map_sha256': esp.sha(mapping_path),
                'overrides': [entry]}
        path = self.file('localization/esp-display-overrides.json', json.dumps(data).encode('utf-8'))
        return mapping, path, data

    def test_reviewed_display_override_preserves_esp_and_original_capture(self):
        mapping, override_path, data = self.display_override_fixture()
        plugin = self.directory / 'package/v240/Clipboard.esp'
        captured_path = self.directory / 'localization/esp-map.json'
        before = (plugin.read_bytes(), captured_path.read_bytes())
        field = mapping['fields'][0]
        current = esp.validated_english_fields(mapping, root=self.directory)
        self.assertEqual(current[field['key']]['text'], 'Corrected fixture')
        generated = self.generate(plugin.parent)
        self.assertEqual(generated['english_display_overrides'], data['overrides'])
        self.assertEqual(generated['inputs']['localization/esp-display-overrides.json'], esp.sha(override_path))
        self.assertEqual(esp.read_strings(plugin.parent / 'Strings/Clipboard_en.STRINGS')[field['string_id']], 'Corrected fixture')
        result = self.compare(self.directory / 'package/base/Clipboard.esp', plugin)
        self.assertEqual(result['fields'], mapping['fields'])
        self.assertEqual(result['english_display_overrides'], data['overrides'])
        self.assertEqual((plugin.read_bytes(), captured_path.read_bytes()), before)

    def test_display_override_requires_exact_hashes_identity_and_reason(self):
        mapping, override_path, original = self.display_override_fixture()
        cases = ('captured_map_hash', 'source_esp_hash', 'localized_esp_hash', 'unknown_key', 'duplicate_key',
                 'old_text_hash', 'new_text_hash', 'new_text_value', 'string_id', 'record_type', 'form_id',
                 'field', 'occurrence', 'table', 'boolean_occurrence', 'empty_reason')
        for corruption in cases:
            data = json.loads(json.dumps(original))
            entry = data['overrides'][0]
            if corruption in ('captured_map_hash', 'source_esp_hash', 'localized_esp_hash'):
                name = {'captured_map_hash': 'captured_map_sha256', 'source_esp_hash': 'source_esp_sha256',
                        'localized_esp_hash': 'localized_esp_sha256'}[corruption]
                data[name] = '0' * 64
            elif corruption == 'unknown_key':
                entry['key'] = '$Clipboard_Unknown'
            elif corruption == 'duplicate_key':
                data['overrides'].append(dict(entry))
            elif corruption in ('old_text_hash', 'new_text_hash'):
                entry['captured_text_sha256' if corruption == 'old_text_hash' else 'replacement_text_sha256'] = '0' * 64
            elif corruption == 'new_text_value':
                entry['replacement_text'] = 'Unreviewed text'
            elif corruption in ('string_id', 'occurrence'):
                entry[corruption] += 1
            elif corruption in ('record_type', 'form_id', 'field', 'table'):
                entry[corruption] = 'INVALID'
            elif corruption == 'boolean_occurrence':
                entry['occurrence'] = False
            else:
                entry['reason'] = ' '
            override_path.write_text(json.dumps(data), encoding='utf-8')
            with self.subTest(corruption=corruption), self.assertRaises(ValueError):
                esp.validated_english_fields(mapping, root=self.directory)

    def test_unlisted_source_difference_and_missing_override_are_rejected(self):
        mapping, override_path, _ = self.display_override_fixture()
        original_override = override_path.read_bytes()
        override_path.unlink()
        with self.assertRaisesRegex(ValueError, 'without a reviewed override'):
            esp.validated_english_fields(mapping, root=self.directory)
        override_path.write_bytes(original_override)
        path = self.directory / 'localization/source/esp.en.json'
        sources = json.loads(path.read_bytes())
        sources[1]['text'] = 'Unreviewed <Alias=0>'
        path.write_text(json.dumps(sources), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'without a reviewed override'):
            esp.validated_english_fields(mapping, root=self.directory)

    def test_migration_rejects_stale_corrected_or_unlisted_english_table_value(self):
        self.display_override_fixture()
        plugin = self.directory / 'package/v240/Clipboard.esp'
        self.generate(plugin.parent)
        path = plugin.parent / 'Strings/Clipboard_en.STRINGS'
        original = path.read_bytes()
        for string_id, changed in ((1, 'Fixture'), (4, 'Unreviewed')):
            with self.subTest(string_id=string_id):
                path.write_bytes(original)
                rows = esp.read_strings(path)
                rows[string_id] = changed
                path.write_bytes(independent_table('STRINGS', [(key, value.encode('cp1252')) for key, value in sorted(rows.items())]))
                with self.assertRaisesRegex(ValueError, 'English text changed'):
                    self.compare(self.directory / 'package/base/Clipboard.esp', plugin)

    def test_generation_translates_all_locales_preserving_ids_and_english_russian_bytes(self):
        _, labels = self.generation_fixture()
        plugin = self.directory / "package/v240/Clipboard.esp"
        original_plugin = plugin.read_bytes()
        output = self.directory / "generated"
        report = self.generate(output)
        self.assertEqual(len(report["outputs"]), 39)
        self.assertEqual(report["generated_locales"], (*esp.catalog.LOCALES, "cn"))
        self.assertEqual(report["locale_aliases"], {"cn": "zhhant"})
        self.assertEqual(report, self.generate(output, check=True))
        self.assertEqual(plugin.read_bytes(), original_plugin)
        for kind in esp.KINDS:
            self.assertEqual((output / f"Strings/Clipboard_en.{kind}").read_bytes(),
                             (self.directory / f"candidate/Strings/Clipboard_en.{kind}").read_bytes())
        expected_ru = independent_table("STRINGS", [(1, "Пример".encode("utf-8")), (3, b"1\xC2\xB0"), (4, "Пример retour".encode("utf-8"))])
        self.assertEqual((output / "Strings/Clipboard_ru.STRINGS").read_bytes(), expected_ru)
        for locale, label in labels.items():
            with self.subTest(locale=locale):
                self.assertEqual(esp.read_strings(output / f"Strings/Clipboard_{locale}.STRINGS"), {1: label, 3: "1°", 4: label + " retour"})
                self.assertEqual(esp.read_strings(output / f"Strings/Clipboard_{locale}.DLSTRINGS"), {2: label + " <Alias=0>"})
                self.assertEqual((output / f"Strings/Clipboard_{locale}.ILSTRINGS").read_bytes(), bytes(8))
        for kind in esp.KINDS:
            self.assertEqual((output / f"Strings/Clipboard_cn.{kind}").read_bytes(),
                             (output / f"Strings/Clipboard_zhhant.{kind}").read_bytes())

    def test_incomplete_or_stale_translation_never_generates_partial_tables(self):
        self.generation_fixture()
        path = self.directory / "localization/ja.json"
        original = path.read_bytes()
        output = self.directory / "generated"
        for corruption in ("missing_file", "missing_key", "stale_hash"):
            with self.subTest(corruption=corruption):
                path.write_bytes(original)
                if corruption == "missing_file":
                    path.unlink()
                else:
                    rows = json.loads(original)
                    if corruption == "missing_key":
                        rows.pop(0)
                    else:
                        rows[0]["source_sha256"] = "0" * 64
                    path.write_text(json.dumps(rows), encoding="utf-8")
                with self.assertRaises((ValueError, FileNotFoundError)):
                    self.generate(output)
                self.assertFalse(output.exists())

    def test_captured_map_stale_source_fails_before_table_generation(self):
        mapping_path, _ = self.generation_fixture()
        mapping = json.loads(mapping_path.read_bytes())
        mapping["fields"][0]["text"] = "Stale text"
        mapping_path.write_text(json.dumps(mapping), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "differs from its current English source"):
            self.generate(self.directory / "generated")
        self.assertFalse((self.directory / "generated").exists())

    def test_translated_table_or_alias_staleness_is_detected(self):
        self.generation_fixture()
        output = self.directory / "generated"
        self.generate(output)
        for locale in ("pl", "cn"):
            with self.subTest(locale=locale):
                path = output / f"Strings/Clipboard_{locale}.STRINGS"
                original = path.read_bytes()
                path.write_bytes(b"stale")
                with self.assertRaisesRegex(ValueError, "missing or stale"):
                    self.generate(output, check=True)
                path.write_bytes(original)

    def test_independent_russian_string_table(self):
        # Explicit UTF-8 bytes for Cyrillic "Мир" and a degree label.
        path = self.file("Clipboard_ru.STRINGS", independent_table("STRINGS", [(29, b"\xD0\x9C\xD0\xB8\xD1\x80"), (3, b"1\xC2\xB0")]))
        self.assertEqual(esp.read_strings(path), {29: "Мир", 3: "1°"})

    def test_english_codepage_1252_and_no_utf8_guessing(self):
        path = self.file("Clipboard_en.STRINGS", independent_table("STRINGS", [(1, b"1\xB0"), (2, b"\xC2\xB0")]))
        self.assertEqual(esp.read_strings(path), {1: "1°", 2: "Â°"})

    def test_encoder_uses_explicit_english_encoding(self):
        expected_en = independent_table("STRINGS", [(3, b"1\xB0"), (29, b"Rotate")])
        self.assertEqual(esp.table_bytes("STRINGS", {29: "Rotate", 3: "1°"}, encoding="cp1252"), expected_en)
        expected_ru = independent_table("STRINGS", [(3, b"1\xC2\xB0"), (29, b"\xD0\x9C\xD0\xB8\xD1\x80")])
        self.assertEqual(esp.table_bytes("STRINGS", {29: "Мир", 3: "1°"}), expected_ru)

    def test_sized_table_byte_length_includes_terminator(self):
        for kind in ("DLSTRINGS", "ILSTRINGS"):
            with self.subTest(kind=kind):
                expected = independent_table(kind, [(17, b"\xD0\x9C\xD0\xB8\xD1\x80")])
                self.assertEqual(esp.table_bytes(kind, {17: "Мир"}), expected)
                self.assertEqual(esp.read_strings(self.file(f"Clipboard_ru.{kind}", expected)), {17: "Мир"})

    def test_empty_tables_have_valid_eight_byte_header(self):
        for kind in esp.KINDS:
            with self.subTest(kind=kind):
                self.assertEqual(esp.table_bytes(kind, {}), bytes(8))
                self.assertEqual(esp.read_strings(self.file(f"Clipboard_en.{kind}", bytes(8))), {})

    def test_invalid_russian_utf8_rejected(self):
        malformed = independent_table("STRINGS", [(1, b"\xCC\xE8\xF0")])
        with self.assertRaises(UnicodeDecodeError):
            esp.read_strings(self.file("Clipboard_ru.STRINGS", malformed))

    def test_table_structural_corruption_rejected(self):
        good = independent_table("STRINGS", [(2, b"first"), (5, b"second")])
        for name, corrupt in {
            "short-header": good[:7],
            "bad-data-size": good[:4] + struct.pack("<I", 999) + good[8:],
            "zero-id": good[:8] + bytes(4) + good[12:],
            "duplicate-id": good[:16] + struct.pack("<I", 2) + good[20:],
            "offset-outside-payload": good[:12] + struct.pack("<I", 1000) + good[16:],
            "missing-terminator": good[:-1] + b"x",
        }.items():
            with self.subTest(name=name), self.assertRaises((ValueError, struct.error)):
                esp.read_strings(self.file(f"{name}_ru.STRINGS", corrupt))

    def test_sized_string_corruption_rejected(self):
        for raw in (bytes(4), struct.pack("<I", 9) + b"short\0", struct.pack("<I", 4) + b"badX",
                    struct.pack("<I", 5) + b"a\0bc\0"):
            corrupt = struct.pack("<IIII", 1, len(raw), 7, 0) + raw
            with self.subTest(raw=raw), self.assertRaises((ValueError, struct.error)):
                esp.read_strings(self.file("Clipboard_ru.DLSTRINGS", corrupt))

    def test_supported_migration_preserves_full_contract(self):
        original, candidate = self.fixture()
        captured = self.compare(original, candidate)
        self.assertEqual(captured["record_count"], 2)
        self.assertEqual(captured["empty_fields"], 1)
        self.assertEqual([field["string_id"] for field in captured["fields"]], [1, 2, 3, 4])
        self.assertEqual([field["occurrence"] for field in captured["fields"] if field["field"] == "ITXT"], [0, 1])
        self.assertEqual(captured["table_counts"], {"STRINGS": 3, "DLSTRINGS": 1, "ILSTRINGS": 0})

    def test_migration_rejects_script_wiring_changes(self):
        original, candidate = self.fixture(new_subs=lambda values: [(tag, raw + b"change" if tag == "VMAD" else raw) for tag, raw in values])
        with self.assertRaisesRegex(ValueError, "Non-text data changed"):
            self.compare(original, candidate)

    def test_migration_rejects_button_reordering(self):
        def swap_buttons(values):
            values[3], values[4] = values[4], values[3]
            return values
        original, candidate = self.fixture(new_subs=swap_buttons)
        with self.assertRaisesRegex(ValueError, "English text changed"):
            self.compare(original, candidate)

    def test_migration_rejects_gameplay_flags_and_group_change(self):
        for options in ({"new_flags": 0x20}, {"group_label": b"ACTI"}, {"localized": False}):
            with self.subTest(options=options):
                original, candidate = self.fixture(**options)
                with self.assertRaises(ValueError):
                    self.compare(original, candidate)

    def test_migration_rejects_changed_alias_token(self):
        original, candidate = self.fixture()
        self.file("candidate/Strings/Clipboard_en.DLSTRINGS", independent_table("DLSTRINGS", [(2, b"Rotate <Alias=1>")]))
        with self.assertRaisesRegex(ValueError, "English text changed"):
            self.compare(original, candidate)

    def test_migration_rejects_empty_field_nonzero_id(self):
        def change_empty(values):
            values[5] = ("ITXT", struct.pack("<I", 4))
            return values
        original, candidate = self.fixture(new_subs=change_empty)
        with self.assertRaisesRegex(ValueError, "Empty text did not map to ID 0"):
            self.compare(original, candidate)

    def test_read_compressed_record(self):
        fixture = self.file("compressed.esp", record("TES4", 0, []) + record("MESG", 0x1234, [("EDID", b"Compressed\0")], 0x40000))
        records, _ = esp.read_esp(fixture)
        self.assertEqual(records[1]["subrecords"], [("EDID", b"Compressed\0")])

    def test_compressed_record_boundaries_rejected(self):
        good = record("MESG", 0x1234, [("EDID", b"Compressed\0")], 0x40000)
        compressed_size = struct.unpack_from("<I", good, 4)[0]
        variants = {
            "trailing-data": good[:4] + struct.pack("<I", compressed_size + 4) + good[8:] + b"junk",
            "wrong-inflated-size": good[:24] + struct.pack("<I", 999) + good[28:],
            "truncated-stream": good[:4] + struct.pack("<I", compressed_size - 1) + good[8:-1],
            "short-header": good[:4] + struct.pack("<I", 3) + good[8:27],
        }
        for name, variant in variants.items():
            with self.subTest(name=name), self.assertRaises((ValueError, zlib.error)):
                esp.read_esp(self.file(f"{name}.esp", record("TES4", 0, []) + variant))

    def test_reject_dangling_extended_subrecord(self):
        dangling = subrecord("XXXX", struct.pack("<I", 100))
        fixture = self.file("dangling-extended.esp", record("TES4", 0, [], trailing=dangling))
        with self.assertRaises(ValueError):
            esp.read_esp(fixture)

    def test_extended_subrecord_keeps_following_payload(self):
        payload = b"X" * 70000
        extended = subrecord("XXXX", struct.pack("<I", len(payload))) + struct.pack("<4sH", b"DATA", 0) + payload
        fixture = self.file("extended.esp", record("TES4", 0, [], trailing=extended))
        records, _ = esp.read_esp(fixture)
        self.assertEqual(records[0]["subrecords"], [("DATA", payload)])

    def order_fixture(self, order):
        header = record("TES4", 0, [("HEDR", struct.pack("<fII", 0.95, 3, 0x4000))])
        records = {fid: record("MESG", fid, [("EDID", f"Message{fid}".encode("ascii") + b"\0"),
                                            ("VMAD", struct.pack("<I", fid))])
                   for fid in (0x1000, 0x2000, 0x3000)}
        original = self.file("source.esp", header + group(b"".join(records[fid] for fid in (0x2000, 0x3000, 0x1000))))
        localized_header = header[:8] + struct.pack("<I", 0x80) + header[12:]
        candidate = self.file("candidate/Clipboard.esp", localized_header + group(b"".join(records[fid] for fid in order)))
        for kind in esp.KINDS:
            self.file(f"candidate/Strings/Clipboard_en.{kind}", bytes(8))
        return original, candidate

    def test_canonical_formid_sort_preserves_record_identity(self):
        original, candidate = self.order_fixture((0x1000, 0x2000, 0x3000))
        result = self.compare(original, candidate)
        self.assertEqual(result["record_count"], 4)
        self.assertEqual(result["reordered_record_positions"], 3)

    def test_arbitrary_formid_reorder_rejected(self):
        original, candidate = self.order_fixture((0x3000, 0x2000, 0x1000))
        with self.assertRaisesRegex(ValueError, "canonical FormID sorting"):
            self.compare(original, candidate)

    def test_added_or_duplicated_formid_rejected(self):
        original, candidate = self.order_fixture((0x1000, 0x2000, 0x3000))
        raw = candidate.read_bytes()
        # Replace final record FormID; the 24-byte header immediately follows
        # the last MESG marker and FormID is its fourth 32-bit field.
        last_record = raw.rfind(b"MESG")
        for new_id in (0x1000, 0x9999):
            candidate.write_bytes(raw[:last_record + 12] + struct.pack("<I", new_id) + raw[last_record + 16:])
            with self.subTest(new_id=new_id), self.assertRaisesRegex(ValueError, "FormID"):
                self.compare(original, candidate)

    def test_exact_appended_zero_incc_allowed(self):
        original, candidate = self.fixture()
        raw = candidate.read_bytes()
        header_size = struct.unpack_from("<I", raw, 4)[0]
        boundary = 24 + header_size
        incc = subrecord("INCC", bytes(4))
        candidate.write_bytes(raw[:4] + struct.pack("<I", header_size + len(incc)) + raw[8:boundary] + incc + raw[boundary:])
        result = self.compare(original, candidate)
        self.assertEqual(result["xedit_zero_value_normalizations"], [{"form_id": "00000000", "field": "INCC",
                                                                    "before_hex": None, "after_hex": "00000000"}])

    def test_nonzero_or_preprended_incc_rejected(self):
        original, candidate = self.fixture()
        raw = candidate.read_bytes()
        header_size = struct.unpack_from("<I", raw, 4)[0]
        boundary = 24 + header_size
        for insertion, value in ((boundary, 1), (24, 0)):
            incc = subrecord("INCC", struct.pack("<I", value))
            candidate.write_bytes(raw[:4] + struct.pack("<I", header_size + len(incc)) + raw[8:insertion] + incc + raw[insertion:])
            with self.subTest(insertion=insertion, value=value), self.assertRaisesRegex(ValueError, "Subrecord topology"):
                self.compare(original, candidate)

    def test_empty_magic_effect_description_externalizes_to_zero(self):
        original = self.file("source.esp", record("TES4", 0, []) +
                             group(record("MGEF", 0x0100A7B0, [("EDID", b"EmptyEffect\0"), ("DNAM", b"\0")]), b"MGEF"))
        candidate = self.file("candidate/Clipboard.esp", record("TES4", 0, [], 0x80) +
                              group(record("MGEF", 0x0100A7B0, [("EDID", b"EmptyEffect\0"), ("DNAM", bytes(4))]), b"MGEF"))
        for kind in esp.KINDS:
            self.file(f"candidate/Strings/Clipboard_en.{kind}", bytes(8))
        result = self.compare(original, candidate)
        self.assertEqual(result["empty_fields"], 1)
        self.assertEqual(result["fields"], [])
        self.assertEqual(result["xedit_zero_value_normalizations"], [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
