Scriptname ClipboardQuest extends Quest Conditional

float property MINIMUM_TIME_BETWEEN_NOTIFICATIONS = 3.0 Auto Const

Message Property MISSING_PLUGINS_MENU Auto Const
Message Property YESNO_MENU Auto Const
Message Property INFORM_MENU Auto Const
Message Property PROGRESS_NOTIFICATION Auto Const
Message Property STAUS_NOTIFICATION Auto Const
Message Property SHAPE_MENU Auto Const
Message Property SIZE_MENU Auto Const
Message Property ACTION_SELECT_MENU Auto Const
Message Property ACTION_SELECT_EMPTY_MENU Auto Const
Message Property ROTATION_TYPE_SELECT_MENU Auto Const
Message Property ROTATION_AMOUNT_SELECT_MENU Auto Const
Message Property MOVE_DIRECTION_MENU Auto Const
Message Property MOVE_AMOUNT_MENU Auto Const
Message Property SCALE_TYPE_MENU Auto Const
Message Property SCALE_UP_AMOUNT_MENU Auto Const
Message Property SCALE_DOWN_AMOUNT_MENU Auto Const
Message Property NOT_YET_IMPLEMENTED_MENU Auto Const
Message Property HOTKEY_MENU Auto Const
Message Property HOTKEY_AREA_MENU Auto Const
Message Property HOTKEY_MANUAL_MENU Auto Const
Message Property PICK_FROM_LIST_PAGED Auto Const

; Retained property identity; old saves may store its previous English value.
String Property ENTER_WORKSHOP_MODE = "$Clipboard_EnterWorkshopMode" Auto Const

FormList Property PAGE_SELECTION_MENU_LIST Auto Const
FormList Property PICK_LIST_MENU_LIST Auto Const
FormList Property SLOT_SELECTION_MENU_LIST Auto Const

Form Property ClipboardLabel1 Auto Const
Form Property ClipboardLabel2 Auto Const
Form Property ClipboardLabel3 Auto Const
Form Property ClipboardLabel4 Auto Const
Form Property ClipboardLabel5 Auto Const

ReferenceAlias Property Slot1State Auto Const
ReferenceAlias Property Slot2State Auto Const
ReferenceAlias Property Slot3State Auto Const
ReferenceAlias Property Slot4State Auto Const
ReferenceAlias Property Slot5State Auto Const
ReferenceAlias Property Slot6State Auto Const

ClipboardCopyPylonScript activeTool
ObjectReference[] labels
float lastNotificationTime
bool setup
bool waitingOnInput
bool inputRequestActive
int inputRequestGeneration
; Separate identities keep saved TIM callbacks from completing an owned request.
bool ownedInputRequestActive
int ownedInputGeneration
String ownedInputToken
ObjectReference ownedInputOwner
bool ownedInputStateChanged
bool seenHotkeysMsg
	
String timInput
String progressUpdateLabel
Int progressUpdateCount
Int progressUpdateIndex
Int progressUpdateTotal

Function OnHotkey(string hotkeyAction)
	If activeTool
		activeTool.OnHotkey(hotkeyAction)
	EndIf
EndFunction

Function SetActiveTool(ClipboardCopyPylonScript newActiveTool )
	ClipboardExtension.ReportBuildIdentity()
	If newActiveTool && ClipboardExtension.IsLegacyCleanupActive()
		return
	EndIf
	activeTool = newActiveTool
EndFunction

; The quest exists even when every platform has been destroyed. Update the
; native logging switch immediately instead of relying on a pylon listener.
Function SetDebugLogging(bool loggingEnabled)
	Var[] settingArgs = new Var[3]
	settingArgs[0] = "Clipboard"
	settingArgs[1] = "bEnableLogging:Debug"
	settingArgs[2] = loggingEnabled
	Utility.CallGlobalFunction("MCM", "SetModSettingBool", settingArgs)
	ClipboardExtension.ReloadSettings()
EndFunction

ClipboardCopyPylonScript Function GetActiveTool()
	return activeTool;
EndFunction

