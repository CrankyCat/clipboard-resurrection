#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate editable catalogs and deterministically generate Clipboard text data."""
from __future__ import annotations

import argparse
from collections import Counter
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re
import sys
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ("papyrus", "mcm", "esp", "docs")
KEY_RE = re.compile(r"\$Clipboard_[A-Za-z0-9_]+\Z")
MARKUP_RE = re.compile(r"<[^>]*>|%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?[hlL]*[diuoxXfFeEgGaAcsp%]")
# Canonical catalog identifiers. The verified stock Traditional Chinese cn
# suffix aliases zhhant in UI/ESP files and the native language selector.
# Every supported locale has all in-game surfaces. Documentation translation
# has its own explicit review state and enabled locales in docs-layout.json.
LOCALES = ("en", "ru", "de", "es", "esmx", "fr", "it", "ja", "pl", "ptbr", "zhhans", "zhhant")
TRANSLATED_LOCALES = LOCALES[1:]
ADDITIONAL_LOCALES = LOCALES[2:]
LOCALE_ALIASES = {"cn": "zhhant"}
FALLBACK_LOCALES = ()
LATIN_LOCALES = frozenset(("de", "es", "esmx", "fr", "it", "pl", "ptbr"))
FIXED_PRODUCTS = ("Clipboard", "Fallout 4", "F4SE", "MCM", "HUDFramework", "TIM",
                  "Runtime Database", "CommonLibF4RD", "Havok")
FILENAME_RE = re.compile(r"(?<![A-Za-z0-9_])[A-Za-z0-9_][A-Za-z0-9_.-]*\.(?:log|ini|esp|esm|esl|dll|pex|psc|txt|tsv|json|swf|ba2)(?![A-Za-z0-9_])", re.IGNORECASE)
WINDOWS_PATH_RE = re.compile(r"(?:[A-Za-z]:\\|(?<![A-Za-z0-9_])[A-Za-z0-9_.-]+\\)(?:[A-Za-z0-9_.-]+(?: [A-Za-z0-9_.-]+)*\\)*[A-Za-z0-9_.-]+\.[A-Za-z0-9]+")
NUMBER_RE = re.compile(r"[0-9]+(?:[.,\u00a0\u202f ][0-9]+)*")
RANGE_MARKERS = frozenset(("-", "–", "—", "~", "〜", "～", "à", "a", "bis", "do", "até", "до", "по", "to", "から", "至", "到"))


class CatalogError(ValueError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def text_hash(text: str) -> str:
    return sha256(text.encode("utf-8"))


def strict_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise CatalogError(f"Duplicate JSON property: {key}")
        result[key] = value
    return result


def read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=strict_pairs,
                          parse_constant=lambda value: (_ for _ in ()).throw(CatalogError(f"Invalid JSON number: {value}")))
    except (ValueError, UnicodeError) as exc:
        raise CatalogError(f"Invalid JSON/UTF-8 in {path}: {exc}") from exc


def placeholders(text: str) -> Counter:
    """Match the native six-argument formatter, including escaped literal braces."""
    found = Counter()
    pos = 0
    while pos < len(text):
        if text[pos:pos + 2] in ("{{", "}}"):
            pos += 2
        elif text[pos] == "{":
            token = text[pos:pos + 3]
            if len(token) != 3 or token[1] not in "012345" or token[2] != "}":
                raise CatalogError("Unsupported or malformed placeholder; expected {0} through {5}")
            found[token] += 1
            pos += 3
        elif text[pos] == "}":
            raise CatalogError("Unescaped closing brace")
        else:
            pos += 1
    return found


def validate_text(key: str, text: str):
    if not isinstance(key, str) or not KEY_RE.fullmatch(key) or len(key) > 160:
        raise CatalogError(f"Invalid localization key: {key!r}")
    if not isinstance(text, str) or not text or "\x00" in text:
        raise CatalogError(f"Empty, non-string, or NUL text for {key}")
    if any(ord(c) < 32 and c not in "\t\r\n" for c in text):
        raise CatalogError(f"Unsupported control character in text for {key}")
    try:
        encoded = text.encode("utf-8")
    except UnicodeError as exc:
        raise CatalogError(f"Invalid Unicode text for {key}") from exc
    if len(encoded) > 16384:
        raise CatalogError(f"Text exceeds native 16 KiB bound: {key}")
    try:
        placeholders(text)
    except CatalogError as exc:
        raise CatalogError(f"{key}: {exc}") from exc


