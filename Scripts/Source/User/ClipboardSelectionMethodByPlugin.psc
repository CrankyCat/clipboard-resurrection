Scriptname ClipboardSelectionMethodByPlugin extends ClipboardSelectionMethod

Function SortPluginNames(string[] plugins)
	If plugins == None
		return
	EndIf
	int index = 1
	While index < plugins.Length
		If plugins[index - 1] > plugins[index]
			string plugin = plugins[index - 1]
			plugins[index - 1] = plugins[index]
			plugins[index] = plugin
			If index == 1
				index = 2
			Else
				index -= 1
			EndIf
		Else
			index += 1
		EndIf
	EndWhile
EndFunction

Function Show()
	If !CanHandleMethodEvents()
		return
	EndIf
	GetParentTool().BlockTool()
	
	string[] plugins = GetClipboardManager().GetSelectableObjectPoolPlugins()
	If !CanHandleMethodEvents()
		return
	EndIf
	If plugins == None || plugins.Length == 0
		GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PluginSourcesEmpty"))
		GetParentTool().UnblockTool()
		return
	EndIf
	; Sorting this UI copy does not alter physical pattern plugin rows or indices.
	SortPluginNames(plugins)
	int index = GetClipboardMenuManager().ShowPickFromList(ClipboardExtension.GetText("$Clipboard_PickPlugin"), plugins);
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
		int found = GetClipboardManager().SelectAllByPlugin(plugins[index])
		If found == 0
			GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SelectionPluginEmpty", plugins[index]))
		Else
			GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SelectionComplete"), " ", ClipboardExtension.GetText("$Clipboard_SelectionPluginCount", (found As String), plugins[index]))
		EndIf
	EndIf
	GetParentTool().UnblockTool()
EndFunction

String Function GetSelectionMethodName()
	return ClipboardExtension.GetText("$Clipboard_MethodPlugin")
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
int Function InternalBuild_ClipboardSelectionMethodByPlugin() Global
    return 122
EndFunction
; END GENERATED INTERNAL BUILD
