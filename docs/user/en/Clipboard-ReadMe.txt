Clipboard Resurrection 3.0.0

Clipboard copies groups of settlement objects into reusable patterns. Select
objects, save a pattern, then paste it relative to the Clipboard tool's position
and rotation. You can also move, rotate, scale, or scrap a selection.

INSTALLATION

Fallout 4 OG, NG, and AE use the same Clipboard package. Install an F4SE build
matching your installed runtime and the required Runtime Database, which provides
Data\F4SE\Plugins\f4rd-runtime.bin. Start the game through f4se_loader.exe.

F4SE: https://www.nexusmods.com/fallout4/mods/42147
Runtime Database: https://www.nexusmods.com/fallout4/mods/108394

Install the complete package with your mod manager and enable Clipboard.esp.
Keep the included DLL, scripts, Strings and Interface files together. If updating
from a package that used ClipboardExtension.dll, disable that old DLL so only
Data\F4SE\Plugins\clipboard.dll remains active. Back up your patterns before
replacing an existing installation.

Mod Configuration Menu (MCM) provides Clipboard's options and hotkeys. Without
MCM, settings can be overridden in Data\MCM\Settings\clipboard.ini under their
matching INI sections. Restart the game after manual INI changes.
MCM: https://www.nexusmods.com/fallout4/mods/21497

GETTING STARTED

1. Build the Clipboard Tool from the workshop's Special category.
2. Activate its controls and choose a selection method: All, Box, Sphere,
   Cylinder, By Cell, By Plugin, or the manual selection gun.
3. Select the objects you want, then save them to a pattern slot. Confirming an
   overwrite replaces that slot; cancelling leaves the existing pattern intact.
4. Place the tool at the destination and choose a saved pattern to paste.
   The tool's position and rotation determine where the group appears.
5. Wait for the import to finish before starting another operation. Review any
   warning about objects, wires, or power that could not be prepared.

There are 500 slots. The package supplies empty slots.
Pattern files are stored at Data\F4SE\Plugins\Clipboard\<slot>\pattern.ini.
Back up your own pattern files before reinstalling or replacing slot folders.

TEXT AND NUMBER INPUT

Clipboard includes its own input menu; TIM is not required. Use Enter to accept,
or Tab or Escape to cancel. Mouse buttons and an on-screen controller keyboard
are also available; follow the controls shown below the input. Keep
Interface/ClipboardInput.swf installed with the matching DLL and scripts.
Type values directly: Windows clipboard paste is not currently supported.

The MCM option "Hide Condition Boy/Girl During Input" is on by default. When
that mod is installed, Clipboard temporarily hides its overlay while input is
open, then restores the visibility it changed. No additional mod is required.

ROTATION, SCALE AND RESTORE

Rotation accepts degrees greater than 0 and up to 180, with up to six decimal
places. Use a dot as the decimal separator, for example 12.345678. Choose whether
to rotate around the selection's center or the Clipboard tool.

Increase and Decrease take a relative percentage change. Increase accepts more
than 0 and up to 1000; Decrease accepts more than 0 and up to 99. Up to six
decimal places are allowed, but every resulting object size must be an exact
whole-number percentage between 1% and 1000% of its original size. A valid
number may still be unsuitable for the selected sizes. Clipboard rejects it
before changing objects and shows valid increments, limits or nearby values.

For example, increasing 100% objects by 12% makes them 112%. Increasing a mixed
50% / 100% / 150% group by 2% gives 51% / 102% / 153%; a 1% increase is rejected
because some results would be fractional. Decimal changes can be valid:
increasing a 200% object by 0.5% makes it 201%.

Whole scaling changes object sizes and spacing together. Individual scaling
changes object sizes while keeping their positions. Both modes use the same
size restrictions; Clipboard does not silently round or clamp a rejected change.