def protected_tokens(source: dict) -> set[str]:
    text = source["text"]
    result = set(source.get("protected", []))
    result.update(FILENAME_RE.findall(text))
    result.update(WINDOWS_PATH_RE.findall(text))
    result.update(token for token in FIXED_PRODUCTS if token in text)
    return result


def is_markup_match(match: re.Match) -> bool:
    # A literal percentage followed by prose, e.g. "5% and" / "5 % et",
    # must not be mistaken for the printf space flag plus conversion a/e.
    return match[0].startswith("<") or not re.search(r"[0-9][ \t\u00a0\u202f]*$", match.string[:match.start()])


def markup_tokens(text: str) -> list[str]:
    return [match[0] for match in MARKUP_RE.finditer(text) if is_markup_match(match)]


def template_literal_text(text: str) -> str:
    result, pos = [], 0
    while pos < len(text):
        if text[pos:pos + 2] in ("{{", "}}"):
            result.append(text[pos])
            pos += 2
        elif text[pos:pos + 3] in ("{0}", "{1}", "{2}", "{3}", "{4}", "{5}"):
            result.append("\ufff0")
            pos += 3
        else:
            result.append(text[pos])
            pos += 1
    return "".join(result)


def number_tokens(text: str) -> list[dict]:
    # Aliases/printf and six-argument placeholders have their own exact checks.
    # Replace instead of deleting them, so adjacent literal numbers cannot join.
    text = MARKUP_RE.sub(lambda match: "\ufff0" if is_markup_match(match) else match[0], text)
    text = template_literal_text(text)
    result, previous_end = [], 0
    for match in NUMBER_RE.finditer(text):
        between = text[previous_end:match.start()].strip()
        is_range = bool(result) and between.casefold() in RANGE_MARKERS
        sign = ""
        if not is_range and between.endswith(("+", "-", "−")):
            sign = "+" if between[-1] == "+" else "-"
        suffix = re.match(r"[ \t\u00a0\u202f]*([%°])", text[match.end():])
        result.append({"raw": match.group(), "sign": sign, "unit": suffix[1] if suffix else "",
                       "range": is_range})
        previous_end = match.end()
    return result


def english_number(raw: str) -> tuple[Decimal, bool]:
    if re.fullmatch(r"[0-9]{1,3}(?:,[0-9]{3})+(?:\.[0-9]+)?", raw):
        raw = raw.replace(",", "")
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)?", raw):
        raise CatalogError(f"Unsupported English numeric literal: {raw!r}")
    return Decimal(raw), "." in raw


def numeric_value_matches(raw: str, expected: Decimal, source_decimal: bool) -> bool:
    if re.fullmatch(r"[0-9]+", raw):
        return Decimal(raw) == expected
    # A source integer of at least 1,000 establishes grouping rather than a
    # decimal value: 9,999 -> 9.999 / 9 999 / 9 NBSP 999 are equivalent.
    if not source_decimal:
        grouped = re.fullmatch(r"([0-9]{1,3})([.,\u00a0\u202f ])([0-9]{3})(?:\2[0-9]{3})*", raw)
        return bool(expected >= 1000 and grouped and Decimal(raw.replace(grouped[2], "")) == expected)
    # Decimal translation may change its decimal mark, and group its integer
    # part with a different separator. Never discard a fractional part.
    decimal = re.fullmatch(r"(.+)([.,])([0-9]+)", raw)
    if not decimal:
        return False
    integer, mark, fraction = decimal.groups()
    if not re.fullmatch(r"[0-9]+", integer):
        grouped = re.fullmatch(r"([0-9]{1,3})([.,\u00a0\u202f ])([0-9]{3})(?:\2[0-9]{3})*", integer)
        if not grouped or grouped[2] == mark:
            return False
        integer = integer.replace(grouped[2], "")
    try:
        return Decimal(integer + "." + fraction) == expected
    except InvalidOperation:
        return False


