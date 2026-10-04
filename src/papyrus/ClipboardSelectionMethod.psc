Scriptname ClipboardSelectionMethod extends ObjectReference

Message Property MESSAGE_COPY_TO Auto Const
Keyword Property KEYWORD_WORKSHOP_OBJECT Auto Const

ClipboardToolScript parentTool
ClipboardManager clipboard
ClipboardQuest clipboardMenuManager
bool methodDestroyed
bool legacyCleanupComplete
ClipboardQuest legacyCleanupOwner
float relativeX
float relativeY
float relativeZ
float relativeAngleX
float relativeAngleY
float relativeAngleZ

ObjectReference Function GetLegacyParent()
	return parentTool
EndFunction

ClipboardManager Function GetLegacyManager()
	return clipboard
EndFunction

bool Function IsLegacyCleanupComplete()
	return legacyCleanupComplete
EndFunction

bool Function CanRetireLegacyVisuals(ClipboardQuest owner, int token)
	return ClipboardExtension.IsLegacyCleanupActive(token)
EndFunction

bool Function RetireLegacyVisuals(ClipboardQuest owner, int token)
	return ClipboardExtension.IsLegacyCleanupActive(token)
EndFunction

bool Function CanRetireLegacy(ClipboardQuest owner, int token)
	If !owner || owner.GetActiveTool() || !Self.IsDisabled()
		return false
	EndIf
	Form expectedBase = Self.GetBaseObject()
	If expectedBase != Game.GetFormFromFile(0x380A7, "Clipboard.esp") && expectedBase != Game.GetFormFromFile(0x380A6, "Clipboard.esp") && expectedBase != Game.GetFormFromFile(0x38FEA, "Clipboard.esp") && expectedBase != Game.GetFormFromFile(0x380A8, "Clipboard.esp") && expectedBase != Game.GetFormFromFile(0x635A, "Clipboard.esp") && expectedBase != Game.GetFormFromFile(0x6359, "Clipboard.esp") && expectedBase != Game.GetFormFromFile(0x380A9, "Clipboard.esp")
		return false
	EndIf
	If !ClipboardExtension.IsLegacyCleanupReference(Self, expectedBase, token)
		return false
	EndIf
	If legacyCleanupComplete || (!parentTool && !clipboard && methodDestroyed)
		return legacyCleanupOwner == owner && ClipboardExtension.IsLegacyCleanupActive(token)
	EndIf
	If clipboardMenuManager != owner
		return false
	EndIf
	ClipboardCopyPylonScript pylon = parentTool As ClipboardCopyPylonScript
	If !pylon || !pylon.CanOwnLegacyCleanup(owner, token)
		return false
	EndIf
	If !clipboard || !clipboard.CanRetireLegacy(owner, token) || clipboard.GetLegacyParent() != parentTool
		return false
	EndIf
	If !CanRetireLegacyVisuals(owner, token)
		return false
	EndIf
	return ClipboardExtension.IsLegacyCleanupActive(token)
EndFunction

bool Function RetireLegacy(ClipboardQuest owner, int token)
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	methodDestroyed = true
	legacyCleanupOwner = owner
	; Do not invoke Hide: derived visual helpers require separate ownership
	; proof and shared gun/alias state belongs to the current quest bridge.
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
	If !CanRetireLegacy(owner, token) || !RetireLegacyVisuals(owner, token)
		return false
	EndIf
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	Self.Delete()
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return false
	EndIf
	legacyCleanupComplete = true
	parentTool = None
	clipboard = None
	clipboardMenuManager = None
	return true
EndFunction

bool Function CanHandleMethodEvents()
	If methodDestroyed || !parentTool || !clipboard || !clipboardMenuManager
		return false
	EndIf
	return clipboard.CanHandleManualSelection()
EndFunction

ClipboardToolScript Function GetParentTool()
	return parentTool
EndFunction

ClipboardManager Function GetClipboardManager()
	return clipboard
EndFunction

ClipboardQuest Function GetClipboardMenuManager()
	return clipboardMenuManager
EndFunction

Function Initialize(ClipboardToolScript newParentTool, ClipboardManager newClipboard, ClipboardQuest newClipboardMenuManager)
	If methodDestroyed || ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	parentTool = newParentTool
	clipboard = newClipboard
	clipboardMenuManager = newClipboardMenuManager
	
	RegisterForRemoteEvent(parentTool, "OnWorkshopMode")
	RegisterForRemoteEvent(parentTool, "OnWorkshopObjectMoved")
	RegisterForRemoteEvent(parentTool, "OnWorkshopObjectPlaced")
	RegisterForRemoteEvent(parentTool, "OnWorkshopObjectGrabbed")
	RegisterForRemoteEvent(parentTool, "OnWorkshopObjectDestroyed")
	If !CanHandleMethodEvents()
		UnregisterForAllEvents()
		return
	EndIf

	Show()
EndFunction

; Parent object has been destroyed, this must be as well.
Function Destroy()
	If methodDestroyed
		return
	EndIf
	; Set this before Hide yields so queued callbacks cannot restart the method.
	methodDestroyed = true
	Hide()
	
	UnregisterForRemoteEvent(parentTool, "OnWorkshopMode")
	UnregisterForRemoteEvent(parentTool, "OnWorkshopObjectMoved")
	UnregisterForRemoteEvent(parentTool, "OnWorkshopObjectPlaced")
	UnregisterForRemoteEvent(parentTool, "OnWorkshopObjectGrabbed")
	UnregisterForRemoteEvent(parentTool, "OnWorkshopObjectDestroyed")
	
	Self.Disable()
	Self.Delete()
EndFunction

bool Function IsMethodDestroyed()
	return methodDestroyed
EndFunction

Message Function GetSecondaryButtonText()
	return None
EndFunction

String Function GetSelectionMethodName()
	return ""
EndFunction

int Function GetSelectionMethodType()
	return 0;
EndFunction

Function Show()

EndFunction

Function Hide()

EndFunction

Function OnSecondaryButtonActivate(ObjectReference akActionRef)

EndFunction

Function OnWorkshopModeEnter()

EndFunction

Function OnWorkshopModeExit()

EndFunction

; Listen in on the parent object so we know when to update our position.
Event ObjectReference.OnWorkshopObjectMoved(ObjectReference akSender, ObjectReference akReference)
	If !methodDestroyed
		Show()
	EndIf
EndEvent

Event ObjectReference.OnWorkshopObjectPlaced(ObjectReference akSender, ObjectReference akReference)
	If !methodDestroyed
		Show()
	EndIf
EndEvent

Event ObjectReference.OnWorkshopObjectGrabbed(ObjectReference akSender, ObjectReference akReference)
	If !methodDestroyed
		Hide()
	EndIf
EndEvent

Event ObjectReference.OnWorkshopObjectDestroyed(ObjectReference akSender, ObjectReference akReference)
	Destroy()
EndEvent

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardSelectionMethod() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