Both Restore actions set every selected object to exactly 100% size.
Individual Restore keeps all positions. Whole Restore also keeps positions when
the selected sizes differ; if all sizes are equal, it adjusts spacing by the
inverse of that common scale. An already-100% group does not move. Restore is
not an undo history and does not recover earlier mixed-group spacing.

COMPONENT COSTS

Component payment is off by default. Enable it in MCM if pasting should consume
construction materials. Costs use available construction recipes for objects
admitted by the import rules; an admitted object without a recipe has no recipe
component cost.

Cost checks look in the current workshop container first. If it covers every
required material, Clipboard can show those local quantities without searching
linked settlements and the player. The read-only cost view offers an optional
full-source total. Otherwise Clipboard checks all sources and labels the totals
accordingly. Viewing costs does not authorize payment.

When payment is enabled, Clipboard asks for confirmation, checks availability
again after the prompt, and verifies withdrawals before placing objects. It
uses the current workshop, then linked workshops, then player inventory. If
payment cannot be completed or verified, placement stops. Resources already
removed are not automatically refunded.

PATTERNS AND IMPORTS

Patterns store object types, positions, rotations, scales, and internal wires.
They also record eligible wire connections to existing objects outside the
selection. Those outside objects are not copied. Reconnecting them requires
the original tool location and valid original endpoints; moving the tool to a
new destination ignores those external connections. Player-created endpoints
are specific to their save. See Clipboard-Object-Filtering.txt for details.

Patterns do not preserve container contents, weapon or armor modifications,
ownership, or script state. SS2 plot plans, levels and settlers are not saved.
If a required plugin or object is unavailable, Clipboard uses a bottlecap
placeholder. Wires attached to missing or filtered placement objects are skipped.

Clipboard reuses an existing object when its base object, position, rotation and
scale match the pattern placement. It avoids creating another copy and can use
the existing object for wire connections. The duplicate-skipped count appears
only when greater than zero, even if import warnings are disabled.

Use Throttling is off by default. It limits placement work and simultaneous
workshop initialization calls. Turning it off may overload scripting and cause
Papyrus stack dumps that interfere with other mods such as Sim Settlements 2.
Completion checks remain active, and turning it off may not shorten the import.
During throttled placement, "Clipboard: Importing objects..." repeats every ten
seconds. Initialization and powering up show progress percentages in either
mode. These report completed work, not elapsed time; wait for the final result.

Objects that fail preparation or initialization are excluded from later wiring
and power work; successful objects can continue. If Clipboard cannot verify
completion, such as after a timeout or interruption, wiring and power stop for
the whole import.

Selection/export and light-source import have separate options. Imported
non-actors have Havok physics disabled by default to keep loose objects in
place; scripts, animations and workshop actions can still move them. See
Clipboard-Object-Filtering.txt for these controls and optional mod filters.

Keep a save from before upgrading if you may return to an older version.
Older Clipboard DLLs cannot resume operations saved in the new 3.0.0 format.

Clipboard follows the game's language. See Clipboard-Localization.txt for
language choices and troubleshooting, and CHANGELOG.txt for recent changes.

HELP AND CREDITS

Warnings, errors and build identities are recorded in Clipboard.log even when
"Enable Diagnostic Logging" is off. Turn it on in MCM before reproducing a
problem when more detail is needed. Include the action, game version, settings,
and Documents\My Games\Fallout4\F4SE\Clipboard.log; include Papyrus.0.log when
available. Copy Clipboard.log before restarting the game: each launch replaces
it with a new log.

Project and usage videos:
https://www.nexusmods.com/fallout4/mods/39804/
https://www.nexusmods.com/fallout4/mods/39804/?tab=videos&BH=0

Thanks to Struckur, Everett C Sands, WolfMark, Big&Flabby, and the F4SE and
CommonLibF4RD contributors.

Clipboard-owned code and documentation use GPL-3.0-or-later. See LICENSE.txt
for the full terms, including the absence of warranty. Third-party notices
are in Licenses; their separate terms remain applicable.
