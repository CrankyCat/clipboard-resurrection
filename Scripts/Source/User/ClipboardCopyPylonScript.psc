Scriptname ClipboardCopyPylonScript extends ClipboardToolScript

ClipboardQuest Property CLIPBOARD_MENUS Auto Const

Keyword Property KEYWORD_WORKSHOP_OBJECT Auto Const
FormList Property CLIPBOARD_SELECTION_METHODS Auto Const
Form Property CLIPBOARD_MANAGER Auto Const
Form Property CLIPBOARD_BUTTON_STAND Auto Const
Form Property CLIPBOARD_BUTTON_POLE Auto Const
Form Property CLIPBOARD_BUTTON_NUKE Auto Const

Message Property MESSAGE_ACT_ON_SELECTION Auto Const
Message Property MESSAGE_CHANGE_SELECTION_METHOD Auto Const
Message Property MESSAGE_CLEAR_SELECTION Auto Const
Message Property MESSAGE_DESTROY_TOOL_SELECTION Auto Const

ClipboardButtonStand actOnSelectionButton
ClipboardButtonStand selectionMethodActionButton
ClipboardButtonStand changeSelectionMethodButton
ClipboardButtonStand clearSelectionButton
ClipboardButtonStand destroyToolButton

ClipboardManager clipboard
ClipboardSelectionMethod activeSelectionMethod

bool isBlocked;
bool isDestroyed;
bool enableHotkeys;
bool workshopMode;
bool legacyCleanupComplete
int toolActionGeneration

Event OnInit()
	; A newly placed platform cannot join the cleanup's retired snapshot.
	If ClipboardExtension.IsLegacyCleanupActive()
		isDestroyed = true
		Self.Disable()
		Self.Delete()
		return
	EndIf
	ClipboardCopyPylonScript oldTool = CLIPBOARD_MENUS.GetActiveTool()
	If oldTool
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_ToolAlreadyExists"))
		Self.DestroyTool()
		CLIPBOARD_MENUS.SetActiveTool(oldTool)
		return
	EndIf
	CLIPBOARD_MENUS.SetActiveTool(Self)
	; Cleanup may acquire its token after the first OnInit check. Do not
	; create persistent helpers unless this platform actually owns the quest.
	If ClipboardExtension.IsLegacyCleanupActive() || CLIPBOARD_MENUS.GetActiveTool() != Self
		isDestroyed = true
		If CLIPBOARD_MENUS.GetActiveTool() == Self
			CLIPBOARD_MENUS.SetActiveTool(None)
		EndIf
		Self.Disable()
		Self.Delete()
		return
	EndIf
	workshopMode = true;
	WorkshopScript workshop = getLinkedRef(KEYWORD_WORKSHOP_OBJECT) As WorkshopScript;
	RegisterForRemoteEvent(workshop, "OnWorkshopObjectDestroyed")
	RegisterForRemoteEvent(workshop, "OnWorkshopMode")
	
	ClipboardExtension.ReloadSettings();
	
	Self.SetActivateTextOverride(MESSAGE_MOVE_ONTO)

	changeSelectionMethodButton = CreateActivator(CLIPBOARD_BUTTON_POLE, MESSAGE_CHANGE_SELECTION_METHOD, true) As ClipboardButtonStand
	changeSelectionMethodButton.SetParentTool(Self, 30, 32.5, -10, 0, 0, 10)
	changeSelectionMethodButton.SetScale(0.85)
	changeSelectionMethodButton.Disable()
	changeSelectionMethodButton.Enable()
	
	selectionMethodActionButton = CreateActivator(CLIPBOARD_BUTTON_POLE, None, true) As ClipboardButtonStand
	selectionMethodActionButton.SetParentTool(Self, 50, 27.5, -10, 0, 0, 10)
	selectionMethodActionButton.SetScale(0.85)
	selectionMethodActionButton.Disable()
	selectionMethodActionButton.Enable()
	
	clearSelectionButton = CreateActivator(CLIPBOARD_BUTTON_POLE, MESSAGE_CLEAR_SELECTION, true) As ClipboardButtonStand
	clearSelectionButton.SetParentTool(Self, 42, 50, -10, 0, 0, 10)
	clearSelectionButton.SetScale(1.2)
	clearSelectionButton.Disable()
	clearSelectionButton.Enable()
	
	destroyToolButton = CreateActivator(CLIPBOARD_BUTTON_NUKE, MESSAGE_DESTROY_TOOL_SELECTION, true) As ClipboardButtonStand
	destroyToolButton.SetParentTool(Self, -42, 42, -2, 0, 0, -15)
	destroyToolButton.SetScale(0.6)
	destroyToolButton.Disable()
	destroyToolButton.Enable()

	actOnSelectionButton = CreateActivator(CLIPBOARD_BUTTON_STAND, MESSAGE_ACT_ON_SELECTION, true) As ClipboardButtonStand
	actOnSelectionButton.SetParentTool(Self, 0, 45, -5, 0, 0, 90)
	
	UnblockTool()
	
	RegisterForExternalEvent("OnMCMSettingChange|Clipboard", "OnMCMSettingChange")
