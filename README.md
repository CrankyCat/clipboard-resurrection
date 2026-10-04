# Clipboard Resurrection

Clipboard Resurrection is a Fallout 4 mod for copying groups of settlement
objects into reusable patterns. Select objects, save a pattern, and paste it
relative to the Clipboard tool's position and rotation. You can also move,
rotate, scale, restore the size of, or scrap a selection.

The product version is **3.0.0**. Internal build numbers identify matched DLL
and script sets independently of the product version. The tracked counters in
[`config`](config) record the current candidate and last completed package.

## Features

- Select objects by box, sphere, cylinder, cell, plugin, or manual selection gun.
- Save object types, positions, rotations, scales and internal wires. Eligible
  connections to existing objects outside the selection can reconnect at the
  original location; those outside objects are not copied.
- Move and rotate groups, scale sizes with or without changing spacing, and
  restore every selected object to 100% size. Scaling accepts only changes that
  produce exact whole-number object percentages within the supported limits.
- Optionally charge construction components, with confirmation, a fresh
  availability check and verified withdrawals before placement.
- Use Clipboard's built-in text and number input, with keyboard, mouse and
  controller controls. TIM is not required. Windows clipboard paste is not
  currently supported.
- Use 500 empty pattern slots. No `pattern.ini` files are included in the package,
  so packaged patterns cannot overwrite user-created patterns during an update.
- Read the interface and guides in English or eleven translated languages.

Patterns do not preserve container contents, weapon or armor modifications,
ownership, script state, or Sim Settlements 2 plot plans, levels and settlers.
See the [user guide](docs/user/en/Clipboard-ReadMe.txt) for import
rules, scaling and Restore behavior, component costs, and known limitations.

## Installation and compatibility

Install the complete package with a mod manager and enable `Clipboard.esp`.
Keep the included DLL, scripts, Strings and Interface files together. Back up
your own pattern files before replacing an existing installation or slot folders.
If upgrading from a version using `ClipboardExtension.dll`, disable that old DLL
so only `Data/F4SE/Plugins/clipboard.dll` remains active.

- Install [F4SE](https://www.nexusmods.com/fallout4/mods/42147) for your exact Fallout 4 runtime
  and start the game through `f4se_loader.exe`.
- Install [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394),
  which supplies `Data/F4SE/Plugins/f4rd-runtime.bin`.
- [Mod Configuration Menu](https://www.nexusmods.com/fallout4/mods/21497)
  provides Clipboard's settings and hotkeys. Manual INI configuration is
  described in the user guide.

One `clipboard.dll` targets the OG, NG and AE runtime families through
CommonLibF4RD and Runtime Database. Recent gameplay checks cover AE 1.11.240;
OG and NG have offline validation but have not been tested in game. Address
resolution alone does not establish gameplay compatibility, and the recorded
checks of earlier builds do not certify every path in the latest package.

## Documentation

- [User guide](docs/user/en/Clipboard-ReadMe.txt)
- [Object filtering and optional integrations](docs/user/en/Clipboard-Object-Filtering.txt)
- [Language selection and troubleshooting](docs/user/en/Clipboard-Localization.txt)
- [Changelog](docs/user/en/CHANGELOG.txt)
- [Build and package guide](docs/development/BUILDING.md)
- [Localization maintenance](docs/development/LOCALIZATION.md)
- [Package layout](docs/development/PACKAGING.md)

Translated guides are generated from the approved
[translation catalogs](localization/translations) and installed under
`Docs/clipboard/<locale>/`, using their established localized filenames.

## Building from source

Read the [build guide](docs/development/BUILDING.md) for prerequisites, local tool and
runtime paths, and validation requirements. [`Build.ps1`](Build.ps1) is the
entry point for a new numbered package: it prepares the next build number,
generates the language assets and input interface, builds all 16 Papyrus scripts
and the DLL, then validates,
stages and archives them together. New outputs stay under `build/` and `dist/`;
the pipeline does not install into Fallout 4 or a mod manager.

The deployment folder is `Clipboard Resurrection - OG NG AE`. Test ZIPs append
` - build N`; release ZIPs append ` - Major.Minor.Patch`. Use
`Build.ps1 -Release -ReleaseVersion 3.0.0` for a release; omitting the version
prompts for it. The requested release version must match the configured product
version. See the build guide for tool-path arguments and version validation.

The maintained source is organized as follows:

- `src/native/`: current C++ implementation and headers.
- `src/papyrus/`: the 16 maintained Papyrus scripts.
- `src/ui/`: ActionScript source for the owned input menu.
- `assets/`: authored plugin, MCM configuration, defaults, textures and materials.
- `docs/user/en/`: authoritative English guides; `docs/development/`: maintainer guides.
- `localization/source/`, `localization/translations/`, and `localization/metadata/`:
  English text, translated wording, and generation/review metadata.
- `tests/`: native, Papyrus, UI, build, and localization checks plus frozen fixtures.
- `config/`: package recipe, tracked build counters and dependency identities.
- `external/CommonLibF4RD/`: pinned dependency source and its recorded patch.
- `external/F4SE/`: official source pin and verification manifest; the reference
  source is fetched separately after cloning.
- `tools/build/`, `tools/localization/`, `tools/diagnostics/`, and `tools/xedit/`:
  implementation helpers grouped by purpose.

`vcpkg.json` is the product-version source. Generated builds, downloaded
reference sources, runtime fixtures, personal patterns, logs and private working
notes are excluded from the public source snapshot.
See the [repository map](docs/development/REPOSITORY_LAYOUT.md) for edit locations,
generated files, and the unchanged installed package layout.

## Credits and license

**Struckur**, the author of the original Clipboard, originally published the
texture and material assets included with this project. The existing
**Everett C Sands** source copyright and contributor credits are preserved.
Thanks also to WolfMark, Big&Flabby, and the F4SE and CommonLibF4RD contributors.

Project-owned source, build tools and documentation use **GPL-3.0-or-later**,
unless separately licensed. See [LICENSE.txt](LICENSE.txt). Third-party
dependencies and assets retain their own terms; this project does not relicense
them. Dependency notices are included in built packages under
`Docs/clipboard/Licenses/`. There is no warranty.
