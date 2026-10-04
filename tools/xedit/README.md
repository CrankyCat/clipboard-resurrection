# xEdit localization readback

This workflow checks how xEdit resolves Clipboard's localized ESP fields and
compares them with the current source catalogs. Use an isolated fixture and
preserve the reports and input hashes locally. In-game rendering and gameplay
tests remain separate.

`VerifyClipboardLocalization.pas` uses the installed xEdit 4.1.5 script API to
walk only `Clipboard.esp`, call `Check` recursively, and call `GetEditValue` on
every field whose `DefType` is `dtLString`. It uses no plugin mutation API.
The report includes record FormIDs, record and subrecord signatures, repeated
field indices, resolved text, and full element paths. The v2 report uses
JsonDataObjects with explicit UTF-8 encoding and no BOM so the Windows log code
page cannot corrupt Russian. The installed `JSON - Demo.pas` documents this
supported API. xEdit's JVI `Ord` adapter cannot convert a UnicodeString
character to Integer; keep the JSON-based Unicode extraction.

Resolved text is assigned directly from `GetEditValue` to the JSON string
property. A JVI local declared `string` narrows Cyrillic to the Windows ANSI
code page, even though the native API and JSON writer preserve Unicode. The
direct assignment avoids this extraction loss. The comparator's console output uses JSON
ASCII escapes for CP1252 consoles; its evidence file remains UTF-8 and the
comparison still requires exact Unicode text.

For a fresh checkout, prepare the project-local xEdit fixture with explicit
local inputs and the frozen localization migration plugin for readback:

```powershell
tools\xedit\Initialize-LocalizationWorkspace.ps1 `
  -GameData 'C:\Path\To\Fallout 4\Data' -XEditDirectory 'C:\Path\To\FO4Edit'
```

The initializer retains an existing workspace rather than overwriting it. Do
not replace the plugin in a fixture with completed readbacks; retain that
evidence and prepare a separate candidate fixture as described below.

The project-local xEdit fixture includes its copied
executable, `Fallout4.esm`, interface archive, localized ESP, and INI/plugin-list
files, before running the launcher. The launcher validates the localized ESP
hash against `localization/metadata/esp-map.json`, preserves the original xEdit English
tables, and copies the generated 39 tables into the isolated Data folder: three
tables for each of the 12 canonical locales plus the `cn` alias of Traditional
Chinese (`zhhant`). It records the executable, script, ESP, table, fixture,
source-catalog, English ESP source, reviewed display-override metadata and
captured-map hashes. The additional locales must all be
generated before starting any new readback.

Use a fresh `-EvidenceRoot` below this project's `outputs` directory for a new
candidate. The launcher copies the necessary files from the original isolated
fixture once into `<EvidenceRoot>/xedit`; every locale run then shares that
new fixture and its frozen tables. The original fixture and completed EN/RU
evidence remain untouched. Run directories, scripts and reports are separate
under `<EvidenceRoot>/xedit-readback/<locale>`. Without `-EvidenceRoot`, the
historical default remains `outputs/localization-implementation`.

```powershell
$evidence = 'outputs\additional-localization'
tools\xedit\Start-XEditLocalizationVerification.ps1 -Language en -EvidenceRoot $evidence -Launch
# Accept xEdit's module-selection dialog for the isolated Clipboard.esp fixture.
# Wait for CLIPBOARD_L10N_COMPLETE in Messages and readback.json to be written.
# Close only this verification process after its read-only script has completed.
$python = & tools\build\Get-ClipboardPython.ps1
& $python tests\localization\Test-XEditLocalizationReadback.py `
  --run outputs\additional-localization\xedit-readback\en `
  --output outputs\additional-localization\xedit-readback\en\verification.json

tools\xedit\Start-XEditLocalizationVerification.ps1 -Language ru -EvidenceRoot $evidence -Launch
# Accept the same isolated module selection, wait for completion, then close.
& $python tests\localization\Test-XEditLocalizationReadback.py `
  --run outputs\additional-localization\xedit-readback\ru `
  --output outputs\additional-localization\xedit-readback\ru\verification.json
