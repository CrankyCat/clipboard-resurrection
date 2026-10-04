# Manual repository cleanup

Use PowerShell 7 and the project Python resolver. Cleanup is manually invoked
and independent of builds. The helper handles inventory, backup and verification;
you do not need to move files or prepare a plan for routine cleanup.
Source, toolchains and publications remain protected. Completed diagnostic
evidence is retained in verified, restorable archives.

## Run the cleanup report

From the repository root:

```powershell
pwsh -NoProfile -File .\tools\maintenance\Clean-ClipboardRepository.ps1
# Archive eligible diagnostic cases and build output, then prune verified copies.
pwsh -NoProfile -File .\tools\maintenance\Clean-ClipboardRepository.ps1 -Apply
```

Without `-Apply`, the command reports output-case retention, generated build
output and historical audit status. With `-Apply` and no explicit historical plan,
it automatically archives eligible output cases as described below, then invokes
build-folder cleanup: checks persistent inputs, creates and verifies a complete
backup under `.local/build-history`, removes `build/`, then checks input discovery
again. All preparation is automatic. An absent build folder is already clean;
the overall result is `retained` when protected output cases still occupy space.
The outputs report gives case sizes, actions and retention reasons, with a full
JSON inventory under `.local/outputs-retention-reports`.

The default historical audit is the completed October 3 pass. Its status is
`already-clean`, with 620 previously removed files and zero historical files removed
by this invocation. The report verifies the exact reviewed plan, complete deletion journal,
unchanged candidate audit, retained database SHA-256 and original-path index.
It creates no new plan for a completed audit. Missing files alone are never treated
as proof of cleanup: unexplained absence, partial applies, restored candidates or
damaged retention require investigation and a fresh audit.

The separate historical-audit component covers only the supplied audit. It does
not control the reusable outputs retention or build cleanup. New irreversible
historical removals still need a fresh inventory and consumer review; `-AuditPath`
selects an audit within that component's narrow removal policy.
`Clean-ClipboardBuild.ps1` remains available directly for build-folder cleanup.

## Automatic outputs retention

`config/outputs-retention.json` defines the shared policy. Ignored local case
status is recorded in `.local/outputs-retention.json`. A case is a top-level
directory under `outputs`, except `import-diagnostics` and `destroy-crash-captures`,
whose immediate child directories are separate captures. Loose collection/root
files stay in place.

* Keep active fixtures, currently configured build inputs, unresolved cases and
  unclassified cases unpacked. A successful `verification.json` is not sufficient
  to mark a gameplay investigation resolved. The maintained xEdit, translation
  and object-probe workspaces are explicitly protected.
* Archive explicitly completed cases after **14 days without file changes**.
  The approved September 12 crash capture has an immediate archival override.
* Store the complete case under `.local/outputs-archives/<content-id>/`, using
  compressed ZIP64 data and a manifest of original paths, hashes and timestamps.
  Identical contents are stored once within each case archive. Intermediates and
  redundant copies leave the unpacked tree but remain recoverable from the archive.
* Keep concise Markdown reports and small `verification.json`, `summary.json`,
  `analysis.json`, `capture.json` and `report.txt` files at their original paths,
  subject to the size limits in the policy. The full case always remains archived.
* Verify the archive inventory and every content SHA-256 before removal. Recheck
  surviving original files, refuse changed/new files, and delete each verified
  file through its checked Windows handle. Remove only empty subdirectories.
  Leave originals unpacked when the archive would not save space.

The September 12 dump has limited value for unrelated future faults, but retains
memory and thread state that cannot be reconstructed from logs if the same fault
returns. Its exact historical cause remains unproven. Lossless archival reduces
storage without discarding that unique evidence. Archives have **no automatic
expiry or permanent-deletion policy**.

The first retention run on October 3 archived that capture and the retired
`outputs/pre-relocation-build` case. Unpacked outputs fell from 32.57 GiB to
11.47 GiB; retained archives and records use 3.16 GiB, for a net logical reduction
of 17.94 GiB. All 27 package ZIPs and the build-128 counters remained unchanged.
Local verification is `.local/outputs-retention-implementation/verification.json`.

No manual file movement or JSON editing is required. Use these case actions
separately from `-Apply`:

```powershell
# Record completion; the next eligible cleanup archives it after the quiet period.
pwsh -NoProfile -File .\tools\maintenance\Clean-ClipboardRepository.ps1 `
    -CompleteOutputCase 'outputs/my-completed-case'