EndEvent

bool Function IsLegacyDestroyed()
	return isDestroyed
EndFunction

bool Function IsLegacyCleanupComplete()
	return legacyCleanupComplete
EndFunction

ClipboardManager Function GetLegacyManager()
	return clipboard
EndFunction

ObjectReference[] Function GetLegacyOwnedHelpers()
	ObjectReference[] helpers = new ObjectReference[7]
	helpers[0] = clipboard
	helpers[1] = activeSelectionMethod
	helpers[2] = actOnSelectionButton
	helpers[3] = selectionMethodActionButton
	helpers[4] = changeSelectionMethodButton
	helpers[5] = clearSelectionButton
	helpers[6] = destroyToolButton
	return helpers
EndFunction

; Identity-only check avoids recursion through manager/child reverse links.
bool Function CanOwnLegacyCleanup(ClipboardQuest owner, int token)
	If !owner || owner != CLIPBOARD_MENUS || !isDestroyed || !Self.IsDisabled()
		return false
	EndIf
	If owner.GetActiveTool()
		return false
	EndIf
	return ClipboardExtension.IsLegacyCleanupReference(Self, Game.GetFormFromFile(0x2E05, "Clipboard.esp"), token)
EndFunction

bool Function CanRetireLegacy(ClipboardQuest owner, int token)
	If !CanOwnLegacyCleanup(owner, token)
		return false
	EndIf
	If clipboard && !clipboard.IsLegacyCleanupComplete()
		If !clipboard.CanRetireLegacy(owner, token) || clipboard.GetLegacyParent() != Self
			return false
		EndIf
	EndIf
	If activeSelectionMethod && !activeSelectionMethod.IsLegacyCleanupComplete()
		If !activeSelectionMethod.CanRetireLegacy(owner, token) || activeSelectionMethod.GetLegacyParent() != Self
			return false
		EndIf
	EndIf
	ClipboardButtonStand[] buttons = new ClipboardButtonStand[5]
	buttons[0] = actOnSelectionButton
	buttons[1] = selectionMethodActionButton
	buttons[2] = changeSelectionMethodButton
	buttons[3] = clearSelectionButton
	buttons[4] = destroyToolButton
	int index = 0
	While index < buttons.Length
		If buttons[index] && !buttons[index].IsLegacyCleanupComplete()
			If !buttons[index].CanRetireLegacy(owner, token) || buttons[index].GetLegacyParent() != Self
				return false
			EndIf
		EndIf
		index += 1
	EndWhile
	return CanOwnLegacyCleanup(owner, token)
EndFunction

