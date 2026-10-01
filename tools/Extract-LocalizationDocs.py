#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh paragraph catalogs without altering English package documentation."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
DOCUMENTS = (
    ("ReadMe", "Clipboard-ReadMe.txt"),
    ("Filtering", "Clipboard-Object-Filtering.txt"),
    ("Localization", "Clipboard-Localization.txt"),
    ("Changelog", "CHANGELOG.txt"),
)
PROTECTED = re.compile(
    r"https?://[^\s]+|(?:[A-Za-z0-9_.-]+\\)+(?:<slot>\\)?[A-Za-z0-9_.*-]*[A-Za-z0-9_*-]|"
    r"[\w.*-]+\.(?:dll|exe|esp|esl|esm|ini|pex|psc|txt|md|bin)\b|"
    r"\b[A-Za-z0-9_]*[a-z][A-Z][A-Za-z0-9_]*(?:=[01])?\b|"
    r"\[(?:Selection|Balance|Debug|Dialogs|HUD|OptionalFiltering)\]|"
    r"\b(?:Docs/clipboard|cn|zhhans|zhhant|SS2|Auto Beds|Sim Settlements|Seasons|"
    r"Struckur|Everett C Sands|WolfMark|Big&Flabby)\b|"
    r"\b(?:TXST|BNDS|LIGH|SCOL|STAT|LVLN|OMOD|XLCN|XLRL|OG|NG|AE|FF|DLL|PEX|INI|MCM|TIM|F4SE|HUDFramework|CommonLibF4RD)\b|"
    r"\b\d+(?:\.\d+)+\b|GPL-3\.0-or-later|Clipboard\.\*|ClipboardResurrection\.\*"
)


def main():
    catalog, documents = [], []
    previous_path = ROOT / "localization/source/docs.en.json"
    previous = json.loads(previous_path.read_text(encoding="utf-8-sig")) if previous_path.exists() else []
    layout_path = ROOT / "localization/source/docs-layout.json"
    layout = json.loads(layout_path.read_text(encoding="utf-8-sig"))
    if layout.get("schema") != 2:
        raise ValueError("Documentation layout must declare schema 2 and an explicit translation state")
    localized_filenames = {document["filename"]: document["localized_filenames"]
                           for document in layout["documents"] if "localized_filenames" in document}
    for slug, filename in DOCUMENTS:
        path = ROOT / "package/v240/Docs/clipboard" / filename
        raw = path.read_bytes()
        text = raw.decode("utf-8-sig")
        parts, section, paragraph = [], "Introduction", 0
        prefix = f"$Clipboard_Docs_{slug}_"
        old = {row["key"]: row for row in previous if row["key"].startswith(prefix)}
        segments = re.split(r"((?:\r?\n)[ \t]*(?:\r?\n))", text)
        new_texts = {segment.rstrip("\r\n") for segment in segments}
        reserved = {key for key, row in old.items() if row["text"] in new_texts}
        used = set()
        # Named keys still participate in text matching, but only numbered keys
        # contribute to the next numeric identifier.
        next_number = max((int(key.removeprefix(prefix)) for key in old
                           if re.fullmatch(r"[0-9]+", key.removeprefix(prefix))), default=0) + 1
        for segment in segments:
            if not segment:
                continue
            if not segment.strip():
                parts.append({"literal": segment})
                continue
            # Preserve trailing file newline and URL-only blocks verbatim.
            value = segment.rstrip("\r\n")
            trailing = segment[len(value):]
            if not value or re.fullmatch(r"https?://\S+", value):
                parts.append({"literal": segment})
                continue
            paragraph += 1
            key = next((old_key for old_key, row in old.items() if row["text"] == value and old_key not in used), None)
            if key is None:
                preferred = prefix + f"{paragraph:03d}"
                if preferred not in used and preferred not in reserved:
                    key = preferred
                else:
                    while prefix + f"{next_number:03d}" in used or prefix + f"{next_number:03d}" in reserved:
                        next_number += 1
                    key = prefix + f"{next_number:03d}"
                    next_number += 1
            used.add(key)
            if "\n" not in value and (value.isupper() or len(value) < 60):
                section = value
            protected = sorted(set(PROTECTED.findall(value)))
            row = {"key": key, "text": value,
                   "context": f"End-user paragraph/section from {filename}, section: {section}. Preserve paragraph/bullet structure, identifiers/paths, numerical facts and credits; line wrapping may adapt naturally to the target language. Translate the prose naturally; keep canonical English document filenames in translation data, since package generation localizes filenames and their references. Do not translate the canonical GPL license itself."}
            if protected:
                row["protected"] = protected
            catalog.append(row)
            parts.append({"key": key})
            if trailing:
                parts.append({"literal": trailing})
        documents.append({"source": path.relative_to(ROOT).as_posix(),
                          "source_sha256": hashlib.sha256(raw).hexdigest(), "filename": filename, "parts": parts})
        if filename in localized_filenames:
            documents[-1]["localized_filenames"] = localized_filenames[filename]
    directory = ROOT / "localization/source"
    directory.mkdir(parents=True, exist_ok=True)
    # Extraction refreshes English only. It never enables a language or changes
    # the explicit pending/ready decision, and never writes a translation file.
    layout["documents"] = documents
    for filename, data in (("docs.en.json", catalog), ("docs-layout.json", layout)):
        (directory / filename).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Extracted {len(catalog)} documentation entries from {len(documents)} complete English documents.")


if __name__ == "__main__":
    main()
