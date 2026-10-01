Scriptname ClipboardSelectionMethodSphere extends ClipboardSelectionMethod

Int Property SPHERE_BASE_SIZE = 512 Auto Const
Form Property CLIPBOARD_COPY_SPHERE Auto Const
Message Property MESSAGE_INITIATE_SELECTION Auto Const

ObjectReference selectionSphere
Int selectionSize

bool Function CanRetireLegacyVisuals(ClipboardQuest owner, int token)
	; Visual markers are outside the helper discovery/validation allowlist.
	return !selectionSphere && ClipboardExtension.IsLegacyCleanupActive(token)
EndFunction

Event OnInit()
	selectionSize = 0
EndEvent

Function Show()
	If !CanHandleMethodEvents()
		return
	EndIf
	If selectionSize == 0
		selectionSize = (GetClipboardMenuManager().ShowSizeMenu(ClipboardExtension.GetText("$Clipboard_DimensionRadius")) + 1) * 200
	EndIf
	If !CanHandleMethodEvents()
		return
	EndIf
	
	If !selectionSphere
		selectionSphere = Self.PlaceAtMe(CLIPBOARD_COPY_SPHERE,1,true,false,false)
	EndIf
	If !CanHandleMethodEvents()
		Hide()
		return
	EndIf
	selectionSphere.SetScale(selectionSize As Float/SPHERE_BASE_SIZE)
	selectionSphere.Enable()
	GetParentTool().MoveAroundSelf(selectionSphere, 0, selectionSize + 64, 0, 0, 0, 0, true)
EndFunction

Function Hide()
	selectionSphere.Disable()
	selectionSphere.Delete()
	selectionSphere = None
EndFunction

Message Function GetSecondaryButtonText()
	return MESSAGE_INITIATE_SELECTION
EndFunction

Function OnSecondaryButtonActivate(ObjectReference akActionRef)
	If !CanHandleMethodEvents()
		return
	EndIf
	ClipboardManager clipboard = GetClipboardManager()
	
	If !GetParentTool().InWorkshopMode()
		GetClipboardManager().EnterWorkshopModeIfNeeded()
	EndIf
	
	ObjectReference[] workshopObjects = clipboard.GetSelectableObjectPool()
	If !CanHandleMethodEvents()
		return
	EndIf
	ObjectReference[] foundObjs = ClipboardExtension.GetObjectsInSphere(workshopObjects,GetParentTool(), selectionSize);
	If !CanHandleMethodEvents()
		return
	EndIf
	int selectedCount = clipboard.SelectAll(foundObjs)
	
	If foundObjs.Length == 0
		GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SelectionAreaEmpty")," ",ClipboardExtension.GetText("$Clipboard_SelectionAreaRetry"))
	Else
		GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SelectionComplete"), " ", ClipboardExtension.GetText("$Clipboard_SelectionAreaCount", (selectedCount As String), (workshopObjects.Length As String)))
	EndIf
EndFunction

String Function GetSelectionMethodName()
	return ClipboardExtension.GetText("$Clipboard_MethodSphere")
EndFunction

int Function GetSelectionMethodType()
	return 1;
EndFunction

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardSelectionMethodSphere() Global
    return 122
EndFunction
; END GENERATED INTERNAL BUILD