# Protect a case that is still being investigated.
pwsh -NoProfile -File .\tools\maintenance\Clean-ClipboardRepository.ps1 `
    -KeepOutputCase 'outputs/my-active-case'
# Restore the archived crash capture to its original paths.
pwsh -NoProfile -File .\tools\maintenance\Clean-ClipboardRepository.ps1 `
    -RestoreOutputCase 'outputs/destroy-crash-captures/20260912-114013-421'
```

Restore verifies all archived contents, refuses to overwrite changed existing
files, restores missing files, and marks the case active. Each archived case has
an `OUTPUTS-ARCHIVE.txt` restore command and `.clipboard-output-archive.json`
manifest pointer. Keep the ZIP, manifest and original-case pointer together in
backups. The ZIP uses content hashes as entry names; use the restore command to
recreate the original filenames and directories.

The archive and manifest are complete before pruning starts. An interrupted prune
can be resumed with the same routine `-Apply`; it validates the existing archive
and surviving files before continuing. It does not recompress a partially pruned
case. Changed files require review and remain protected. A partially written
archive is preserved separately and can be recreated from the intact originals.
Allow temporary free space for the archive while originals still exist.

Outputs cleanup takes the publication mutex and checks policy, case-status and
configured-input changes before deletion. Keep the checkout idle while applying:
unrelated programs do not necessarily participate in that mutex. Archived files
are not a substitute for an independent backup.

## Create and review a plan for an unapplied audit

When all audited candidates are still present, the report creates a **new** JSON
file under `.local/cleanup-plans/`, prints its SHA-256 and totals, and leaves every
candidate unselected. Existing reports are never overwritten.
`-PlanPath '.local/cleanup-plans/my-review.json'` selects a specific new report
filename. `-PythonPath` is optional; otherwise the existing
`Get-ClipboardPython.ps1` resolver finds installed or bundled Python.

The original input is `.local/cleanup-audit/2026-10-03`. Each candidate
has an exact original path, reason, action, logical byte count, SHA-256, file ID,
link count and timestamp. Current files are hashed again; audit hashes are not
substituted for fresh reads. Current source, helper, configuration, publication
and build-record identities are recorded as guards against stale consumer review.

Review the plan and copy it to another JSON file in `.local/cleanup-plans/`.
Change only `selected` to `true` on individually accepted entries and add a
meaningful `review` explaining their disposition. A runtime database review must
address historical replay consumers of that original path. Identical bytes alone
do not establish that removing the path is acceptable. Unselected rows remain in
the plan so the original-path index is complete. There is no `-Force` bypass.

After the historical removal decision has been approved, explicitly apply that
reviewed file. This advanced mode is separate from the routine `-Apply` above:

```powershell
$plan = '.local/cleanup-plans/approved.json'
$reviewedHash = (Get-FileHash -LiteralPath $plan -Algorithm SHA256).Hash
pwsh -NoProfile -File .\tools\maintenance\Clean-ClipboardRepository.ps1 `
    -Apply -PlanPath $plan -ReviewedPlanSha256 $reviewedHash
```

The digest binds the invocation to the reviewed bytes; it is not an electronic
approval or a signature. Review the file before obtaining its digest. An unselected
report is not authorization to remove history. The October 3 approved plan has
already been applied and must not be supplied to `-Apply` again.

## Historical-plan guarantees and limits

* Use the same named Windows mutex as the build/publication helpers, including
  release export. A competing build, publication or cleanup causes refusal.
* Require exact workspace containment and reject traversal, drive/UNC paths,
  alternate streams, ambiguous Windows names and all reparse points, including
  junctions in ancestors. Reject hardlinked removal candidates.
* Permit only exact paths in the audit's narrow compiler-intermediate list or its
  full runtime database group. Other paths stay protected even if a plan is edited.
* Recheck configuration and preservation baselines, guard sets and hashes. Active
  input paths from `.local/build-paths.json`, native/Papyrus records, CMake presets/includes and relevant
  environment variables are additional exclusions. All external paths are outside
  the removal scope. Changed configuration requires a new review.
* Open **all** selected files without write/delete sharing and verify their size,
  timestamp, file identity and SHA-256 before deleting anything. Validate each
  opened handle's final path. Delete using the verified Windows handle, not a
  later lookup of the filename. Directories are never removed by the helper.
* Before any runtime removal, verify every original in that hash group, copy one
  database to `.local/cleanup-retained/sha256/<sha256>/f4rd-runtime.bin`, verify it,
  and write `.local/cleanup-retained/indexes/<reviewed-plan-sha256>.json`. The index
  records all original paths and identities, retained bytes and selected removals.
  Retention files are write-once through this helper; existing corrupt content is
  rejected, never overwritten. Retained bytes and index stay locked during removal.
  This uses a separate byte copy, with no hardlinks or junctions.
