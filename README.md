# Clipboard Resurrection

Clipboard Resurrection is a Fallout 4 mod for copying groups of settlement
objects into reusable patterns. Select objects, save a pattern, and paste it
relative to the Clipboard tool's position and rotation. You can also move,
rotate, scale, restore the size of, or scrap a selection.

The product version is **3.0.0**. The latest built package is **internal build
122**; build numbers identify matched DLL and script sets independently of the
product version.

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
See the [user guide](package/v240/Docs/clipboard/Clipboard-ReadMe.txt) for import
rules, scaling and Restore behavior, component costs, and known limitations.

## Installation and compatibility

Install the complete package with a mod manager and enable `Clipboard.esp`.
Keep the included DLL, scripts, Strings and Interface files together. Back up
your own pattern files before replacing an existing installation or slot folders.
If upgrading from a version using `ClipboardExtension.dll`, disable that old DLL
so only `Data/F4SE/Plugins/clipboard.dll` remains active.

- Install [F4SE](https://f4se.silverlock.org/) for your exact Fallout 4 runtime
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

- [User guide](package/v240/Docs/clipboard/Clipboard-ReadMe.txt)
- [Object filtering and optional integrations](package/v240/Docs/clipboard/Clipboard-Object-Filtering.txt)
- [Language selection and troubleshooting](package/v240/Docs/clipboard/Clipboard-Localization.txt)
- [Changelog](package/v240/Docs/clipboard/CHANGELOG.txt)
- [Build and package guide](tools/BUILDING.md)
- [Localization maintenance](localization/README.md)
- [Package layout](package/README.md)

Translated guides are included under
[`package/v240/Docs/clipboard`](package/v240/Docs/clipboard), using their localized
filenames.

## Building from source

Read the [build guide](tools/BUILDING.md) for prerequisites, local tool and
runtime paths, and validation requirements. `tools/Build-Deployment.ps1` is the
entry point for a new numbered package: it prepares the next build number,
builds the input interface, all 16 Papyrus scripts and the DLL, then validates,
stages and archives them together. Outputs stay under `build/` and `Deployments/`;
the pipeline does not install into Fallout 4 or a mod manager.

The maintained source is organized as follows:

- `native/`: current C++ implementation, tests and build configuration.
- `Scripts/Source/User/`: the 16 maintained Papyrus scripts.
- `package/base/` and `package/v240/`: shared assets and current package overlays.
- `localization/`: language catalogs, source text and generation metadata.
- `external/CommonLibF4RD/`: pinned dependency source and its recorded patch.
- `external/F4SE/`: official source pin and verification manifest; the reference
  source is fetched separately after cloning.
- `tools/`: build, localization and validation helpers.

`native/vcpkg.json` is the product-version source. Generated builds, downloaded
reference sources, runtime fixtures, personal patterns, logs and private working
notes are excluded from the public source snapshot.

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
