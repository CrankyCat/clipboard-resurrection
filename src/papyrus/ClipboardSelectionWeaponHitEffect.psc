Scriptname ClipboardSelectionWeaponHitEffect extends ActiveMagicEffect

Actor Property PLAYER_REF Auto Const
ClipboardQuest Property CLIPBOARD_MENUS Auto Const

Event OnEffectStart(Actor akTarget, Actor akCaster)
	If PLAYER_REF != akCaster
		return
	EndIf
	
	ClipboardSelectionWeapon wpn = PLAYER_REF.GetEquippedWeapon() As ClipboardSelectionWeapon
	if (wpn && wpn.GetClipboardManager())
		wpn.GetClipboardManager().Select(akTarget);
	EndIf
EndEvent

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardSelectionWeaponHitEffect() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
