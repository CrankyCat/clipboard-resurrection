# Clipboard localization sources

The English catalogs are the maintained text baseline. The expanded source
pipeline targets twelve canonical catalog locales: `en`, `ru`, `de`, `es`,
`esmx`, `fr`, `it`, `ja`, `pl`, `ptbr`, `zhhans` and `zhhant`. Each non-English
catalog is an editable translation of the current English baseline. Runtime
catalogs, MCM translations and ESP string tables are generated data; do not edit
those generated files.

All eleven translated catalogs cover the current in-game text and four user
guides, including input, scaling, Restore, component costs and external wires.
Guide setting names must match the corresponding localized MCM labels. Preserve
proper mod names such as Condition Boy/Girl.

These canonical catalog names are distinct from the game's stock suffixes.
The inspected Fallout 4 archive uses `cn` for Traditional Chinese and contains
the other stock codes listed above, except `zhhans` and `zhhant`. Those two are
additional compatibility names, requiring matching game configuration and font
support. Generation supplies `cn` copies of the Traditional Chinese MCM and
ESP tables, and native lookup maps `cn` to `zhhant.tsv`. `es` targets Spain,
`esmx` targets Mexico, and `ptbr` targets Brazil. Chinese variants retain their
respective scripts.

`source/papyrus.en.json`, `source/mcm.en.json`, `source/esp.en.json`, and
`source/docs.en.json` are UTF-8
JSON arrays. Every row contains `key`, `text`, and a translator-facing `context`.
Keys are stable ASCII names beginning with `$Clipboard_`; they must be unique
across all four files. Keep keys stable when changing wording. Do not translate
script names, setting IDs, pattern field names, plugin paths, or user data.

Keep the fixed Clipboard header HTML directly in the MCM config with `html: true`.
An HTML fragment returned by its former translation key displayed tags literally
in game. The brand name stays unchanged in every language; the original
centered, 28-point markup therefore needs no translation entry. Validation rejects
HTML-bearing keys used by MCM HTML text rows while preserving ordinary labels.

ESP rows that share one actual localized string ID can supply
`translation_group` naming a leader key with identical English text. The client
translates that group once with its combined context and preserves every
per-field catalog key; validation rejects divergent translations within a group.
Do not group unrelated fields merely because their English text matches.

English deployment documentation lives in `package/v240/Docs/clipboard/`:
`Clipboard-ReadMe.txt`, `Clipboard-Object-Filtering.txt`,
`Clipboard-Localization.txt` and `CHANGELOG.txt`.

`source/docs-layout.json` uses schema 2 with `translation_status: ready` and all
eleven non-English locales enabled for documentation. The current English
catalog has 71 paragraphs.
Finalize English before translating, then refresh affected translations and
restore each locale's filename maps when enabling its guides. Do not update
source hashes while leaving obsolete text in place.

Each document's `localized_filenames` map assigns one filename per enabled
locale. Canonical English filenames remain stable in the source layout and
translation catalogs. Generation writes localized filenames and replaces their
references in the generated guides in one pass. Other text is unchanged. This
keeps protected catalog tokens and translation provenance intact. Extraction
preserves the filename maps. Names must be unique within each locale and safe
for Windows, with `.txt` extensions and normalized Unicode.

After changing finalized English wording, run `python tools/Extract-LocalizationDocs.py`
to refresh the paragraph catalog and exact English hashes/whitespace layout, then
refresh the affected translations.
Extraction updates English source data only and preserves the pending/ready
state and enabled locale list. Add future requested locale codes to
`enabled_locales` when their translation work is authorized. Each enabled locale requires a complete, current translation
of every documentation paragraph, including the changelog; generation writes
UTF-8 BOM files beneath `Docs/clipboard/<code>/`. Localized readmes refer to
`../LICENSE.txt` and `../Licenses`. The canonical GPL license is never translated.

Each `localization/<locale>.json`, including `ru.json`, is a UTF-8 JSON array
whose rows contain:

```json
{
  "key": "$Clipboard_Example",
  "text": "Русский текст",
  "source_sha256": "SHA-256 of the exact current English text encoded as UTF-8"
}
```

