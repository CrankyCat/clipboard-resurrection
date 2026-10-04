Scriptname ClipboardSelectionMethodGun extends ClipboardSelectionMethod

float property MINIMUM_TIME_BETWEEN_NOTIFICATIONS = 3.0 Auto Const
Actor Property PLAYER_REF Auto Const
Message Property MESSAGE_TOGGLE_SELECT_DESELECT Auto Const
RefCollectionAlias Property WORKSHOP_OBJECT_COLLECTION Auto Const
Weapon Property CLIPBOARD_SELECTION_GUN Auto Const
Ammo Property CLIPBOARD_SELECTION_AMMO Auto Const
Keyword Property CLIPBOARD_SELECTED Auto Const
Int Property MAX_AMMO_CAPACITY = 10000 Auto Const
Int Property GROUP_SELECT_TIMER_ID = 9912341 Auto Const
int lockCount
int grabGroupCount 
float lastGroupUpdate
bool grabSelect
bool initialized
bool selectToggle
ObjectReference grabObject

bool Function RetireLegacyVisuals(ClipboardQuest owner, int token)
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return false
	EndIf
	CancelTimer(GROUP_SELECT_TIMER_ID)
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return false
	EndIf
	CancelTimerGameTime(GROUP_SELECT_TIMER_ID)
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return false
	EndIf
	; Shared weapon and alias ownership were checked by the quest. Retirement
	; releases only this method's saved reference, never that shared state.
	grabObject = None
	return true
EndFunction

Event OnInit()
	selectToggle = true
EndEvent

bool Function CanHandleGunEvents()
	If IsMethodDestroyed()
		return false
	EndIf
	ClipboardManager manager = GetClipboardManager()
	If !manager
		return false
	EndIf
	return manager.CanHandleManualSelection()
EndFunction

Event OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form source, Projectile ammo, bool isPower, \
  bool isSneak, bool isBash, bool isBlocked, string mat)
	If akAggressor != PLAYER_REF
		return
	EndIf
	If !CanHandleGunEvents()
		UnregisterSelectionEvents()
		return
	EndIf
	RegisterForSelectionHits(!selectToggle)
	while lockCount >= 10
        Utility.WaitMenuMode(0.005)
        If !CanHandleGunEvents()
            return
        EndIf
    endwhile
    lockCount += 1
	bool isSelected = GetClipboardManager().IsSelected(akTarget)
	
	If isSelected && !selectToggle
		GetClipboardManager().Deselect(akTarget)
	ElseIf !isSelected && selectToggle
		GetClipboardManager().Select(akTarget)
	EndIf
	
    lockCount -= 1
EndEvent	

Function RegisterForSelectionHits(bool rebuildList)
	If !CanHandleGunEvents()
		UnregisterSelectionEvents()
		return
	EndIf
	If rebuildList
		ObjectReference[] candidates
		If selectToggle
			; Gun selection is an explicit manual path. Its candidate pool retains
			; historical single-reference safeguards but does not apply the broad
			; automatic transfer filters.
			candidates = GetClipboardManager().GetManualSelectableObjectPool()
		Else
			candidates = GetClipboardManager().GetSelectedObjects()
		EndIf
		; The alias is shared: an old scan must not replace the new tool's pool.
		If !CanHandleGunEvents()
			UnregisterSelectionEvents()
			return
		EndIf
		WORKSHOP_OBJECT_COLLECTION.RemoveAll()
		If !CanHandleGunEvents()
			return
		EndIf
		WORKSHOP_OBJECT_COLLECTION.AddArray(candidates)
	EndIf
	; Pool discovery and native registration can yield across tool destruction.
	If CanHandleGunEvents()
		RegisterForHitEvent(WORKSHOP_OBJECT_COLLECTION,PLAYER_REF)
	EndIf
	If !CanHandleGunEvents()
		UnregisterSelectionEvents()
	EndIf
EndFunction