bool Function RetireLegacy(ClipboardQuest owner, int token)
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	; The orchestrator retires dependencies first; never recurse into them.
	If clipboard && !clipboard.IsLegacyCleanupComplete()
		return false
	EndIf
	If activeSelectionMethod && !activeSelectionMethod.IsLegacyCleanupComplete()
		return false
	EndIf
	If actOnSelectionButton && !actOnSelectionButton.IsLegacyCleanupComplete()
		return false
	EndIf
	If selectionMethodActionButton && !selectionMethodActionButton.IsLegacyCleanupComplete()
		return false
	EndIf
	If changeSelectionMethodButton && !changeSelectionMethodButton.IsLegacyCleanupComplete()
		return false
	EndIf
	If clearSelectionButton && !clearSelectionButton.IsLegacyCleanupComplete()
		return false
	EndIf
	If destroyToolButton && !destroyToolButton.IsLegacyCleanupComplete()
		return false
	EndIf
	isBlocked = true
	UnregisterForAllEvents()
	If !CanOwnLegacyCleanup(owner, token)
		return false
	EndIf
	UnregisterForExternalEvent("OnMCMSettingChange|Clipboard")
	If !CanOwnLegacyCleanup(owner, token)
		return false
	EndIf
	CancelTimer()
	If !CanOwnLegacyCleanup(owner, token)
		return false
	EndIf
	CancelTimerGameTime()
	If !CanOwnLegacyCleanup(owner, token)
		return false
	EndIf
	Self.Delete()
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return false
	EndIf
	; Keep ownership links through Delete so an interrupted call is retryable.
	legacyCleanupComplete = true
	clipboard = None
	activeSelectionMethod = None
	actOnSelectionButton = None
	selectionMethodActionButton = None
	changeSelectionMethodButton = None
	clearSelectionButton = None
	destroyToolButton = None
	return true
EndFunction

Function OnMCMSettingChange(string modName, string id)
	If isDestroyed
		; Retire an old saved subscription when it next delivers an event.
		UnregisterForExternalEvent("OnMCMSettingChange|Clipboard")
		return
	EndIf
	If modName == "Clipboard"
		ClipboardExtension.ReloadSettings();
		If !isDestroyed
			ClipboardManager manager = GetClipboardManager()
			If manager
				manager.OnMCMSettingChange()
			EndIf
		EndIf
	EndIf
EndFunction

bool Function InWorkshopMode()
	return workshopMode;
EndFunction

ClipboardQuest Function GetClipboardMenuManager()
	return CLIPBOARD_MENUS
EndFunction

ClipboardManager Function GetClipboardManager()
	If !clipboard && !isDestroyed
		clipboard = CreateActivator(CLIPBOARD_MANAGER) As ClipboardManager
		clipboard.SetWorkshop(getLinkedRef(KEYWORD_WORKSHOP_OBJECT) As WorkshopScript)
		clipboard.SetReferenceObject(Self)
		clipboard.Disable()
	EndIf
	return clipboard
EndFunction

ClipboardButtonStand Function GetInitiateCopyButton()
	return actOnSelectionButton
EndFunction

Event ObjectReference.OnWorkshopObjectDestroyed(ObjectReference akSender, ObjectReference akActionRef)
	If isDestroyed
		return
	EndIf
	If akActionRef
		bool isSelected = GetClipboardManager().IsSelected(akActionRef)
		If isSelected 
			GetClipboardManager().Deselect(akActionRef)
			GetClipboardMenuManager().ShowUpdate(ClipboardExtension.GetText("$Clipboard_SelectedStatus", (GetClipboardManager().GetSelectedCount() As String)))
		EndIf
	EndIf
EndEvent

Function OnHotkey(string hotkeyAction)
	If isBlocked || isDestroyed
		return
	EndIf
	
	If (hotkeyAction == "activateActionQuickBtn")
		int quickActionGeneration = toolActionGeneration
		int count = GetClipboardManager().GetSelectedCount()
		int wireCount = GetClipboardManager().GetSelectedWireCount()
		int pluginCount = GetClipboardManager().GetSelectedPluginCount()
		int type = activeSelectionMethod.GetSelectionMethodType()
		string method = activeSelectionMethod.GetSelectionMethodName()
		int btn = GetClipboardMenuManager().ShowQuickMenu(count,wireCount,pluginCount,type,method)
		If isBlocked || !IsToolActionCurrent(quickActionGeneration)
			return
		EndIf
		If btn == 1
			ButtonPressed(actOnSelectionButton);
		ElseIf btn == 2
			ButtonPressed(changeSelectionMethodButton);
		ElseIf btn == 3
			ButtonPressed(selectionMethodActionButton);
		ElseIf btn == 4
			ButtonPressed(clearSelectionButton);
		ElseIf btn == 5
			ButtonPressed(destroyToolButton);
		ElseIf btn == 6
			TranslateAroundSelf(Game.GetPlayer(), 0, -45, 0, 0, 0, 0, 1000)
		EndIf
	ElseIf (hotkeyAction == "activateActionMenuBtn")
		ButtonPressed(actOnSelectionButton)
	ElseIf (hotkeyAction == "activateSelectionMethodBtn")
		ButtonPressed(changeSelectionMethodButton)
	ElseIf (hotkeyAction == "activateSelectionScanBtn")
		ButtonPressed(selectionMethodActionButton)
	ElseIf (hotkeyAction == "activateClearSelectionBtn")
		ButtonPressed(clearSelectionButton)
	ElseIf (hotkeyAction == "activateDestroyToolBtn")
		ButtonPressed(destroyToolButton)
	EndIf