def validate_literal_tokens(source: dict, translated: str):
    key, original = source["key"], source["text"]
    for token in protected_tokens(source):
        if original.count(token) != translated.count(token):
            raise CatalogError(f"Protected identifier/path mismatch: {key}: {token}")
    # Source hotkey help follows these bounded patterns; ordinary prose letters
    # and punctuation are not automatically treated as hotkeys.
    hotkey = re.search(r"\bSuggested(?: Key)?: ([^\s])\Z", original)
    if hotkey:
        key_pattern = r"(?<![A-Za-z0-9_])" + re.escape(hotkey[1]) + r"(?![A-Za-z0-9_])"
        if not re.search(key_pattern, translated):
            raise CatalogError(f"Suggested hotkey changed: {key}: {hotkey[1]}")
    # Documentation already has explicit protected identifiers; its prose can
    # reflow. These additional ordered numeric checks target the in-game UI.
    if source.get("surface") == "docs":
        return
    # Slot/page endpoints can themselves be runtime arguments or Bethesda
    # aliases. Their values are protected separately; preserve range direction.
    dynamic_atom = r"(?:\{[0-5]\}|<Alias=[^>]+>|%\.0f)"
    range_connector = "(?:" + "|".join(re.escape(value) for value in sorted(RANGE_MARKERS, key=len, reverse=True)) + ")"
    dynamic_range = re.compile(r"(" + dynamic_atom + r")[ \t]*" + range_connector + r"[ \t]*(" + dynamic_atom + r")", re.IGNORECASE)
    if Counter(dynamic_range.findall(original)) != Counter(dynamic_range.findall(translated)):
        raise CatalogError(f"Dynamic range endpoint/direction mismatch: {key}")
    before, after = number_tokens(original), number_tokens(translated)
    if len(before) != len(after):
        raise CatalogError(f"Literal numeric count mismatch: {key}")
    for left, right in zip(before, after):
        value, decimal = english_number(left["raw"])
        contextual_percent = (not left["unit"] and right["unit"] == "%" and
                              re.search(r"\bpercent(?:age)?\b", original, re.IGNORECASE))
        if (not numeric_value_matches(right["raw"], value, decimal) or
                left["sign"] != right["sign"] or
                (left["unit"] != right["unit"] and not contextual_percent) or
                (left["range"] and not right["range"])):
            raise CatalogError(f"Literal number/sign/unit/range mismatch: {key}: {left['raw']}")


def validate_translation(source: dict, translated: str):
    key = source["key"]
    validate_text(key, translated)
    if placeholders(source["text"]) != placeholders(translated):
        raise CatalogError(f"Placeholder mismatch: {key}")
    # Preserve markup and positional printf arguments in their original order.
    if markup_tokens(source["text"]) != markup_tokens(translated):
        raise CatalogError(f"Bethesda/HTML/printf markup mismatch: {key}")
    for pattern in (r"^\s*", r"\s*$"):
        if re.search(pattern, source["text"]).group() != re.search(pattern, translated).group():
            raise CatalogError(f"Leading/trailing whitespace mismatch: {key}")
    if source.get("surface") != "docs" and Counter(re.findall(r"\r\n|\r|\n", source["text"])) != Counter(re.findall(r"\r\n|\r|\n", translated)):
        raise CatalogError(f"Line-break mismatch: {key}")
    if source.get("surface") == "docs":
        if re.findall(r"(?m)^\s*- ", source["text"]) != re.findall(r"(?m)^\s*- ", translated):
            raise CatalogError(f"Documentation bullet structure mismatch: {key}")
        if re.search(r"\r?\n[ \t]*\r?\n", translated):
            raise CatalogError(f"Documentation translation added an extra paragraph: {key}")
    validate_literal_tokens(source, translated)


def load_sources(root: Path = ROOT) -> tuple[dict, dict]:
    entries, inputs = {}, {}
    for name in SOURCES:
        path = root / "localization" / "source" / f"{name}.en.json"
        data = read_json(path)
        if not isinstance(data, list) or not data:
            raise CatalogError(f"Source must be a nonempty array: {path}")
        inputs[str(path.relative_to(root)).replace("\\", "/")] = sha256(path.read_bytes())
        for row in data:
            if not isinstance(row, dict) or not {"key", "text", "context"} <= row.keys():
                raise CatalogError(f"Source row lacks key/text/context: {path}")
            key, value = row["key"], row["text"]
            validate_text(key, value)
            if not isinstance(row["context"], str) or not row["context"].strip():
                raise CatalogError(f"Missing translator context: {key}")
            if key in entries:
                raise CatalogError(f"Duplicate source key across catalogs: {key}")
            entries[key] = {**row, "surface": name}
    if sum(row["surface"] != "docs" for row in entries.values()) > 4096:
        raise CatalogError("Source exceeds native 4096-key bound")
    for key, row in entries.items():
        leader = row.get("translation_group", key)
        if leader not in entries or entries[leader]["text"] != row["text"] or entries[leader]["surface"] != row["surface"]:
            raise CatalogError(f"Translation group must reference same-surface identical English: {key}")
        if entries[leader].get("translation_group", leader) != leader:
            raise CatalogError(f"Translation group leader cannot point to another group: {key}")
    return entries, inputs


