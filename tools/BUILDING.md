# Building Clipboard Resurrection

The maintained native project is in `native/`; its `vcpkg.json` is the product
version source. The current product version is **3.0.0**. The tracked internal
build counters record **122** as the last completed local package, so the next
numbered package is **123**. A source checkout is not an installable mod.

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
  [input asset guide](../UI/ClipboardInput/README.md).
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
.\tools\Initialize-F4SEReference.ps1
```

This checks the pinned upstream commit and file hashes. It verifies an existing
cache without overwriting it. See the [F4SE reference guide](../external/F4SE/README.md)
for the exact pin and notices.

### Tool and fixture locations

`Build-Deployment.ps1` accepts all the following settings and passes the resolved
paths to the individual build helpers. Explicit arguments take precedence over
environment variables and discovery.

| Parameter | Default when omitted |
| --- | --- |
| `GameRoot` | `CLIPBOARD_GAME_ROOT`, then the compiler tree recorded by a previous Papyrus build; otherwise required. |
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

MSBuild, the compiler, linker and dumpbin all come from `VisualStudioPath`.
CMake is pinned to that same installation using its
[generator instance setting](https://cmake.org/cmake/help/latest/variable/CMAKE_GENERATOR_INSTANCE.html).
[Visual Studio discovery](https://github.com/microsoft/vswhere/wiki/Find-VC) does
not install missing components or silently substitute a different toolset.

## Build a complete package

Use `Build-Deployment.ps1` for each new package. After installing the prerequisites
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
.\tools\Build-Deployment.ps1 @build -CheckOnly
.\tools\Build-Deployment.ps1 @build
```

`-CheckOnly` resolves and checks the required tool files, SDK libraries/headers,
native fixture version resources, Python version, Papyrus compiler/flags and
pinned F4SE reference. It prints the selected paths without compiling, stamping,
staging or allocating a build number. The real build runs the same preflight
before stamping. This detects configuration mistakes; it does not prove that
all dependencies will compile or that the Runtime Database contents are valid.
Those checks still run during the full build.

For a separate Papyrus compiler validation, use
`Build-Papyrus.ps1 -CheckOnly -GameRoot $build.GameRoot`. That script's
`-CheckOnly` actually invokes the compiler in no-assembly mode and writes local
imports/logs. Its verified F4SE merged imports remain ahead of game Base/DLC
imports. It is different from the deployment helper's read-only preflight.

Keep machine-specific configuration in an ignored directory such as `outputs/`,
not in these tracked scripts or public documentation. Generated build records
contain local paths and remain ignored.

The deployment helper holds a checkout-wide build lock and runs these steps:

1. Select the next unpublished internal build number and stamp all 16 maintained
   Papyrus scripts.
2. Compile the owned input SWF and record its source, asset, and protocol hashes.
3. Compile all 16 scripts and the native DLL with the same internal build number.
4. Run the native host checks and validate dependencies, metadata, localization,
   ESP revisions, and recorded source/output hashes.
5. Stage the complete package and a numbered ZIP under `Deployments/`.

This process stages files locally; it does not install into Fallout 4, write into
a mod manager, upload a release, or create a GitHub repository. The package
contains empty pattern slots and rejects bundled `pattern.ini` files. No player
pattern data is included.

Generated native files are under `build/native/vs2026`, native build records
under `build/native/release-1.11.240`, and Papyrus output under
`build/papyrus/v240`. Logs and build records are local generated data and should
not be committed. Existing helper names containing `V240` and the `Modern079`
Papyrus target identify the build fixture; they do not limit the mod to one
runtime family.

## Build identity and validation

Keep both tracked counters:

- `Clipboard.InternalBuild.json` is the current candidate number.
- `Clipboard.PublishedBuild.json` records the last successfully staged package.

Here, "published" means the completed local package, not a GitHub or mod-site
release. Existing deployment records also prevent number reuse. Failed attempts
retain their unpublished candidate number for retry. Do not manually change an
individual script stamp or restage an already completed number. ZIP filenames
include the internal build number independently of the product version.

The native build runs fourteen CTests. Staging rechecks build identities, the
native/Papyrus contract, vendored dependency integrity, runtime symbols, input
asset provenance, and localization/package consistency. Source-level checks can
also be run without creating a numbered package:

```powershell
.\tools\Test-V240VendoredDependency.ps1
.\tools\Test-V240PapyrusContract.ps1
.\tools\Test-RuntimeSymbols.ps1
python .\tools\Build-Localization.py --check
.\tools\Test-BuildPaths.ps1
```

`Test-BuildPaths.ps1` uses synthetic fixtures and stubbed compilation steps under
`outputs/` to check path precedence, missing-input failures and deployment
parameter forwarding/order. It never compiles or stages a mod. Portability checks
and a preflight on an existing installation do not certify a complete build on a
fresh Windows machine.

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
  to the maintained native Release outputs. Results stay under `outputs/`.
- `Capture-ClipboardImportDiagnostic.ps1` requires `-RunName`, `-Phase` and
  `-InstalledMod`. Supply `-PatternPath`, `-SettingsPath` and `-ProfilePath` only
  for the inputs you intend to capture. Omitted pattern/settings paths are not
  guessed. `-GameUserRoot` defaults to Fallout 4's Documents directory.
- `Capture-ClipboardDestroyCrash.ps1` accepts `-ProcDumpPath`, or finds
  `procdump64.exe` on `PATH`. Use its `-CheckOnly` to inspect the plan without
  attaching; an ordinary invocation attaches to the selected running game.
- `Initialize-LocalizationWorkspace.ps1` requires `-GameData` and accepts
  `-XEditDirectory` (or finds `FO4Edit.exe` on `PATH`). It checks the inputs before
  creating its isolated workspace. See the [xEdit guide](xedit/README.md).
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

Edit `package/v240/Clipboard.esp`, not a generated deployment copy.
`localization/esp-baseline` contains frozen inputs used to validate the localized
plugin; those baseline files are not packaged.

`localization/esp-revision.json` records the reviewed removal of the menu-renaming
KYWD override `0011FBD3`. Validation permits that deletion and the corresponding
TES4 record-count decrement while checking the other records and language-table
values. Additional ESP changes need an updated, reviewed record/localization
comparison; changing a checksum alone does not establish correctness.
