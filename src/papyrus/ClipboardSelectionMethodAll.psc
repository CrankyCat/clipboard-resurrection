Scriptname ClipboardSelectionMethodAll extends ClipboardSelectionMethod

Function Show()
	If !CanHandleMethodEvents()
		return
	EndIf
	If !GetParentTool().InWorkshopMode()
		GetClipboardManager().EnterWorkshopModeIfNeeded()
	EndIf
	If !CanHandleMethodEvents()
		return
	EndIf

	GetParentTool().BlockTool()
	int found = GetClipboardManager().SelectAll(GetClipboardManager().GetSelectableObjectPool())
	If found == 0
		GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SettlementEmpty"))
	Else
		GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SelectionComplete"), " ", ClipboardExtension.GetText("$Clipboard_SelectionAllCount", (found As String)))
	EndIf
	GetParentTool().UnblockTool()
EndFunction

String Function GetSelectionMethodName()
	return ClipboardExtension.GetText("$Clipboard_MethodAll")
EndFunction

int Function GetSelectionMethodType()
	return 0;
EndFunction

Message Function GetSecondaryButtonText()
	return None
EndFunction

Function OnSecondaryButtonActivate(ObjectReference akActionRef)
EndFunction

Event ObjectReference.OnWorkshopObjectMoved(ObjectReference akSender, ObjectReference akReference)
EndEvent

Event ObjectReference.OnWorkshopObjectPlaced(ObjectReference akSender, ObjectReference akReference)
EndEvent

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardSelectionMethodAll() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
