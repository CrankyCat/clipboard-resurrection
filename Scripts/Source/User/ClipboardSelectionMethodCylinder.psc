Scriptname ClipboardSelectionMethodCylinder extends ClipboardSelectionMethod

Int Property CYLINDER_BASE_SIZE = 105 Auto Const

Form Property CLIPBOARD_COPY_CYLINDER Auto Const

Message Property MESSAGE_INITIATE_SELECTION Auto Const

ObjectReference selectionCylinder
Int selectionSize

bool Function CanRetireLegacyVisuals(ClipboardQuest owner, int token)
	return !selectionCylinder && ClipboardExtension.IsLegacyCleanupActive(token)
EndFunction

Event OnInit()
	selectionSize = 0
EndEvent

Function Show()
	If !CanHandleMethodEvents()
		return
	EndIf
	If selectionSize == 0
		selectionSize = (GetClipboardMenuManager().ShowSizeMenu(ClipboardExtension.GetText("$Clipboard_DimensionRadius")) + 1) * 110
	EndIf
	If !CanHandleMethodEvents()
		return
	EndIf
	
	If !selectionCylinder
		selectionCylinder = Self.PlaceAtMe(CLIPBOARD_COPY_CYLINDER,1,true,false,false)
	EndIf
	If !CanHandleMethodEvents()
		Hide()
		return
	EndIf
	
	float scale = selectionSize/CYLINDER_BASE_SIZE
	selectionCylinder.SetScale(scale)
	GetParentTool().MoveAroundSelf(selectionCylinder, 0, selectionSize + 64, -scale * 128, 90, 0, -GetParentTool().GetAngleZ(), true)
EndFunction

Function Hide()
	selectionCylinder.Disable()
	selectionCylinder.Delete()
	selectionCylinder = None
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
	ObjectReference[] foundObjs = ClipboardExtension.GetObjectsInCylinder(workshopObjects,GetParentTool(), selectionSize);
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
	return ClipboardExtension.GetText("$Clipboard_MethodCylinder")
EndFunction

int Function GetSelectionMethodType()
	return 1;
EndFunction

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardSelectionMethodCylinder() Global
    return 122
EndFunction
; END GENERATED INTERNAL BUILD