EndFunction

Event ObjectReference.OnActivate(ObjectReference akSender, ObjectReference akActionRef)
	ButtonPressed(akSender)
EndEvent

Function ButtonPressed(ObjectReference akSender)
	If isBlocked || isDestroyed
		return
	EndIf
	toolActionGeneration += 1
	int actionGeneration = toolActionGeneration
	BlockTool()
	RunToolAction(akSender, actionGeneration)
	; Always release the original platform's reservation. A destroyed platform
	; or interrupted older action must not unblock/revive a replacement action.
	If actionGeneration == toolActionGeneration
		UnblockTool()
	EndIf
EndFunction

bool Function IsToolActionCurrent(int actionGeneration)
	return actionGeneration == toolActionGeneration && !isDestroyed && !ClipboardExtension.IsLegacyCleanupActive()
EndFunction

Function InterruptInputAction()
	toolActionGeneration += 1
	If !isDestroyed
		UnblockTool()
	EndIf
EndFunction

Function RunToolAction(ObjectReference akSender, int actionGeneration)
	If !IsToolActionCurrent(actionGeneration)
		return
	EndIf
	If akSender == actOnSelectionButton
		If GetClipboardManager().GetSelectedCount() > 0
			int actionIndex = GetClipboardMenuManager().ShowActionSelectMenu()
			If !IsToolActionCurrent(actionGeneration)
				return
			EndIf
			If actionIndex == 0
				; Cancelled, do nothing.
			ElseIf actionIndex == 1
				GetClipboardManager().SavePattern(self)
			ElseIf actionIndex == 2
				GetClipboardManager().PastePattern(self,workshopMode)
			ElseIf actionIndex == 3
				GetClipboardManager().DeleteSelected()
			ElseIf actionIndex == 4
				int rotateType = GetClipboardMenuManager().ShowRotationSelectMenu()
				If !IsToolActionCurrent(actionGeneration)
					return
				EndIf
				float rotateAmount = GetClipboardMenuManager().ShowRotationAngleMenuForOwner(Self)
				If rotateAmount > 0 && IsToolActionCurrent(actionGeneration)
					If rotateType == 0
						GetClipboardManager().RotateSelected( rotateAmount, None)
					ElseIf rotateType == 1
						GetClipboardManager().RotateSelected( -rotateAmount, None)
					ElseIf rotateType == 2
						GetClipboardManager().RotateSelected( rotateAmount, self)
					Else
						GetClipboardManager().RotateSelected( -rotateAmount, self)
					EndIf
				EndIf
			ElseIf actionIndex == 5
				int direction = GetClipboardMenuManager().ShowMoveDirectionMenu()
				If !IsToolActionCurrent(actionGeneration)
					return
				EndIf
				int amount = GetClipboardMenuManager().ShowMoveAmountMenuForOwner(Self)
				If amount > 0 && IsToolActionCurrent(actionGeneration)
					int moveX = 0
					int moveY = 0
					int moveZ = 0
					
					If direction == 0
						moveX = -amount
					ElseIf direction == 1
						moveX = amount
					ElseIf direction == 2
						moveZ = amount
					ElseIf direction == 3
						moveZ = -amount
					ElseIf direction == 4
						moveY = amount
					ElseIf direction == 5
						moveY = -amount
					EndIf
					
					GetClipboardManager().MoveSelected( moveX, moveY, moveZ, self)
				EndIf
			ElseIf actionIndex == 6
				int scaleActionIndex = GetClipboardMenuManager().ShowScaleTypeMenu()
				If !IsToolActionCurrent(actionGeneration)
					return
				EndIf
				If scaleActionIndex == 0
					float scale = GetClipboardMenuManager().ShowScaleUpMenuForOwner(Self)
					if scale != 0 && IsToolActionCurrent(actionGeneration)
						GetClipboardManager().ScaleSelected(scale,true,workshopMode)
					EndIf
				ElseIf scaleActionIndex == 1
					float scale = GetClipboardMenuManager().ShowScaleDownMenuForOwner(Self)
					if scale != 0 && IsToolActionCurrent(actionGeneration)
						GetClipboardManager().ScaleSelected(scale,true,workshopMode)
					EndIf
				ElseIf scaleActionIndex == 2
					GetClipboardManager().ScaleSelected(-1,true,workshopMode)
				ElseIf scaleActionIndex == 3
					float scale = GetClipboardMenuManager().ShowScaleUpMenuForOwner(Self)
					if scale != 0 && IsToolActionCurrent(actionGeneration)
						GetClipboardManager().ScaleSelected(scale,false,workshopMode)
					EndIf
				ElseIf scaleActionIndex == 4
					float scale = GetClipboardMenuManager().ShowScaleDownMenuForOwner(Self)
					if scale != 0 && IsToolActionCurrent(actionGeneration)
						GetClipboardManager().ScaleSelected(scale,false,workshopMode)
					EndIf
				ElseIf scaleActionIndex == 5
					GetClipboardManager().ScaleSelected(-1,false,workshopMode)
				EndIf
			ElseIf actionIndex == 7
				GetClipboardManager().ClearOwnershipSelected()
			ElseIf actionIndex == 8
				GetClipboardManager().DisplaySelectionDetails()
			ElseIf actionIndex == 9
				GetClipboardManager().ListPluginsSelected()
			ElseIf actionIndex == 10
				GetClipboardManager().ListPluginsPattern()
			ElseIf actionIndex == 11
				GetClipboardManager().MoveToPatternSource(Self)
			ElseIf actionIndex == 12
				GetClipboardManager().ShowSelectionComponentCost()
			ElseIf actionIndex == 13
				GetClipboardManager().ShowPatternComponentCost()
			Else
				GetClipboardMenuManager().ShowNotYetImplementedMenu()
			EndIf
		Else
			int actionIndex = GetClipboardMenuManager().ShowActionSelectEmptyMenu()
			If !IsToolActionCurrent(actionGeneration)
				return
			EndIf
			If actionIndex == 0
				; Cancelled, do nothing.
			ElseIf actionIndex == 1
				GetClipboardManager().PastePattern(self,workshopMode)
			ElseIf actionIndex == 2
				GetClipboardManager().ListPluginsPattern()
			ElseIf actionIndex == 3
				GetClipboardManager().MoveToPatternSource(Self)
			ElseIf actionIndex == 4
				GetClipboardManager().ShowPatternComponentCost()
			Else
				GetClipboardMenuManager().ShowNotYetImplementedMenu()
			EndIf
		EndIf
		
	ElseIf akSender == changeSelectionMethodButton
		int methodIndex = GetClipboardMenuManager().ShowShapeMenu()
		If !IsToolActionCurrent(actionGeneration)
			return
		EndIf
		If activeSelectionMethod 
			activeSelectionMethod.Destroy()
		EndIf
		activeSelectionMethod = CreateActivator(CLIPBOARD_SELECTION_METHODS.GetAt(methodIndex)) As ClipboardSelectionMethod
		activeSelectionMethod.Initialize(Self, GetClipboardManager(), GetClipboardMenuManager())
		selectionMethodActionButton.SetActivateTextOverride(activeSelectionMethod.GetSecondaryButtonText())
	ElseIf akSender == selectionMethodActionButton
		activeSelectionMethod.OnSecondaryButtonActivate(selectionMethodActionButton)
	ElseIf akSender == clearSelectionButton
		GetClipboardManager().ClearSelection()
	ElseIf akSender == destroyToolButton
		DestroyTool()
		return
	EndIf