def load_translations(path: Path, sources: dict, *, allow_partial=False, ignored_keys=()) -> dict:
    data = read_json(path)
    if not isinstance(data, list):
        raise CatalogError(f"Translation must be an array: {path}")
    translated, seen = {}, set()
    for row in data:
        if not isinstance(row, dict) or not {"key", "text", "source_sha256"} <= row.keys():
            raise CatalogError("Translation rows require key, text and source_sha256")
        key = row["key"]
        if key in seen:
            raise CatalogError(f"Duplicate translation key: {key}")
        seen.add(key)
        if key not in sources and key in ignored_keys:
            continue
        if key not in sources:
            raise CatalogError(f"Stale/unknown translation key: {key}")
        if row["source_sha256"] != text_hash(sources[key]["text"]):
            raise CatalogError(f"Stale English source hash for translation: {key}")
        validate_translation(sources[key], row["text"])
        translated[key] = row
    missing = sources.keys() - translated.keys()
    if missing and not allow_partial:
        raise CatalogError(f"Missing {len(missing)} translation keys: {', '.join(sorted(missing)[:12])}")
    for key, row in sources.items():
        leader = row.get("translation_group", key)
        if key in translated and leader in translated and translated[key]["text"] != translated[leader]["text"]:
            raise CatalogError(f"Shared string-ID translation group differs: {key}")
    return translated


def translation_sources(sources: dict, locale: str, *, document_locales=None) -> dict:
    if locale not in TRANSLATED_LOCALES:
        raise CatalogError(f"Unsupported translation locale: {locale}")
    if document_locales is None:
        document_locales = load_document_layout(ROOT)["enabled_locales"]
    if locale in document_locales:
        return sources
    return {key: row for key, row in sources.items() if row["surface"] != "docs"}


def validate_locale_script(locale: str, source: dict, translated: str):
    """Reject foreign letters in Latin locales, retaining source-specific symbols.

    This bounded check is not a language classifier. Combining marks, digits and
    punctuation are unaffected; feminine/masculine ordinal indicators are valid
    Latin UI text despite their Unicode names not containing LATIN.
    """
    if locale not in LATIN_LOCALES:
        return

    def foreign_letters(text):
        return Counter(char for char in text
                       if unicodedata.category(char).startswith("L")
                       and "LATIN" not in unicodedata.name(char, "")
                       and char not in "ªº")

    unexpected = foreign_letters(translated) - foreign_letters(source["text"])
    if unexpected:
        details = ", ".join(f"U+{ord(char):04X} {unicodedata.name(char, 'UNNAMED')}"
                            for char in sorted(unexpected))
        raise CatalogError(f"Unexpected non-Latin letters for {locale}: {source['key']}: {details}")


def load_locale_translations(root: Path, sources: dict) -> tuple[dict, dict]:
    translations, inputs = {}, {}
    layout = load_document_layout(root)
    for locale in TRANSLATED_LOCALES:
        path = root / "localization" / f"{locale}.json"
        required = translation_sources(sources, locale, document_locales=layout["enabled_locales"])
        retained = set(layout.get("retained_translation_keys", {}).get(locale, []))
        translations[locale] = load_translations(path, required, ignored_keys=retained - required.keys())
        for key, row in translations[locale].items():
            validate_locale_script(locale, sources[key], row["text"])
        inputs[path.relative_to(root).as_posix()] = sha256(path.read_bytes())
    return translations, inputs


def escaped(text: str) -> str:
    return text.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def runtime_bytes(entries: dict[str, str]) -> bytes:
    data = "".join(f"{key}\t{escaped(entries[key])}\n" for key in sorted(entries)).encode("utf-8")
    if len(data) > 4 * 1024 * 1024:
        raise CatalogError("Runtime catalog exceeds 4 MiB")
    return data


