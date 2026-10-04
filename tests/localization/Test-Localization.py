#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host tests for localization migration/translation data integrity; no API calls."""
import importlib.util
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True


def module(filename, name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).resolve().parents[2] / "tools/localization" / filename)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


client = module("Translate-Localization.py", "translate_localization")
catalog = client.catalog
extractor = module("Extract-LocalizationDocs.py", "extract_localization_docs")


class LocalizationTests(unittest.TestCase):
    def source(self, text="Selected {0} objects.", key="$Clipboard_Test"):
        return {"key": key, "text": text, "context": "A player notification", "surface": "papyrus"}

    def test_translation_preserves_placeholders_and_markup(self):
        source = self.source("<Alias=Slot1State>: {0} of {1}; {0}; %.0f")
        catalog.validate_translation(source, "<Alias=Slot1State>: {1} из {0}; {0}; %.0f")
        for text in ("<Alias=Slot2State>: {0} of {1}; {0}; %.0f",
                     "<Alias=Slot1State>: {0} of {1}; %.0f",
                     "<Alias=Slot1State>: {0} of {1}; {0}; %.1f"):
            with self.subTest(text=text), self.assertRaises(catalog.CatalogError):
                catalog.validate_translation(source, text)

    def test_invalid_templates_are_rejected(self):
        for text in ("{6}", "{01}", "{", "}", "{value}"):
            with self.subTest(text=text), self.assertRaises(catalog.CatalogError):
                catalog.placeholders(text)
        self.assertEqual(catalog.placeholders("{{literal}} {5}"), {"{5}": 1})

    def test_literal_numeric_values_units_signs_and_ranges_cannot_change(self):
        for original, changed in (("Limit: 9,999", "Limit: 9,998"), ("5%", "50%"),
                                  ("5%", "5"), ("5°", "5%"), ("1-100", "100-1"),
                                  ("1-100", "1 100"), ("1-100", "1+100"),
                                  ("Offset -2.5", "Offset 2.5"), ("Offset +2.5", "Offset -2.5"),
                                  ("Offset 2.5", "Offset 25"), ("Scale 1.25", "Scale 1.025"),
                                  ("Only {0} objects", "Only {0} objects, 2 remaining"),
                                  ("Literal {{0}}", "Literal {{1}}"),
                                  ("NEXT: {0}-{1}", "NEXT: {1}-{0}"),
                                  ("NEXT: {0}-{1}", "NEXT: {0}+{1}")):
            with self.subTest(original=original, changed=changed), self.assertRaises(catalog.CatalogError):
                catalog.validate_translation(self.source(original), changed)
        catalog.validate_translation(self.source("Range 1-100"), "Диапазон 1–100")
        catalog.validate_translation(self.source("Offset -2.5"), "Смещение −2,5")
        catalog.validate_translation(self.source("5% and 10°"), "5 % и 10 °")
        catalog.validate_translation(self.source("5% and 10°"), "5 % et 10 °")
        catalog.validate_translation(self.source("NEXT: {0}-{1}"), "次へ: {0}～{1}")
        catalog.validate_translation(self.source("Showing %.0f-%.0f"), "Показаны %.0f–%.0f")
        catalog.validate_translation(self.source("Showing %.0f-%.0f"), "Affichage de %.0f à %.0f")
        catalog.validate_translation(self.source("Slots 1-100"), "Emplacements de 1 à 100")
        catalog.validate_translation(self.source("NEXT: {0}-{1}"), "SUIVANT : de {0} à {1}")
        with self.assertRaises(catalog.CatalogError):
            catalog.validate_translation(self.source("NEXT: {0}-{1}"), "SUIVANT : de {1} à {0}")
        with self.assertRaises(catalog.CatalogError):
            catalog.validate_translation(self.source("Slots 1-100"), "Emplacements de 100 à 1")

    def test_thousands_and_decimal_localization_preserves_numeric_value(self):
        for translated in ("9999", "9.999", "9,999", "9 999", "9\u00a0999", "9\u202f999"):
            with self.subTest(translated=translated):
                catalog.validate_translation(self.source("9,999"), translated)
        for translated in ("9,99", "99.99", "99 99", "9.9990", "9,998"):
            with self.subTest(translated=translated), self.assertRaises(catalog.CatalogError):
                catalog.validate_translation(self.source("9,999"), translated)
        for translated in ("1234,5", "1.234,5", "1 234,50", "1,234.50"):
            with self.subTest(translated=translated):
                catalog.validate_translation(self.source("1,234.5"), translated)
        with self.assertRaises(catalog.CatalogError):
            catalog.validate_translation(self.source("1"), "1.000")

    def test_contextual_percent_unit_can_clarify_percentage_prompt(self):
        source = self.source("Enter a percentage between 1 and 99:")
        catalog.validate_translation(source, "1～99%の割合を入力してください:")
        catalog.validate_translation(source, "1%～99%の割合を入力してください:")
        with self.assertRaises(catalog.CatalogError):
            catalog.validate_translation(self.source("Enter a distance between 1 and 99:"), "1～99%:")

    def test_automatic_paths_products_and_hotkeys_are_protected(self):
        for original, changed in (("Review Clipboard.log", "Review Different.log"),
                                  (r"Log: Documents\My Games\Fallout4\F4SE\Clipboard.log", r"Log: Documents\My Games\Fallout4\F4SE\Different.log"),
                                  ("Clipboard selection", "Zwischenablage selection"),
                                  ("Show menu. Suggested Key: P", "Menü zeigen. Taste: K"),
                                  ("Clear selection. Suggested Key: [", "清除选择。建议按键：［"),
                                  ("Select objects. Suggested: '", "Auswahl. Taste: ;")):
            with self.subTest(original=original, changed=changed), self.assertRaises(catalog.CatalogError):
                catalog.validate_translation(self.source(original), changed)
        catalog.validate_translation(self.source("Show menu. Suggested Key: P"), "メニューを表示。推奨キー：P")
        catalog.validate_translation(self.source("Select objects. Suggested: '"), "オブジェクトを選択。推奨キー：「'」")
        catalog.validate_translation(self.source("Clipboard Tool"), "Clipboard-Werkzeug")
        original = self.source(r"Native logging uses Documents\My Games\Fallout4\F4SE\Clipboard.log")
        catalog.validate_translation(original, r"Журнал: Documents\My Games\Fallout4\F4SE\Clipboard.log")
        self.assertNotIn("Native logging uses Documents", catalog.protected_tokens(original))

    def test_encoding_and_tsv_escaping(self):
        text = "Русский\\path\nnext\tvalue\r"
        data = catalog.runtime_bytes({"$Clipboard_Test": text})
        self.assertFalse(data.startswith(b"\xef\xbb\xbf"))
        self.assertEqual(data.decode("utf-8"), "$Clipboard_Test\tРусский\\\\path\\nnext\\tvalue\\r\n")
        ui = catalog.interface_bytes({"$Clipboard_Test": "Русский"})
        self.assertEqual(ui[:2], b"\xff\xfe")
        self.assertEqual(ui[2:].decode("utf-16-le"), "$Clipboard_Test\tРусский\r\n")
        with self.assertRaises(catalog.CatalogError):
            catalog.interface_bytes({"$Clipboard_Test": text})

    def test_latin_locale_script_rejects_foreign_letters(self):
        source = self.source("Some objects may already exist; wiring and power were skipped.")
        for locale in catalog.LATIN_LOCALES:
            with self.subTest(locale=locale), self.assertRaisesRegex(catalog.CatalogError, "Unexpected non-Latin letters"):
                catalog.validate_locale_script(locale, source,
                    "Alguns objetos შესაძლოა já existam; fiação e energia foram ignoradas.")
        for text in ("Clipboard: 1ª e 2º, ação, usuário, conexão.",
                     "Éléments, größer, łódź, déjà, citta\u0300."):
            catalog.validate_locale_script("ptbr", source, text)
        for locale, text in (("ru", "Объекты"), ("ja", "オブジェクト"),
                             ("zhhans", "对象"), ("zhhant", "物件")):
            catalog.validate_locale_script(locale, source, text)

    def test_latin_locale_preserves_bounded_source_specific_foreign_letters(self):
        source = self.source("Rotation symbol α")
        catalog.validate_locale_script("it", source, "Simbolo di rotazione α")
        for text in ("Simbolo β", "Simboli αα"):
            with self.subTest(text=text), self.assertRaisesRegex(catalog.CatalogError, "Unexpected non-Latin letters"):
                catalog.validate_locale_script("it", source, text)

    def test_f4se_ui_line_buffer_boundary_counts_utf16_units(self):
        key = "$Clipboard_Test"
        available = 511 - len(key) - 1 - 2
        catalog.interface_bytes({key: "a" * available})
        with self.assertRaises(catalog.CatalogError):
            catalog.interface_bytes({key: "a" * (available + 1)})
        with self.assertRaises(catalog.CatalogError):
            catalog.interface_bytes({key: "a" * (available - 1) + "\U0001f642"})

    def test_strict_json_rejects_duplicate_properties(self):
        with self.assertRaises(catalog.CatalogError):
            json.loads('{"key":"one","key":"two"}', object_pairs_hook=catalog.strict_pairs)

    def test_mcm_localization_cannot_change_actions(self):
        source = self.source("Show Quick Menu")
        source["surface"] = "mcm"
        sources = {source["key"]: source}
        good = {"content": [{"text": source["key"], "action": {"function": "OnHotkey"}}]}
        restored = catalog.replace_keys(good, sources, set())
        self.assertEqual(restored["content"][0]["text"], source["text"])
        bad = {"action": {"function": source["key"]}}
        with self.assertRaises(catalog.CatalogError):
            catalog.replace_keys(bad, sources, set())

    def test_mcm_html_header_does_not_translate_markup(self):
        html = "<p align='center'><font size='28'>Clipboard</font></p>"
        source = {**self.source(html), "surface": "mcm"}
        sources = {source["key"]: source}
        broken = {"content": [{"type": "text", "html": True, "text": source["key"]}]}
        with self.assertRaisesRegex(catalog.CatalogError, "MCM HTML"):
            catalog.replace_keys(broken, sources, set())
        fixed = {"content": [{"type": "text", "html": True, "text": html}]}
        self.assertEqual(catalog.replace_keys(fixed, {}, set()), fixed)

    def test_resume_skips_stale_english_and_rejects_duplicates(self):
        source = self.source()
        sources = {source["key"]: source}
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "ru.json"
            row = {"key": source["key"], "text": "Выбрано объектов: {0}.", "source_sha256": catalog.text_hash(source["text"])}
            path.write_text(json.dumps([row]), encoding="utf-8")
            self.assertEqual(len(client.previous_translations(path, sources)[0]), 1)
            changed = {source["key"]: {**source, "text": "Current selected objects: {0}."}}
            self.assertEqual(client.previous_translations(path, changed), ({}, 1))
            path.write_text(json.dumps([row, row]), encoding="utf-8")
            with self.assertRaises(catalog.CatalogError):
                client.previous_translations(path, sources)

    def test_api_response_missing_extra_or_duplicate_keys_rejected(self):
        source = self.source()
        row = {"key": source["key"], "text": "Выбрано объектов: {0}."}
        def response(rows, finish="stop"):
            return {"choices": [{"finish_reason": finish, "message": {"content": json.dumps({"translations": rows})}}]}
        self.assertIn(source["key"], client.parse_response(response([row]), [source]))
        for reply in (response([]), response([row, row]), response([{**row, "key": "$Clipboard_Unknown"}]), response([row], "length")):
            with self.assertRaises(catalog.CatalogError):
                client.parse_response(reply, [source])

    def test_generation_uses_every_locales_own_runtime_and_mcm_text(self):
        source = self.source("Dialogs")
        source["surface"] = "mcm"
        values = dict(zip(catalog.TRANSLATED_LOCALES,
                          ("Диалоги", "Dialoge", "Diálogos", "Diálogos de México", "Dialogues", "Dialoghi",
                           "ダイアログ", "Dialogi", "Diálogos brasileiros", "对话", "對話")))
        translations = {locale: {source["key"]: {"text": text}} for locale, text in values.items()}
        outputs = catalog.expected_outputs({source["key"]: source}, translations)
        self.assertEqual(len(outputs), 25)
        english = outputs["Interface/Translations/Clipboard_en.txt"]
        # Fixed EN/RU bytes preserve the original supported-language format.
        self.assertEqual(english, b"\xff\xfe" + "$Clipboard_Test\tDialogs\r\n".encode("utf-16-le"))
        self.assertEqual(outputs["F4SE/Plugins/Clipboard/Localization/en.tsv"], b"$Clipboard_Test\tDialogs\n")
        self.assertEqual(outputs["F4SE/Plugins/Clipboard/Localization/ru.tsv"], "$Clipboard_Test\tДиалоги\n".encode("utf-8"))
        self.assertEqual(outputs["Interface/Translations/Clipboard_ru.txt"], b"\xff\xfe" + "$Clipboard_Test\tДиалоги\r\n".encode("utf-16-le"))
        for locale, text in values.items():
            with self.subTest(locale=locale):
                self.assertEqual(outputs[f"F4SE/Plugins/Clipboard/Localization/{locale}.tsv"].decode("utf-8"),
                                 f"$Clipboard_Test\t{text}\n")
                self.assertEqual(outputs[f"Interface/Translations/Clipboard_{locale}.txt"].decode("utf-16"),
                                 f"$Clipboard_Test\t{text}\r\n")
                self.assertNotEqual(outputs[f"Interface/Translations/Clipboard_{locale}.txt"], english)
        self.assertEqual(outputs["Interface/Translations/Clipboard_cn.txt"], outputs["Interface/Translations/Clipboard_zhhant.txt"])
        self.assertNotIn("F4SE/Plugins/Clipboard/Localization/cn.tsv", outputs)
        translations.pop("ja")
        with self.assertRaisesRegex(catalog.CatalogError, "every supported translated locale"):
            catalog.expected_outputs({source["key"]: source}, translations)

    def test_complete_translation_coverage_required(self):
        source = self.source()
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "ru.json"
            path.write_text("[]", encoding="utf-8")
            with self.assertRaises(catalog.CatalogError):
                catalog.load_translations(path, {source["key"]: source})
            self.assertEqual(catalog.load_translations(path, {source["key"]: source}, allow_partial=True), {})

    def test_usage_includes_unaccepted_billable_responses(self):
        report = {"runs": [{"requests": [{"accepted": False, "usage": {"prompt_tokens": 3, "completion_tokens": 4, "total_tokens": 7}}, {"accepted": True, "usage": {"prompt_tokens": 5, "completion_tokens": 6, "total_tokens": 11}}]}]}
        self.assertEqual(client.CounterUsage(report)["total_tokens"], 18)

    def catalog_fixture(self, root):
        def write(path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
        rows = []
        for surface in catalog.SOURCES:
            key = "$Clipboard_Docs_ReadMe_001" if surface == "docs" else "$Clipboard_" + surface
            row = {**self.source("Dialogs" if surface == "mcm" else "Objects: {0}", key), "surface": surface}
            rows.append(row)
            write(root / "localization/source" / f"{surface}.en.json", [row])
        english_doc = root / "docs/user/en/Clipboard-ReadMe.txt"
        english_doc.parent.mkdir(parents=True, exist_ok=True)
        english_doc.write_bytes(b"Objects: {0}\r\n")
        write(root / "localization/metadata/docs-layout.json", {
            "schema": 2, "translation_status": "ready", "enabled_locales": ["ru"],
            "retained_translation_keys": {"ru": ["$Clipboard_Docs_ReadMe_001"]},
            "documents": [{"source": english_doc.relative_to(root).as_posix(),
                           "source_sha256": catalog.sha256(english_doc.read_bytes()),
                           "filename": "Clipboard-ReadMe.txt",
                           "parts": [{"key": "$Clipboard_Docs_ReadMe_001"}, {"literal": "\r\n"}]}]})
        for locale in catalog.TRANSLATED_LOCALES:
            labels = ("Dialog fixture", "Object fixture: {0}") if locale in catalog.LATIN_LOCALES else ("Диалоги", "Объекты: {0}")
            translations = [{"key": row["key"], "text": labels[0] if row["key"].endswith("mcm") else labels[1], "source_sha256": catalog.text_hash(row["text"])} for row in rows if locale == "ru" or row["surface"] != "docs"]
            write(root / "localization/translations" / f"{locale}.json", translations)
        for filename in ("config.json", "keybinds.json"):
            baseline = {"text": "Dialogs", "action": {"params": ["Clipboard", "{value}"]}}
            write(root / "tests/fixtures/mcm/MCM/Config/Clipboard" / filename, baseline)
            write(root / "assets/MCM/Config/Clipboard" / filename, {**baseline, "text": "$Clipboard_mcm"})

    def test_complete_build_is_repeatable_and_detects_stale_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            output = root / "candidate"
            first = catalog.build(root, output)
            self.assertEqual(first, catalog.build(root, output, check=True))
            self.assertEqual(first["translated_locales"], list(catalog.TRANSLATED_LOCALES))
            self.assertEqual(first["fallback_locales"], [])
            self.assertEqual(first["locale_aliases"], {"cn": "zhhant"})
            self.assertEqual(len(first["outputs"]), 26)
            self.assertTrue(all(f"localization/translations/{locale}.json" in first["inputs"] for locale in catalog.TRANSLATED_LOCALES))
            self.assertNotIn(b"$Clipboard_Docs_", (output / "F4SE/Plugins/Clipboard/Localization/en.tsv").read_bytes())
            self.assertEqual((output / "Docs/clipboard/ru/Clipboard-ReadMe.txt").read_text(encoding="utf-8-sig"), "Объекты: {0}\n")
            target = output / "Interface/Translations/Clipboard_zhhant.txt"
            target.write_bytes(b"stale")
            with self.assertRaises(catalog.CatalogError):
                catalog.build(root, output, check=True)

    def test_missing_additional_locale_or_key_fails_before_writing(self):
        for corruption in ("missing_file", "missing_key", "stale_source_hash", "extra_document"):
            with self.subTest(corruption=corruption), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                self.catalog_fixture(root)
                path = root / "localization/translations/ja.json"
                rows = catalog.read_json(path)
                if corruption == "missing_file":
                    path.unlink()
                else:
                    if corruption == "missing_key":
                        rows.pop()
                    elif corruption == "stale_source_hash":
                        rows[0]["source_sha256"] = "0" * 64
                    else:
                        rows.append(next(row for row in catalog.read_json(root / "localization/translations/ru.json") if row["key"] == "$Clipboard_Docs_ReadMe_001"))
                    path.write_text(json.dumps(rows), encoding="utf-8")
                output = root / "candidate"
                with self.assertRaises((catalog.CatalogError, FileNotFoundError)):
                    catalog.build(root, output)
                self.assertFalse(output.exists())

    def test_foreign_script_fails_before_package_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            path = root / "localization/translations/ptbr.json"
            rows = catalog.read_json(path)
            rows[0]["text"] += " შესაძლოა"
            path.write_text(json.dumps(rows), encoding="utf-8")
            output = root / "candidate"
            with self.assertRaisesRegex(catalog.CatalogError, "Unexpected non-Latin letters for ptbr"):
                catalog.build(root, output)
            self.assertFalse(output.exists())

    def test_extra_runtime_locale_and_missing_native_output_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            output = root / "candidate"
            catalog.build(root, output)
            runtime = output / "F4SE/Plugins/Clipboard/Localization"
            extra = runtime / "xx.tsv"
            extra.write_bytes(b"unexpected")
            with self.assertRaisesRegex(catalog.CatalogError, "Unexpected localization package files"):
                catalog.build(root, output, check=True)
            extra.unlink()
            (runtime / "pl.tsv").unlink()
            with self.assertRaisesRegex(catalog.CatalogError, "missing or stale"):
                catalog.build(root, output, check=True)

    def test_document_scope_requires_explicit_locale_enablement(self):
        sources = {"$Clipboard_Test": self.source(), "$Clipboard_Doc": {**self.source(key="$Clipboard_Doc"), "surface": "docs"}}
        for locale in catalog.TRANSLATED_LOCALES:
            self.assertEqual(catalog.translation_sources(sources, locale, document_locales=[locale]), sources)
            self.assertEqual(set(catalog.translation_sources(sources, locale, document_locales=[])), {"$Clipboard_Test"})
        with self.assertRaisesRegex(catalog.CatalogError, "Unsupported translation locale"):
            catalog.translation_sources(sources, "xx")

    def test_pending_english_review_preserves_old_translations_without_generating_docs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            path = root / "localization/metadata/docs-layout.json"
            layout = catalog.read_json(path)
            layout.update(translation_status="pending", enabled_locales=[])
            path.write_text(json.dumps(layout), encoding="utf-8")
            english = root / layout["documents"][0]["source"]
            english.write_bytes(b"Revised English awaiting review.\n")
            russian = root / "localization/translations/ru.json"
            rows = catalog.read_json(russian)
            next(row for row in rows if row["key"] == "$Clipboard_Docs_ReadMe_001")["source_sha256"] = "0" * 64
            russian.write_text(json.dumps(rows), encoding="utf-8")
            before = russian.read_bytes()
            output = root / "candidate"
            report = catalog.build(root, output)
            self.assertEqual(report, catalog.build(root, output, check=True))
            self.assertEqual(len(report["outputs"]), 25)
            self.assertEqual(report["documentation"]["translation_status"], "pending")
            self.assertFalse((output / "Docs").exists())
            self.assertFalse((output / "Clipboard-ReadMe-Russian.txt").exists())
            self.assertEqual(before, russian.read_bytes())
            rows[0]["source_sha256"] = "0" * 64
            russian.write_text(json.dumps(rows), encoding="utf-8")
            with self.assertRaisesRegex(catalog.CatalogError, "Stale English source hash"):
                catalog.build(root, output, check=True)

    def test_ready_docs_require_current_complete_translation_and_use_locale_folder(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            path = root / "localization/metadata/docs-layout.json"
            layout = catalog.read_json(path)
            layout["enabled_locales"].append("ja")
            path.write_text(json.dumps(layout), encoding="utf-8")
            with self.assertRaisesRegex(catalog.CatalogError, "Missing 1 translation keys"):
                catalog.build(root, root / "candidate")
            japanese = root / "localization/translations/ja.json"
            rows = catalog.read_json(japanese)
            rows.append({"key": "$Clipboard_Docs_ReadMe_001", "text": "Object fixture: {0}",
                         "source_sha256": catalog.text_hash("Objects: {0}")})
            japanese.write_text(json.dumps(rows), encoding="utf-8")
            report = catalog.build(root, root / "candidate")
            self.assertIn("Docs/clipboard/ja/Clipboard-ReadMe.txt", report["outputs"])
            english = root / layout["documents"][0]["source"]
            english.write_bytes(b"Changed after translation.\n")
            with self.assertRaisesRegex(catalog.CatalogError, "English documentation changed"):
                catalog.build(root, root / "candidate", check=True)

    def test_pending_docs_cannot_enable_languages_or_change_deployment_path(self):
        for change in ({"translation_status": "pending"},
                       {"documents": [{"source": "assets/current/Clipboard-ReadMe.txt", "filename": "Clipboard-ReadMe.txt"}]}):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                self.catalog_fixture(root)
                path = root / "localization/metadata/docs-layout.json"
                layout = catalog.read_json(path)
                layout.update(change)
                path.write_text(json.dumps(layout), encoding="utf-8")
                with self.assertRaises(catalog.CatalogError):
                    catalog.build(root, root / "candidate")

    def test_localized_document_names_and_references_preserve_source_catalogs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            path = root / "localization/metadata/docs-layout.json"
            layout = catalog.read_json(path)
            names = {"Clipboard-ReadMe.txt": "Руководство.txt",
                     "Clipboard-Object-Filtering.txt": "Документ-Clipboard-Localization.txt",
                     "Clipboard-Localization.txt": "Локализация.txt",
                     "CHANGELOG.txt": "Изменения.txt"}
            english_text = "Read " + ", ".join(names) + ".\n"
            localized_text = ("См. " + ", ".join(names) + ".\n"
                              "../Clipboard-ReadMe.txt; Clipboard-ReadMe.txt.bak; "
                              "Old-Clipboard-ReadMe.txt; ../LICENSE.txt; ../Licenses\n"
                              "参阅Clipboard-ReadMe.txt了解详情。\n")
            layout["documents"] = []
            sources, translations = {}, {"ru": {}}
            for index, (canonical, translated) in enumerate(names.items()):
                key = f"$Clipboard_Docs_Test_{index}"
                source = root / "docs/user/en" / canonical
                source.write_bytes(english_text.encode("utf-8"))
                layout["documents"].append({"filename": canonical,
                    "localized_filenames": {"ru": translated},
                    "source": source.relative_to(root).as_posix(),
                    "source_sha256": catalog.sha256(source.read_bytes()), "parts": [{"key": key}]})
                sources[key] = {"surface": "docs", "text": english_text}
                translations["ru"][key] = {"text": localized_text}
            path.write_text(json.dumps(layout, ensure_ascii=False), encoding="utf-8")
            before = json.dumps([sources, translations], ensure_ascii=False)
            outputs, inputs = catalog.document_outputs(root, sources, translations)
            expected_text = ("См. " + ", ".join(names.values()) + ".\n"
                             "../Руководство.txt; Clipboard-ReadMe.txt.bak; "
                             "Old-Clipboard-ReadMe.txt; ../LICENSE.txt; ../Licenses\n"
                             "参阅Руководство.txt了解详情。\n")
            self.assertEqual(set(outputs), {f"Docs/clipboard/ru/{name}" for name in names.values()})
            self.assertTrue(all(data.decode("utf-8-sig") == expected_text for data in outputs.values()))
            self.assertEqual(json.dumps([sources, translations], ensure_ascii=False), before)
            for document in layout["documents"]:
                source = root / document["source"]
                self.assertEqual(source.read_bytes(), english_text.encode("utf-8"))
                self.assertEqual(inputs[document["source"]], document["source_sha256"])
                self.assertEqual(catalog.document_filename(document, "en"), document["filename"])

    def test_localized_document_filename_mapping_requires_exact_enabled_locales(self):
        for mapping in (None, [], {}, {"ru": "Руководство.txt", "ja": "説明書.txt"},
                        {"ja": "説明書.txt"}):
            with self.subTest(mapping=mapping), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                self.catalog_fixture(root)
                path = root / "localization/metadata/docs-layout.json"
                layout = catalog.read_json(path)
                layout["documents"][0]["localized_filenames"] = mapping
                path.write_text(json.dumps(layout), encoding="utf-8")
                with self.assertRaisesRegex(catalog.CatalogError, "must match enabled locales"):
                    catalog.build(root, root / "candidate")
                self.assertFalse((root / "candidate").exists())

    def test_localized_document_filenames_reject_unsafe_windows_names(self):
        invalid = (None, 123, "", ".txt", "ReadMe.md", "../ReadMe.txt", r"dir\ReadMe.txt",
                   "C:ReadMe.txt", "Read<Me.txt", "Read>Me.txt", 'Read"Me.txt',
                   "Read|Me.txt", "Read?Me.txt", "Read*Me.txt", "ReadMe.txt.",
                   "ReadMe.txt ", "Read\x00Me.txt", "Read\nMe.txt", "Read\u202eMe.txt",
                   "Read\ud800Me.txt", "Le\u0301ame.txt", "CON.txt", "nul.txt",
                   "LPT1.txt", "COM9.txt", "COM¹.txt", "CON.extra.txt", "CON .txt",
                   "CONIN$.txt", "x" * 252 + ".txt")
        for filename in invalid:
            with self.subTest(filename=filename), self.assertRaises(catalog.CatalogError):
                catalog.validate_document_filename(filename)
        for filename in ("Руководство.txt", "お読みください.txt", "说明.txt", "Léame.txt",
                         "Journal des modifications.txt", "COM10.txt"):
            with self.subTest(filename=filename):
                catalog.validate_document_filename(filename)

    def test_localized_document_filenames_reject_casefold_and_canonical_collisions(self):
        for first, second in (("Leame.txt", "LEAME.txt"),
                              ("LÉAME.txt", "léame.txt"),
                              ("CHANGELOG.txt", "Изменения.txt"),
                              ("Руководство.txt", "clipboard-readme.txt"),
                              ("CHANGELOG.txt", None)):
            with self.subTest(first=first, second=second), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                self.catalog_fixture(root)
                path = root / "localization/metadata/docs-layout.json"
                layout = catalog.read_json(path)
                layout["documents"][0]["localized_filenames"] = {"ru": first}
                other = {"filename": "CHANGELOG.txt", "source": "docs/user/en/CHANGELOG.txt"}
                if second is not None:
                    other["localized_filenames"] = {"ru": second}
                layout["documents"].append(other)
                path.write_text(json.dumps(layout), encoding="utf-8")
                with self.assertRaisesRegex(catalog.CatalogError, "Duplicate or ambiguous"):
                    catalog.load_document_layout(root)

    def test_pending_documents_accept_empty_filename_mapping_and_legacy_fallback(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            path = root / "localization/metadata/docs-layout.json"
            layout = catalog.read_json(path)
            document = layout["documents"][0]
            self.assertEqual(catalog.document_filename(document, "ru"), "Clipboard-ReadMe.txt")
            document["localized_filenames"] = {}
            layout.update(translation_status="pending", enabled_locales=[])
            path.write_text(json.dumps(layout), encoding="utf-8")
            report = catalog.build(root, root / "candidate")
            self.assertEqual(report["documentation"]["localized_paths"], [])
            self.assertFalse((root / "candidate/Docs").exists())

    def test_english_extraction_preserves_translation_pause_and_catalog_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            path = root / "localization/metadata/docs-layout.json"
            layout = catalog.read_json(path)
            layout.update(translation_status="pending", enabled_locales=[])
            path.write_text(json.dumps(layout), encoding="utf-8")
            for _, filename in extractor.DOCUMENTS:
                (root / "docs/user/en" / filename).write_text(
                    f"{filename}\n\nEnglish review paragraph.\n", encoding="utf-8")
            russian = root / "localization/translations/ru.json"
            before = russian.read_bytes()
            previous_root = extractor.ROOT
            try:
                extractor.ROOT = root
                with contextlib.redirect_stdout(io.StringIO()):
                    extractor.main()
            finally:
                extractor.ROOT = previous_root
            refreshed = catalog.read_json(path)
            self.assertEqual(refreshed["translation_status"], "pending")
            self.assertEqual(refreshed["enabled_locales"], [])
            self.assertEqual(len(refreshed["documents"]), 4)
            self.assertIn("CHANGELOG.txt", {row["filename"] for row in refreshed["documents"]})
            self.assertEqual(before, russian.read_bytes())
            self.assertEqual(len(catalog.build(root, root / "candidate")["outputs"]), 25)

    def test_english_extraction_preserves_named_and_numeric_keys(self):
        prefix = "$Clipboard_Docs_ReadMe_"
        for numbered in (False, True):
            with self.subTest(numbered=numbered), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                self.catalog_fixture(root)
                previous = [{"key": prefix + "OwnedInput", "text": "Unchanged named paragraph."}]
                if numbered:
                    previous.extend([
                        {"key": prefix + "001", "text": "Unchanged first paragraph."},
                        {"key": prefix + "041", "text": "Unchanged later paragraph."},
                    ])
                source_path = root / "localization/source/docs.en.json"
                source_path.write_text(json.dumps(previous), encoding="utf-8")
                paragraphs = ["New opening paragraph."] + [row["text"] for row in previous]
                for _, filename in extractor.DOCUMENTS:
                    text = "\n\n".join(paragraphs) if filename == "Clipboard-ReadMe.txt" else filename
                    (root / "docs/user/en" / filename).write_text(text + "\n", encoding="utf-8")
                previous_root = extractor.ROOT
                try:
                    extractor.ROOT = root
                    with contextlib.redirect_stdout(io.StringIO()):
                        extractor.main()
                    refreshed = {row["text"]: row["key"] for row in catalog.read_json(source_path)
                                 if row["key"].startswith(prefix)}
                    for row in previous:
                        self.assertEqual(refreshed[row["text"]], row["key"])
                    # Reserved 001 must remain with its unchanged paragraph;
                    # allocation must advance beyond the existing numeric max.
                    self.assertEqual(refreshed[paragraphs[0]], prefix + ("042" if numbered else "001"))
                    layout_path = root / "localization/metadata/docs-layout.json"
                    before = (source_path.read_bytes(), layout_path.read_bytes())
                    with contextlib.redirect_stdout(io.StringIO()):
                        extractor.main()
                    self.assertEqual(before, (source_path.read_bytes(), layout_path.read_bytes()))
                finally:
                    extractor.ROOT = previous_root

    def test_english_extraction_preserves_localized_filenames(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.catalog_fixture(root)
            path = root / "localization/metadata/docs-layout.json"
            layout = catalog.read_json(path)
            layout["documents"][0]["localized_filenames"] = {"ru": "Руководство.txt"}
            path.write_text(json.dumps(layout), encoding="utf-8")
            for _, filename in extractor.DOCUMENTS:
                (root / "docs/user/en" / filename).write_text(
                    f"{filename}\n\nEnglish paragraph.\n", encoding="utf-8")
            previous_root = extractor.ROOT
            try:
                extractor.ROOT = root
                with contextlib.redirect_stdout(io.StringIO()):
                    extractor.main()
            finally:
                extractor.ROOT = previous_root
            refreshed = catalog.load_document_layout(root)
            self.assertEqual(refreshed["enabled_locales"], ["ru"])
            readme = next(row for row in refreshed["documents"] if row["filename"] == "Clipboard-ReadMe.txt")
            self.assertEqual(readme["localized_filenames"], {"ru": "Руководство.txt"})

    def test_shared_id_group_uses_one_translation_without_changing_field_keys(self):
        first = self.source("Same text", "$Clipboard_First")
        second = {**first, "key": "$Clipboard_Second", "translation_group": first["key"]}
        sources = {first["key"]: first, second["key"]: second}
        translations = {first["key"]: {"key": first["key"], "text": "Одинаковый текст", "source_sha256": catalog.text_hash(first["text"])}}
        client.expand_groups(sources, translations, client.translation_groups(sources))
        self.assertEqual(translations[first["key"]]["text"], translations[second["key"]]["text"])
        translations[second["key"]]["text"] = "Другой текст"
        with self.assertRaises(catalog.CatalogError):
            client.expand_groups(sources, translations, client.translation_groups(sources))

    def test_document_protected_identifiers_are_enforced(self):
        row = self.source("Use Clipboard.log and Motion_Keyframed.")
        row["protected"] = ["Clipboard.log", "Motion_Keyframed"]
        catalog.validate_translation(row, "Используйте Clipboard.log и Motion_Keyframed.")
        with self.assertRaises(catalog.CatalogError):
            catalog.validate_translation(row, "Используйте Clipboard.log и ключевые кадры.")

    def test_document_extraction_protects_paths_codes_names_and_credits(self):
        tokens = (r"Data\F4SE\Plugins\Clipboard\<slot>\pattern.ini", "Docs/clipboard",
                  "[OptionalFiltering]", "cn", "zhhans", "zhhant", "SS2", "Auto Beds",
                  "Sim Settlements", "Seasons", "Struckur", "Everett C Sands", "WolfMark",
                  "Big&Flabby")
        for token in tokens:
            original = "Documentation refers to " + token + "."
            row = {**self.source(original), "surface": "docs",
                   "protected": extractor.PROTECTED.findall(original)}
            with self.subTest(token=token):
                self.assertIn(token, row["protected"])
                catalog.validate_translation(row, "Документация ссылается на " + token + ".")
                with self.assertRaises(catalog.CatalogError):
                    catalog.validate_translation(row, "Документация ссылается на другой элемент.")
        self.assertEqual(extractor.PROTECTED.findall("acne and zhhantsuffix"), [])

    def test_document_extraction_allows_regional_numeric_grouping(self):
        original = "500 slots; slots 1 through 10; 5,000 units; 128 objects."
        row = {**self.source(original), "surface": "docs",
               "protected": extractor.PROTECTED.findall(original)}
        self.assertEqual(row["protected"], [])
        catalog.validate_translation(row, "500 emplacements ; emplacements 1 à 10 ; 5 000 unités ; 128 objets.")
        self.assertIn("3.0.0", extractor.PROTECTED.findall("Clipboard Resurrection 3.0.0"))

    def test_document_prose_can_reflow_but_bullet_structure_cannot_change(self):
        row = {**self.source("A wrapped\nparagraph."), "surface": "docs"}
        catalog.validate_translation(row, "Переведённый абзац.")
        bullets = {**row, "text": "- First item\n- Second item"}
        with self.assertRaises(catalog.CatalogError):
            catalog.validate_translation(bullets, "- Только один пункт")
        with self.assertRaises(catalog.CatalogError):
            catalog.validate_translation(row, "Первый абзац.\n\nВторой абзац.")


if __name__ == "__main__":
    unittest.main()
