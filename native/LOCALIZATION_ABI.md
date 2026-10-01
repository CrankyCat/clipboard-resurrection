# Localization native contract

This document describes the maintained OG/NG/AE localization implementation and
the source references for its engine interfaces. Source and host checks do not
certify in-game rendering or runtime behavior.

## Additive Papyrus service

`ClipboardExtension.GetText(String key, String arg0="", String arg1="",
String arg2="", String arg3="", String arg4="", String arg5="")` returns a
String and is an ordinary global native. Diagnostic logs remain English. The
catalog service does not depend on MCM, TIM, Scaleform or HUD interfaces.

The English catalog loads before native registration, without accessing game
settings. `kGameDataReady` snapshots the game language and loads the selected
supported translation. Read/format and initialization use a shared mutex, so concurrent
Papyrus callers cannot observe a partial catalog. There is no polling,
per-notification file read, settings mutation, saved localization state or
callback/job change. Restart the game after changing its language, consistent
with ESP/MCM translation selection.

## Runtime dependencies and fixture anchors

The pinned CommonLibF4RD `include/RE/Bethesda/Settings.h` supplies both exact
family tuples, which are also listed in `RuntimeSymbols.h`. No raw address is
called. Both wrappers return a pointer
by reading an engine-owned global pointer; no engine function or virtual method
is invoked. `Setting::GetKey`, `GetType` and `GetString` read fields locally.

| Global | OG ID | NG/AE ID | OG 1.10.163 RVA | NG 1.10.984 RVA | AE 1.11.221 RVA | AE 1.11.240 RVA |
|---|---:|---:|---:|---:|---:|---:|
| `INISettingCollection*` | 791183 | 2704108 | `0x05EDB528` | `0x03195198` | `0x0343B038` | `0x0344B4B8` |
| `INIPrefSettingCollection*` | 767844 | 2703234 | `0x05B5BE58` | `0x030EF6D0` | `0x03394A60` | `0x0339FAE0` |

These RVAs come directly from each fixture's official `f4se/GameSettings.cpp`.
They are assertions in `RuntimeCompatibilityTests.cpp`, used only to reject an
incorrect resolver result when inspecting that exact executable. They are not
fallback addresses or a runtime patch whitelist. The compiled calls continue
to use CommonLib's Windows-x64 singleton-pointer wrappers and Runtime Database.
The complete build/matrix report records actual address-resolution results.

Source identities:

- OG F4SE 0.6.23: `ea143b59b14346d941f5efe42d97769cac83fa38`.
- NG F4SE 0.7.2: `7ad9b820f08273f29d8313e551a6e09ff10f9b64`.
- AE F4SE 0.7.9: `4692e9bba0f87d8b8d1a0b79110bf212f2b2ada7`, verified reference
  under `external/F4SE/source-0.7.9`.
- AE 1.11.221 anchors: F4SE 0.7.8 `f4se/GameSettings.cpp`.
- CommonLibF4RD upstream baseline: `8a1da09250c16ac909de2bca94c8a66a4e7fd956`.
  `CommonLibF4RD.snapshot.json` and `patches/README.md` describe the vendored tree
  and its local patches. Project integrity checks validate that tree.

## Exact setting/list access

Official F4SE `Translation.cpp` reads `GetINISetting("sLanguage:General")` and
uses a string setting for the language suffix, with `en` as its missing-setting
fallback. `GameSettings.cpp` searches the INI collection first and then the
preferences collection only when the key is absent. Clipboard uses that same
key and precedence. Missing collection/key, wrong type, empty value or an
unsupported language select English. After trimming surrounding ASCII
whitespace, the selector accepts these case-insensitive fixed codes:
`en`, `ru`, `de`, `es`, `esmx`, `fr`, `it`, `ja`, `pl`, `ptbr`, `zhhans` and
`zhhant`. The existing full-name values `english` and `russian` continue to
select English and Russian. Regional Spanish and Chinese script variants remain
separate selections. The case-insensitive legacy `cn` suffix used by Fallout 4's
Traditional Chinese archives maps to canonical `zhhant`, including the same
surrounding-whitespace normalization. It does not add a thirteenth native
catalog. Unknown aliases select English. The selector returns only
static allowlisted suffixes, never the supplied setting text. Runtime loading
appends `.tsv` to that fixed suffix beneath the configured catalog directory.
This expansion changes no Papyrus declaration, saved state, ABI, engine layout
or runtime dependency.

The OG/NG/AE official `GameSettings.h` declarations agree on the fields accessed:

| Storage | Offset/size |
|---|---|
| `Setting` value union / string pointer | `0x08` |
| `Setting` name pointer | `0x10` |
| `Setting` size | `0x18` |
| `SettingCollectionList` unused field | `0x118` |
| `SettingCollectionList` first heap-node pointer | `0x120` |
| INI and preferences collection size | `0x128` |
| List node | Setting pointer at `0`, next pointer at `8` |

CommonLib represents `0x118/0x120` as the embedded first node of `BSSimpleList`.
Its stock `INISettingCollection::GetSetting` starts with that embedded value
and dereferences it without checking for null. The official F4SE implementation
starts from the pointer at `0x120` and null-checks each node's setting.
`LocalizationRuntime.cpp` therefore advances the CommonLib iterator once before
dereferencing any entry, then uses null-checked `Setting` access. This preserves
the official traversal with CommonLib's existing layout; no raw offset overlay,
vendor patch, engine allocation or engine vtable call was introduced. The three
CommonLib class sizes are asserted in the implementation.

## Catalog and formatting rules

Files are loose `Data/F4SE/Plugins/Clipboard/Localization/<language>.tsv`, one for
each of the twelve fixed language codes above, with `en.tsv` always loaded as
the source fallback.
They contain UTF-8, optionally with a UTF-8 BOM, and LF or CRLF records. An entry
is an ASCII `$Clipboard_` key followed by one tab and escaped text. Keys admit
letters, digits and underscore after the prefix. Empty lines and lines beginning
with `#` are ignored. Backslash escapes are exactly `\\`, `\n`, `\r` and `\t`.
Literal braces use `{{` and `}}`; placeholders are exactly `{0}` through `{5}`.

Files are limited to 4 MiB, 4096 entries, 160 bytes per key and 16 KiB per decoded
value. Invalid UTF-8, embedded NUL, duplicate keys, empty values, unknown escapes,
unescaped controls/tabs and invalid braces reject the entire file. A translated
value is usable only if the English key exists and its placeholder multiset
matches, including repeated placeholders. An unusable translated file/key falls
back to English; missing English text has a fixed English emergency message and
a bounded diagnostic log entry. A corrupt translated file cannot leave a partial
translated catalog active. A language refresh clears the previous translated
catalog before loading the new selection, so a missing file cannot retain text
from an earlier language.

Formatting scans the template once and appends each argument literally. Player
names, plugin filenames, pattern labels, markup and braces inside arguments
are not recursively formatted, looked up or translated. Encoding is preserved
through `BSFixedString`; font coverage, clipping, text input and multilingual
pattern-name round trips still require live tests.

## Host verification and retained source fingerprints

`ClipboardLocalizationTests` covers UTF-8 Latin diacritics, Cyrillic, Japanese
and both Chinese scripts; atomic rejection; duplicate keys; all escapes;
every supported locale's normalization; unsupported/path-like locale fallback;
reordered and repeated placeholders; literal user arguments; bounds; and file
I/O. Run CTest `clipboard_localization` to check these contracts. The complete
native build and four-family offline resolution remain separate pipeline gates.

Exact SHA-256 fingerprints of the inspected local source bytes:

| File | SHA-256 |
|---|---|
| CommonLib `Settings.h` | `0AE2B1F845FA8DF05AD758A004F4A7B4E50EA364395DCF79480DA7187785055A` |
| CommonLib `BSTList.h` | `D589479C25CD2D0AF115865F5519B2B3CB3E99DBCFAB6702539C6D3F9565082F` |
| OG `GameSettings.h` | `95C1E12BBF113062DCB77B10B370FFFB934A2195BF75DDB0C14142557A7750EF` |
| OG `GameSettings.cpp` | `7F179CC1BA58B3778EBC990C133DE611D9FA761705D011CE8A422613DE177F31` |
| NG `GameSettings.h` | `77C07444F8995D45A8F6099755FE5C5C837BA7F434170E69ECEA5CEE28980A96` |
| NG `GameSettings.cpp` | `5681292166D7B683A709A8534C118F0A4CE8D29201D89E36CC85E20764CB4A2B` |
| AE 0.7.9 `GameSettings.h` | `FF42B70EC0CD38629201E057D9F8910562E9632C3EE128EF485E1867047A7190` |
| AE 0.7.9 `GameSettings.cpp` | `CD142F93F797999B2AC2AD5E4C529E8B2978EBE3158B7CD1CABF4066742C7C1D` |
| AE 0.7.9 `Translation.cpp` | `B83E37C543D9E634C934B027D818CC92E60414EA48570182099501D95AB23921` |
| Preserved 0.7.8 `GameSettings.cpp` | `E3DCD3D241D0089B129949F6A2CC156B90B9285DDD4F1D6E1D8E1171E098EB77` |