; User-invoked maintenance, never an automatic load/selection scan. Native
; tokens invalidate suspended work across loads and exclude other native jobs.
Function CleanUpOldPlatforms()
	int token = ClipboardExtension.BeginLegacyCleanup()
	If token == 0
		Debug.Notification(ClipboardExtension.GetText("$Clipboard_CleanupBusy"))
		return
	EndIf
	; MCM invokes the quest while its menu is open; wait for menus to close.
	Utility.Wait(0.1)
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return
	EndIf
	ClipboardSelectionWeapon cleanupWeapon = Game.GetFormFromFile(0x3790C, "Clipboard.esp") As ClipboardSelectionWeapon
	If !cleanupWeapon || Game.GetFormFromFile(0x3790B, "Clipboard.esp") != Self
		ClipboardExtension.EndLegacyCleanup(token)
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupFailed"))
		return
	EndIf
	If activeTool || cleanupWeapon.GetClipboardManager()
		ClipboardExtension.EndLegacyCleanup(token)
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupActive"))
		return
	EndIf
	If !ShowYesNoMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupConfirm"), ClipboardExtension.GetText("$Clipboard_CleanupPreserve"), ClipboardExtension.GetText("$Clipboard_CleanupPrepare"))
		ClipboardExtension.EndLegacyCleanup(token)
		return
	EndIf
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return
	EndIf
	int[] result = RunLegacyCleanup(token, cleanupWeapon)
	; RunLegacyCleanup has returned and released its candidate references before
	; ending the exclusive session. Never end or report a later session's work.
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return
	EndIf
	ClipboardExtension.EndLegacyCleanup(token)
	If ClipboardExtension.GetSettingValueBool("Debug", "bEnableLogging", false)
		Debug.Trace("[Clipboard] Legacy cleanup status=" + result[0] + ", retired=" + result[1] + ", skipped=" + result[2])
	EndIf
	If result[0] == 1
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupNone"))
	ElseIf result[0] == 2
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupBlocked"))
	ElseIf result[0] == 3
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupResult", (result[1] As String), (result[2] As String)), ClipboardExtension.GetText("$Clipboard_CleanupReload"))
	ElseIf result[0] == 4
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupStopped"), ClipboardExtension.GetText("$Clipboard_CleanupResult", (result[1] As String), (result[2] As String)))
	ElseIf result[0] == 5
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupActive"))
	Else
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupTitle"), ClipboardExtension.GetText("$Clipboard_CleanupFailed"))
	EndIf
EndFunction

; Result: status (0 failure, 1 empty, 2 blocked, 3 retired, 4 interrupted,
; 5 active tool), successful retirement requests, skipped candidates.
int[] Function RunLegacyCleanup(int token, ClipboardSelectionWeapon cleanupWeapon)
	int[] result = new int[3]
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return result
	EndIf
	If activeTool || cleanupWeapon.GetClipboardManager()
		result[0] = 5
		return result
	EndIf
	ObjectReference[] candidates = ClipboardExtension.GetLegacyCleanupCandidates(token)
	If candidates == None
		return result
	EndIf
	If candidates.Length == 1 && candidates[0] == None
		return result
	EndIf
	int i = 0
	int pending = 0
	; Complete preflight before any event unregistration, link clearing or Delete.
	While i < candidates.Length
		If !ClipboardExtension.IsLegacyCleanupActive(token)
			return result
		EndIf
		If !CanRetireLegacyCandidate(candidates[i], token) || !HasCompleteLegacyGroup(candidates[i], candidates)
			result[2] += 1
		ElseIf !LegacyCandidateIsComplete(candidates[i])
			pending += 1
		EndIf
		i += 1
	EndWhile
	If result[2] > 0
		result[0] = 2
		return result
	EndIf
	If pending == 0
		result[0] = 1
		return result
	EndIf
	Debug.Notification(ClipboardExtension.GetText("$Clipboard_CleanupProgress"))
	int phase = 0
	While phase < 3
		i = 0
		While i < candidates.Length
			If !ClipboardExtension.IsLegacyCleanupActive(token)
				result[0] = 4
				result[2] = pending - result[1]
				return result
			EndIf
			If activeTool || cleanupWeapon.GetClipboardManager()
				result[0] = 4
				result[2] = pending - result[1]
				return result
			EndIf
			If LegacyCandidatePhase(candidates[i]) == phase && !LegacyCandidateIsComplete(candidates[i])
				If !RetireLegacyCandidate(candidates[i], token)
					result[0] = 4
					result[2] = pending - result[1]
					return result
				EndIf
				result[1] += 1
				; Give the game time between small groups; every next mutation
				; rechecks the token, active ownership and the individual object.
				If result[1] % 8 == 0
					Utility.Wait(0.01)
				EndIf
			EndIf
			i += 1
		EndWhile
		phase += 1
	EndWhile
	result[0] = 3
	return result
EndFunction


int Function LegacyCandidatePhase(ObjectReference candidate)
	If candidate As ClipboardCopyPylonScript
		return 2
	ElseIf candidate As ClipboardManager
		return 1
	ElseIf (candidate As ClipboardSelectionMethod) || (candidate As ClipboardButtonStand)
		return 0
	EndIf
	return -1
EndFunction

bool Function CanRetireLegacyCandidate(ObjectReference candidate, int token)
	ClipboardCopyPylonScript pylon = candidate As ClipboardCopyPylonScript
	ClipboardManager manager = candidate As ClipboardManager
	ClipboardSelectionMethod method = candidate As ClipboardSelectionMethod
	ClipboardButtonStand button = candidate As ClipboardButtonStand
	If pylon
		return pylon.CanRetireLegacy(Self, token)
	ElseIf manager
		return manager.CanRetireLegacy(Self, token)
	ElseIf method
		return method.CanRetireLegacy(Self, token)
	ElseIf button
		return button.CanRetireLegacy(Self, token)
	EndIf
	return false
