# Clipboard input asset

`src/ui/ClipboardInput/ClipboardInput.as` is the authored source for the dedicated
`ClipboardInputMenu`. It does not modify `SPECIALMenu`, TIM, or the retired
`Clipboard.swf` HUD widget. The native adapter is
`src/native/ClipboardInputUI.cpp`.

## Build

Run `tools/build/Build-ClipboardInputUI.ps1` from PowerShell. It uses the installed
JPEXS 26.2.1 compiler library and its Flash player type definitions, together
with a JDK. Supply `-FFDecRoot` and `-JavaHome`, or set `FFDEC_ROOT` and
`JAVA_HOME`. Otherwise it looks for the standard FFDec installation and a JDK
containing `javac.exe` on `PATH`. The numbered deployment helper forwards both
options; see the [build guide](BUILDING.md).
No compiler binaries or Flash player libraries are distributed with the mod.

The helper creates `build/generated/ui/Interface/ClipboardInput.swf` and records its
protocol, content hash, source hashes, and compiler/type-library hashes in
`build/interface/clipboard-input-build.json`. The numbered deployment workflow
builds this asset alongside the DLL and scripts. Rebuild the asset after any
source/helper change; do not edit the generated SWF or its build record.

The Java helper constructs the minimal AS3 movie directly, compiles the source,
and reopens the result to verify the document class and script count. Stage
size is 1280 by 720 with aspect-preserving scaling. JPEXS script export provides
an additional compiled-bytecode/decompilation inspection. Neither check is live
Scaleform rendering, input-device, font, or runtime ABI certification.

## Protocol 1

The native service owns each request's immutable token, input rules, retained
terminal result, and release state. The movie receives already localized text
and one numeric/text validation mode. `clipboardProtocol` is checked before
`Initialize`. `Initialize` then calls the native `ClipboardReady(token, 1)`
function after constructing and focusing its controls.

`ClipboardSubmit(token, text, cancelled)` returns a synchronous Boolean
acknowledgment. The movie disables its controls only after a true result. A false
result preserves the entry for correction or resubmission. Numeric validation,
UTF-8 validity, UTF-16 length, forbidden control characters, and exactly-once
terminal commitment are native responsibilities; a text-field filter is not a
security or correctness boundary. Empty submission follows the public wrappers'
cancel behavior. Submitted text is never used as HTML and is not logged.

The native adapter queues token-bearing show/hide messages on the UI lane.
It retains its lease through queued creation and actual menu removal. An old
show/hide payload cannot target a later token. Movie/handler references are
local to one menu; the callback handler stores a token, never a menu pointer.
Text-entry mode is balanced at removal/destruction. Delayed closing cannot
release focus merely by committing a result.

## Input behavior

- Protocol input types: 0 is an unsigned integer, 1 is a positive decimal with
  at most six fractional places, and 2 is single-line text. Decimal bounds are
  checked natively against the complete string before float conversion; invalid
  submitted text is not truncated or stripped into a different value. Decimal
  controller input has a dot key. The wire protocol version remains 1.
- Keyboard: ordinary text editing, Enter accepts on release after a press in
  this dialog, Tab or Escape cancels. The movie returns true from the engine's
  `ProcessUserEvent` callback so mapped buttons cannot pass through to underlying
  menus or world objects, including while initialization or closing is pending.
  Windows clipboard paste is not currently supported. Type values when checking
  numeric validation.
- Mouse: select/edit the field; click localized Accept or Cancel.
- Controller: the first action reveals a character grid. D-pad selects a key,
  A enters it, B cancels, X deletes, Y changes letter case, and Start accepts.
  Accept/Cancel are also selectable grid targets. The controller text grid is
  Latin letters, digits, space and common punctuation; a physical keyboard can
  enter other supported Unicode characters.
- No deadline applies while the menu is ready and the player is typing.
- The menu pauses game simulation, disables the pause menu while it owns focus,
  and does not permit saving. Load/interruption tests are performed through
  controlled state tests or a separately initiated load where the game allows it.

## Presentation

The menu uses a compact, centered 500-unit panel in the 1280-by-720 design space.
Title and entry use the game's `$MAIN_Font_Bold` alias and centered alignment;
thin bracket rules and a translucent green fill replace the large opaque frame.
The native menu requests the engine's blurred background. A small, separate
bottom button bar shows Enter/Tab hints (Start/B after controller use). The
localized D-pad help remains inside the panel. The character grid consumes layout
space only when revealed, and wrapped title/help/error text expands the panel.

This is independently authored presentation. FallUI Confirm Boxes replaces its
own message/confirmation SWFs and its settings do not theme this custom menu.
No TIM or FallUI asset, ActionScript implementation, or host button bar is loaded.

## Focused live checks

Exercise all five existing prompts, cancellation, invalid/boundary numeric values,
Unicode and 50-character names, rapid reopen, long typing, and both input devices.
Confirm the game does not react to typed hotkeys and that cursor, movement and
menus recover after every outcome. Test with TIM installed for other mods and
without TIM. A missing/incompatible asset must report a failed request, leave
the operation unapplied, and permit recovery after the correct asset is restored.
Full settlement imports are not necessary for these UI checks.

For the ENTER regression, leave the cursor over a world object outside the panel
and press/release Enter in a numeric prompt. The value must apply once without
activating/selecting that object. Repeat with held Enter and an invalid value;
the invalid value must keep the prompt open. Check mouse Accept and Tab/Escape
once as well. `node tests/ui/Test-ClipboardInputUI.mjs` exercises the actual source
event-handler bodies with host fixtures; it does not certify live Scaleform or
engine input routing. It also accepts an exported compiled `.as` path.
