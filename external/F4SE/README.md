# Current official F4SE reference

[`current-reference.json`](current-reference.json) designates the complete,
unmodified official **F4SE 0.7.9** tree fetched into
`external/F4SE/source-0.7.9`. It is a pinned 441-file hash manifest.
The source cache is ignored by Git and excluded from Clipboard packages;
fetch it from upstream after cloning this repository.

The [official release](https://github.com/ianpatt/f4se/releases/tag/v0.7.9)
is pinned to commit `4692e9bba0f87d8b8d1a0b79110bf212f2b2ada7`.
The [official F4SE site](https://f4se.silverlock.org/) associates it with the
Fallout 4 1.11.240 fixture. All 441 files matched a fresh checkout of this tag
on 2026-09-06 (local date); comparison normalizes text line endings and hashes
non-UTF-8 files verbatim. The aggregate hash is
`84bef19380ad0ea2276860a892e4586ed0ce28dd27edec10cd451ed43cfff183`.
Each record hashed into the aggregate is its lowercase file hash, two spaces,
forward-slash relative path, and LF, sorted by ordinal path.

Run from the project root:

```powershell
.\tools\Initialize-F4SEReference.ps1
.\tools\Test-F4SECurrentReference.ps1
.\tools\Build-Papyrus.ps1 -CheckOnly
```

The default Papyrus target `Modern079` verifies the full source reference before
merging its vanilla and modified script imports. It generates imports and checks
under `build/papyrus/v240`; a full build also records the source commit, reference
manifest hash and tree hash with its PEX metadata. `-Target Legacy221` keeps the
preserved 0.7.8 inputs and original `build/papyrus/v221` paths. `Build-V221.ps1`
selects that legacy target explicitly.

This official tree is an ABI reference and Papyrus import source. The modern
DLL compiles the independently pinned CommonLibF4RD F4SE adapter and Clipboard's
maintained bridge; it does not compile this official SDK tree. The old SDK trees
under `Original Project Files` and `native/v221/vendor` remain historical or
legacy reproduction inputs. The OG/NG source references remain useful for
cross-family ABI review and are not obsolete modern build inputs.

Upstream F4SE source retains its own notices and terms. This pointer does not
relicense or redistribute it, or replace CommonLibF4RD. The initializer verifies
the upstream tag's exact commit and the complete normalized source hash. An
existing cache is verified without being overwritten. No historical analysis
folder is needed for the build.