EndFunction

Function BlockTool()
	isBlocked = true
	changeSelectionMethodButton.AddKeyword(BLOCK_PLAYER_ACTIVATION)
	selectionMethodActionButton.AddKeyword(BLOCK_PLAYER_ACTIVATION)
	clearSelectionButton.AddKeyword(BLOCK_PLAYER_ACTIVATION)
	actOnSelectionButton.AddKeyword(BLOCK_PLAYER_ACTIVATION)
	destroyToolButton.AddKeyword(BLOCK_PLAYER_ACTIVATION)
	
EndFunction

Function UnblockTool()
	If isDestroyed || ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	isBlocked = false
	
	changeSelectionMethodButton.RemoveKeyword(BLOCK_PLAYER_ACTIVATION)
	selectionMethodActionButton.RemoveKeyword(BLOCK_PLAYER_ACTIVATION)
	clearSelectionButton.RemoveKeyword(BLOCK_PLAYER_ACTIVATION)
	actOnSelectionButton.RemoveKeyword(BLOCK_PLAYER_ACTIVATION)
	destroyToolButton.RemoveKeyword(BLOCK_PLAYER_ACTIVATION)

EndFunction

Function HideTool()
	changeSelectionMethodButton.Hide()
	selectionMethodActionButton.Hide()
	actOnSelectionButton.Hide()
	destroyToolButton.Hide()
	clearSelectionButton.Hide()
	Self.Disable()