EndFunction

bool Function LegacyCandidateIsComplete(ObjectReference candidate)
	ClipboardCopyPylonScript pylon = candidate As ClipboardCopyPylonScript
	ClipboardManager manager = candidate As ClipboardManager
	ClipboardSelectionMethod method = candidate As ClipboardSelectionMethod
	ClipboardButtonStand button = candidate As ClipboardButtonStand
	If pylon
		return pylon.IsLegacyCleanupComplete()
	ElseIf manager
		return manager.IsLegacyCleanupComplete()
	ElseIf method
		return method.IsLegacyCleanupComplete()
	ElseIf button
		return button.IsLegacyCleanupComplete()
	EndIf
	return false
EndFunction

bool Function HasCompleteLegacyGroup(ObjectReference candidate, ObjectReference[] candidates)
	If LegacyCandidateIsComplete(candidate)
		return true
	EndIf
	ClipboardCopyPylonScript pylon = candidate As ClipboardCopyPylonScript
	ClipboardManager manager = candidate As ClipboardManager
	ClipboardSelectionMethod method = candidate As ClipboardSelectionMethod
	ClipboardButtonStand button = candidate As ClipboardButtonStand
	ObjectReference parentRef
	If pylon
		ObjectReference[] helpers = pylon.GetLegacyOwnedHelpers()
		If helpers == None || helpers.Length != 7
			return false
		EndIf
		int i = 0
		While i < helpers.Length
			If helpers[i] && candidates.Find(helpers[i]) < 0
				return false
			EndIf
			i += 1
		EndWhile
		return true
	ElseIf manager
		parentRef = manager.GetLegacyParent()
	ElseIf method
		parentRef = method.GetLegacyParent()
		ClipboardManager methodManager = method.GetLegacyManager()
		If methodManager && candidates.Find(methodManager) < 0
			return false
		EndIf
	ElseIf button
		parentRef = button.GetLegacyParent()
	EndIf
	return parentRef && candidates.Find(parentRef) >= 0
EndFunction

bool Function RetireLegacyCandidate(ObjectReference candidate, int token)
	ClipboardCopyPylonScript pylon = candidate As ClipboardCopyPylonScript
	ClipboardManager manager = candidate As ClipboardManager
	ClipboardSelectionMethod method = candidate As ClipboardSelectionMethod
	ClipboardButtonStand button = candidate As ClipboardButtonStand
	If pylon
		return pylon.RetireLegacy(Self, token)
	ElseIf manager
		return manager.RetireLegacy(Self, token)
	ElseIf method
		return method.RetireLegacy(Self, token)
	ElseIf button
		return button.RetireLegacy(Self, token)
	EndIf
	return false
EndFunction

bool Function IsTextInputMenuLoaded()
	; Preserve the public helper name while removing the TIM dependency.
	return ClipboardExtension.IsOwnedInputAvailable()
EndFunction

bool Function IsModConfigurationManagerLoaded()
	return F4SE.GetPluginVersion("MCM") >= 0
EndFunction

Function setup()
	labels = new ObjectReference[5]
	labels[0] = Game.GetPlayer().PlaceAtMe(ClipboardLabel1, 1, False, True, True)
	labels[1] = Game.GetPlayer().PlaceAtMe(ClipboardLabel2, 1, False, True, True)
	labels[2] = Game.GetPlayer().PlaceAtMe(ClipboardLabel3, 1, False, True, True)
	labels[3] = Game.GetPlayer().PlaceAtMe(ClipboardLabel4, 1, False, True, True)
	labels[4] = Game.GetPlayer().PlaceAtMe(ClipboardLabel5, 1, False, True, True)
	
	Slot1State.ForceRefTo(labels[0])
	Slot2State.ForceRefTo(labels[1])
	Slot3State.ForceRefTo(labels[2])
	Slot4State.ForceRefTo(labels[3])
	Slot5State.ForceRefTo(labels[4])
	
	setup = true
EndFunction

Function UpdateSlotLabel(int labelIndex, int slotIndex)
	UpdateLabel(labelIndex, ClipboardManager.GetPatternName(slotIndex))
EndFunction

Function UpdateLabel(int labelIndex, String labelText)
	labels[labelIndex].GetBaseObject().SetName(labelText)
EndFunction

Function ShowUpdate(String labelText, bool forceShow = false)
	if !setup
		setup()
	endif
	
	float time = Utility.GetCurrentRealTime()	
	if forceShow || lastNotificationTime > time || lastNotificationTime + MINIMUM_TIME_BETWEEN_NOTIFICATIONS <= time
		UpdateLabel(0, labelText)
		STAUS_NOTIFICATION.Show()
		lastNotificationTime = time
	EndIf
EndFunction

Function ShowProgressUpdate(int count, int currentIndex, int endIndex, String labelText = " ", bool forceShow = false)
	if !setup
		setup()
	endif

	float time = Utility.GetCurrentRealTime()	
	if forceShow || lastNotificationTime > time || lastNotificationTime + MINIMUM_TIME_BETWEEN_NOTIFICATIONS <= time
		UpdateLabel(0, labelText)
		PROGRESS_NOTIFICATION.show(count, currentIndex*100/endIndex)
		lastNotificationTime = time
	EndIf
