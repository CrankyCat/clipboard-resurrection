Scriptname ClipboardSelectionMethodBox extends ClipboardSelectionMethod

Int Property WALL_BASE_SIZE = 256 Auto Const
Int Property WALL_SEGEMENT_SIZE = 500 Auto Const
Int Property Y_OFFSET = 64 Auto Const

Form Property CLIPBOARD_COPY_WALL Auto Const

Message Property MESSAGE_SET_SELECTION_SIZE Auto Const
Message Property MESSAGE_INITIATE_SELECTION Auto Const

ObjectReference[] selectionObjs
bool boxBuilt
bool boxBuilding
bool showRequested
int boxGeneration
Int width
Int height
Int depth

bool Function CanRetireLegacyVisuals(ClipboardQuest owner, int token)
	; A pending cage or retained walls need separate ownership proof. Do not
	; silently delete them through the legacy helper-only allowlist.
	If boxBuilding || (selectionObjs != None && selectionObjs.Length > 0)
		return false
	EndIf
	return ClipboardExtension.IsLegacyCleanupActive(token)
EndFunction

bool Function RetireLegacyVisuals(ClipboardQuest owner, int token)
	If !CanRetireLegacyVisuals(owner, token)
		return false
	EndIf
	showRequested = false
	boxBuilt = false
	boxGeneration += 1
	return true
EndFunction

Function Show()
	If !CanHandleMethodEvents()
		return
	EndIf
	showRequested = true
	boxGeneration += 1
	If width == 0
		width = (GetClipboardMenuManager().ShowSizeMenu(ClipboardExtension.GetText("$Clipboard_DimensionWidth")) + 1)
		depth = (GetClipboardMenuManager().ShowSizeMenu(ClipboardExtension.GetText("$Clipboard_DimensionDepth")) + 1)
		height = (GetClipboardMenuManager().ShowSizeMenu(ClipboardExtension.GetText("$Clipboard_DimensionHeight")) + 1)
	EndIf
	If !CanHandleMethodEvents()
		return
	EndIf

	If boxBuilt || boxBuilding
		return
	EndIf

	int buildGeneration = boxGeneration
	boxBuilding = true
	ObjectReference[] createdObjs = ClipboardExtension.CreateSelectionBox(GetParentTool(), CLIPBOARD_COPY_WALL, width, depth, height)
	boxBuilding = false

	; Show and Hide can both run while the latent native is suspended. Reject
	; any result produced for an older visibility generation, including the
	; Show -> Hide -> Show ABA sequence used while grabbing and placing the tool.
	If buildGeneration != boxGeneration || !showRequested || !CanHandleMethodEvents()
		If createdObjs != None && createdObjs.Length > 0
			DeleteCageObjects(createdObjs)
		EndIf
		If showRequested && !boxBuilt && !boxBuilding && CanHandleMethodEvents()
			Show()
		EndIf
		return
	EndIf

	If createdObjs != None && createdObjs.Length > 0
		selectionObjs = createdObjs
		If selectionObjs != None && selectionObjs.Length > 0
			boxBuilt = true
			If ClipboardExtension.GetSettingValueBool("Debug", "bEnableLogging", false)
				Debug.Trace("[Clipboard] Box cage created with " + selectionObjs.Length + " references at " + width + "x" + depth + "x" + height + ".")
			EndIf
		EndIf
	Else
		selectionObjs = None
		boxBuilt = false
		ClipboardExtension.LogIssue("[Clipboard] Box cage creation returned no references at " + width + "x" + depth + "x" + height + ".")
		GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_BoxCreationFailed"), " ", ClipboardExtension.GetText("$Clipboard_BoxCreationReviewLog"))
	EndIf
EndFunction

Function DeleteCageObjects(ObjectReference[] cageObjects)
	; Box walls are force-persistent Clipboard visualization helpers, not
	; workshop-owned objects.  Sending them through ScrapObjects can leave any
	; wall without a resolvable WorkshopItem owner behind permanently.
	int i = 0
	While i < cageObjects.Length
		If cageObjects[i]
			cageObjects[i].Disable()
			cageObjects[i].Delete()
		EndIf
		i += 1
	EndWhile
EndFunction

Message Function GetSecondaryButtonText()
	return MESSAGE_INITIATE_SELECTION
EndFunction

Function Hide()
	showRequested = false
	boxGeneration += 1
	ObjectReference[] objectsToScrap = selectionObjs
	selectionObjs = None
	boxBuilt = false
	If objectsToScrap != None && objectsToScrap.Length > 0
		DeleteCageObjects(objectsToScrap)
	EndIf
EndFunction

Function OnSecondaryButtonActivate(ObjectReference akActionRef)
	If !CanHandleMethodEvents()
		return
	EndIf
	ClipboardManager clipboard = GetClipboardManager()
	GetParentTool().BlockTool()
	
	
	If !GetParentTool().InWorkshopMode()
		GetClipboardManager().EnterWorkshopModeIfNeeded()
	EndIf
	
	ObjectReference[] workshopObjects = clipboard.GetSelectableObjectPool()
	If !CanHandleMethodEvents()
		return
	EndIf
	ObjectReference[] foundObjs = ClipboardExtension.GetObjectsInBox(workshopObjects,GetParentTool(), width, depth, height);
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
	return ClipboardExtension.GetText("$Clipboard_MethodBox")
EndFunction

int Function GetSelectionMethodType()
	return 1;
EndFunction

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardSelectionMethodBox() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
