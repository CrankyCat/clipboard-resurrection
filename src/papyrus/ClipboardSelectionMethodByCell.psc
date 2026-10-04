Scriptname ClipboardSelectionMethodByCell extends ClipboardSelectionMethod

Function Show()
	If !CanHandleMethodEvents()
		return
	EndIf
	GetParentTool().BlockTool()
	string[] cellLabels = new string[2]
	cellLabels[0] = ClipboardExtension.GetText("$Clipboard_CellScopeWorkshop")
	cellLabels[1] = ClipboardExtension.GetText("$Clipboard_CellScopeTool")
	
	int index = GetClipboardMenuManager().ShowPickFromList(ClipboardExtension.GetText("$Clipboard_PickCell"), cellLabels);
	If !CanHandleMethodEvents()
		return
	EndIf
	If index >= 0
		If !GetParentTool().InWorkshopMode()
			GetClipboardManager().EnterWorkshopModeIfNeeded()
		EndIf
		If !CanHandleMethodEvents()
			return
		EndIf
		int found = 0
		If index == 0
			found = GetClipboardManager().SelectAllInWorkshopCells()
		ElseIf index == 1
			found = GetClipboardManager().SelectAllInCell(GetParentTool().GetParentCell())
		EndIf
		If found == 0
			GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CellScopeEmpty"))
		Else
			GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SelectionComplete"), " ", ClipboardExtension.GetText("$Clipboard_SelectionCellCount", (found As String)))
		EndIf
	EndIf
	GetParentTool().UnblockTool()
EndFunction

String Function GetSelectionMethodName()
	return ClipboardExtension.GetText("$Clipboard_MethodCell")
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
int Function InternalBuild_ClipboardSelectionMethodByCell() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