EndFunction

int Function ShowScaleTypeMenu()
	return SCALE_TYPE_MENU.Show()
EndFunction

Function ShowNotYetImplementedMenu()
	NOT_YET_IMPLEMENTED_MENU.Show()
EndFunction

int Function ShowActionSelectMenu()
	return ACTION_SELECT_MENU.Show()
EndFunction

int Function ShowActionSelectEmptyMenu()
	return ACTION_SELECT_EMPTY_MENU.Show()
EndFunction

int Function ShowShapeMenu()
	return SHAPE_MENU.Show()
EndFunction

int Function ShowSizeMenu(String dimensionLabel)
	if !setup
		setup()
	endif

	UpdateLabel(0,dimensionLabel)
	return SIZE_MENU.Show()
EndFunction

int Function ShowQuickMenu(int selectionSize, int wireCount, int pluginCount, int selectionMethodType, string selectionMethod)
	if !setup
		setup()
	endif
	
	UpdateLabel(0,""+selectionSize)
	UpdateLabel(1,""+wireCount)
	UpdateLabel(2,""+pluginCount)
	UpdateLabel(3,selectionMethod)
	
	int results
	If selectionMethodType == 0
		results = HOTKEY_MENU.Show();
		If results >= 3
			results += 1
		EndIf
	ElseIf selectionMethodType == 1
		results = HOTKEY_AREA_MENU.Show();
	ElseIf selectionMethodType == 2
		results = HOTKEY_MANUAL_MENU.Show();
	EndIf
	
	return results;
EndFunction

Function ShowInformMenu(String messageLine1, String messageLine2 = " ", String messageLine3 = " ", String messageLine4 = " ", String messageLine5 = " ")
	if !setup
		setup()
	endif
	
	UpdateLabel(0,messageLine1)
	UpdateLabel(1,messageLine2)
	UpdateLabel(2,messageLine3)
	UpdateLabel(3,messageLine4)
	UpdateLabel(4,messageLine5)
	
	INFORM_MENU.Show()
EndFunction

bool Function ShowYesNoMenu(String messageLine1, String messageLine2 = " ", String messageLine3 = " ", String messageLine4 = " ", String messageLine5 = " ")
	if !setup
		setup()
	endif
	
	UpdateLabel(0,messageLine1)
	UpdateLabel(1,messageLine2)
	UpdateLabel(2,messageLine3)
	UpdateLabel(3,messageLine4)
	UpdateLabel(4,messageLine5)
	
	return YESNO_MENU.Show() == 1
EndFunction

bool Function ShowWorkshopModeCheck(WorkshopScript workshop)
	int workshopModeOnAction = ClipboardExtension.GetSettingValueInt("Dialogs","iWorkshopModeOnAction",1)

	If workshopModeOnAction == 0
		return false
	EndIf
	
	return (workshopModeOnAction == 2 || ShowYesNoMenu(ClipboardExtension.GetText("$Clipboard_EnterWorkshopMode")))
EndFunction

int Function ShowPickFromList(string typeName, string[] options)
	If options == None || options.Length == 0
		return -1
	EndIf
	If !setup
		setup()
	EndIf
	UpdateLabel(0," "+typeName)

	If options.Length <= 10
		string msg = "";
		int i = 0;
		While i < options.Length
			msg += (i+1) + ": " + options[i] + "\n";
			i += 1;
		EndWhile
		UpdateLabel(1,msg)
	
		Message msgDialog = PICK_LIST_MENU_LIST.GetAt(options.Length-1) As Message;
		
		If !msgDialog
			return -1
		EndIf
		int selection = msgDialog.Show() - 1
		If selection < 0 || selection >= options.Length
			return -1
		EndIf
		return selection
	Else
		int page = 0
		int PAGE_SIZE = 10
		While true
			int start = page * PAGE_SIZE
			int end =  Math.Min((page + 1) * PAGE_SIZE - 1,options.Length-1) As Int
			
			int nextPage = page + 1
			If nextPage * PAGE_SIZE >= options.Length
				nextPage = 0
			EndIf
			
			int prevPage = page - 1
			If prevPage < 0
				prevPage = (options.Length - 1) / PAGE_SIZE
			EndIf
			
			int prevEnd = Math.Min((prevPage + 1) * PAGE_SIZE, options.Length) As Int
			int nextEnd = Math.Min((nextPage + 1) * PAGE_SIZE, options.Length) As Int
			string msg = ClipboardExtension.GetText("$Clipboard_ListPreviousPage", ((prevPage * PAGE_SIZE + 1) As String), (prevEnd As String))
			msg += ClipboardExtension.GetText("$Clipboard_ListNextPage", ((nextPage * PAGE_SIZE + 1) As String), (nextEnd As String))
			
			int i = 0;
			While (i < PAGE_SIZE) && (start + i < options.Length)
				msg += (i+1) + ": " + options[start + i] + "\n";
				i += 1;
			EndWhile
			
			UpdateLabel(1,msg)
			
			int results = PICK_FROM_LIST_PAGED.Show(start + 1, end + 1)
			If results <= 0
				return -1
			ElseIf results == 1
				page = prevPage
			ElseIf results == 2
				page = nextPage
			ElseIf results >= PAGE_SIZE + 3 || start + results - 3 >= options.Length
				ShowInformMenu(ClipboardExtension.GetText("$Clipboard_ListSelectionInvalid", typeName))
			Else 
				return page * PAGE_SIZE + results - 3;
			EndIf
		EndWhile	
	EndIf
