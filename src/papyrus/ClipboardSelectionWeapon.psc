Scriptname ClipboardSelectionWeapon extends Weapon
ClipboardQuest Property CLIPBOARD_MENUS Auto Const

ClipboardManager activeClipboardManager

Function SetClipboardManager(ClipboardManager newClipboardManager)
	activeClipboardManager = newClipboardManager
EndFunction

ClipboardManager Function GetClipboardManager()
	return activeClipboardManager
EndFunction

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardSelectionWeapon() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