* Preserve the reviewed plan and a flushed per-file intent/completion journal under
  `.local/cleanup-runs/<id>/`. Validation failure leaves candidates untouched.
  Disk or process failure after removal begins can leave a **partial apply**;
  this is not an atomic transaction or an automatic rollback mechanism.

Allow free space for the retained database (264,506,801 bytes) and small records
before runtime removal. An interrupted copy/index is retained for investigation;
the helper does not erase or overwrite it on retry. Do not rerun a partly applied
plan blindly: consult its journal, reconcile missing originals, and create a new
audited baseline for the remaining paths. In particular, applying only part of a
runtime group makes its old full-group preflight intentionally fail on a later run.

Keep this checkout idle while applying. The mutex coordinates project publication
entry points; unrelated programs, manual compilers, editors or a process in another
Windows login session do not necessarily participate. This is not a defense against
an administrator concurrently altering the filesystem. The helper cannot discover
an arbitrary command line's future inputs. Review any such consumers explicitly.

To reproduce a historical inspection after consolidation, use the original-path
index and verified retained SHA-256 to restore a **copy** of the database at the
required original location before running that historical inspector. Never rewrite
the historical report to imply it was originally captured at the retention path.
Keep the retained database and index together in any later backup/archive.

## October 3 candidate scope

The initial historical pass was applied on October 3, 2026: all 70 full database originals
were retired after retaining one verified copy and the complete original-path
index, and the 550 audited compiler intermediates were removed. Net historical
file-size reduction is 19,630,766,925 bytes (18.283 GiB). The remaining categories
below were protected by that pass. The subsequent outputs-retention policy above
now supports lossless case archival. The original audit and plan are historical evidence;
do not reuse them to apply a second cleanup. A future historical cleanup needs
a fresh inventory and reviewed plan. See the local completion record at
`.local/disposable-build/2026-10-03/verification.json`.

For the reusable build-folder cleanup, use `Clean-ClipboardBuild.ps1` as described
in [BUILDING.md](BUILDING.md). It is independent of the consumed historical plan.

| Category | Scope | Treatment |
| --- | --- | --- |
| Full runtime database | 70 files, each 264,506,801 bytes; SHA-256 `21c2ee6c7af0e3afe4747db70dc2e3da91d952b2b2c820ff7a1bd7d803cbe1b9` | Proposal: retire reviewed original paths into one retained blob and complete path index. Removing all 70 originals after copying one saves a net **18,250,969,269 logical bytes (16.998 GiB)**, equivalent to 69 copies. |
| Seven-byte database stubs | Four files | Excluded; not usable database copies. |
| Pre-relocation compiler intermediates | 550 files: 326 `.obj`, 2 `.pch`, 198 `.tlog`, 20 `.lastbuildstate`, 4 `.exp` | **1,379,797,656 bytes (1.285 GiB)** proposed for explicit removal after review. The full mixed tree is 2.003 GiB. |
| Duplicate xEdit game assets | Two copies each of `Fallout4.esm` and `Fallout4 - Interface.ba2`; 819,856,591 duplicate bytes (0.764 GiB) | Protected; both fixture trees still have consumers. |
| Historical extracted build 125 | 668 files, 6,478,064 bytes (6.178 MiB), exact ZIP match | Protected along with all historical Deployments. |
| Crash dump | `outputs/destroy-crash-captures/20260912-114013-421/Fallout4.exe_260912_054049.dmp`, 21,882,130,286 bytes (20.379 GiB) | Excluded from the initial deletion audit. Subsequently approved for automatic lossless case archival under the retention policy above. |

These are logical sizes, not measured allocated-space savings. The compiler
candidate list is explicit; no entire containing directory is disposable.
Remaining Papyrus sources, `.lib`, `.dll`, `.pdb`, logs, binlogs, JSON metadata,
captured reports, save games and live acceptance evidence were excluded from that
initial removal list. Completed cases may now be archived with original-path
manifests and restore pointers; active/unresolved evidence remains unpacked.

## Consumers that require the current paths