```

Repeat the same sequential workflow for `de`, `es`, `esmx`, `fr`, `it`, `ja`,
`pl`, `ptbr`, `zhhans`, `zhhant` and `cn`. The last run uses `-l:cn` and compares
against `localization/translations/zhhant.json`; it verifies the actual Fallout 4 Chinese
suffix independently of the explicit `zhhant` package suffix. Other translated
locales compare against their matching `localization/translations/<locale>.json`. English
uses the current validated English source. The captured ESP map remains
immutable: any English difference must have an exact field identity, original
and replacement hashes, and reason in `localization/metadata/esp-display-overrides.json`.
This permits the approved page-three previous-range correction to `101-200`
without changing the ESP. Each translation must retain the current English
source hash, placeholders and markup.

Omit `-Launch` to prepare files and inspect `launch.json` without starting xEdit.
The launcher refuses to start while another FO4Edit process exists, and refuses
to overwrite a completed readback or replace table inputs referenced by a
completed report with different bytes. Retain earlier run directories before a
deliberate repeat with unchanged tables; use a new evidence root when the tables
change. The generated `launch.json` contains the exact arguments.
The main arguments are `-fo4 -script:"<absolute script>" -l:<locale>`,
`-nobuildrefs`, and isolated `-D`, `-S`, `-P`, `-I`, `-CustomIni`, `-M`, `-C`,
`-T`, `-B`, `-O`, and `-R` paths. Default FO4 decoding is retained: English
uses Windows-1252 and every other supported locale uses UTF-8. Do not force
UTF-8 for English tables.

The supported `-script` mode is a GUI mode. In xEdit 4.1.5, recognizing a `.pas`
script forces `tmScript`; `-autoload` and `-autoexit` are parsed only in
`tmEdit`, and `tmScript` is absent from `wbPluginModes`. Adding `-edit` does not
make script execution headless. Module-selection or first-run dialogs may
therefore need acceptance. `tmScript` itself does not disable editing or saving;
read-only scope comes from this script's API calls and the isolated fixture.
Do not operate another user's open xEdit session. The script writes its report
at completion, before application shutdown; xEdit's additional `-R` log is
flushed on normal close.

The Python verifier reads `readback.json` with format
`clipboard-xedit-readback-v2`. It requires every mapped nonempty field and every
preserved ID-zero empty field: currently 484 nonempty and 13 empty fields,
497 total across 118 records including TES4. It rejects duplicate, missing or
additional fields and text differences, requires zero `Check` errors, and
rechecks the input hashes. New schema-2 launch records must capture all 39
tables exactly once and select their recorded language with `-l:<locale>`.
The comparator still accepts the historical schema-1 EN/RU launch records.
Historical English expectations use the captured map; Russian expectations use
the frozen original Russian tables and their recorded hashes. These results
explicitly report `current_catalog_attested: false`, so a recheck of retained
evidence does not certify current translations or the new display correction.
Write any historical revalidation result under the new evidence directory;
never overwrite the original verification reports.
`Test-XEditLocalizationReadbackLocales.py` supplies seven host tests for source
catalog selection, Chinese alias separation, current English corrections,
historical input preservation, stale source and placeholder rejection. Those
fixtures do not execute xEdit or establish a readback pass.
A successful readback proves actual xEdit resolution for that executable and
those tables.
Fallout 4 font rendering, menu layout, language switching, and gameplay remain
separate tests.

API and mode behavior were checked against the installed `Edit Scripts\xEditAPI.pas`
and `Check for errors.pas`, plus the official xEdit `dev-4.1.5` sources:

- [Script adapter](https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.5/xEdit/JvI/xejviScriptAdapter.pas)
- [Core interface and localized-string resolution](https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.5/Core/wbInterface.pas)
- [Command-line initialization](https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.5/xEdit/xeInit.pas)
- [Script execution and module selection](https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.5/xEdit/xeMainForm.pas)

The localization migration/readback helpers use the frozen localized ESP in
`tests/fixtures/esp/Clipboard.esp` to preserve the captured mapping identity.
Run `tools/build/Build-GeneratedAssets.ps1` before preparing readback; tables
come from `build/generated/localization/Strings`. The maintained plugin is
`assets/Clipboard.esp`; its reviewed revision is validated separately during
packaging. Historical readback evidence is never rewritten.