EndFunction

Function ShowTool()
	If isDestroyed || ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	changeSelectionMethodButton.Show()
	selectionMethodActionButton.Show()
	actOnSelectionButton.Show()
	destroyToolButton.Show()
	clearSelectionButton.Show()
	If isDestroyed || ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	Self.Enable()
EndFunction

Function DestroyTool()
	If isDestroyed
		return
	EndIf
	isDestroyed = true
	toolActionGeneration += 1
	ClipboardExtension.CancelOwnedInput(Self)
	UnregisterForExternalEvent("OnMCMSettingChange|Clipboard")
	If clipboard
		; Stop queued selections/import work before cleanup calls can yield.
		clipboard.BeginDestroy()
	EndIf
	HideTool()
	If clipboard
		clipboard.ClearSelection()
	EndIf
	
	changeSelectionMethodButton.Destroy()
	selectionMethodActionButton.Destroy()
	actOnSelectionButton.Destroy()
	destroyToolButton.Destroy()
	clearSelectionButton.Destroy()
	activeSelectionMethod.Destroy()
	
	WorkshopScript workshop = getLinkedRef(KEYWORD_WORKSHOP_OBJECT) As WorkshopScript
	UnregisterForRemoteEvent(workshop, "OnWorkshopObjectDestroyed")
	UnregisterForRemoteEvent(workshop, "OnWorkshopMode")
	UnregisterForRemoteEvent(changeSelectionMethodButton, "OnActivate")
	UnregisterForRemoteEvent(selectionMethodActionButton, "OnActivate")
	UnregisterForRemoteEvent(actOnSelectionButton, "OnActivate")
	UnregisterForRemoteEvent(destroyToolButton, "OnActivate")
	UnregisterForRemoteEvent(clearSelectionButton, "OnActivate")
	UnregisterForRemoteEvent(activeSelectionMethod, "OnActivate")
	
	changeSelectionMethodButton = None
	selectionMethodActionButton = None
	actOnSelectionButton = None
	destroyToolButton = None
	clearSelectionButton = None
	activeSelectionMethod = None
	
	If clipboard
		clipboard.Destroy()
		clipboard = None
	EndIf

	; A late teardown must not clear a replacement tool's quest registration.
	If CLIPBOARD_MENUS.GetActiveTool() == Self
		CLIPBOARD_MENUS.SetActiveTool(None)
	EndIf
	Self.Delete()
EndFunction

Event OnWorkshopObjectPlaced(ObjectReference akReference)
	If !activeSelectionMethod && !isDestroyed
		int methodIndex = ClipboardExtension.GetSettingValueInt("Selection","iInitialSelectionMethod",3)
		activeSelectionMethod = CreateActivator(CLIPBOARD_SELECTION_METHODS.GetAt(methodIndex)) As ClipboardSelectionMethod
		activeSelectionMethod.Initialize(Self, GetClipboardManager(), GetClipboardMenuManager())
		selectionMethodActionButton.SetActivateTextOverride(activeSelectionMethod.GetSecondaryButtonText())
	EndIf
EndEvent

Event OnWorkshopObjectDestroyed(ObjectReference akReference)
	DestroyTool()
EndEvent

Event ObjectReference.OnWorkshopMode(ObjectReference akSender, Bool aStart)
	workshopMode = aStart;
EndEvent

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardCopyPylonScript() Global
    return 122
EndFunction
; END GENERATED INTERNAL BUILD
