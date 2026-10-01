Scriptname ClipboardButtonStand extends ObjectReference

Keyword Property BLOCK_PLAYER_ACTIVATION Auto Const
String Property Anim Auto Const

ClipboardToolScript parentTool
float relativeX
float relativeY
float relativeZ
float relativeAngleX
float relativeAngleY
float relativeAngleZ
bool buttonDestroyed
bool legacyCleanupComplete
ClipboardQuest legacyCleanupOwner

ObjectReference Function GetLegacyParent()
	return parentTool
EndFunction

bool Function IsLegacyCleanupComplete()
	return legacyCleanupComplete
EndFunction

bool Function CanRetireLegacy(ClipboardQuest owner, int token)
	If !owner || owner.GetActiveTool() || !Self.IsDisabled()
		return false
	EndIf
	Form expectedBase = Self.GetBaseObject()
	If expectedBase != Game.GetFormFromFile(0x31DBC, "Clipboard.esp") && expectedBase != Game.GetFormFromFile(0x5411, "Clipboard.esp") && expectedBase != Game.GetFormFromFile(0x32556, "Clipboard.esp")
		return false
	EndIf
	If !ClipboardExtension.IsLegacyCleanupReference(Self, expectedBase, token)
		return false
	EndIf
	If legacyCleanupComplete || (!parentTool && buttonDestroyed)
		return legacyCleanupOwner == owner && ClipboardExtension.IsLegacyCleanupActive(token)
	EndIf
	ClipboardCopyPylonScript pylon = parentTool As ClipboardCopyPylonScript
	return pylon && pylon.CanOwnLegacyCleanup(owner, token)
EndFunction

bool Function RetireLegacy(ClipboardQuest owner, int token)
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	buttonDestroyed = true
	legacyCleanupOwner = owner
	UnregisterForAllEvents()
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	UnregisterForExternalEvent("OnMCMSettingChange|Clipboard")
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	CancelTimer()
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	CancelTimerGameTime()
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	Self.Delete()
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return false
	EndIf
	legacyCleanupComplete = true
	parentTool = None
	return true
EndFunction

EVENT onLoad()
	If buttonDestroyed
		return
	EndIf
	if Self.Is3DLoaded()
		setMotionType(Motion_Keyframed, TRUE)
	endif
endEVENT

Event OnActivate(ObjectReference akActionRef)
	If buttonDestroyed
		return
	EndIf
    playAnimation(Anim)
EndEvent

Function SetParentTool(ClipboardToolScript newParentTool, float relX, float relY, float relZ, float relAngleX, float relAngleY, float relAngleZ)
	If buttonDestroyed || ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	parentTool = newParentTool
	
	relativeX = relX
	relativeY = relY
	relativeZ = relZ
	relativeAngleX = relAngleX
	relativeAngleY = relAngleY
	relativeAngleZ = relAngleZ
	
	RegisterForRemoteEvent(parentTool, "OnWorkshopObjectMoved")
	RegisterForRemoteEvent(parentTool, "OnWorkshopObjectPlaced")
	RegisterForRemoteEvent(parentTool, "OnWorkshopObjectGrabbed")
	RegisterForRemoteEvent(parentTool, "OnWorkshopObjectDestroyed")
	If buttonDestroyed || ClipboardExtension.IsLegacyCleanupActive()
		UnregisterForAllEvents()
		return
	EndIf

	Show()
EndFunction

; Parent object has been destroyed, this must be as well.
Function Destroy()
	buttonDestroyed = true
	UnregisterForRemoteEvent(parentTool, "OnWorkshopObjectMoved")
	UnregisterForRemoteEvent(parentTool, "OnWorkshopObjectPlaced")
	UnregisterForRemoteEvent(parentTool, "OnWorkshopObjectGrabbed")
	UnregisterForRemoteEvent(parentTool, "OnWorkshopObjectDestroyed")
	Self.Disable()
	Self.Delete()
EndFunction

; Position the object relative to the parent object
Function Show()
	If buttonDestroyed || !parentTool || ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	ClipboardCopyPylonScript pylon = parentTool As ClipboardCopyPylonScript
	If pylon && pylon.IsLegacyDestroyed()
		return
	EndIf
	float sin = Math.sin(-parentTool.GetAngleZ())
	float cos = Math.cos(-parentTool.GetAngleZ())
	float rotatedX = relativeX * cos - relativeY * sin
	float rotatedY = relativeY * cos + relativeX * sin
	Self.MoveTo(parentTool, rotatedX, rotatedY, relativeZ)
	If buttonDestroyed || !parentTool || ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	Self.SetAngle(parentTool.GetAngleX() + relativeAngleX, parentTool.GetAngleY() + relativeAngleY, parentTool.GetAngleZ() + relativeAngleZ)
	If buttonDestroyed || !parentTool || ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	Self.Enable()
EndFunction

; Disable the object well the parent doesn't have a position
Function Hide()
	Self.Disable()
EndFunction

Function Block()
	Self.AddKeyword(BLOCK_PLAYER_ACTIVATION)
EndFunction

Function Unblock()
	Self.RemoveKeyword(BLOCK_PLAYER_ACTIVATION)
EndFunction

; Listen in on the parent object so we know when to update our position.
Event ObjectReference.OnWorkshopObjectMoved(ObjectReference akSender, ObjectReference akReference)
	Show()
EndEvent

Event ObjectReference.OnWorkshopObjectPlaced(ObjectReference akSender, ObjectReference akReference)
	Show()
EndEvent

Event ObjectReference.OnWorkshopObjectGrabbed(ObjectReference akSender, ObjectReference akReference)
	Hide()
EndEvent

Event ObjectReference.OnWorkshopObjectDestroyed(ObjectReference akSender, ObjectReference akReference)
	Destroy()
EndEvent

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardButtonStand() Global
    return 122
EndFunction
; END GENERATED INTERNAL BUILD