EndFunction

bool Function ShowMissingPluginsMenu(string[] missingPlugins)
	if !setup
		setup()
	endif
	
	UpdateLabel(0,missingPlugins[0])
	If missingPlugins.Length > 1
		UpdateLabel(1,missingPlugins[1])
	Else
		UpdateLabel(1," ")
	EndIf
	If missingPlugins.Length > 2
		UpdateLabel(2,missingPlugins[2])
	Else
		UpdateLabel(2," ")
	EndIf
	If missingPlugins.Length > 3
		UpdateLabel(3,missingPlugins[3])
	Else
		UpdateLabel(3," ")
	EndIf
	If missingPlugins.Length == 5
		UpdateLabel(4,missingPlugins[4])
	ElseIf missingPlugins.Length > 5
		UpdateLabel(4,ClipboardExtension.GetText("$Clipboard_MoreMissingPlugins", ((missingPlugins.Length - 4) As String)))
	Else
		UpdateLabel(4," ")
	EndIf
	
	return MISSING_PLUGINS_MENU.Show() == 1
EndFunction

int Function ShowRotationSelectMenu()
	return ROTATION_TYPE_SELECT_MENU.Show()
EndFunction

int Function ShowRotationAmountSelectMenu()
	return ShowRotationAmountSelectMenuForOwner(activeTool)
EndFunction

int Function ShowRotationAmountSelectMenuForOwner(ObjectReference owner)
	; Retained integer API for old callers. Current tools use the float API below.
	return ShowRotationAngleMenuForOwner(owner) As Int
EndFunction

float Function ShowRotationAngleMenu()
	return ShowRotationAngleMenuForOwner(activeTool)
EndFunction

float Function ShowRotationAngleMenuForOwner(ObjectReference owner)
	if IsTextInputMenuLoaded()
		return RequestDecimalInputForOwner(owner, "$Clipboard_RotationInput", "90", 180, "$Clipboard_RotationInvalid")
	Else
		int amountIndex = ROTATION_AMOUNT_SELECT_MENU.Show()
		If amountIndex == 0
			return 1
		ElseIf amountIndex == 1
			return 5
		ElseIf amountIndex == 2
			return 10
		ElseIf amountIndex == 3
			return 15
		ElseIf amountIndex == 4
			return 30
		ElseIf amountIndex == 5
			return 45
		ElseIf amountIndex == 6
			return 60
		ElseIf amountIndex == 7
			return 90
		ElseIf amountIndex == 8
			return 120
		Else
			return 180
		EndIf
	EndIf
EndFunction

