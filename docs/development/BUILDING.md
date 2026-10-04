# Building Clipboard Resurrection

The maintained native source is in `src/native/`; CMake configuration and the
product-version source, `vcpkg.json`, are at the repository root. The current
product version is **3.0.0**. The tracked counters in `config/` record the
candidate and last completed local package; the build selects the next unused
number automatically. A source checkout is not an installable mod.

## Prerequisites

The build helpers target Windows x64 and PowerShell 5.1 or later. Install:

- Git, for the pinned external F4SE source reference.
- Visual Studio 2026 (including Build Tools) with the C++ desktop tools, MSVC
  v145, CMake 4.2 or later, and vcpkg. The default versions are MSVC
  **14.52.36725** and Windows SDK **10.0.28000.0**. Both versions can be selected
  explicitly; the chosen versions must be installed. The Windows SDK must be
  registered by its installer.
- Python **3.10 or later** for localization and package validation. No translation
  service or API key is needed to build the existing translations.
- A JDK providing `java.exe` and `javac.exe`, plus **JPEXS 26.2.1**, including its
  compiler library and `playerglobal32_0.swc` definitions. See the
  [input asset guide](INPUT_UI.md).
- A local Fallout 4 installation with the Creation Kit Papyrus compiler and
  extracted game script sources. The helper expects `Papyrus Compiler` and
  `Data/Scripts/Source` beneath the supplied game directory, including
  `Base/Institute_Papyrus_Flags.flg` and the required game/DLC imports.
- For the native build's provenance and validation checks, a local Fallout 4
  **1.11.240.0** executable, the **F4SE 0.7.9** runtime DLL, and a Runtime Database
  file (`f4rd-runtime.bin`). These external files are not distributed here.

The pinned CommonLibF4RD source is included under `external/CommonLibF4RD`.
CMake obtains its other dependencies through the pinned vcpkg manifest. The
complete official F4SE reference is fetched separately and kept in an ignored
cache; it supplies Papyrus imports and does not replace CommonLibF4RD.

From the repository root, initialize and verify that reference:

```powershell
.\tools\build\Initialize-F4SEReference.ps1
```

This checks the pinned upstream commit and file hashes. It verifies an existing
cache without overwriting it. See the [F4SE reference guide](../../external/F4SE/README.md)
for the exact pin and notices.

### Tool and fixture locations

`Build.ps1` accepts all the following settings and passes the resolved
paths to the individual build helpers. Explicit arguments take precedence over
environment variables, ignored `.local/build-paths.json`, and discovery, in that
order. Local JSON uses `schemaVersion: 1` and the path parameter names below;
relative paths are resolved from the repository root. Build inputs must live
outside the disposable `build/` directory.

| Parameter | Default when omitted |
| --- | --- |
| `GameRoot` | `CLIPBOARD_GAME_ROOT`, local configuration, then `.local/toolchains/fallout4-papyrus`; otherwise required. Previous build records are never used for discovery. |
| `VisualStudioPath` | Latest installed VS 2026 with C++ tools, found with `vswhere`, across editions and Build Tools. |
| `CMakePath` | CMake bundled with the selected VS installation, then `cmake.exe` on `PATH`. |
| `VcpkgRoot` | `VCPKG_ROOT`, then `VC/vcpkg` within the selected VS installation. |
| `VCToolsVersion` | `14.52.36725`. |
| `WindowsSdkVersion` | `10.0.28000.0`, under the registered Windows SDK root. |
| `Fallout4Executable` | `Fallout4.exe` under `GameRoot`. |
| `F4SEDllPath` | `f4se_1_11_240.dll` under `GameRoot`. |
| `RuntimeDatabasePath` | `Data/F4SE/Plugins/f4rd-runtime.bin` under `GameRoot`. |
| `FFDecRoot` | `FFDEC_ROOT`, then the standard `FFDec` directory under Program Files (x86). |
| `JavaHome` | `JAVA_HOME`, then the JDK containing `javac.exe` on `PATH`; a JRE alone is insufficient. |
| `PythonPath` | Python on `PATH`, excluding Windows Store aliases; an available local Codex runtime is a final fallback. |

