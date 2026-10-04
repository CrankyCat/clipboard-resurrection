# Package generation and assembly

Run [`Build.ps1`](../../Build.ps1) to create a complete numbered local package.
The pipeline is:

```text
authored sources -> build/generated and compiled outputs -> build/stage/N -> dist/build-N
```

## Authored inputs

[`config/package.json`](../../config/package.json) defines the installed destinations,
asset inventory, languages and aliases, script counts, license notices, and empty
pattern slots. Every destination has one source; collisions, including differences
only in letter case, fail. There is no base/overlay precedence.

- `assets/`: the maintained ESP, MCM configuration, defaults, materials and textures.
- `docs/user/en/`: the four authoritative English guides.
- `localization/source/`, `localization/translations/`, and `localization/metadata/`:
  approved wording, stable keys, localized filenames and review metadata.
- `src/`: native, Papyrus and input-menu source.
- Root `LICENSE.txt` and dependency notices: copied to `Docs/clipboard/` and
  `Docs/clipboard/Licenses/` respectively.

The original textures and materials were published by **Struckur**, the original
Clipboard author. Existing credits, including **Everett C Sands**, are preserved.
Assets and dependencies retain their original terms; see the
[project license and credits](../../README.md#credits-and-license).

`tests/fixtures/mcm/` contains the frozen English MCM comparison inputs.
`tests/fixtures/esp/` contains the frozen localization migration plugins. These
fixtures are validation inputs, never package payloads. Edit `assets/Clipboard.esp`
for current plugin changes; keep the reviewed ESP revision checks.

## Generated files

The numbered build runs `tools/build/Build-GeneratedAssets.ps1` automatically.
It uses local approved catalogs to generate runtime TSV catalogs, MCM translations,
translated guides and ESP string tables under `build/generated/localization/`.
This step does not call a translation service. Missing or stale in-game translations,
or stale enabled guide translations, fail validation. Disabled guide translations
remain disabled according to their existing review state.

The input compiler writes `build/generated/ui/Interface/ClipboardInput.swf`.
Native outputs remain under `build/native/vs2026`; Papyrus outputs remain under
`build/papyrus/current`. Generated files are ignored and regenerated from source.
Papyrus build stamps still update the 16 maintained scripts; that workflow has
not changed.

The four English guides are copied to installed `Docs/clipboard/` paths.
Translated guides retain their established localized filenames beneath locale
subdirectories. English extraction and translation review are authoring steps:
the package build does not silently refresh approvals or translate changed prose.

The recipe generates slots 1–500, each with one empty `PLACEHOLDER` file. No
`pattern.ini` is allowed. This preserves the installed pattern-slot structure
without maintaining 500 empty source files. Users should still back up their
patterns before replacing an installation or its slot folders.

## Validation and completed packages

`Stage-Package.ps1` resolves the recipe, verifies the recorded native, Papyrus,
UI and localization inputs, and assembles `build/stage/N/`. Independent checks
retain the ESP/non-text, MCM, language, script, DLL, license and slot contracts.
Every staged file must match its planned bytes, and inputs are checked again
after assembly. The ZIP is reopened and every entry is checked for exact path,
size, CRC and SHA-256; missing, duplicate and unexpected entries fail.

With `-Archive`, the complete directory is renamed to `dist/build-N/` only after
all checks pass. It contains the installable package directory, verified ZIP,
SHA-256 manifest, package plan and `.build.json` record. Existing completed
directories are never overwritten. Failed attempts stay under `build/stage/`
and retain their candidate number. A completed record prevents number reuse
even if the process stops before updating the tracked published counter.

Without `-Archive`, staging produces a validated preview under `build/stage/`
and does not publish or advance the completed-build counter. For normal work,
use `Build.ps1`, which always creates the verified archive.

The installable directory is always `Clipboard Resurrection - OG NG AE`.
Test archives are `Clipboard Resurrection - OG NG AE - build N.zip`.
Release archives are `Clipboard Resurrection - OG NG AE - Major.Minor.Patch.zip`.
Use `Build.ps1 -Release -ReleaseVersion 3.0.0` for a release. Omitting the release
version prompts for it; the value must match the configured product version.
Release archives retain the internal build identity in their build records.

Existing flat packages under `dist/` and historical `Deployments/` remain unchanged.
The release exporter discovers both layouts and copies the newest verified ZIP
byte-for-byte to `dist/Clipboard Resurrection - OG NG AE - <version>.zip`.
Pass `-ReleaseVersion` to the exporter or provide it when prompted. A different existing
release ZIP is never overwritten. No build helper installs into Fallout 4 or a
mod manager, or uploads a release.

The same package targets OG, NG and AE. Runtime Database remains an external
dependency. Offline build/package validation does not certify in-game behavior;
see the [compatibility status](../../README.md#installation-and-compatibility).