int Function ShowSlotSelectionMenu()
	If !setup
		setup()
	EndIf
	Int pageSelectionCount = PAGE_SELECTION_MENU_LIST.GetSize();
	Int slotSelectionCount = SLOT_SELECTION_MENU_LIST.GetSize();
	int slotsPerPage = 10;
	Int pageIndex = 0
	Int slotIndex = -1
	While true
		If slotIndex >= 0
			
			Int firstSlot = pageIndex * 100 + slotIndex * 10 + 1
			
			string body = firstSlot + ": " + ClipboardManager.GetPatternName(firstSlot) + "\n"
			body += (firstSlot + 1) + ": " + ClipboardManager.GetPatternName(firstSlot + 1) + "\n"
			body += (firstSlot + 2) + ": " + ClipboardManager.GetPatternName(firstSlot + 2) + "\n"
			body += (firstSlot + 3) + ": " + ClipboardManager.GetPatternName(firstSlot + 3) + "\n"
			body += (firstSlot + 4) + ": " + ClipboardManager.GetPatternName(firstSlot + 4) + "\n"
			body += (firstSlot + 5) + ": " + ClipboardManager.GetPatternName(firstSlot + 5) + "\n"
			body += (firstSlot + 6) + ": " + ClipboardManager.GetPatternName(firstSlot + 6) + "\n"
			body += (firstSlot + 7) + ": " + ClipboardManager.GetPatternName(firstSlot + 7) + "\n"
			body += (firstSlot + 8) + ": " + ClipboardManager.GetPatternName(firstSlot + 8) + "\n"
			body += (firstSlot + 9) + ": " + ClipboardManager.GetPatternName(firstSlot + 9)
			
			UpdateLabel(0,firstSlot)
			UpdateLabel(1,firstSlot+9)
			UpdateLabel(2,body)
			
			Message slotSelectionMenu = SLOT_SELECTION_MENU_LIST.GetAt(slotIndex) As Message
			Int results = slotSelectionMenu.show()
			If results == 0
				slotIndex = -1
			Else
				return pageIndex * 100 + slotIndex * 10 + results
			EndIf
			
		Else
			Message pageSelectionMenu = PAGE_SELECTION_MENU_LIST.GetAt(pageIndex) As Message			
			string body = "";
			Int i =0
			While i < slotSelectionCount
				Int firstSlot = pageIndex * 100 + i * 10 + 1
				Int lastSlot = firstSlot + slotsPerPage - 1
				Int count = ClipboardExtension.GetPatternCount(firstSlot, lastSlot)
				if count == 0
					body += ClipboardExtension.GetText("$Clipboard_SlotRangeCount", (firstSlot As String), (lastSlot As String), (count As String))
				elseIf count == 1				
					body += ClipboardExtension.GetText("$Clipboard_SlotRangeCount", (firstSlot As String), (lastSlot As String), (count As String))
				else
					body += ClipboardExtension.GetText("$Clipboard_SlotRangeCount", (firstSlot As String), (lastSlot As String), (count As String))
				EndIf
				i += 1
			EndWhile
			
			UpdateLabel(0,body)
			
			Int results = pageSelectionMenu.show();
			If results == 0
				return -1
			ElseIf results == 1
				pageIndex = (pageIndex + pageSelectionCount - 1) % pageSelectionCount
			ElseIf results == 2
				pageIndex = (pageIndex + 1) % pageSelectionCount
			Else
				slotIndex = results - 3
			EndIf
		EndIf
	EndWhile
EndFunction

int Function ShowMoveDirectionMenu()
	return MOVE_DIRECTION_MENU.Show()
EndFunction

int Function ShowMoveAmountMenu()
	return ShowMoveAmountMenuForOwner(activeTool)
EndFunction

int Function ShowMoveAmountMenuForOwner(ObjectReference owner)
	if IsTextInputMenuLoaded()
		return RequestIntegerInputForOwner(owner, "$Clipboard_DistanceInput", "256", 4, 9999, "$Clipboard_DistanceInvalid")
	Else
		int amountIndex = MOVE_AMOUNT_MENU.Show()
		If amountIndex == 0
			return 1
		ElseIf amountIndex == 1
			return 2
		ElseIf amountIndex == 2
			return 4
		ElseIf amountIndex == 3
			return 8
		ElseIf amountIndex == 4
			return 16
		ElseIf amountIndex == 5
			return 32
		ElseIf amountIndex == 6
			return 64
		ElseIf amountIndex == 7
			return 128
		ElseIf amountIndex == 8
			return 256
		Else
			return 512
		EndIf
	EndIf
EndFunction

float Function ShowScaleUpMenu()
	return ShowScaleUpMenuForOwner(activeTool)
EndFunction

float Function ShowScaleUpMenuForOwner(ObjectReference owner)
	if IsTextInputMenuLoaded()
		float scaleUpAmount = RequestScaleInputForOwner(owner, true)
		If scaleUpAmount == 0
			return 0
		EndIf
		return scaleUpAmount / 100.0 + 1
	Else
		int amountIndex = SCALE_UP_AMOUNT_MENU.Show()
		If amountIndex == 0
			return 1.05
		ElseIf amountIndex == 1
			return 1.1
		ElseIf amountIndex == 2
			return 1.25
		ElseIf amountIndex == 3
			return 1.5
		ElseIf amountIndex == 4
			return 2
		ElseIf amountIndex == 5
			return 3
		Else
			return 6
		EndIf
	EndIf	
EndFunction

float Function ShowScaleDownMenu()
	return ShowScaleDownMenuForOwner(activeTool)
EndFunction

float Function ShowScaleDownMenuForOwner(ObjectReference owner)
	if IsTextInputMenuLoaded()
		float scaleDownAmount = RequestScaleInputForOwner(owner, false)
		If scaleDownAmount == 0
			return 0
		EndIf
		return 1.0 - (scaleDownAmount / 100.0)
	Else
		int amountIndex = SCALE_DOWN_AMOUNT_MENU.Show()
		If amountIndex == 0
			return 0.99
		ElseIf amountIndex == 1
			return 0.95
		ElseIf amountIndex == 2
			return 0.9
		ElseIf amountIndex == 3
			return 0.75
		ElseIf amountIndex == 4
			return 0.5
		ElseIf amountIndex == 5
			return 0.25
		Else
			return 0.1
		EndIf
	EndIf
EndFunction

