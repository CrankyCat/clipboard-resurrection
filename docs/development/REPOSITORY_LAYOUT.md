# Repository layout

Use the location that owns the input you want to change. Installed mod paths
are an output contract and do not determine repository source locations.

| Change | Authoritative location |
| --- | --- |
| Native C++ behavior | `src/native/` |
| Papyrus behavior | `src/papyrus/` |
| Input dialog | `src/ui/ClipboardInput/ClipboardInput.as` |
| English readme and other user guides | `docs/user/en/` |
| English in-game text | `localization/source/` |
| Translated wording | `localization/translations/` |
| Translation layout, terminology and review metadata | `localization/metadata/` |
| Current plugin, MCM, defaults, textures and materials | `assets/` |
| Package destinations, notices and generated empty slots | `config/package.json` |
| Frozen English MCM comparisons | `tests/fixtures/mcm/` |
| Frozen ESP regression inputs | `tests/fixtures/esp/` |
| Product version and C++ dependencies | root `vcpkg.json` |
| Internal build identity | both `config/Clipboard.*Build.json` files |
| Native dependency identities | `config/dependencies/` |

## Building and testing

Run [`Build.ps1`](../../Build.ps1) from the repository root. The implementation
is in `tools/build/`; localization, diagnostics and xEdit helpers have their
own directories under `tools/`. Tests are grouped under `tests/build`,
`tests/native`, `tests/papyrus`, `tests/ui`, and `tests/localization`.
See [BUILDING.md](BUILDING.md) for prerequisites and validation boundaries.

Root `CMakeLists.txt` and `CMakePresets.json` configure the native component.
`CMakeUserPresets.json` remains ignored local configuration. Generated native,
Papyrus and UI records go under `build/`; new completed packages and their
manifests go under `dist/`. Existing `Deployments/` archives stay unchanged.

## Source and generated payloads

The build generates catalogs, string tables and translated guides beneath
`build/generated/localization/`, and the SWF beneath `build/generated/ui/`.
These are ignored outputs; edit their authored inputs instead. The package
recipe generates exactly 500 empty pattern slots during staging.

Candidates are assembled and verified under `build/stage/N/`. A completed
package, ZIP, manifest and records are published together to `dist/build-N/`.
The package directory is `Clipboard Resurrection - OG NG AE`; ZIPs append
` - build N` for tests or ` - Major.Minor.Patch` for releases.
See [PACKAGING.md](PACKAGING.md) for the generation and publication contracts.

The four English guides are edited in `docs/user/en/`. Run the documentation
extractor after English changes; it refreshes `localization/source/docs.en.json`
and `localization/metadata/docs-layout.json`. Preserve stable translation keys
and review state. Staging copies English guides to `Docs/clipboard/`, alongside
translated guides with their established localized filenames.

The DLL remains `F4SE/Plugins/clipboard.dll` in the package. Papyrus files,
ESP identities, MCM keys, serialized data names, and the installed `Scripts`,
`Interface`, `Strings`, `MCM`, and `Docs/clipboard` paths remain unchanged.

## Local history

Private Codex documents are under `.local/codex/docs/` and remain ignored.

Local compiler/game-script inputs are under `.local/toolchains/fallout4-papyrus/`,
with machine paths in ignored `.local/build-paths.json`. Published-build symbols
and diagnostic records are retained under `.local/build-history/`. None of these
are discovered from generated files. `build/` is disposable: it contains only
regenerable output, and cleanup archives any remaining unpublished evidence first.
The pre-migration snapshot and checks are local evidence, not public source.
Diagnostic cases live under `outputs/`. Routine repository cleanup archives
completed cases after 14 quiet days under `.local/outputs-archives/`, retaining
concise reports and restore pointers. Active fixtures and unresolved cases stay
unpacked. The policy is `config/outputs-retention.json`; ignored local case status
is managed through the cleanup command. See [CLEANUP.md](CLEANUP.md).
Historical v221 code stays in `legacy/v221/`; its ignored helper scripts are
under `tools/legacy/`. Its frozen Papyrus snapshot and manifests remain in
`Scripts/Source/Legacy221/`; this is not the maintained `src/papyrus/` tree.
The legacy stager reads static inputs from the immutable deployed v221 snapshot
under `Original Project Files/`, excluding its old compiled scripts, disabled
DLL and mod-manager metadata. It does not consume modern `assets/`.

Older reports preserve the paths and file identities observed at their recording
dates. Deleted generated paths are historical references, not build prerequisites.
Private navigation and the old-to-current path map are in
`.local/codex/docs/REFERENCE_MAP.md`. The root `AGENTS.md` contains current working
instructions; its previous accumulated history is archived with the private docs.

Versions remain in real dependency pins, runtime fixtures, compatibility code,
and historical records. Current source and package paths use purpose-based
names rather than `v240`.
