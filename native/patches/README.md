<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Local CommonLibF4RD patches

The patches below apply to the vendored CommonLibF4RD upstream
baseline `8a1da09250c16ac909de2bca94c8a66a4e7fd956`. The dependency and these patches'
vendor changes retain the dependency's MIT terms and existing notices in
`external/CommonLibF4RD/LICENSE`. No v221 or historical source is changed.

The pristine 407-file normalized text tree SHA-256 is
`1d2b4b6c9b60aa7e6ebcaf309fed54b94a9c8b17b0613e8117589cdb48dde7b8`.
The lazy-globals-only predecessor tree SHA-256 is
`d533b76e2b8835af7e11de6922746754b966e15e5b3fe24485f051a030936a61`.
The current tree with both patches has SHA-256
`9da28d71c89df24e7551343a31d84b8d6b7d88abb85d6f1cf38ebfd96e9576e0`.
`CommonLibF4RD.snapshot.json` records the upstream and current digests, the truthful upstream baseline
commit, and each patch file SHA-256. `tools/Test-V240VendoredDependency.ps1`
validates the patched tree using its existing normalization algorithm.

## Lazy globals: reason and API adjustment

The original `RE/Fallout.h` umbrella brings five namespace-scope relocations into
Clipboard's translation units. Binary inspection confirmed all five were linked
into the DLL's CRT initializer table and called `REL::IDDatabase::id2offset`
before `F4SEPlugin_Load`. A missing database or symbol could therefore terminate
the process before Clipboard's checked loader preflight.

The patch changes only those five declarations to inline accessors returning a
reference to a function-local static relocation. Their types and Runtime Database
IDs remain unchanged. Merely including the headers now performs no lookup for
these declarations; a future caller triggers resolution when invoking an accessor.

| Previous variable | Replacement accessor |
|---|---|
| `RE::PowerArmor::fNewBatteryCapacity` | `RE::PowerArmor::GetNewBatteryCapacity()` |
| `RE::CombatUtilities::fWorldGravity` | `RE::CombatUtilities::GetWorldGravity()` |
| `RE::Workshop::CurrentPlacementItemData` | `RE::Workshop::GetCurrentPlacementItemData()` |
| `RE::Workshop::CurrentRow` | `RE::Workshop::GetCurrentRow()` |
| `RE::Workshop::PlacementItem` | `RE::Workshop::GetPlacementItem()` |

This is a source API adjustment for consumers of these variables: replace the
old qualified variable with the corresponding accessor call. The returned
relocation reference supports the previous relocation operations. Searches of
the pinned dependency and maintained Clipboard source found no consumers, so no
call sites require changes. Clipboard's 32-symbol operational dependency preflight
does not need these five unused symbols. Any future Clipboard use must first add
the relevant tuple to that preflight inventory.

## Reproduction and checks

From a pristine copy of the upstream baseline, apply the patch with
`external/CommonLibF4RD` as the current directory:

```powershell
git apply ../../native/patches/commonlib-lazy-globals.patch
git apply ../../native/patches/commonlib-papyrus-ownership.patch
```

Use `git apply --check --reverse` against the vendored headers to check that
each patch is present. Retrieve pristine files from the pinned upstream commit
when comparing the baseline. Only line endings are normalized when comparing
source trees; the patches preserve unrelated source text.

Rebuild the plugin and inspect its CRT initializer table/disassembly to verify
that no initializer for these five globals remains. A successful source hash
check alone does not prove the resulting DLL's initialization order, and neither
check certifies in-game behavior.

## Papyrus final ownership

`commonlib-papyrus-ownership.patch` adds the existing `F4_HEAP_REDEFINE_NEW`
operators to `BSScript::Array`, `Struct` and `Object`. The generic intrusive
smart pointer deletes its pointee when its counter reaches zero. Those three
engine-created types previously had no game-heap delete, so a final plugin
release called the CRT deallocator. Official F4SE and the OG/NG/AE executable
release paths instead destroy the payload and use the game's heap. The
existing MemoryManager resolver tuples already cover this path; no address,
layout, vtable, refcount protocol or public Papyrus contract changes.

The same patch corrects `Object::~Object` to decrement the 32-bit counter at
`lockStructure & ~1`, matching all three engine destructors. The prior body
decremented a local copy of that address and left the shared counter unchanged.
The pointer tag, null check, variable teardown and remaining members are kept.

The ownership patch affects four files and is independent of the lazy-globals
patch. Both patches
retain the dependency's MIT licensing; neither changes the upstream pin.

`RuntimeCompatibilityTests.cpp` checks the heap operators are declared and
the three sizes unchanged at compile time. Actual Object teardown invokes the
game string-pool release even for empty fields, so it cannot be executed in a
standalone host without an artificial engine environment. Inspect the matching
DLL/PDB for the corrected pointed-counter decrement and game-heap final-release
paths instead. These static checks do not execute the game allocator, certify
F4SE loading or prove live save/revert behavior.