Function Show()
	If !CanHandleGunEvents()
		UnregisterSelectionEvents()
		return
	EndIf
	If !initialized
		If ClipboardExtension.GetSettingValueBool("Dialogs","bEnableManualMethodDialog",true)
			GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_ManualSelectionHelp"))
		EndIf
		initialized = true
	EndIf
	If !CanHandleGunEvents()
		return
	EndIf
	; Claim the bridge before equipping, so Hide can recognize an in-flight Show.
	ClipboardSelectionWeapon wpn = CLIPBOARD_SELECTION_GUN As ClipboardSelectionWeapon
	If wpn
		wpn.SetClipboardManager(GetClipboardManager())
	EndIf
	If !CanHandleGunEvents()
		UnregisterSelectionEvents()
		return
	EndIf

	If !PLAYER_REF.IsEquipped(CLIPBOARD_SELECTION_GUN)
		PLAYER_REF.EquipItem(CLIPBOARD_SELECTION_GUN,true,true)
		If selectToggle
			Debug.Notification(ClipboardExtension.GetText("$Clipboard_SelectGunEquipped"))
		Else
			Debug.Notification(ClipboardExtension.GetText("$Clipboard_DeselectGunEquipped"))
		EndIf
	EndIf
	
	If !CanHandleGunEvents()
		UnregisterSelectionEvents()
		return
	EndIf
	
	int ammoCount = PLAYER_REF.GetItemCount(CLIPBOARD_SELECTION_AMMO)
	If ammoCount < MAX_AMMO_CAPACITY
		PLAYER_REF.AddItem(CLIPBOARD_SELECTION_AMMO, MAX_AMMO_CAPACITY - ammoCount, true)
	EndIf
	
	RegisterForMenuOpenCloseEvent("Console")
	RegisterForRemoteEvent(GetClipboardManager().GetWorkshop(), "OnWorkshopObjectMoved")
	RegisterForRemoteEvent(GetClipboardManager().GetWorkshop(), "OnWorkshopObjectGrabbed")
	RegisterForRemoteEvent(GetClipboardManager().GetWorkshop(), "OnWorkshopMode")
	RegisterForSelectionHits(true)
EndFunction

Function Hide()
	UnregisterSelectionEvents()
	CancelTimer(GROUP_SELECT_TIMER_ID)
	ClipboardSelectionWeapon wpn = CLIPBOARD_SELECTION_GUN As ClipboardSelectionWeapon
	ClipboardManager manager = GetClipboardManager()
	bool ownsWeapon = wpn && manager && wpn.GetClipboardManager() == manager
	If ownsWeapon
		wpn.SetClipboardManager(None)
	EndIf

	If ownsWeapon && PLAYER_REF.IsEquipped(CLIPBOARD_SELECTION_GUN)
		PLAYER_REF.UnequipItem(CLIPBOARD_SELECTION_GUN, true, true)
		PLAYER_REF.RemoveItem(CLIPBOARD_SELECTION_GUN, 1, true)
	EndIf
	
	int ammoCount = PLAYER_REF.GetItemCount(CLIPBOARD_SELECTION_AMMO)
	If ownsWeapon && ammoCount > 0
		PLAYER_REF.RemoveItem(CLIPBOARD_SELECTION_AMMO, ammoCount, true)
	EndIf
	
	If ownsWeapon
		WORKSHOP_OBJECT_COLLECTION.RemoveAll()
	EndIf
EndFunction

Function UnregisterSelectionEvents()
	UnregisterForAllHitEvents()
	UnregisterForMenuOpenCloseEvent("Console")
	ClipboardManager manager = GetClipboardManager()
	If manager
		UnregisterForRemoteEvent(manager.GetWorkshop(), "OnWorkshopObjectMoved")
		UnregisterForRemoteEvent(manager.GetWorkshop(), "OnWorkshopObjectGrabbed")
		UnregisterForRemoteEvent(manager.GetWorkshop(), "OnWorkshopMode")
	EndIf
EndFunction

Message Function GetSecondaryButtonText()
	return MESSAGE_TOGGLE_SELECT_DESELECT