| Current consumer | Required location or policy |
| --- | --- |
| `tools/xedit/Initialize-LocalizationWorkspace.ps1` and `Start-LocalizationXEdit.ps1` | `outputs/localization-implementation/xedit` remains the prepared xEdit workspace. |
| `tools/xedit/Start-XEditLocalizationVerification.ps1` | Default `outputs/localization-implementation`; supports explicit evidence roots below `outputs`. The documented additional-locale workspace is `outputs/additional-localization/xedit`. Preserve both complete trees. |
| `tools/localization/ClipboardLocalizationEsp.py` | Manual capture's default ESP is `outputs/localization-implementation/xedit/Data/Clipboard.esp`. Routine packaging supplies its inputs explicitly; that does not retire this diagnostic default. |
| `tools/localization/Translate-Localization.py` and `docs/development/LOCALIZATION.md` | Existing usage/report defaults are `outputs/localization-translation/usage.json` and `outputs/additional-localization/translation/<locale>/usage.json`; the guide also uses `outputs/localization-build/manifest.json`. Preserve report provenance. |
| `tools/diagnostics/Capture-ClipboardImportDiagnostic.ps1` | New captures continue under `outputs/import-diagnostics`; staged identity now comes from a fully verified numbered `dist/build-N` publication. |
| `tools/diagnostics/Capture-ClipboardDestroyCrash.ps1` and `Analyze-ObjectProbe.py` | Evidence destinations remain `outputs/destroy-crash-captures` and `outputs/object-probe-analysis`. |
| `tests/native/Test-ClipboardRuntimeMatrix.ps1` | Requires explicit external database/game images, defaults new output to `build/validation/runtime-matrix`, and copies the database into its `offline-root/Data/F4SE/Plugins` for the inspector. Historical offline-root copies are replay inputs. No maintained helper has a fixed default to one of the 70 candidate paths. |
| `tools/build/ClipboardBuildPaths.ps1` | The audit recorded the compiler at `build/papyrus/game-fixture-20260911`. The separately authorized compiler relocation now uses `.local/toolchains/fallout4-papyrus` and persistent `.local/build-paths.json`, without a previous-build discovery fallback. Protect the whole toolchain and local configuration. Historical build records retain their original paths. |
| `tools/build/ClipboardBuildIdentity.ps1` and `Export-ClipboardRelease.ps1` | Preserve numbered and release archives, manifests and records in both `dist` and `Deployments`: they guard number reuse and enable verified release exports. |

The reference scan covers maintained tools, tests, configuration and developer
docs, with a separate check of the four ignored `tools/legacy` helpers. Historical
reports and ad hoc commands can still depend on original runtime database paths;
their replay requirement is why runtime rows need an explicit disposition review.

Keep authored source/assets/catalogs/regression fixtures, external configured
inputs, `Original Project Files`, `legacy`, `CMakeUserPresets.json`, both build
counters, all current DLL/PDB/PEX records, all 27 existing ZIPs and rollback
packages, `.local/codex/docs`, and `.local` rollback/audit/evidence directories.
Historical plan application never deletes `build`. Routine `-Apply` delegates to
the disposable-build helper with its retention/preflight requirements; see
[BUILDING.md](BUILDING.md). The historical plan itself does not authorize other
removal categories.
Product 3.0.0 and internal build 128 remain unchanged. No package allocation,
mod rebuild, game launch, game/MO2 installation or Papyrus stamping is part of
this workflow.

Unique evidence is archived only as complete eligible cases through outputs
retention. The older historical-plan mode cannot delete unique evidence.

## Publication lookup and validation

```powershell
.\tools\build\Get-ClipboardPublication.ps1                  # highest numbered dist/build-N
.\tools\build\Get-ClipboardPublication.ps1 -InternalBuild 128
$python = & .\tools\build\Get-ClipboardPython.ps1
& $python -B .\tests\build\Test-RepositoryMaintenance.py
& $python -B .\tests\build\Test-OutputsRetention.py
```

Publication lookup verifies one successful record, build/product identity,
archive and manifest hashes, every staged and ZIP file, exact file sets and
containment. A corrupt or incomplete highest-numbered publication is rejected;
it does not silently fall back. Select an older numbered publication explicitly.
Historical flat packages remain untouched and are not capture-stage candidates.

Import capture accepts `-InternalBuild` and `-PythonPath`. Explicit installed-mod,
game-user, pattern, settings and profile inputs, publication and DLL/PEX identities
are checked before creating a capture directory. Missing optional logs remain
visible in the resulting capture; differing installed hashes remain useful evidence.
Preflight cannot prevent later disk errors or a log changing during copying; the
existing source-stability records still apply. Disk identities do not prove which
files the game loaded through the mod manager.

The tests create disposable checkout fixtures under `build/validation/cleanup-tests`
or the Windows temporary directory and exercise deletion only in those fixtures.
They include archive corruption, interrupted prune/resume, byte-exact restore,
new/changed evidence, protected cases, quiet-period selection, cross-process PowerShell
mutex contention, changed/missing files, protection rules, containment, junctions,
hardlinks, content retention/indexing, publication failures and capture preflight.
No full mod build or live runtime test is needed for these maintenance changes.