float Function RequestScaleInputForOwner(ObjectReference owner, bool increase)
	String headerKey = "$Clipboard_ScaleDownInput"
	int maximum = 99
	If increase
		headerKey = "$Clipboard_ScaleUpInput"
		maximum = 1000
	EndIf
	While IsInputOwnerValid(owner)
		String value = RequestOwnedTextInput(owner, headerKey, "10", 1, 64, 0, maximum)
		If !value
			return 0
		EndIf
		int generation = ownedInputGeneration
		String errorText = ClipboardExtension.GetSelectionScaleInputError(owner, value, increase)
		If generation != ownedInputGeneration || !IsInputOwnerValid(owner)
			return 0
		EndIf
		If !errorText
			return value As Float
		EndIf
		ShowInformMenu(errorText)
		If generation != ownedInputGeneration || !IsInputOwnerValid(owner)
			return 0
		EndIf
	EndWhile
	return 0
EndFunction

String function ShowPatternNameMenu(string defaultName)
	return ShowPatternNameMenuForOwner(activeTool, defaultName)
EndFunction

String Function ShowPatternNameMenuForOwner(ObjectReference owner, String defaultName)
	if IsTextInputMenuLoaded()
		return RequestOwnedTextInput(owner, "$Clipboard_PatternNameInput", defaultName, 2, 50, 0, 0)
	EndIf	
	
	return defaultName
EndFunction

int Function RequestIntegerInput(String headerKey, String defaultValue, int maxChars, int maximum, String invalidKey)
	return RequestIntegerInputForOwner(activeTool, headerKey, defaultValue, maxChars, maximum, invalidKey)
EndFunction

int Function RequestIntegerInputForOwner(ObjectReference owner, String headerKey, String defaultValue, int maxChars, int maximum, String invalidKey)
	; Native input validates exact digits/range before accepting. Preserve the
	; existing iterative defensive retry and zero cancellation sentinel.
	While true
		String value = RequestOwnedTextInput(owner, headerKey, defaultValue, 0, maxChars, 1, maximum)
		If !value
			return 0
		EndIf
		int amount = value As Int
		If amount >= 1 && amount <= maximum
			return amount
		EndIf
		ShowInformMenu(ClipboardExtension.GetText(invalidKey))
	EndWhile
EndFunction

float Function RequestDecimalInputForOwner(ObjectReference owner, String headerKey, String defaultValue, int maximum, String invalidKey)
	; Native validation accepts positive decimal text with at most six places.
	; Keep the value floating-point through the caller and preserve cancel=0.
	While true
		String value = RequestOwnedTextInput(owner, headerKey, defaultValue, 1, 64, 0, maximum)
		If !value
			return 0.0
		EndIf
		float amount = value As Float
		If amount > 0.0 && amount <= maximum
			return amount
		EndIf
		ShowInformMenu(ClipboardExtension.GetText(invalidKey))
	EndWhile
EndFunction

String Function RequestTextInput(String headerKey, String defaultValue, int inputType, int maxChars)
	If inputType == 0
		return RequestOwnedTextInput(activeTool, headerKey, defaultValue, inputType, maxChars, 0, 2147483647)
	EndIf
	return RequestOwnedTextInput(activeTool, headerKey, defaultValue, inputType, maxChars, 0, 0)
EndFunction

bool Function IsInputOwnerValid(ObjectReference owner)
	If !owner || owner.IsDeleted()
		return false
	EndIf
	ClipboardCopyPylonScript pylon = owner As ClipboardCopyPylonScript
	If pylon && pylon.IsLegacyDestroyed()
		return false
	EndIf
	ClipboardManager manager = owner As ClipboardManager
	If manager && manager.IsLegacyDestroyed()
		return false
	EndIf
	return true
EndFunction