The source hash detects translations that need review after English changes.
Each translated locale covers all current dynamic Papyrus/native strings, MCM
strings and nonempty ESP fields. The build manifest records current counts.
Documentation coverage is
required only for the separately enabled document locales. The `cn` alias uses `zhhant.json` and
does not add another translation source. Preserve placeholders,
Bethesda aliases, markup, printf formatting, line breaks, and leading/trailing
whitespace. Dynamic text accepts `{0}` through `{5}`; write literal braces as
`{{` and `}}`. Arguments may move for the target language's word order, but each placeholder
must appear the same number of times. Prefer count-neutral wording where a
template cannot select grammatical number at runtime. Player-entered names and
independently inserted aliases must not be translated or grammatically altered.

`translation-context.json` supplies project behavior, regional language
profiles, terminology guidance and additional context for specific keys. It
distinguishes selection from placement, copy/export from paste/import,
numbered pattern slots from save games, electrical wires from power setup,
and incomplete workshop callbacks from successful completion. Its glossary is
guidance for contextual translation, not a dictionary substitution rule.

Validation preserves literal numeric values, ordered bounds and range direction,
signs, percent/degree symbols, protected technical names/paths and suggested
hotkeys. Known localized range connectors and equivalent grouping/decimal marks
are accepted in source context; fractional values cannot be discarded. Runtime
range endpoints also retain their order. A percent symbol may be added when the
source already specifies a percentage. Clear target-language number notation
still requires semantic review, beyond mathematical equivalence.

The seven Latin-script catalogs reject new non-Latin letters that were not
present in the same English source row. This bounded guard permits diacritics,
combining marks, ordinal indicators such as `ª`/`º`, punctuation and preserved
source symbols. It detects contamination such as an accidental Georgian word
in Portuguese prose; it does not establish fluency or classify language quality.

Documentation prose may reflow its wrapped lines in the target language while retaining
the exact paragraph boundaries, bullet structure, protected identifiers, and
complete English source reconstruction. Runtime/UI line breaks stay exact.

The modern MCM JSON under `package/v240/MCM/Config/Clipboard` uses keys only in
display fields. Validation resolves every key to English and compares the
result with `package/base` JSON, preserving its exact English presentation and
all action, ID, option-order and setting semantics. The base package remains
available unchanged for the separate legacy build.

With Python 3.10 or newer, run from the project root:

```powershell
python tools/Translate-Localization.py --locale de --dry-run
python tools/Translate-Localization.py --locale de --key-file '<external credential file>' --max-batches 1
python tools/Translate-Localization.py --locale de --key-file '<external credential file>'
python tools/Build-Localization.py --report outputs/localization-build/manifest.json
python tools/Build-Localization.py --check
python tools/Test-Localization.py
```

Repeat the translation commands with each required locale. Re-running the same
command resumes its accepted source catalog; there is no separate resume flag.
Use `--output` for a different project-local source JSON and `--report` for a
different project-local usage report. The defaults are `localization/<locale>.json`
and `outputs/additional-localization/translation/<locale>/usage.json` for the ten
new languages. Russian retains its established
`outputs/localization-translation/usage.json` default.

Repeat `--surface papyrus`, `--surface mcm`, or `--surface esp` to translate
selected in-game surfaces. `--surface docs` is available only after English is
finalized and that locale is enabled in `docs-layout.json`. The client preserves
explicitly inactive documentation rows when saving unrelated in-game translations.
It freezes the selected English catalog files, documentation translation state
and shared context for each run and stops if those inputs change. A partial/pilot run does not
satisfy the complete-catalog requirement for generation or staging.