EndFunction

Function OnSecondaryButtonActivate(ObjectReference akActionRef)
	selectToggle = !selectToggle
	If selectToggle
		Debug.Notification(ClipboardExtension.GetText("$Clipboard_SelectGunEquipped"))
	Else
		Debug.Notification(ClipboardExtension.GetText("$Clipboard_DeselectGunEquipped"))
	EndIf
	Show()
EndFunction

Event OnMenuOpenCloseEvent(string asMenuName, bool abOpening)
	If !CanHandleGunEvents()
		UnregisterSelectionEvents()
		return
	EndIf
    If ( asMenuName == "Console" && !abOpening)
	
        ObjectReference obj = Game.GetCurrentConsoleRef()
		If obj
			If GetClipboardManager().IsSelected(obj)
				GetClipboardManager().Deselect(obj)
			Else
				GetClipboardManager().Select(obj)
			EndIf
			
			RegisterForSelectionHits(!selectToggle)
		EndIf
    EndIf
	
endEvent

Event ObjectReference.OnWorkshopObjectMoved(ObjectReference akSender, ObjectReference akTarget)
	If !CanHandleGunEvents()
		return
	EndIf
	If akTarget == GetClipboardManager().GetWorkshop()
		return
	EndIf
	
	If grabSelect
		If GetClipboardManager().Select(akTarget,false)
			grabGroupCount += 1
		EndIf
	Else
		If GetClipboardManager().Deselect(akTarget,false)
			grabGroupCount += 1
		EndIf
	EndIf
	
	StartTimer(2, GROUP_SELECT_TIMER_ID)
	
	float time = Utility.GetCurrentRealTime()	
	if grabGroupCount > 2 && (lastGroupUpdate > time || lastGroupUpdate + MINIMUM_TIME_BETWEEN_NOTIFICATIONS <= time)
		lastGroupUpdate = time;
		If grabSelect
			GetClipboardMenuManager().ShowUpdate(ClipboardExtension.GetText("$Clipboard_GrabSelected", (grabGroupCount As String)))
		Else
			GetClipboardMenuManager().ShowUpdate(ClipboardExtension.GetText("$Clipboard_GrabDeselected", (grabGroupCount As String)))
		EndIf
	EndIf
EndEvent

Event OnTimer(int timerId)
	If !CanHandleGunEvents()
		return
	EndIf
	If timerId == GROUP_SELECT_TIMER_ID		
		If grabGroupCount <= 1
			return
		EndIf

		If grabSelect
			GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_GroupSelectionComplete"), " ", ClipboardExtension.GetText("$Clipboard_GroupSelectedCount", (grabGroupCount As String)) )
		Else
			GetClipboardMenuManager().ShowInformMenu(ClipboardExtension.GetText("$Clipboard_GroupDeselectionComplete"), " ", ClipboardExtension.GetText("$Clipboard_GroupDeselectedCount", (grabGroupCount As String)) )
		Endif
	EndIf
EndEvent

Event ObjectReference.OnWorkshopObjectGrabbed(ObjectReference akSender, ObjectReference akTarget)
	If !CanHandleGunEvents()
		return
	EndIf
	grabGroupCount = 1
	grabSelect = !GetClipboardManager().IsSelected(akTarget)
	grabObject = akTarget
	If grabSelect
		GetClipboardManager().Select(akTarget)
	Else
		GetClipboardManager().Deselect(akTarget)
	EndIf
EndEvent

Event ObjectReference.OnWorkshopMode(ObjectReference akSender, Bool aStart)
	If !CanHandleGunEvents()
		UnregisterSelectionEvents()
		return
	EndIf
	Show()
	If aStart 
		PLAYER_REF.DrawWeapon()
	EndIf
EndEvent

String Function GetSelectionMethodName()
	return ClipboardExtension.GetText("$Clipboard_MethodManual")
EndFunction

int Function GetSelectionMethodType()
	return 2;
EndFunction

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardSelectionMethodGun() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