`GameRoot` must contain the Papyrus compiler and game imports. If that compiler
tree differs from the native fixture installation, supply `Fallout4Executable`,
`F4SEDllPath` and `RuntimeDatabasePath` separately. Mod-manager installations
commonly need those overrides: the helpers do not search virtual filesystems or
guess mod and profile names.

The local Papyrus toolchain keeps `Papyrus Compiler/` and
`Data/Scripts/Source/` together beneath `.local/toolchains/fallout4-papyrus/`.
Keep the compiler, flags, and vanilla/DLC/User imports there; they are inputs,
not generated build output. Set the separate native fixture and JDK paths in
`.local/build-paths.json` to make a no-argument preflight independent of `build/`.
Do not commit machine-specific paths or the proprietary compiler/game sources.

After a successful publication, `build/` can be removed in full while no build
is running. Publication retains the matching DLL/PDB, all 16 PEX files, native
and Papyrus attestations, supporting metadata and logs under
`.local/build-history/build-N/<content-identity>/`. The publication record links
to the verified retention manifest. This history is diagnostic evidence, never
an input-discovery fallback. Historical records keep their original paths.
The cleanup helper also archives any unpublished build contents before deletion;
failed/in-progress work must not be silently discarded. Cleanup never resets
the counters in `config` or removes completed packages in `dist`/`Deployments`.

```powershell
# Report only; no deletion.
.\tools\maintenance\Clean-ClipboardBuild.ps1
# Save and verify a complete pre-clean ZIP, remove build/, and repeat preflight.
.\tools\maintenance\Clean-ClipboardBuild.ps1 -Apply
```

The pre-clean archive includes the whole remaining build tree and therefore uses
space under `.local/build-history/`; its manifest records every original hash.
Published-package diagnostic snapshots remain separate from these cleanup backups.