def interface_bytes(entries: dict[str, str]) -> bytes:
    lines = []
    for key, value in entries.items():
        if any(c in value for c in "\n\r\t"):
            raise CatalogError(f"F4SE UI catalog value contains newline/tab: {key}")
    for key in sorted(entries):
        line = f"{key}\t{entries[key]}\r\n"
        # F4SE ReadLine_w(buffer, 512) needs room to consume the LF as well
        # as the terminating NUL; otherwise the next read terminates import.
        if len(line.encode("utf-16-le")) // 2 > 511:
            raise CatalogError(f"F4SE UI catalog line exceeds 511 UTF-16 units including CRLF: {key}")
        lines.append(line)
    return b"\xff\xfe" + "".join(lines).encode("utf-16-le")


def replace_keys(value, sources: dict, used: set, path: tuple = ()):
    if isinstance(value, dict):
        # Scaleform translation of an HTML-bearing key displayed its tags
        # literally in MCM. Keep fixed brand/layout HTML in config.json.
        text = value.get("text")
        if (value.get("html") is True and isinstance(text, str) and
                text in sources and re.search(r"<[^>]*>", sources[text]["text"])):
            raise CatalogError(f"MCM HTML must not come from a translated text key: {path}")
        return {k: replace_keys(v, sources, used, path + (k,)) for k, v in value.items()}
    if isinstance(value, list):
        return [replace_keys(v, sources, used, path + (i,)) for i, v in enumerate(value)]
    if isinstance(value, str) and value.startswith("$Clipboard_"):
        display_field = bool(path) and path[-1] in ("displayName", "text", "help", "desc")
        option = len(path) >= 3 and path[-3:-1] == ("valueOptions", "options") and isinstance(path[-1], int)
        if not display_field and not option:
            raise CatalogError(f"Localization key used in non-display MCM field: {path}")
        if value not in sources or sources[value]["surface"] != "mcm":
            raise CatalogError(f"MCM uses unknown/non-MCM key: {value}")
        used.add(value)
        return sources[value]["text"]
    return value


def validate_mcm(root: Path, sources: dict) -> dict:
    inputs, used = {}, set()
    for filename in ("config.json", "keybinds.json"):
        relative = Path("MCM/Config/Clipboard") / filename
        baseline = read_json(root / "package/base" / relative)
        path = root / "package/v240" / relative
        modern = read_json(path)
        if replace_keys(modern, sources, used) != baseline:
            raise CatalogError(f"MCM English text or non-display behavior changed: {filename}")
        inputs[path.relative_to(root).as_posix()] = sha256(path.read_bytes())
        baseline_path = root / "package/base" / relative
        inputs[baseline_path.relative_to(root).as_posix()] = sha256(baseline_path.read_bytes())
    expected = {k for k, row in sources.items() if row["surface"] == "mcm"}
    if expected != used:
        raise CatalogError(f"Unused MCM source keys: {', '.join(sorted(expected - used))}")
    return inputs


def expected_outputs(sources: dict, translations: dict) -> dict[str, bytes]:
    if translations.keys() != set(TRANSLATED_LOCALES):
        raise CatalogError("Generated catalogs require every supported translated locale")
    english = {k: row["text"] for k, row in sources.items() if row["surface"] != "docs"}
    result = {}
    for locale in LOCALES:
        strings = english if locale == "en" else {
            k: row["text"] for k, row in translations[locale].items() if sources[k]["surface"] != "docs"
        }
        if strings.keys() != english.keys():
            raise CatalogError(f"Generated catalog has incomplete in-game keys: {locale}")
        result[f"F4SE/Plugins/Clipboard/Localization/{locale}.tsv"] = runtime_bytes(strings)
        ui = {k: v for k, v in strings.items() if sources[k]["surface"] == "mcm"}
        result[f"Interface/Translations/Clipboard_{locale}.txt"] = interface_bytes(ui)
    for alias, locale in LOCALE_ALIASES.items():
        result[f"Interface/Translations/Clipboard_{alias}.txt"] = result[f"Interface/Translations/Clipboard_{locale}.txt"]
    return result


def document_filename(document: dict, locale: str) -> str:
    """Resolve a deployment basename without changing the English identity."""
    if locale == "en" or "localized_filenames" not in document:
        return document["filename"]
    return document["localized_filenames"][locale]


