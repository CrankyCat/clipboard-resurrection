# Package source

The modern package combines shared inputs in `base/` with the current overlays
in `v240/`, the built input interface, 16 compiled Papyrus scripts and one
`clipboard.dll`. The product version is 3.0.0; the latest built package is
internal build 122.

## Shared inputs and current overlays

`base/` contains the shared plugin, materials, textures, MCM configuration,
default settings, license and 500 pattern-slot directories. Each slot contains
an empty `PLACEHOLDER` file. No `pattern.ini` files are included: omitting saved
patterns prevents package updates from overwriting users' pattern content.
Users should still back up their patterns before replacing an installation or
its slot folders.

The original texture and material assets were published by **Struckur**, the
author of the original Clipboard. Existing source and contributor credits,
including **Everett C Sands**, are preserved. Assets and dependencies retain
their original terms; see the [project license and credits](../README.md#credits-and-license).

`v240/` contains the current plugin, interface, localization and MCM overlays,
plus the English end-user guides in `Docs/clipboard/`. Eleven translated guide
sets use localized filenames beneath that directory. The documentation layout
and translation sources are maintained under `localization/`.

The modern stager omits the superseded `base/Clipboard-ReadMe.txt` and places
the unchanged base `LICENSE.txt` alongside the current guides at
`Docs/clipboard/LICENSE.txt`. Dependency notices are collected into
`Docs/clipboard/Licenses/` when staging.

Generated DLL and PEX files are build outputs rather than checked-in package
inputs. The required owned input menu is `Interface/ClipboardInput.swf`;
the deprecated `Clipboard.swf` is excluded.

## Building a package

Use [`tools/Build-Deployment.ps1`](../tools/Build-Deployment.ps1) for each new
numbered package, following the [build guide](../tools/BUILDING.md). It builds,
validates and stages a complete matched set, then writes the numbered directory
and ZIP beneath `Deployments/`. It does not copy into the game or a mod manager.
`Stage-Clipboard.ps1` and `Stage-Package-V240.ps1` are its staging helpers.

The same package targets OG, NG and AE through Runtime Database and
CommonLibF4RD. Runtime Database is installed separately and is not bundled.
Runtime eligibility and offline validation do not establish in-game
compatibility; see the [compatibility status](../README.md#installation-and-compatibility).