String Function RequestOwnedTextInput(ObjectReference owner, String headerKey, String defaultValue, int inputType, int maxChars, int minimum, int maximum)
	; Reserve this receiver before calls can yield. Token results live natively;
	; events are hints, never the authoritative result or completion barrier.
	If ownedInputRequestActive
		Debug.Notification(ClipboardExtension.GetText("$Clipboard_CleanupBusy"))
		return ""
	EndIf
	ownedInputRequestActive = true
	ownedInputGeneration += 1
	int requestGeneration = ownedInputGeneration
	ownedInputOwner = owner
	ownedInputToken = ""
	ownedInputStateChanged = false
	If inputRequestActive || waitingOnInput
		; Old saves may contain a TIM wait that never registered a load event.
		; No new code dispatches TIM, so retire only that legacy request before
		; opening ours. Its late callback/cleanup cannot touch the owned fields.
		inputRequestGeneration += 1
		timInput = ""
		waitingOnInput = false
		inputRequestActive = false
		UnRegisterForExternalEvent("TIM::Accept")
		UnRegisterForExternalEvent("TIM::Cancel")
	EndIf
	If !IsInputOwnerValid(owner)
		FinishOwnedTextInputRequest(requestGeneration)
		return ""
	EndIf
	; Keep the load receiver registered: a suspended historical TIM cleanup
	; must not unregister the next owned request's interruption handling.
	RegisterForRemoteEvent(Game.GetPlayer(), "OnPlayerLoadGame")
	RegisterForExternalEvent("Clipboard::InputState", "ReceiveOwnedInputState")
	String token = ""
	If requestGeneration == ownedInputGeneration && IsInputOwnerValid(owner)
		token = ClipboardExtension.BeginOwnedInput(owner, ClipboardExtension.GetText(headerKey), defaultValue, inputType, maxChars, minimum, maximum)
	EndIf
	If requestGeneration == ownedInputGeneration
		ownedInputToken = token
	ElseIf token != ""
		; A load can interrupt argument evaluation before Begin reaches native
		; code. Retire that exact late dispatch, never the owner's newer prompt.
		ClipboardExtension.AbandonOwnedInput(token)
	EndIf
	int requestState = 0
	bool finished = false
	While token != "" && requestGeneration == ownedInputGeneration && !finished
		requestState = ClipboardExtension.GetOwnedInputState(token)
		If requestState == 0
			; Native session invalidation is observable even if the load event
			; or a terminal notification is lost.
			finished = true
		Else
			finished = ClipboardExtension.IsOwnedInputFinished(token)
		EndIf
		If !finished
			If !IsInputOwnerValid(owner)
				ClipboardExtension.CancelOwnedInput(owner)
			EndIf
			If !ownedInputStateChanged
				; Bounded-rate status recovery; there is no typing timeout.
				Utility.WaitMenuMode(0.1)
			EndIf
			ownedInputStateChanged = false
		EndIf
	EndWhile
	String result = ""
	If token != "" && requestGeneration == ownedInputGeneration && IsInputOwnerValid(owner)
		requestState = ClipboardExtension.GetOwnedInputState(token)
		If requestState == 3 && ClipboardExtension.IsOwnedInputFinished(token)
			result = ClipboardExtension.GetOwnedInputResult(token)
		EndIf
	EndIf
	; Acknowledge only our exact token. An interrupted request must never
	; cancel or release a newer request belonging to the same platform.
	If token != ""
		If requestGeneration != ownedInputGeneration
			ClipboardExtension.AbandonOwnedInput(token)
			result = ""
		ElseIf !ClipboardExtension.AcknowledgeOwnedInput(token)
			ClipboardExtension.AbandonOwnedInput(token)
			result = ""
		EndIf
	EndIf
	If requestGeneration != ownedInputGeneration || !IsInputOwnerValid(owner)
		result = ""
	EndIf
	FinishOwnedTextInputRequest(requestGeneration)
	If (token == "" || requestState == 5) && requestGeneration == ownedInputGeneration
		ShowInformMenu(ClipboardExtension.GetText("$Clipboard_TextInputUnavailable"))
	EndIf
	return result
EndFunction

Function FinishOwnedTextInputRequest(int requestGeneration)
	If requestGeneration != ownedInputGeneration
		return
	EndIf
	ownedInputToken = ""
	ownedInputOwner = None
	ownedInputStateChanged = false
	ownedInputRequestActive = false
EndFunction

Function ReceiveOwnedInputState(String token, int requestState)
	If ownedInputRequestActive && token != "" && token == ownedInputToken
		; Lost, duplicate and out-of-order notifications are harmless: the
		; waiting owner always re-reads native state and the release barrier.
		ownedInputStateChanged = true
	EndIf
EndFunction

Function FinishTextInputRequest()
	; Compatibility unwind for requests suspended in pre-owned-input saves.
	; This never touches an owned token, result or event subscription.
	waitingOnInput = false
	UnRegisterForExternalEvent("TIM::Accept")
	UnRegisterForExternalEvent("TIM::Cancel")
	timInput = ""
	inputRequestActive = false
EndFunction

Event Actor.OnPlayerLoadGame(Actor akSender)
	inputRequestGeneration += 1
	timInput = ""
	waitingOnInput = false
	inputRequestActive = false
	ownedInputGeneration += 1
	ownedInputToken = ""
	ownedInputStateChanged = false
	ownedInputRequestActive = false
	ObjectReference interruptedOwner = ownedInputOwner
	ownedInputOwner = None
	ClipboardCopyPylonScript pylon = interruptedOwner As ClipboardCopyPylonScript
	If pylon
		pylon.InterruptInputAction()
	EndIf
	UnRegisterForExternalEvent("TIM::Accept")
	UnRegisterForExternalEvent("TIM::Cancel")
EndEvent

Function ReceiveInput(String newInput)
	; Historical callbacks may still be registered in old saves. They can
	; release only the old wait and always cancel it; no new result is accepted.
	If waitingOnInput && !ownedInputRequestActive
		timInput = ""
		waitingOnInput = false
		If !inputRequestActive
			; A suspended request from the older script has no owner helper.
			UnRegisterForExternalEvent("TIM::Accept")
			UnRegisterForExternalEvent("TIM::Cancel")
		EndIf
	EndIf
EndFunction

Function ReceiveNoInput(String newInput)
	ReceiveInput("")
EndFunction

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardQuest() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