def validate_document_filename(filename):
    if (not isinstance(filename, str) or not filename.endswith(".txt") or
            not filename[:-4] or filename != unicodedata.normalize("NFC", filename) or
            any(char in '<>:"/\\|?*' or unicodedata.category(char) in ("Cc", "Cf", "Cs")
                for char in filename) or filename.endswith((".", " ")) or
            len(filename.encode("utf-16-le")) // 2 > 255):
        raise CatalogError(f"Invalid localized documentation filename: {filename!r}")
    device = filename.split(".", 1)[0].rstrip(" .").upper()
    if (device in ("CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$") or
            re.fullmatch(r"(?:COM|LPT)[1-9¹²³]", device)):
        raise CatalogError(f"Reserved localized documentation filename: {filename!r}")


def load_document_layout(root: Path) -> dict:
    path = root / "localization/source/docs-layout.json"
    layout = read_json(path)
    if (not isinstance(layout, dict) or layout.get("schema") != 2 or
            not isinstance(layout.get("documents"), list) or not layout["documents"]):
        raise CatalogError("Invalid documentation layout schema")
    status, locales = layout.get("translation_status"), layout.get("enabled_locales")
    if (status not in ("pending", "ready") or not isinstance(locales, list) or
            any(locale not in TRANSLATED_LOCALES for locale in locales) or
            len(set(locales)) != len(locales) or (status == "pending" and locales)):
        raise CatalogError("Invalid documentation translation state or enabled locales")
    retained = layout.get("retained_translation_keys", {})
    if (not isinstance(retained, dict) or any(
            locale not in TRANSLATED_LOCALES or not isinstance(keys, list) or
            any(not isinstance(key, str) or not re.fullmatch(r"\$Clipboard_Docs_[A-Za-z0-9_]+", key) for key in keys) or
            len(set(keys)) != len(keys) for locale, keys in retained.items())):
        raise CatalogError("Invalid retained documentation translation keys")
    filenames = set()
    for document in layout["documents"]:
        if not isinstance(document, dict):
            raise CatalogError("Invalid documentation layout entry")
        filename = document.get("filename")
        if (filename not in ("Clipboard-ReadMe.txt", "Clipboard-Object-Filtering.txt",
                             "Clipboard-Localization.txt", "CHANGELOG.txt") or
                document.get("source") != f"package/v240/Docs/clipboard/{filename}" or
                filename in filenames):
            raise CatalogError("Unexpected or duplicate English documentation path")
        filenames.add(filename)
        if "localized_filenames" in document:
            localized = document["localized_filenames"]
            if not isinstance(localized, dict) or set(localized) != set(locales):
                raise CatalogError("Localized documentation filenames must match enabled locales")
            for translated_filename in localized.values():
                validate_document_filename(translated_filename)
    canonical = {name.casefold(): name for name in filenames}
    for locale in locales:
        localized = set()
        for document in layout["documents"]:
            filename = document_filename(document, locale).casefold()
            if (filename in localized or
                    (filename in canonical and canonical[filename] != document["filename"])):
                raise CatalogError(f"Duplicate or ambiguous documentation filename for {locale}")
            localized.add(filename)
    return layout


def document_outputs(root: Path, sources: dict, translations: dict) -> tuple[dict, dict]:
    path = root / "localization/source/docs-layout.json"
    layout = load_document_layout(root)
    inputs = {path.relative_to(root).as_posix(): sha256(path.read_bytes())}
    # Resolve every reference in one pass: a translated basename may itself
    # contain an English basename, and must not be translated a second time.
    reference_pattern = re.compile(
        r"(?<![A-Za-z0-9_.-])(?:" + "|".join(re.escape(row["filename"]) for row in layout["documents"]) +
        r")(?![A-Za-z0-9_-]|\.[A-Za-z0-9_])")
    references = {locale: {row["filename"]: document_filename(row, locale)
                           for row in layout["documents"]}
                  for locale in layout["enabled_locales"]}
    outputs, used = {}, set()
    for document in layout["documents"]:
        relative = document["source"]
        source_path = root / relative
        ensure_project_output(source_path, root)
        original = source_path.read_bytes()
        inputs[relative] = sha256(original)
        # English is still being edited. Preserve old translation records as
        # history; they cannot generate deployment guides in this state.
        if layout["translation_status"] == "pending":
            continue
        if sha256(original) != document.get("source_sha256"):
            raise CatalogError(f"English documentation changed; refresh its source catalog: {relative}")
        if not isinstance(document.get("parts"), list):
            raise CatalogError(f"Documentation layout needs paragraph extraction: {relative}")
        english = []
        localized = {locale: [] for locale in layout["enabled_locales"]}
        for part in document["parts"]:
            if set(part) == {"literal"}:
                english.append(part["literal"])
                for parts in localized.values():
                    parts.append(part["literal"])
            elif set(part) == {"key"}:
                key = part["key"]
                if key not in sources or sources[key]["surface"] != "docs":
                    raise CatalogError(f"Unknown documentation key: {key}")
                used.add(key)
                english.append(sources[key]["text"])
                for locale, parts in localized.items():
                    if key not in translations[locale]:
                        raise CatalogError(f"Missing documentation translation for {locale}: {key}")
                    parts.append(translations[locale][key]["text"])
            else:
                raise CatalogError("Invalid documentation layout part")
        if "".join(english) != original.decode("utf-8-sig"):
            raise CatalogError(f"Documentation catalog does not exactly reconstruct English: {relative}")
        for locale, parts in localized.items():
            target = f"Docs/clipboard/{locale}/{document_filename(document, locale)}"
            text = reference_pattern.sub(lambda match: references[locale][match.group()], "".join(parts))
            outputs[target] = text.encode("utf-8-sig")
    expected = {key for key, row in sources.items() if row["surface"] == "docs"}
    if layout["translation_status"] == "ready" and expected != used:
        raise CatalogError("Documentation layout does not use every docs source key")
    return outputs, inputs


def ensure_project_output(path: Path, root: Path):
    resolved = path.resolve()
    if not resolved.is_relative_to(root.resolve()):
        raise CatalogError("Localization output must remain inside the project")
    if resolved.is_relative_to((root / "Original Project Files").resolve()):
        raise CatalogError("Historical inputs are immutable")


def write_atomic(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    temporary.replace(path)


def build(root: Path, output: Path, check: bool = False) -> dict:
    ensure_project_output(output, root)
    sources, inputs = load_sources(root)
    inputs.update(validate_mcm(root, sources))
    translations, translation_inputs = load_locale_translations(root, sources)
    inputs.update(translation_inputs)
    outputs = expected_outputs(sources, translations)
    documents, document_inputs = document_outputs(root, sources, translations)
    outputs.update(documents)
    inputs.update(document_inputs)
    for name, data in outputs.items():
        target = output / name
        if check:
            if not target.is_file() or target.read_bytes() != data:
                raise CatalogError(f"Generated catalog missing or stale: {target}")
    allowed_ui = {Path(name).name for name in outputs if name.startswith("Interface/")}
    ui_path = output / "Interface/Translations"
    unexpected = [p.name for p in ui_path.glob("Clipboard_*.txt") if p.name not in allowed_ui]
    runtime_path = output / "F4SE/Plugins/Clipboard/Localization"
    allowed_runtime = {f"{locale}.tsv" for locale in LOCALES}
    unexpected += [p.name for p in runtime_path.glob("*") if p.name not in allowed_runtime]
    if unexpected:
        raise CatalogError(f"Unexpected localization package files: {', '.join(unexpected)}")
    if not check:
        for name, data in outputs.items():
            write_atomic(output / name, data)
    layout = load_document_layout(root)
    return {"schema": 1, "source_locale": "en", "translated_locales": list(TRANSLATED_LOCALES),
            "documentation": {"translation_status": layout["translation_status"],
                              "enabled_locales": layout["enabled_locales"],
                              "english_paths": [f"Docs/clipboard/{row['filename']}" for row in layout["documents"]],
                              "localized_paths": sorted(documents)},
            "locale_aliases": dict(LOCALE_ALIASES), "fallback_locales": list(FALLBACK_LOCALES), "total_keys": len(sources),
            "surface_counts": dict(Counter(row["surface"] for row in sources.values())),
            "inputs": dict(sorted(inputs.items())),
            "outputs": {name: {"sha256": sha256(data), "bytes": len(data)} for name, data in sorted(outputs.items())}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, help="Project-local package root (default: package/v240)")
    parser.add_argument("--check", action="store_true", help="Validate exact existing generated files without writing")
    parser.add_argument("--report", type=Path, help="Optional project-local JSON manifest; no credentials included")
    args = parser.parse_args()
    root = args.root.resolve()
    report = build(root, (args.output or root / "package/v240").resolve(), args.check)
    if args.report:
        ensure_project_output(args.report, root)
        write_atomic(args.report, (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    print(json.dumps(report, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (CatalogError, OSError) as exc:
        print(f"Localization validation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