MSBuild, the compiler, linker and dumpbin all come from `VisualStudioPath`.
CMake is pinned to that same installation using its
[generator instance setting](https://cmake.org/cmake/help/latest/variable/CMAKE_GENERATOR_INSTANCE.html).
[Visual Studio discovery](https://github.com/microsoft/vswhere/wiki/Find-VC) does
not install missing components or silently substitute a different toolset.

## Build a complete package

Use `Build.ps1` for each new package. After installing the prerequisites
and initializing the F4SE reference, run from the repository root. Replace every
example path below with your own; omit overrides only when their defaults apply:

```powershell
$build = @{
    GameRoot = 'C:\Path\To\Fallout 4'
    VisualStudioPath = 'C:\Path\To\Visual Studio\2026\BuildTools'
    CMakePath = 'C:\Path\To\CMake\bin\cmake.exe'
    VcpkgRoot = 'C:\Path\To\vcpkg'
    VCToolsVersion = '14.52.36725'
    WindowsSdkVersion = '10.0.28000.0'
    Fallout4Executable = 'C:\Path\To\Runtime Fixture\Fallout4.exe'
    F4SEDllPath = 'C:\Path\To\F4SE\f4se_1_11_240.dll'
    RuntimeDatabasePath = 'C:\Path\To\Runtime Database\f4rd-runtime.bin'
    FFDecRoot = 'C:\Path\To\FFDec'
    JavaHome = 'C:\Path\To\JDK'
    PythonPath = 'C:\Path\To\Python\python.exe'
}
.\Build.ps1 @build -CheckOnly
.\Build.ps1 @build
```

`-CheckOnly` resolves and checks the required tool files, SDK libraries/headers,
native fixture version resources, Python version, Papyrus compiler/flags and
pinned F4SE reference. It prints the selected paths without compiling, stamping,
staging or allocating a build number. The real build runs the same preflight
before stamping. This detects configuration mistakes; it does not prove that
all dependencies will compile or that the Runtime Database contents are valid.
Those checks still run during the full build.

For a separate Papyrus compiler validation, use
`tools/build/Build-Papyrus.ps1 -CheckOnly -GameRoot $build.GameRoot`. That script's
`-CheckOnly` actually invokes the compiler in no-assembly mode and writes local
imports/logs. Its verified F4SE merged imports remain ahead of game Base/DLC
imports. It is different from the deployment helper's read-only preflight.

Keep machine-specific configuration in an ignored directory such as `.local/`,
not in these tracked scripts or public documentation. Generated build records
contain local paths and remain ignored.

The deployment helper holds a checkout-wide build lock and runs these steps:

1. Select the next unpublished internal build number and stamp all 16 maintained
   Papyrus scripts.
2. Generate the approved catalogs, translated guides and ESP string tables
   offline under `build/generated/localization/`. Compile the owned input SWF
   under `build/generated/ui/` and record its source, asset and protocol hashes.
3. Compile all 16 scripts and the native DLL with the same internal build number.
4. Run the native host checks and validate dependencies, metadata, localization,
   ESP revisions, and recorded source/output hashes.
5. Resolve `config/package.json`, generate the 500 empty slots, and assemble
   `build/stage/N/`. Verify every staged and archived file before publishing
   the complete directory to `dist/build-N/` in one rename.

The installable folder is always `Clipboard Resurrection - OG NG AE` inside
`dist/build-N/`. Test ZIPs use `Clipboard Resurrection - OG NG AE - build N.zip`.

For a release, run `Build.ps1 @build -Release -ReleaseVersion 3.0.0`, substituting
the intended product version. The ZIP is named
`Clipboard Resurrection - OG NG AE - 3.0.0.zip`, without the internal build number.
The completed build stays under `dist/build-N/`; a verified byte-for-byte release
copy and matching `.build.json` are also exported directly under `dist/`.

Omitting `-ReleaseVersion` with `-Release` prompts for the release version.
It must use `Major.Minor.Patch` without leading zeros and match `vcpkg.json`.
Invalid or mismatched versions stop before stamping or compilation. Update the
configured product version and affected documentation separately before requesting
a new version; the build does not silently relabel different product bytes.
`-ReleaseVersion` without `-Release` is rejected. Supply the version explicitly
for noninteractive runs, including release preflight with `-CheckOnly`.

To export an existing verified package without rebuilding, run
`tools/build/Export-ClipboardRelease.ps1 -ReleaseVersion 3.0.0`. It also prompts
if the version is omitted and applies the same format/match checks. The exporter
recognizes old and new package names, verifies the archive hash, and copies the
ZIP unchanged. A different existing release ZIP is never overwritten.

This process stages files locally; it does not install into Fallout 4, write into
a mod manager, upload a release, or create a GitHub repository. The package
contains empty pattern slots and rejects bundled `pattern.ini` files. No player
pattern data is included.

Generated native files are under `build/native/vs2026`, native build records
under `build/metadata/native`, and Papyrus output under
`build/papyrus/current`. Logs and build records are local generated data and should
not be committed. The maintained Papyrus target is `Current`; the compatibility
alias `Modern079` selects the same pinned F4SE 0.7.9 imports. Exact game/F4SE
versions remain in fixture definitions and build records, not current source
folder names. Existing `Deployments/` archives are preserved and consulted when
checking completed build numbers; new packages are written only to `dist/`.

## Build identity and validation

Keep both tracked counters:

- `config/Clipboard.InternalBuild.json` is the current candidate number.
- `config/Clipboard.PublishedBuild.json` records the last successfully staged package.

Here, "published" means the completed local package, not a GitHub or mod-site
release. Existing deployment records also prevent number reuse. Failed attempts
retain their unpublished candidate number for retry. Do not manually change an
individual script stamp or restage an already completed number. Test archive
filenames include the build number; release filenames use the shared deployment
base name and product version. Both retain the internal build number in their
`dist/build-N/` location and `.build.json` record.

The native build runs fourteen CTests. Staging rechecks build identities, the
native/Papyrus contract, vendored dependency integrity, runtime symbols, input
asset provenance, and localization/package consistency. Source-level checks can
also be run without creating a numbered package:

```powershell
.\tests\native\Test-NativeDependencies.ps1
.\tests\papyrus\Test-PapyrusContract.ps1
.\tests\native\Test-RuntimeSymbols.ps1
.\tools\build\Build-GeneratedAssets.ps1
python .\tools\localization\Build-Localization.py --check
python .\tests\build\Test-PackagePlan.py
.\tests\build\Test-BuildPaths.ps1
```

`Test-BuildPaths.ps1` uses synthetic fixtures and stubbed compilation steps under
`build/validation/build-path-tests/` to check path precedence, missing-input failures and deployment
parameter forwarding/order. It never compiles or stages a mod. Portability checks
and a preflight on an existing installation do not certify a complete build on a
fresh Windows machine.

For the manual report-first maintenance helper and protected historical inputs,
see [repository cleanup](CLEANUP.md). Cleanup is separate from the build pipeline.

These checks do not establish gameplay behavior. The maintained DLL targets the
OG, NG, and AE runtime families using Runtime Database resolution and runtime
selected layouts. An offline symbol match does not establish ABI safety, F4SE
loading, Papyrus registration, save/reload behavior, or performance on a given
runtime. Validate the exact DLL, scripts, and assets together in game when a
change requires those checks.

### Optional diagnostic and localization helpers

These helpers are separate from the numbered package workflow:

- `Test-ClipboardRuntimeMatrix.ps1` requires `-RuntimeDatabasePath` and an
  explicit `-ExecutablePath` array of locally supplied game images. It no longer
  assumes a private fixture directory. `-InspectorPath` and `-DllPath` default
  to the maintained native Release outputs. Results stay under
  `build/validation/runtime-matrix/` unless another output directory is supplied.
- `Capture-ClipboardImportDiagnostic.ps1` requires `-RunName`, `-Phase` and
  `-InstalledMod`. Supply `-PatternPath`, `-SettingsPath` and `-ProfilePath` only
  for the inputs you intend to capture. Omitted pattern/settings paths are not
  guessed. `-GameUserRoot` defaults to Fallout 4's Documents directory.
- `Capture-ClipboardDestroyCrash.ps1` accepts `-ProcDumpPath`, or finds
  `procdump64.exe` on `PATH`. Use its `-CheckOnly` to inspect the plan without
  attaching; an ordinary invocation attaches to the selected running game.
- `Initialize-LocalizationWorkspace.ps1` requires `-GameData` and accepts
  `-XEditDirectory` (or finds `FO4Edit.exe` on `PATH`). It checks the inputs before
  creating its isolated workspace. See the [xEdit guide](../../tools/xedit/README.md).
- `Build-ObjectProbe.ps1` accepts `-GameRoot` and `-Probe Object` or `Power`.
  A standalone probe package additionally requires `-ReadmePath` with the
  instructions to include. `-CheckOnly` needs no readme and performs the compiler
  check only. The helper does not depend on excluded internal documents.

### Identifying installed components

`Clipboard.log` records the DLL's internal build number at startup. On Clipboard
use, script reports identify each reporting PEX's own compiled number and whether
it matches the DLL. A `match=false` entry identifies a mixed installation;
matching entries cover only the named components. A missing report does not
verify a script. Ordinary Clipboard interaction triggers reporting without
requiring a full import.

Build identities and warnings/errors are recorded even with **Enable Diagnostic
Logging** off. That setting controls additional diagnostics. Script identity
reports also appear through `Debug.Trace` when the engine's Papyrus tracing is
enabled. `Clipboard.log` is replaced at process startup, so preserve a log needed
for troubleshooting before restarting the game.

## Editing the ESP

Edit `assets/Clipboard.esp`, not a generated deployment copy.
`tests/fixtures/esp` contains frozen inputs used to validate the localized
plugin; those baseline files are not packaged.

`localization/metadata/esp-revision.json` records the reviewed removal of the menu-renaming
KYWD override `0011FBD3`. Validation permits that deletion and the corresponding
TES4 record-count decrement while checking the other records and language-table
values. Additional ESP changes need an updated, reviewed record/localization
comparison; changing a checksum alone does not establish correctness.