Use an absolute Python executable if Python is absent from `PATH`. The
translation client defaults to `gpt-6-astra` for future translation work at the official
OpenAI [Chat Completions endpoint](https://api.openai.com/v1/chat/completions).
Use `--model` only when explicitly selecting a different model. Existing current
translations are reused regardless of the model that originally produced them.
A dry run does not read the credential
or contact the API. Normal runs reuse valid existing translations and request
only missing or stale-English entries, saving each accepted batch atomically.
The client refuses endpoint redirects, keeps the credential out of files and
logs, validates every response before acceptance, and writes resumable request
and token-usage evidence to the selected per-locale usage report. The report
records model, source/context/prompt hashes, accepted batches and reported usage.
Never store the key in this directory, source control, or a package. Only the
current English text and its per-string, project and locale context are
submitted for translation;
the historical Russian package is not used as wording input.

`Build-Localization.py` defaults to `package/v240`; `--output` accepts another
project-local package root. It produces:

- Twelve `F4SE/Plugins/Clipboard/Localization/<locale>.tsv` files: all current
  in-game keys per canonical locale, UTF-8 without BOM,
  one ASCII key, a literal tab, escaped text, and LF per row. Text escapes are
  `\\`, `\n`, `\r`, and `\t`. Native lookup uses explicit English fallback.
- Thirteen `Interface/Translations/Clipboard_<suffix>.txt` files: all MCM keys
  for each canonical locale and the `cn` alias,
  UTF-16 LE with BOM, literal tabs and CRLF. The basename matches `Clipboard.esp`.
  Each full line must fit F4SE's 511 UTF-16-code-unit limit, including its key,
  tab and CRLF; generation validates this bound, including surrogate pairs.
- Fully translated files replace the previous ten English-content fallback
  sets. F4SE loads the MCM catalog
  matching `sLanguage` literally; it does not automatically load English for
  a missing Clipboard locale file. An arbitrary custom suffix needs its own
  catalog alias. Native missing-file or missing-key fallback remains English.
- Translated documents for enabled documentation locales only: each receives
  the three guides and changelog under `Docs/clipboard/<code>/`, using the
  localized filenames and matching document references from the layout.
  All eleven non-English locales are enabled, producing 44 translated guides
  alongside the four maintained English package sources.

The optional build manifest records all source/translation/MCM input hashes and
generated output sizes/hashes. The generator rejects stale/missing keys,
duplicates, malformed Unicode/JSON/templates, changes to MCM behavior, and
unexpected files in its runtime catalog directory. The xEdit ESP migration and
its `.STRINGS`/`.DLSTRINGS`/`.ILSTRINGS` generation are separate from this text
generator and require their own field-to-ID and non-text-record comparisons.
Run `python tools/ClipboardLocalizationEsp.py generate` after catalog validation,
then use the same command with `--check` to verify its exact output. English
ESP tables use Windows-1252; Russian and other locale tables use UTF-8. The
expected inventory is 39 tables: STRINGS, DLSTRINGS and ILSTRINGS for each of
the twelve locales plus `cn`. Empty ILSTRINGS files remain present. The
current package inventory is recorded by the numbered deployment's manifest and
archive verification. Use `tools/Build-Deployment.ps1` for a new local package,
including documentation updates; it builds a matching DLL and all 16 scripts.
The committed ESP comes from xEdit, and this
tool only reads ESP bytes. `tools/Test-LocalizationEsp.py` exercises independent
encoding fixtures and rejection of changed plugin identities or gameplay data.

One user-approved display correction is recorded in
`esp-display-overrides.json`: page 3's previous-page label changes from `100-101`
to `101-200`. It targets only `$Clipboard_ESP_ClipboardPage3SelectMessage_DESC_0`,
MESG `010035A3`, `DESC` occurrence 0, DLSTRINGS ID `303`. The override pins the
captured ESP/map hashes, field identity, original text hash and replacement text
hash. The actual ESP, captured map, string ID and navigation code remain unchanged.
Current English generation and every translation use the corrected display text;
strict validation rejects an unlisted source difference or a stale override.

Run the catalog, ESP and translation-client test helpers after changes to their
respective pipeline. Current documentation extraction also tests stable named
keys alongside numeric keys and byte-identical repeated extraction. Historical
test counts, runtime mappings and unchanged-artifact claims apply only to the
candidates identified in their original evidence reports.

Host tests validate data integrity and reproducible generation. They do not
certify MCM substitution, multilingual glyph coverage or layout, game input,
save migration, or OG/NG/AE gameplay. Runtime/UI changes may require focused
in-game language checks; a documentation-only refresh does not require repeating
previously accepted gameplay tests. Fluent in-context wording review remains
separate from automated structural validation and xEdit readback.

Project-owned catalogs and tools use GPL-3.0-or-later under the repository's
existing notices and contributor boundaries. Keep the canonical English license
with any translated documentation; this directory does not replace that license.
