Scriptname ClipboardManager extends ObjectReference

FormList Property SELECTION_EFFECT_LIST Auto Const
ActorValue Property ACTOR_VALUE_POWER_GENERATED Auto Const
ClipboardQuest Property CLIPBOARD_MENUS Auto Const
Keyword Property CLIPBOARD_SELECTED Auto Const
Keyword Property KEYWORD_WORKSHOP_OBJECT Auto Const
Int Property MAX_PENDING_COUNT = 50 Auto Const
Keyword Property WorkshopCaravanKeyword const auto mandatory
{ keyword used for links between workshop locations }
WorkshopParentScript Property WorkshopParent Auto Const mandatory
{ parent quest - holds most general workshop properties }

Int Property SELECTION_EFFECT_TIMER_ID = 1573453 AutoReadOnly

WorkshopScript workshopRef
ObjectReference referenceObject
int pendingEventCount
int effectIndex
bool isDestroyed
bool importInProgress
bool destroyInProgress
bool generatorInitializationFailed
bool legacyCleanupComplete
bool exportInputInProgress
int exportRequestGeneration

; Progress ownership is independent of diagnostic logging.
int importPowerProgressToken = 0

Event OnInit()
	effectIndex = ClipboardExtension.GetSettingValueInt("Selection","iEffectIndex",0)
EndEvent

ObjectReference Function GetLegacyParent()
	return referenceObject
EndFunction

bool Function IsLegacyDestroyed()
	return isDestroyed
EndFunction

bool Function IsLegacyCleanupComplete()
	return legacyCleanupComplete
EndFunction

bool Function CanRetireLegacy(ClipboardQuest owner, int token)
	If !owner || owner != CLIPBOARD_MENUS || !isDestroyed || importInProgress || destroyInProgress || pendingEventCount != 0
		return false
	EndIf
	If !Self.IsDisabled() || owner.GetActiveTool()
		return false
	EndIf
	If !ClipboardExtension.IsLegacyCleanupReference(Self, Game.GetFormFromFile(0x343C8, "Clipboard.esp"), token)
		return false
	EndIf
	If referenceObject
		ClipboardCopyPylonScript pylon = referenceObject As ClipboardCopyPylonScript
		If !pylon || !pylon.CanOwnLegacyCleanup(owner, token)
			return false
		EndIf
		ClipboardManager linkedManager = pylon.GetLegacyManager()
		If linkedManager && linkedManager != Self
			return false
		EndIf
	EndIf
	return ClipboardExtension.IsLegacyCleanupActive(token)
EndFunction

bool Function RetireLegacy(ClipboardQuest owner, int token)
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	; Unlike BeginDestroy, this runs for historical isDestroyed=true helpers.
	CancelTimer(SELECTION_EFFECT_TIMER_ID)
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	CancelTimerGameTime(SELECTION_EFFECT_TIMER_ID)
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	UnregisterForAllEvents()
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	UnregisterForExternalEvent("OnMCMSettingChange|Clipboard")
	If !CanRetireLegacy(owner, token)
		return false
	EndIf
	If referenceObject
		; Releases only this retired tool's selection links/highlighting.
		; Selected/imported settlement references are never deleted here.
		Form shader = None
		If SELECTION_EFFECT_LIST && effectIndex >= 0 && effectIndex < SELECTION_EFFECT_LIST.GetSize()
			shader = SELECTION_EFFECT_LIST.GetAt(effectIndex)
		EndIf
		If !CanRetireLegacy(owner, token)
			return false
		EndIf
		ClipboardExtension.ClearSelection(referenceObject, shader)
		If !CanRetireLegacy(owner, token)
			return false
		EndIf
	EndIf
	Self.Delete()
	If !ClipboardExtension.IsLegacyCleanupActive(token)
		return false
	EndIf
	legacyCleanupComplete = true
	workshopRef = None
	referenceObject = None
	return true
EndFunction

Function BeginDestroy()
	If isDestroyed
		return
	EndIf
	isDestroyed = true
	exportRequestGeneration += 1
	ClipboardExtension.CancelOwnedInput(referenceObject)
	ClearImportProgress()
	CancelTimer(SELECTION_EFFECT_TIMER_ID)
	ClipboardExtension.CancelImportedObjects(referenceObject)
	importInProgress = false
EndFunction

Function Destroy()
	BeginDestroy()
	; Keep referenceObject available to suspended import stacks until they exit.
	; This helper is force-persistent and was previously never marked deleted.
	Self.Delete()
EndFunction

bool Function StopDestroyedImport()
	If isDestroyed
		ClipboardExtension.CancelImportedObjects(referenceObject)
		importInProgress = false
		return true
	EndIf
	return false
EndFunction

Event OnTimer(int timerId)
	If !isDestroyed && timerId == SELECTION_EFFECT_TIMER_ID
		UpdateSelectionEffect()
	EndIf
EndEvent

Function OnMCMSettingChange()
	If !isDestroyed
		StartTimer(2, SELECTION_EFFECT_TIMER_ID)
	EndIf
EndFunction

Function UpdateSelectionEffect()
	If isDestroyed
		return
	EndIf
	int newEffectIndex = ClipboardExtension.GetSettingValueInt("Selection","iEffectIndex",0)
	If effectIndex != newEffectIndex
		EffectShader oldShader = SELECTION_EFFECT_LIST.GetAt(effectIndex) As EffectShader;
		EffectShader newShader = SELECTION_EFFECT_LIST.GetAt(newEffectIndex) As EffectShader;
		ClipboardExtension.RemoveShaderEffectToSelection(referenceObject,oldShader);
		Utility.Wait(0.25)
		If isDestroyed
			return
		EndIf
		ClipboardExtension.ApplyShaderEffectToSelection(referenceObject,newShader);
		effectIndex = newEffectIndex;
	EndIf

EndFunction

bool Function ShowFinishedDialogs()
	return ClipboardExtension.GetSettingValueBool("Dialogs","bEnableActionFinishedDialogs",true);
EndFunction

ObjectReference[] Function GetSelectableObjectPool()
	ObjectReference[] objs = workshopRef.GetLinkedRefChildren(KEYWORD_WORKSHOP_OBJECT);
	return ClipboardExtension.GetSelectableObjectPool(objs, referenceObject)
EndFunction

ObjectReference[] Function GetManualSelectableObjectPool()
	ObjectReference[] objs = workshopRef.GetLinkedRefChildren(KEYWORD_WORKSHOP_OBJECT);
	return ClipboardExtension.GetManualSelectableObjectPool(objs, referenceObject)
EndFunction

ObjectReference[] Function LimitWideSelectionDistance(ObjectReference[] objs)
	float maxDistance = ClipboardExtension.GetSettingValueFloat("Selection", "fWideSelectionMaxDistance", 5000.0)
	If maxDistance < 0.0
		maxDistance = 5000.0
	EndIf

	; Native arrays may exceed 128. Rebuilding one with Array.Add silently
	; loses every later candidate once Papyrus reaches that growth limit.
	; The maximum now applies only to worldspace-persistent storage, not
	; references parented to actual settlement cells.
	return ClipboardExtension.FilterObjectsByDistance(objs, referenceObject, maxDistance)
EndFunction

Function SetReferenceObject(ObjectReference obj)
	ClipboardExtension.ReportBuildIdentity()
	referenceObject = obj;
EndFunction

Function SetWorkshop(WorkshopScript newWorkshopRef)
	workshopRef = newWorkshopRef
EndFunction

WorkshopScript Function GetWorkshop()
	return workshopRef
EndFunction

bool Function IsSelected(ObjectReference obj)
	return obj.GetRefsLinkedToMe(CLIPBOARD_SELECTED).Length > 0
EndFunction

int Function GetSelectedCount()
	return ClipboardExtension.GetSelectionCount(referenceObject)
EndFunction

int Function GetSelectedWireCount()
	return ClipboardExtension.GetSelectionWireCount(referenceObject)
EndFunction

int Function GetSelectedPluginCount()
	return ClipboardExtension.GetSelectedPlugins(referenceObject).Length
EndFunction

ObjectReference[] Function GetSelectedObjects()
	return ClipboardExtension.GetSelectedObjectReferences(referenceObject);
EndFunction

int Function ClearSelection()
	return ClipboardExtension.ClearSelection(referenceObject,SELECTION_EFFECT_LIST.GetAt(effectIndex))
EndFunction

bool Function CanHandleManualSelection()
	; Saved hit/event handlers can outlive their tool. The quest already owns
	; the single active tool; only its manager may change manual selection.
	If isDestroyed || destroyInProgress || !referenceObject || !CLIPBOARD_MENUS || ClipboardExtension.IsLegacyCleanupActive()
		return false
	EndIf
	return CLIPBOARD_MENUS.GetActiveTool() == referenceObject
EndFunction

bool Function Deselect(ObjectReference obj, bool showNotifications = true)
	If !CanHandleManualSelection()
		return false
	EndIf
	If ClipboardExtension.Deselect(referenceObject, obj,SELECTION_EFFECT_LIST.GetAt(effectIndex))
		If showNotifications
			CLIPBOARD_MENUS.ShowUpdate(ClipboardExtension.GetText("$Clipboard_SelectionSize", (GetSelectedCount() As String)))
		EndIf
		return true
	EndIf
	return false
EndFunction

int Function DeselectAll(ObjectReference[] objs)
	If !CanHandleManualSelection()
		return 0
	EndIf
	int count = ClipboardExtension.DeselectAll(referenceObject, objs, SELECTION_EFFECT_LIST.GetAt(effectIndex))
	return count;
EndFunction

bool Function Select(ObjectReference obj, bool showNotifications = true)
	If !CanHandleManualSelection()
		return false
	EndIf
	If obj == workshopRef
		return false;
	EndIf

	If ClipboardExtension.Select(referenceObject, obj, SELECTION_EFFECT_LIST.GetAt(effectIndex))
		If showNotifications
			CLIPBOARD_MENUS.ShowUpdate(ClipboardExtension.GetText("$Clipboard_SelectionSize", (GetSelectedCount() As String)))
		EndIf
		return true
	EndIf
	return false
EndFunction

int Function SelectAll(ObjectReference[] objs)
	If !CanHandleManualSelection()
		return 0
	EndIf
	int count = ClipboardExtension.SelectAll(referenceObject, objs, SELECTION_EFFECT_LIST.GetAt(effectIndex))
	return count;
EndFunction

int Function SelectAllInParentCell(ObjectReference obj)
	return SelectAll(LimitWideSelectionDistance(ClipboardExtension.GetObjectsByCell(obj.GetParentCell())))
EndFunction

int Function SelectAllInCell(Cell obj)
	return SelectAll(LimitWideSelectionDistance(ClipboardExtension.GetObjectsByCell(obj)))
EndFunction

int Function SelectAllInWorkshopCells()
	ObjectReference[] objs = workshopRef.GetLinkedRefChildren(KEYWORD_WORKSHOP_OBJECT)
	return SelectAll(LimitWideSelectionDistance(ClipboardExtension.GetObjectsByWorkshopCells(objs, workshopRef)))
EndFunction

int Function SelectAllByPlugin(string pluginName)
	ObjectReference[] pluginObjects = ClipboardExtension.FilterObjectsByPlugin(GetSelectableObjectPool(),pluginName)
	return SelectAll(LimitWideSelectionDistance(pluginObjects))
EndFunction

string[] Function GetSelectableObjectPoolPlugins()
	return ClipboardExtension.GetPlugins(referenceObject,LimitWideSelectionDistance(GetSelectableObjectPool()))
EndFunction

bool Function IsSlotEmpty(int slot) global
	return ClipboardExtension.GetPatternGeneralInformation(slot).objectCount == 0
EndFunction

int Function GetPatternSize(int slot) global
	return ClipboardExtension.GetPatternGeneralInformation(slot).objectCount
EndFunction

String Function GetPatternCreator(int slot) global
	return ClipboardExtension.GetPatternGeneralInformation(slot).characterName
EndFunction

Int Function GetPatternWorkshopId(int slot) global
	return ClipboardExtension.GetPatternGeneralInformation(slot).workshopId
EndFunction

WorkshopScript[] Function GetLinkedWorkshops()
	WorkshopScript[] linkedWorkshops = new WorkshopScript[0]
	If !workshopRef || workshopRef.IsDeleted() || !workshopRef.myLocation || !WorkshopParent || !WorkshopCaravanKeyword
		return linkedWorkshops
	EndIf
	Location[] linkedLocations = workshopRef.myLocation.GetAllLinkedLocations(WorkshopCaravanKeyword)
	String progressLabel = ClipboardExtension.GetText("$Clipboard_CompileLinkedWorkshops")
	int index = 0
	while (index < linkedLocations.Length)
		If linkedLocations[index]
			WorkshopScript linkedWorkshop = WorkshopParent.GetWorkshopFromLocation(linkedLocations[index])
			If linkedWorkshop && !linkedWorkshop.IsDeleted() && linkedWorkshops.Find(linkedWorkshop) < 0
				linkedWorkshops.Add(linkedWorkshop)
			EndIf
		EndIf
		index += 1
		If index == 1 || index % 8 == 0 || index == linkedLocations.Length
			CLIPBOARD_MENUS.ShowProgressUpdate(index, index, linkedLocations.Length, progressLabel)
		EndIf
	endwhile
	return linkedWorkshops
EndFunction

ObjectReference[] Function GetComponentSources()
	float phaseStart = BeginComponentCostTiming()
	WorkshopScript[] workshops = GetLinkedWorkshops()
	ObjectReference[] containers = new ObjectReference[0]
	ObjectReference sourceRef = None
	If workshopRef && !workshopRef.IsDeleted()
		sourceRef = workshopRef.GetContainer()
		If sourceRef && !sourceRef.IsDeleted()
			containers.Add(sourceRef)
		EndIf
	EndIf
	int index = 0
	String progressLabel = ClipboardExtension.GetText("$Clipboard_CompileWorkshopContainers")
	while (index < workshops.Length)
		If workshops[index] && workshops[index] != workshopRef && !workshops[index].IsDeleted()
			sourceRef = workshops[index].GetContainer()
			If sourceRef && !sourceRef.IsDeleted() && containers.Find(sourceRef) < 0
				containers.Add(sourceRef)
			EndIf
		EndIf
		index += 1
		If index == 1 || index % 8 == 0 || index == workshops.Length
			CLIPBOARD_MENUS.ShowProgressUpdate(index, index, workshops.Length, progressLabel)
		EndIf
	endwhile
	
	sourceRef = Game.GetPlayer()
	If sourceRef && !sourceRef.IsDeleted() && containers.Find(sourceRef) < 0
		containers.Add(sourceRef)
	EndIf
	EndComponentCostTiming("sources", phaseStart, containers.Length, 0)
	return containers;
EndFunction

ObjectReference[] Function GetLocalComponentSources()
	float phaseStart = BeginComponentCostTiming()
	ObjectReference[] containers = new ObjectReference[0]
	If workshopRef && !workshopRef.IsDeleted()
		ObjectReference sourceRef = workshopRef.GetContainer()
		If sourceRef && !sourceRef.IsDeleted()
			containers.Add(sourceRef)
		EndIf
	EndIf
	EndComponentCostTiming("local-sources", phaseStart, containers.Length, 0)
	return containers
EndFunction

; Pure affordability predicate: do not show a shortage until all sources were checked.
bool Function ComponentsCovered(ClipboardExtension:ComponentEntry[] components, int[] availableCounts)
	int i = 0
	While i < components.Length
		If availableCounts[i] < components[i].count
			return false
		EndIf
		i += 1
	EndWhile
	return true
EndFunction

; Caller supplies fresh empty containers and a components.Length count array.
; Outputs live only in this operation. True means the complete source set was read.
bool Function ReadComponentAvailability(ClipboardExtension:ComponentEntry[] components, ObjectReference[] containers, int[] availableCounts, bool forceAllSources)
	float phaseStart = BeginComponentCostTiming()
	ObjectReference[] sources = new ObjectReference[0]
	int[] counts = new int[components.Length]
	bool allSources = forceAllSources
	If components.Length > 0
		If !allSources
			sources = GetLocalComponentSources()
			counts = GetAvailableComponentCounts(components, sources)
			allSources = !ComponentsCovered(components, counts)
		EndIf
		If allSources
			; Re-read local stock too: do not merge balances from different scans.
			sources = GetComponentSources()
			counts = GetAvailableComponentCounts(components, sources)
		EndIf
	EndIf
	int i = 0
	While i < sources.Length
		containers.Add(sources[i])
		i += 1
	EndWhile
	i = 0
	While i < components.Length
		availableCounts[i] = counts[i]
		i += 1
	EndWhile
	If allSources
		EndComponentCostTiming("availability-all", phaseStart, containers.Length, components.Length)
	Else
		EndComponentCostTiming("availability-local", phaseStart, containers.Length, components.Length)
	EndIf
	return allSources
EndFunction

float Function BeginComponentCostTiming()
	If ClipboardExtension.GetSettingValueBool("Debug", "bEnableLogging", false)
		return Utility.GetCurrentRealTime()
	EndIf
	return -1.0
EndFunction

Function EndComponentCostTiming(string phase, float phaseStart, int sourceCount, int componentCount)
	If phaseStart >= 0.0
		ClipboardExtension.ReportComponentCostTiming(phase, Utility.GetCurrentRealTime() - phaseStart, sourceCount, componentCount)
	EndIf
EndFunction

int Function CountComponentInSource(ObjectReference sourceRef, Form componentForm)
	; Retain the legacy helper's nonnegative contract for suspended/external callers.
	int count = ClipboardExtension.CountComponentSource(componentForm, sourceRef)
	If count < 0
		return 0
	EndIf
	return count
EndFunction

Function ShowComponentCost(ClipboardExtension:ComponentEntry[] components)
	SortComponentList(components)
	ObjectReference[] containers = new ObjectReference[0]
	int[] availableCounts = new int[components.Length]
	; Check every row, including hidden ones, before declaring local sufficiency.
	bool allSources = ReadComponentAvailability(components, containers, availableCounts, false)
	String msg = FormatComponentCost(components, availableCounts, allSources)
	If !allSources && components.Length > 0
		If CLIPBOARD_MENUS.ShowYesNoMenu(msg + ClipboardExtension.GetText("$Clipboard_ComponentAllSourcesQuestion"))
			containers = new ObjectReference[0]
			allSources = ReadComponentAvailability(components, containers, availableCounts, true)
			CLIPBOARD_MENUS.ShowInformMenu(FormatComponentCost(components, availableCounts, allSources))
		EndIf
	Else
		CLIPBOARD_MENUS.ShowInformMenu(msg)
	EndIf
EndFunction

String Function FormatComponentCost(ClipboardExtension:ComponentEntry[] components, int[] availableCounts, bool allSources)
	int i =0
	String msg = ClipboardExtension.GetText("$Clipboard_ComponentCostHeading")
	If allSources
		msg += ClipboardExtension.GetText("$Clipboard_ComponentAvailableAll")
	Else
		msg += ClipboardExtension.GetText("$Clipboard_ComponentAvailableLocal")
	EndIf
	While i < components.Length && i < 18
		msg += "[" + components[i].name + "]     " + components[i].count + " / " + availableCounts[i] + "\n"
		i += 1
	EndWhile
	If components.Length > 18
		msg += ClipboardExtension.GetText("$Clipboard_MoreComponentsLine", ((components.Length - 18) As String))
	EndIf
	
	return msg
EndFunction

Function ShowPatternComponentCost()
	int slot = CLIPBOARD_MENUS.ShowSlotSelectionMenu()
	If slot > 0
		ShowComponentCost(ClipboardExtension.GetPatternComponentCost(slot))
	EndIf
EndFunction

Function ShowSelectionComponentCost()
	ShowComponentCost(ClipboardExtension.GetSelectionComponentCost(referenceObject))
EndFunction

Function SortComponentList(ClipboardExtension:ComponentEntry[] components)
	int i = 1
	ClipboardExtension:ComponentEntry temp;
	While i < components.Length
		If components[i-1].name > components[i].name
			temp = components[i-1]
			components[i-1] = components[i]
			components[i] = temp
			If i > 1
				i-=1
			Else
				i+=1
			EndIf
		Else
			i+=1
		EndIf
	EndWhile
EndFunction

int[] Function GetAvailableComponentCounts(ClipboardExtension:ComponentEntry[] components, ObjectReference[] containers)
	float phaseStart = BeginComponentCostTiming()
	int[] availableCounts = new int[components.Length]
	String progressLabel = ClipboardExtension.GetText("$Clipboard_CheckComponents")
	Form[] componentForms = new Form[components.Length]
	int i =0
	While i < components.Length
		componentForms[i] = Game.GetForm(components[i].formId)
		i += 1
	EndWhile
	int j = 0
	While j < containers.Length && components.Length > 0
		; Slice before native dispatch so marshalling also resolves at most 32
		; source handles. Reuse this chunk for all forms, only within this scan.
		ObjectReference[] sourceBatch = new ObjectReference[0]
		While j < containers.Length && sourceBatch.Length < 32
			sourceBatch.Add(containers[j])
			j += 1
		EndWhile
		i = 0
		While i < components.Length
			int available = availableCounts[i]
			int batchCount = ClipboardExtension.CountComponentSources(componentForms[i], sourceBatch, 0)
			If batchCount > 2147483647 - available
				available = 2147483647
			ElseIf batchCount > 0
				available += batchCount
			EndIf
			availableCounts[i] = available
			i += 1
			If j == containers.Length && (i == 1 || i % 4 == 0 || i == components.Length)
				CLIPBOARD_MENUS.ShowProgressUpdate(i, i, components.Length, progressLabel)
			EndIf
		EndWhile
	EndWhile
	EndComponentCostTiming("inventory", phaseStart, containers.Length, components.Length)
	return availableCounts
EndFunction

bool Function HasRequiredComponents(ClipboardExtension:ComponentEntry[] components, int[] availableCounts)
	bool missingAny = false
	int i = 0
	While i < components.Length
		If availableCounts[i] < components[i].count
			missingAny = true
		EndIf
		i += 1
	EndWhile
	If missingAny
		string msg = ClipboardExtension.GetText("$Clipboard_MissingComponentsHeading")
		int k = 0
		int m = 0
		While k < components.Length
			If availableCounts[k] < components[k].count
				if m < 18
					msg += "[" + components[k].name + "]     " + availableCounts[k] + " / " + components[k].count + "\n"
				EndIf
				m += 1
			EndIf
			k += 1
		EndWhile
		If m > 18
			msg += ClipboardExtension.GetText("$Clipboard_MoreMissingComponents", ((m - 18) As String))
		EndIf
		
		CLIPBOARD_MENUS.ShowInformMenu(msg);
		return false;
	EndIf
	return true
EndFunction

; Read-only preview and approval. No resources are removed by this function.
bool Function ConfirmComponentCost(ClipboardExtension:ComponentEntry[] components)
	SortComponentList(components)
	ObjectReference[] containers = new ObjectReference[0]
	int[] availableCounts = new int[components.Length]
	bool allSources = ReadComponentAvailability(components, containers, availableCounts, false)
	If !HasRequiredComponents(components, availableCounts)
		return false
	EndIf
	string msg = FormatComponentCost(components, availableCounts, allSources)
	msg += ClipboardExtension.GetText("$Clipboard_ContinueQuestion")
	return CLIPBOARD_MENUS.ShowYesNoMenu(msg)
EndFunction

bool Function ConsumeComponentCost(ClipboardExtension:ComponentEntry[] components)
	If isDestroyed
		return false
	EndIf
	; Fresh local/full preflight after all prompts, before the first removal.
	ObjectReference[] containers = new ObjectReference[0]
	int[] availableCounts = new int[components.Length]
	bool allSources = ReadComponentAvailability(components, containers, availableCounts, false)
	If !HasRequiredComponents(components, availableCounts) || isDestroyed
		return false
	EndIf
	float phaseStart = BeginComponentCostTiming()
	String progressLabel = ClipboardExtension.GetText("$Clipboard_RemoveComponents")
	int i = 0
	While i < components.Length
		Form compForm = Game.GetForm(components[i].formId)
		Component compRef = compForm As Component
		int j = 0
		int needed = components[i].count
		while (j < containers.Length || !allSources) && needed > 0 && !isDestroyed
			If j >= containers.Length
				; Local preflight can succeed but stock/removal can change afterwards.
				; Expand once, without retrying any already-attempted source. An
				; unreadable post-withdrawal count returns below before reaching here.
				ObjectReference[] expanded = GetComponentSources()
				int sourceIndex = 0
				While sourceIndex < expanded.Length
					If containers.Find(expanded[sourceIndex]) < 0
						containers.Add(expanded[sourceIndex])
					EndIf
					sourceIndex += 1
				EndWhile
				allSources = true
			EndIf
			If j < containers.Length && !isDestroyed
				Int beforeCount = ClipboardExtension.CountComponentSource(compForm, containers[j])
				Int count = beforeCount
				If count > 0
					If count > needed
						count = needed;
					EndIf
				
					If compRef
						containers[j].RemoveComponents(compRef,count)
					Else
						containers[j].RemoveItem(compForm,count,true,None)
					EndIf
					; Both removal natives return void. Credit only the observed reduction,
					; bounded by what was requested; never silently accept an unpaid cost.
					int afterCount = ClipboardExtension.CountComponentSource(compForm, containers[j])
					If afterCount < 0
						; A removal may have occurred. An unreadable balance cannot prove
						; payment or justify charging another source for the same cost.
						EndComponentCostTiming("withdrawal-unverified", phaseStart, containers.Length, i + 1)
						ClipboardExtension.LogIssue("[Clipboard] Component payment stopped: post-withdrawal inventory could not be verified.")
						CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_ComponentPaymentIncomplete"))
						return false
					EndIf
					int removed = beforeCount - afterCount
					If removed > count
						removed = count
					EndIf
					If removed > 0
						needed -= removed
					EndIf
				EndIf
				j += 1
			EndIf
		EndWhile
		If needed > 0 || isDestroyed
			EndComponentCostTiming("withdrawal-incomplete", phaseStart, containers.Length, i + 1)
			CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_ComponentPaymentIncomplete"))
			return false
		EndIf
		i += 1
		If i == 1 || i % 4 == 0 || i == components.Length
			CLIPBOARD_MENUS.ShowProgressUpdate(i, i, components.Length, progressLabel)
		EndIf
	EndWhile
	EndComponentCostTiming("withdrawal", phaseStart, containers.Length, components.Length)
	return true;
EndFunction

; Retain the legacy entry point for existing callers and suspended saves.
bool Function CheckComponentCost(ClipboardExtension:ComponentEntry[] components)
	If !ConfirmComponentCost(components)
		return false
	EndIf
	return ConsumeComponentCost(components)
EndFunction

Function EnterWorkshopModeIfNeeded()
	bool enter = CLIPBOARD_MENUS.ShowWorkshopModeCheck(GetWorkshop())
	If enter
		workshopRef.StartWorkshop()
		ObjectReference[] selectedObjects = ClipboardExtension.GetSelectedObjectReferences(referenceObject);

		int count = ClipboardExtension.GetFullyLoadedCount(selectedObjects)
		int stableChecks = 0
		int checkCount = 0
		While stableChecks < 8 && checkCount < 60
			Utility.Wait(0.5)
			int newCount = ClipboardExtension.GetFullyLoadedCount(selectedObjects)
			If newCount == count
				stableChecks += 1
			Else
				stableChecks = 0
				count = newCount;
			EndIf
			checkCount += 1;
		EndWhile
	EndIf
EndFunction

bool Function WaitOnSelectionLoad(ObjectReference[] importedObjects = None, bool disableImportedHavok = false, Float[] targetPositionX = None, Float[] targetPositionY = None, Float[] targetPositionZ = None, Float[] targetAngleX = None, Float[] targetAngleY = None, Float[] targetAngleZ = None)
	; Retained for older suspended callers. New imports use PrepareImportedObjects.
	; Never invent an original transform after objects have already been enabled.
	Bool[] havokHandledObjects = None
	If disableImportedHavok
		If importedObjects == None || importedObjects.Length > 128
			ClipboardExtension.LogIssue("[Clipboard] Legacy import wait cannot safely restore Havok: missing rows or more than 128 rows. Native import preparation is required.")
			return false
		EndIf
		If targetPositionX == None || targetPositionY == None || targetPositionZ == None || targetAngleX == None || targetAngleY == None || targetAngleZ == None
			ClipboardExtension.LogIssue("[Clipboard] Legacy import wait cannot safely restore Havok: original transform snapshots are missing.")
			return false
		EndIf
		If targetPositionX.Length != importedObjects.Length || targetPositionY.Length != importedObjects.Length || targetPositionZ.Length != importedObjects.Length || targetAngleX.Length != importedObjects.Length || targetAngleY.Length != importedObjects.Length || targetAngleZ.Length != importedObjects.Length
			ClipboardExtension.LogIssue("[Clipboard] Legacy import wait cannot safely restore Havok: original transform snapshot lengths do not match the imported rows.")
			return false
		EndIf
		havokHandledObjects = new Bool[128]
	EndIf

	bool loaded = false
	int w = 0
	while w < 60 && !loaded && !isDestroyed
		; Set keyframed motion as soon as each imported non-actor has loaded 3D.
		; This mirrors the motion operation used by DefaultDisableHavokOnLoad
		; without its editor-location reset, which is invalid for created refs.
		If havokHandledObjects != None
			int objectIndex = 0
			While objectIndex < importedObjects.Length
				ObjectReference importedObject = importedObjects[objectIndex]
				If importedObject && !havokHandledObjects[objectIndex] && importedObject.Is3DLoaded()
					havokHandledObjects[objectIndex] = true
					If (importedObject as Actor) == None && (importedObject as GenericMotorScript) == None
						importedObject.SetMotionType(importedObject.Motion_Keyframed, true)
						; A dynamic object may move between Enable and its loaded-3D
						; callback. Restore the transform captured while it was disabled,
						; rather than using the editor-location reset from the vanilla
						; helper script.
						If targetPositionX != None && objectIndex < targetPositionX.Length
							If targetPositionY != None && objectIndex < targetPositionY.Length
								If targetPositionZ != None && objectIndex < targetPositionZ.Length
									If targetAngleX != None && objectIndex < targetAngleX.Length
										If targetAngleY != None && objectIndex < targetAngleY.Length
											If targetAngleZ != None && objectIndex < targetAngleZ.Length
												If importedObject.GetPositionX() != targetPositionX[objectIndex] || importedObject.GetPositionY() != targetPositionY[objectIndex] || importedObject.GetPositionZ() != targetPositionZ[objectIndex] || importedObject.GetAngleX() != targetAngleX[objectIndex] || importedObject.GetAngleY() != targetAngleY[objectIndex] || importedObject.GetAngleZ() != targetAngleZ[objectIndex]
													importedObject.SetPosition(targetPositionX[objectIndex], targetPositionY[objectIndex], targetPositionZ[objectIndex])
													importedObject.SetAngle(targetAngleX[objectIndex], targetAngleY[objectIndex], targetAngleZ[objectIndex])
												EndIf
											EndIf
										EndIf
									EndIf
								EndIf
							EndIf
						EndIf
					EndIf
				EndIf
				objectIndex += 1
			EndWhile
		EndIf

		loaded = ClipboardExtension.IsSelectionFullyLoaded(referenceObject);
		If !loaded
			Utility.Wait(0.5)
		EndIf
		w += 1
	EndWhile

	return loaded && !isDestroyed
EndFunction

Function InitializePlacedPowerGenerators(ObjectReference[] placedObjects)
	; Retain the old callable name for suspended callers; current imports use the
	; general animation pass after workshop callbacks, wires and power refresh.
	generatorInitializationFailed = !InitializePlacedAnimations(placedObjects)
EndFunction

Function ClearImportProgress()
	; Release the HUD and native power session on completion, interruption,
	; tool destruction or a new import, even when diagnostic logging is off.
	ClipboardExtension.ClearImportedProgress(referenceObject)
	If importPowerProgressToken > 0
		ClipboardExtension.EndImportedPowerProgress(referenceObject, importPowerProgressToken)
		importPowerProgressToken = 0
	EndIf
EndFunction

bool Function InitializePlacedAnimations(ObjectReference[] placedObjects, int powerProgressToken = 0)
	If placedObjects == None
		return false
	EndIf
	Int[] candidates = ClipboardExtension.GetImportedAnimationCandidates(referenceObject, placedObjects)
	If isDestroyed || candidates == None || candidates.Length < 4
		return false
	EndIf
	If candidates[0] != 1 || candidates[1] != placedObjects.Length || candidates[2] < 0 || (candidates.Length - 4) % 2 != 0
		return false
	EndIf
	int unavailableCount = candidates[2]
	int i = 4
	int previousRow = -1
	While i < candidates.Length && !isDestroyed
		int rowIndex = candidates[i]
		int animationKind = candidates[i + 1]
		If rowIndex <= previousRow || rowIndex >= placedObjects.Length || animationKind < 1 || animationKind > 3
			return false
		EndIf
		previousRow = rowIndex
		ObjectReference placedObject = placedObjects[rowIndex]
		If placedObject
			If !placedObject.Is3DLoaded()
				unavailableCount += 1
			ElseIf animationKind > 0 && !placedObject.IsDestroyed() && !isDestroyed
				bool powered = placedObject.IsPowered()
				If animationKind == 1 || animationKind == 3
					; These models expose the reviewed Workshop power sequences.
					; Reset can be rejected in the current graph state. It is not a
					; prerequisite for Powered/Unpowered and must not drive warnings.
					String powerEvent = "Unpowered"
					If powered
						powerEvent = "Powered"
					EndIf
					placedObject.PlayAnimation(powerEvent)
					; An event return describes the transition, not visible motion.
					; An already-current state may reject an unnecessary transition.
				EndIf
				; Motor blades are independent of the graph, generation, sound,
				; and activation. Native preparation preserves their Havok motion.
				If animationKind == 2 || animationKind == 3
					GenericMotorScript motor = placedObject as GenericMotorScript
					If motor && !isDestroyed
						int openState = placedObject.GetOpenState()
						bool motorOn = placedObject.IsPowered() && !placedObject.IsDestroyed() && (openState == 0 || openState >= 3)
						placedObject.FanMotorOn(motorOn)
					ElseIf !isDestroyed
						unavailableCount += 1
					EndIf
				EndIf
			EndIf
		EndIf
		If powerProgressToken > 0 && !isDestroyed
			ClipboardExtension.AdvanceImportedPowerProgress(referenceObject, powerProgressToken, rowIndex)
		EndIf
		i += 2
	EndWhile
	; Direct looping controllers and other model defaults start on Enable/OnLoad.
	; Do not invent animation names or activate objects to restart them.
	return unavailableCount == 0 && !isDestroyed
EndFunction

int Function PastePattern(ObjectReference positionReference, bool inWorkshopMode)
	If isDestroyed
		return 0
	EndIf
	If destroyInProgress
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_CleanupBusy"))
		return 0
	EndIf
	If importInProgress
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PasteAlreadyRunning"))
		return 0
	EndIf
	importInProgress = true
	ClearImportProgress()
	int slot = CLIPBOARD_MENUS.ShowSlotSelectionMenu()
	If slot <= 0 || StopDestroyedImport()
		importInProgress = false
		return 0
	EndIf

	ClipboardExtension:PatternGeneralEntry generalEntry = ClipboardExtension.GetPatternGeneralInformation(slot)
	If StopDestroyedImport()
		return 0
	EndIf
	If !generalEntry || generalEntry.objectCount <= 0
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PasteSlotEmpty", (slot As String)))
		importInProgress = false
		return 0
	EndIf
	
	string[] plugins = ClipboardExtension.GetPatternPlugins(slot)
	If StopDestroyedImport()
		return 0
	EndIf
	If plugins == None || plugins.Length < generalEntry.pluginCount
		ClipboardExtension.LogIssue("[Clipboard] PastePattern aborted: slot " + slot + " has an incomplete plugin table.", true)
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PastePluginTableIncomplete"))
		importInProgress = false
		return 0
	EndIf

	String[] missingPlugins = new String[0]
	String plugin
	int i = 0
	While i < generalEntry.pluginCount
		plugin = plugins[i]
		If "Fallout4.esm" != plugin && !Game.IsPluginInstalled(plugin)
			missingPlugins.Add(plugin)
		EndIf
		i += 1
	EndWhile

	If StopDestroyedImport()
		return 0
	EndIf
	If missingPlugins.Length > 0
		If !CLIPBOARD_MENUS.ShowMissingPluginsMenu(missingPlugins)
			importInProgress = false
			return 0
		EndIf
	EndIf
	If StopDestroyedImport()
		return 0
	EndIf

	bool chargeComponentCost = ClipboardExtension.GetSettingValueBool("Balance", "bEnableComponentCost", true)
	ClipboardExtension:ComponentEntry[] components = None
	If chargeComponentCost
		components = ClipboardExtension.GetPatternComponentCost(slot)
		If StopDestroyedImport()
			return 0
		EndIf
		If !ConfirmComponentCost(components)
			importInProgress = false
			return 0
		EndIf
	EndIf
	If StopDestroyedImport()
		return 0
	EndIf

	; Finish slot, missing-plugin and read-only cost prompts before build mode.
	If !inWorkshopMode
		EnterWorkshopModeIfNeeded()
	EndIf
	If StopDestroyedImport()
		return 0
	EndIf

	ClearSelection();
	Utility.Wait(0.1)
	If StopDestroyedImport()
		return 0
	EndIf
	; Pay only after preflight and its prompts have completed, just before placement.
	If chargeComponentCost
		If !ConsumeComponentCost(components)
			importInProgress = false
			return 0
		EndIf
	EndIf
	If StopDestroyedImport()
		return 0
	EndIf
	ClipboardExtension:ImportPlacementRow[] placement = ClipboardExtension.PastePatternObjectsWithReuse(referenceObject, slot)
	ObjectReference[] newObjs = None
	ObjectReference[] wireRows = None
	int reusedRowCount = 0
	If placement != None
		newObjs = ClipboardExtension.GetImportPlacementRows(placement, false)
		wireRows = ClipboardExtension.GetImportPlacementRows(placement, true)
		reusedRowCount = ClipboardExtension.GetImportPlacementReusedCount(placement)
	EndIf
	If StopDestroyedImport()
		; Preparation has not taken ownership yet; do not leave new refs disabled.
		If newObjs != None
			ClipboardExtension.EnableObjects(newObjs)
		EndIf
		return 0
	EndIf
	If newObjs == None || newObjs.Length == 0 || wireRows == None || wireRows.Length != newObjs.Length || reusedRowCount < 0 || reusedRowCount > newObjs.Length
		ClipboardExtension.LogIssue("[Clipboard] PastePattern aborted: PastePatternObjectsWithReuse returned invalid result rows for slot " + slot + ".", true)
		If newObjs != None
			ClipboardExtension.EnableObjects(newObjs)
		EndIf
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PastePlacementFailed"), " ", ClipboardExtension.GetText("$Clipboard_ReviewNativeLog"))
		importInProgress = false
		return 0
	EndIf
	String reusedNotice = ""
	String reusedSummary = ""
	If reusedRowCount > 0
		reusedNotice = ClipboardExtension.GetText("$Clipboard_PasteReusedRows", (reusedRowCount As String))
		reusedSummary = "\n" + reusedNotice
		; Report actual duplicates even when warning/finished dialogs are disabled
		; or later preparation/workshop processing cannot complete.
		Debug.Notification(reusedNotice)
	EndIf
	bool disableImportedHavok = ClipboardExtension.GetSettingValueBool("Balance", "bDisableHavokOnImportedObjects", true)
	If StopDestroyedImport()
		ClipboardExtension.EnableObjects(newObjs)
		return 0
	EndIf
	If StopDestroyedImport()
		ClipboardExtension.EnableObjects(newObjs)
		ClearImportProgress()
		return 0
	EndIf
	Int[] preparation = ClipboardExtension.PrepareImportedRows(referenceObject, newObjs, disableImportedHavok)
	If StopDestroyedImport()
		ClearImportProgress()
		return 0
	EndIf
	If preparation == None || preparation.Length != newObjs.Length + 10
		ClipboardExtension.LogIssue("[Clipboard] Native import preparation returned an invalid summary for slot " + slot + ".", true)
		ClipboardExtension.EnableObjects(newObjs)
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PreparationFailed"), ClipboardExtension.GetText("$Clipboard_PreparationObjectsMayExist") + reusedSummary, ClipboardExtension.GetText("$Clipboard_ReviewBothLogs"))
		ClipboardExtension.CancelImportedObjects(referenceObject)
		ClearImportProgress()
		importInProgress = false
		return 0
	EndIf
	int placedObjectCount = preparation[1]
	If preparation[9] != 1 || preparation[0] < 0 || preparation[0] > 1 || placedObjectCount < 0 || placedObjectCount > newObjs.Length
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PreparationInterrupted"), ClipboardExtension.GetText("$Clipboard_PreparationObjectsMayExist") + reusedSummary, ClipboardExtension.GetText("$Clipboard_ReviewBothLogs"))
		ClearImportProgress()
		importInProgress = false
		return 0
	EndIf
	If placedObjectCount <= 0 && reusedRowCount <= 0
		ClipboardExtension.LogIssue("[Clipboard] PastePattern aborted: slot " + slot + " placed no objects from " + newObjs.Length + " result rows.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PasteNoObjects"), reusedNotice, ClipboardExtension.GetText("$Clipboard_ReviewNativeLog"))
		ClearImportProgress()
		importInProgress = false
		return 0
	EndIf
	String importWarnings = ""
	; Keep support logs stable while player-facing warnings follow the game language.
	; Build diagnostic-only text only while logging is enabled.
	String importDiagnosticWarnings = ""
	If newObjs.Length != generalEntry.objectCount || placedObjectCount + reusedRowCount != generalEntry.objectCount
		If ClipboardExtension.GetSettingValueBool("Debug", "bEnableLogging", false)
			Debug.Trace("[Clipboard] PastePattern slot " + slot + " placed " + placedObjectCount + " objects from " + newObjs.Length + " result rows for " + generalEntry.objectCount + " pattern objects.")
			importDiagnosticWarnings = "Some pattern rows were skipped. "
		EndIf
		importWarnings = ClipboardExtension.GetText("$Clipboard_WarningRowsSkipped")
	EndIf
	; Only verified successful new rows enter later phases. Keep existing reused
	; endpoints, except when they alias a failed newly placed reference.
	ObjectReference[] preparedObjs = ClipboardExtension.GetSuccessfulImportRows(newObjs, newObjs, preparation, false)
	wireRows = ClipboardExtension.GetSuccessfulImportRows(wireRows, newObjs, preparation, false)
	If preparedObjs == None || preparedObjs.Length != newObjs.Length || wireRows == None || wireRows.Length != newObjs.Length
		ClipboardExtension.CancelImportedObjects(referenceObject)
		ClearImportProgress()
		importInProgress = false
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PreparationFailed"), ClipboardExtension.GetText("$Clipboard_PreparationObjectsMayExist") + reusedSummary, ClipboardExtension.GetText("$Clipboard_ReviewBothLogs"))
		return placedObjectCount
	EndIf
	Int excludedObjectCount = preparation[6] + preparation[7]
	Utility.Wait(0.1)
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	; The legacy event API only acknowledges dispatch. The import-specific job
	; limits outstanding callbacks and resumes us after their actual returns.
	; Keep this wait outside a modal menu so workshop scripts can make progress.
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	Int[] workshopResult = ClipboardExtension.InitializeImportedWorkshopRows(referenceObject, preparedObjs)
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	If workshopResult == None || workshopResult.Length != preparedObjs.Length + 11
		ClipboardExtension.LogIssue("[Clipboard] Workshop initialization returned an invalid summary; import stopped before wires and power.", true)
		ClipboardExtension.CancelImportedObjects(referenceObject)
		; Completion is unknown. Retain the import guard on this tool.
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_WorkshopUnverified"), ClipboardExtension.GetText("$Clipboard_WorkshopObjectsPlaced") + reusedSummary, ClipboardExtension.GetText("$Clipboard_WorkshopReviewBeforeRetry"))
		ClearImportProgress()
		return placedObjectCount
	EndIf
	If workshopResult[10] != 1 || workshopResult[0] < 0 || workshopResult[5] != 0
		ClipboardExtension.CancelImportedObjects(referenceObject)
		ClipboardExtension.LogIssue("[Clipboard] PastePattern slot " + slot + " stopped: workshop initialization incomplete; returned calls " + workshopResult[3] + " / " + workshopResult[1] + "; outstanding calls " + workshopResult[5] + "; wiring and power were skipped.")
		; A timed-out callback can still return later. Do not permit another
		; paste on this tool while its reported completion remains uncertain.
		importInProgress = workshopResult[5] != 0
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_WorkshopIncomplete"), ClipboardExtension.GetText("$Clipboard_ObjectCount", (placedObjectCount As String)) + reusedSummary, ClipboardExtension.GetText("$Clipboard_WorkshopWiringSkipped"))
		ClearImportProgress()
		return placedObjectCount
	EndIf
	If workshopResult[0] != 1
		ClipboardExtension.LogIssue("[Clipboard] PastePattern slot " + slot + " continuing for successful workshop rows; returned calls " + workshopResult[3] + " / " + workshopResult[1] + "; outstanding calls " + workshopResult[5] + "; failed objects " + workshopResult[4] + ".")
	EndIf
	ObjectReference[] readyObjs = ClipboardExtension.GetSuccessfulImportRows(preparedObjs, preparedObjs, workshopResult, true)
	wireRows = ClipboardExtension.GetSuccessfulImportRows(wireRows, preparedObjs, workshopResult, true)
	If readyObjs == None || readyObjs.Length != newObjs.Length || wireRows == None || wireRows.Length != newObjs.Length
		ClipboardExtension.CancelImportedObjects(referenceObject)
		importInProgress = true
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_WorkshopUnverified"), ClipboardExtension.GetText("$Clipboard_WorkshopObjectsPlaced") + reusedSummary, ClipboardExtension.GetText("$Clipboard_WorkshopReviewBeforeRetry"))
		ClearImportProgress()
		return placedObjectCount
	EndIf
	excludedObjectCount += workshopResult[4]
	importPowerProgressToken = ClipboardExtension.BeginImportedPowerProgress(referenceObject, readyObjs)
	If excludedObjectCount > 0
		String excludedNotice = ClipboardExtension.GetText("$Clipboard_PasteExcludedRows", (excludedObjectCount As String))
		Debug.Notification(excludedNotice)
		importWarnings += excludedNotice
		importDiagnosticWarnings += "Failed or unready objects excluded from wiring/power: " + excludedObjectCount + ". "
		reusedSummary += "\n" + excludedNotice
	EndIf
	Utility.Wait(0.1)
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	ClipboardExtension.ApplyShaderEffectToSelection(referenceObject, SELECTION_EFFECT_LIST.GetAt(effectIndex));
	Utility.Wait(0.1)
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	ObjectReference[] newWires = new ObjectReference[0]
	int expectedImportWires = ClipboardExtension.GetPatternImportWireCount(referenceObject, slot)
	newWires = ClipboardExtension.PastePatternWiresForRows(referenceObject,slot,wireRows)
	If newWires == None
		newWires = new ObjectReference[0]
		; A successful empty native array also compares equal to None.
		; Check real wire rows when metadata says zero; wire_count never gates creation.
		bool wiresExpected = expectedImportWires > 0
		If !wiresExpected
			ClipboardExtension:PatternWireEntry[] patternWireRows = ClipboardExtension.GetPatternWires(slot)
			wiresExpected = patternWireRows != None && patternWireRows.Length > 0
		EndIf
		If wiresExpected
			ClipboardExtension.LogIssue("[Clipboard] PastePattern slot " + slot + " returned no wires although the pattern requests them.")
			importDiagnosticWarnings += "Wire creation failed. "
			importWarnings += ClipboardExtension.GetText("$Clipboard_WarningWireCreationFailed")
		EndIf
	EndIf
	Utility.Wait(0.1)
	; Enable created wires even if the tool was destroyed during the latent call.
	ClipboardExtension.EnableObjects(newWires);
	Utility.Wait(0.1)
	If newWires.Length < expectedImportWires
		importWarnings += ClipboardExtension.GetText("$Clipboard_WarningWiresMissing")
		importDiagnosticWarnings += "Some pattern wires were not created. "
	EndIf
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	Int[] powerResult = None
	; Keep the existing section/key for configuration compatibility. Split power
	; defaults On for every pattern, independently of diagnostic logging.
	bool splitPowerPasses = ClipboardExtension.GetSettingValueBool("ImportStateTracking", "bSplitPowerPasses", true)
	If ClipboardExtension.GetSettingValueBool("Debug", "bEnableLogging", false)
		Debug.Trace("[Clipboard] POWER_MODE slot=" + slot + " splitPowerPasses=" + splitPowerPasses)
	EndIf
	If splitPowerPasses
		; Defer Clipboard's explicit wireless refreshes, not
		; engine-side effects of AddConnection or unrelated queued listeners.
		powerResult = ClipboardExtension.ConnectImportedPowerForRows(referenceObject, readyObjs, newObjs)
		If StopDestroyedImport()
			ClearImportProgress()
			return placedObjectCount
		EndIf
		If powerResult != None && powerResult.Length == 9 && powerResult[0] == 1
			If StopDestroyedImport()
				ClearImportProgress()
				return placedObjectCount
			EndIf
			powerResult = ClipboardExtension.RefreshImportedPowerForRows(referenceObject, readyObjs, newObjs)
		EndIf
	Else
		powerResult = ClipboardExtension.ReconnectImportedPowerForRows(referenceObject, readyObjs, newObjs)
	EndIf
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	If powerResult == None || powerResult.Length != 9
		ClipboardExtension.LogIssue("[Clipboard] Native imported-power reconnection returned an invalid summary for slot " + slot + ".", true)
		importDiagnosticWarnings += "Power reconnection failed. "
		importWarnings += ClipboardExtension.GetText("$Clipboard_WarningPowerFailed")
	Else
		If powerResult[0] != 1
			importWarnings += ClipboardExtension.GetText("$Clipboard_WarningPowerIncomplete")
			importDiagnosticWarnings += "Power reconnection was incomplete. "
		EndIf
	EndIf
	Utility.Wait(0.1)
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf

	; Refresh animation using the final power state, including consumers such
	; as motor-driven ceiling fans. Missing rows do not block loaded rows.
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	bool animationsInitialized = InitializePlacedAnimations(readyObjs, importPowerProgressToken)
	If !animationsInitialized || isDestroyed
		ClipboardExtension.ClearImportedProgress(referenceObject)
	EndIf
	ClipboardExtension.EndImportedPowerProgress(referenceObject, importPowerProgressToken)
	importPowerProgressToken = 0
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	If !animationsInitialized
		importWarnings += ClipboardExtension.GetText("$Clipboard_WarningGeneratorAnimation")
		importDiagnosticWarnings += "Some imported animations could not be initialized. "
	EndIf

	ClearImportProgress()
	If StopDestroyedImport()
		ClearImportProgress()
		return placedObjectCount
	EndIf
	If importDiagnosticWarnings != ""
		ClipboardExtension.LogIssue("[Clipboard] Import warnings: " + importDiagnosticWarnings)
	EndIf
	If importWarnings != "" && ClipboardExtension.GetSettingValueBool("Debug", "bDisplayImportWarnings", false)
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PasteWarnings", (slot As String)), ClipboardExtension.GetText("$Clipboard_PasteCountsWithWires", (placedObjectCount As String), (generalEntry.objectCount As String), (newWires.Length As String)) + reusedSummary, importWarnings)
	ElseIf ShowFinishedDialogs()
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PasteFinished", (slot As String)), ClipboardExtension.GetText("$Clipboard_PasteObjectCounts", (placedObjectCount As String), (generalEntry.objectCount As String)) + reusedSummary, ClipboardExtension.GetText("$Clipboard_PasteWireCount", (newWires.Length As String)))
	EndIf
	importInProgress = false
	return placedObjectCount
EndFunction

String Function GetPatternName(int slot) global
	ClipboardExtension:PatternGeneralEntry generalEntry = ClipboardExtension.GetPatternGeneralInformation(slot)
	String patternName = generalEntry.patternName
	If !patternName
		If generalEntry.objectCount > 0
			patternName = generalEntry.characterName + " (" + generalEntry.objectCount + ")"
		Else
			patternName = ClipboardExtension.GetText("$Clipboard_EmptySlot")
		EndIf
	EndIf
	return patternName
EndFunction


int Function SavePattern(ObjectReference positionReference)
	If isDestroyed || exportInputInProgress
		return 0
	EndIf
	exportInputInProgress = true
	exportRequestGeneration += 1
	int requestGeneration = exportRequestGeneration
	ObjectReference requestOwner = referenceObject
	RegisterForRemoteEvent(Game.GetPlayer(), "OnPlayerLoadGame")
	int savedCount = SavePatternForOperation(requestOwner, requestGeneration)
	If requestGeneration == exportRequestGeneration
		exportInputInProgress = false
	EndIf
	return savedCount
EndFunction

bool Function IsExportInputCurrent(ObjectReference requestOwner, int requestGeneration)
	If isDestroyed || requestGeneration != exportRequestGeneration || requestOwner != referenceObject
		return false
	EndIf
	If !CLIPBOARD_MENUS.IsInputOwnerValid(requestOwner)
		return false
	EndIf
	return !isDestroyed && requestGeneration == exportRequestGeneration
EndFunction

Event Actor.OnPlayerLoadGame(Actor akSender)
	exportRequestGeneration += 1
	exportInputInProgress = false
EndEvent

int Function SavePatternForOperation(ObjectReference requestOwner, int requestGeneration)
	If !IsExportInputCurrent(requestOwner, requestGeneration)
		return 0
	EndIf
	int slot = CLIPBOARD_MENUS.ShowSlotSelectionMenu()
	If slot <= 0 || !IsExportInputCurrent(requestOwner, requestGeneration)
		return 0
	EndIf

	ClipboardExtension:PatternGeneralEntry oldGeneralEntry = ClipboardExtension.GetPatternGeneralInformation(slot)
	If oldGeneralEntry.objectCount > 0
		If !CLIPBOARD_MENUS.ShowYesNoMenu(ClipboardExtension.GetText("$Clipboard_PatternExists", (slot As String)), ClipboardExtension.GetText("$Clipboard_ObjectCount", (oldGeneralEntry.objectCount As String)), " ", ClipboardExtension.GetText("$Clipboard_OverwritePatternQuestion"))
			return 0
		EndIf
	EndIf
	If !IsExportInputCurrent(requestOwner, requestGeneration)
		return 0
	EndIf
	int count = GetSelectedCount();
	String characterName = Game.GetPlayer().GetBaseObject().GetName()
	String patternName = characterName + " (" + count + ")"
	patternName = CLIPBOARD_MENUS.ShowPatternNameMenuForOwner(requestOwner, patternName)
	If !patternName || !IsExportInputCurrent(requestOwner, requestGeneration)
		return 0
	EndIf
	
	ClipboardExtension:PatternGeneralEntry savedPattern = ClipboardExtension.WritePatternFile(slot, patternName, characterName, referenceObject)
	If savedPattern == None
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PatternSaveFailed", (slot As String)))
		return 0
	EndIf
	If ShowFinishedDialogs()
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_PatternSaved", (slot As String)), ClipboardExtension.GetText("$Clipboard_ObjectCount", (savedPattern.objectCount As String)))
	EndIf
	return savedPattern.objectCount
EndFunction

String Function GetSourceSettlementName(WorkshopScript workshop, String plugin, int localId)
	Location settlement = workshop.myLocation
	If !settlement
		settlement = workshop.GetCurrentLocation()
	EndIf
	String settlementName = ""
	If settlement
		settlementName = settlement.GetName()
	EndIf
	If !settlementName && workshop.myMapMarker
		settlementName = workshop.myMapMarker.GetDisplayName()
	EndIf
	If !settlementName
		settlementName = plugin + " #" + localId
	EndIf
	return settlementName
EndFunction

Bool Function IsReferenceWithinSourceWorkshop(ObjectReference obj, WorkshopScript workshop)
	If !obj || !workshop
		return false
	EndIf
	return ClipboardExtension.IsPositionWithinWorkshop(workshop, obj.GetParentCell(), obj.GetPositionX(), obj.GetPositionY(), obj.GetPositionZ())
EndFunction

Function MoveToPatternSource(ClipboardToolScript tool)
	int slot = CLIPBOARD_MENUS.ShowSlotSelectionMenu()
	If slot <= 0
		return
	EndIf
	Actor player = Game.GetPlayer()
	If !tool || !player
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: tool or current workshop is unavailable.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceMovementUnavailable"))
		return
	EndIf

	ClipboardExtension:PatternGeneralEntry generalInformation = ClipboardExtension.GetPatternGeneralInformation(slot)
	If !generalInformation || generalInformation.objectCount <= 0
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: slot " + slot + " has no valid general information.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourcePatternInvalid"))
		return
	EndIf

	String[] pluginList = ClipboardExtension.GetPatternPlugins(slot);
	If pluginList == None
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: slot " + slot + " returned no plugin list.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourcePluginListUnreadable"))
		return
	EndIf
	If generalInformation.workshopPlugin < 0 || generalInformation.workshopPlugin >= pluginList.Length
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: invalid workshop plugin row " + generalInformation.workshopPlugin + ".")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceWorkshopIndexInvalid"))
		return
	EndIf

	String plugin = pluginList[generalInformation.workshopPlugin]
	If !plugin
		plugin = "Fallout4.esm"
	EndIf
	
	WorkshopScript workshop = Game.GetFormFromFile(generalInformation.workshopId,plugin) As WorkshopScript;
	
	If !workshop
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceWorkshopMissing"))
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: workshop " + generalInformation.workshopId + " from " + plugin + " was not found.")
		return
	EndIf
	String settlementName = GetSourceSettlementName(workshop, plugin, generalInformation.workshopId)
	If workshopRef != workshop || tool.GetLinkedRef(KEYWORD_WORKSHOP_OBJECT) != workshop || !IsReferenceWithinSourceWorkshop(player, workshop) || !IsReferenceWithinSourceWorkshop(tool, workshop)
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceWorkshopNames", settlementName))
		return
	EndIf

	ClipboardExtension:PatternReferenceEntry referenceInformation = ClipboardExtension.GetPatternReferenceInformation(slot)
	If !referenceInformation
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: slot " + slot + " returned no reference information.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourcePositionUnreadable"))
		return
	EndIf
	If referenceInformation.cellPlugin < 0 || referenceInformation.cellPlugin >= pluginList.Length
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: invalid cell plugin row " + referenceInformation.cellPlugin + ".")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceCellIndexInvalid"))
		return
	EndIf

	String cellPlugin = pluginList[referenceInformation.cellPlugin]
	If !cellPlugin
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: the source-cell plugin name is empty.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceCellPluginUnreadable"))
		return
	EndIf
	Cell sourceCell = Game.GetFormFromFile(referenceInformation.cellId, cellPlugin) As Cell
	If !sourceCell
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: source cell " + referenceInformation.cellId + " from " + cellPlugin + " is unavailable.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceDifferentCell", settlementName))
		return
	EndIf

	float maxCoordinate = 100000000.0
	float maxAngleRadians = 1000.0
	bool transformValid = referenceInformation.positionX >= -maxCoordinate && referenceInformation.positionX <= maxCoordinate
	transformValid = transformValid && referenceInformation.positionY >= -maxCoordinate && referenceInformation.positionY <= maxCoordinate
	transformValid = transformValid && referenceInformation.positionZ >= -maxCoordinate && referenceInformation.positionZ <= maxCoordinate
	transformValid = transformValid && referenceInformation.angleX >= -maxAngleRadians && referenceInformation.angleX <= maxAngleRadians
	transformValid = transformValid && referenceInformation.angleY >= -maxAngleRadians && referenceInformation.angleY <= maxAngleRadians
	transformValid = transformValid && referenceInformation.angleZ >= -maxAngleRadians && referenceInformation.angleZ <= maxAngleRadians
	If !transformValid
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: slot " + slot + " contains a non-finite or out-of-range transform.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceTransformInvalid"))
		return
	EndIf

	float PI = 3.141592653589793238462643383279502884197;
	float angleX = referenceInformation.angleX/PI*180
	float angleY = referenceInformation.angleY/PI*180
	float angleZ = referenceInformation.angleZ/PI*180
	; Preserve the normal activation landing point, 45 units behind the tool.
	float playerOffsetX = 45.0 * Math.Sin(-angleZ)
	float playerOffsetY = -45.0 * Math.Cos(-angleZ)
	If !ClipboardExtension.IsPositionWithinWorkshop(workshop, sourceCell, referenceInformation.positionX, referenceInformation.positionY, referenceInformation.positionZ) || !ClipboardExtension.IsPositionWithinWorkshop(workshop, sourceCell, referenceInformation.positionX + playerOffsetX, referenceInformation.positionY + playerOffsetY, referenceInformation.positionZ)
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource aborted: source or landing position is outside the source workshop's buildable area or coordinate space.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceDifferentCell", settlementName))
		return
	EndIf
	; Menus and Papyrus calls yield. Recheck actual membership before moving.
	If isDestroyed || workshopRef != workshop || !IsReferenceWithinSourceWorkshop(player, workshop) || !IsReferenceWithinSourceWorkshop(tool, workshop)
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourceWorkshopNames", settlementName))
		return
	EndIf
	If ClipboardExtension.GetSettingValueBool("Debug", "bEnableLogging", false)
		Debug.Trace("[Clipboard] Moving tool to slot " + slot + " source: " + referenceInformation.positionX + ", " + referenceInformation.positionY + ", " + referenceInformation.positionZ + ".")
	EndIf
	tool.HideTool()
	; MoveTo supplies a real cell/worldspace anchor across exterior cell borders.
	tool.MoveTo(workshop, referenceInformation.positionX - workshop.GetPositionX(), referenceInformation.positionY - workshop.GetPositionY(), referenceInformation.positionZ - workshop.GetPositionZ(), false)
	tool.SetAngle(angleX, angleY, angleZ)
	tool.ShowTool()
	If !IsReferenceWithinSourceWorkshop(player, workshop) || !IsReferenceWithinSourceWorkshop(tool, workshop) || Math.Abs(tool.GetPositionX() - referenceInformation.positionX) > 1.0 || Math.Abs(tool.GetPositionY() - referenceInformation.positionY) > 1.0 || Math.Abs(tool.GetPositionZ() - referenceInformation.positionZ) > 1.0
		ClipboardExtension.LogIssue("[Clipboard] MoveToPatternSource: tool destination verification failed or player left the settlement; player was not moved.")
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_SourcePlayerNotMoved", settlementName))
		return
	EndIf
	; Do not TranslateTo across intervening terrain or buildings.
	player.MoveTo(tool, playerOffsetX, playerOffsetY, 0.0, true)
EndFunction

Function ShowPluginList(String msg, String[] pluginList)
	String plugin
	int index = 1
	int pluginIndex
	While index < pluginList.Length
		If pluginList[index-1] > pluginList[index]
			plugin = pluginList[index-1]
			pluginList[index-1] = pluginList[index]
			pluginList[index] = plugin
			
			If index == 1
				index = 2
			Else
				index -= 1
			EndIf
		Else
			index += 1
		EndIf
	EndWhile
	

	plugin = "<b><u>" + msg + "</u></b>\n\n"
	index = 0
	While index < pluginList.Length
		If pluginList[index] != "" && pluginList[index] != " "
			plugin += pluginList[index] + "\n"
		EndIf
		index += 1
	EndWhile
	CLIPBOARD_MENUS.ShowInformMenu(plugin)
EndFunction

Function ListPluginsPattern()
	int slot = CLIPBOARD_MENUS.ShowSlotSelectionMenu()
	If slot <= 0
		return
	EndIf
	String[] pluginList = ClipboardExtension.GetPatternPlugins(slot)
	
	ShowPluginList(ClipboardExtension.GetText("$Clipboard_PatternPluginCount", (slot As String), (pluginList.Length As String)), pluginList)
EndFunction

Function ListPluginsSelected()
	string[] plugins = ClipboardExtension.GetSelectedPlugins(referenceObject);
	ShowPluginList(ClipboardExtension.GetText("$Clipboard_SelectionPluginCountTitle", (plugins.Length As String)), plugins)
EndFunction

Function DisplaySelectionDetails()
	ClipboardExtension:SelectionDetails details = ClipboardExtension.GetSelectionDetails(referenceObject);
	
	String text = ClipboardExtension.GetText("$Clipboard_SelectionDetailsCounts", (details.objectCount As String), (details.wireCount As String), (details.pluginCount As String))
	text += ClipboardExtension.GetText("$Clipboard_SelectionDetailsCenter", (details.centerX As String), (details.centerY As String), (details.centerZ As String))
	text += ClipboardExtension.GetText("$Clipboard_SelectionDetailsArea", (details.areaX As String), (details.areaY As String), (details.areaZ As String))
	text += ClipboardExtension.GetText("$Clipboard_SelectionDetailsScale", (details.minimumScale As String), (details.averageScale As String), (details.maximumScale As String))
	
	CLIPBOARD_MENUS.ShowInformMenu(text)
EndFunction

; Delete all objects currently selected.
;    Warn, no scrap
bool Function DeleteSelected()
	If isDestroyed || destroyInProgress || importInProgress
		return false
	EndIf
	ObjectReference[] selectedObjects = ClipboardExtension.GetSelectedObjectReferences(referenceObject);
	If selectedObjects == None || selectedObjects.Length == 0
		ClipboardExtension.LogIssue("[Clipboard] DeleteSelected aborted: the selected-object snapshot is empty.")
		return false
	EndIf

	If !CLIPBOARD_MENUS.ShowYesNoMenu(ClipboardExtension.GetText("$Clipboard_DestroyWarning"), ClipboardExtension.GetText("$Clipboard_ObjectCount", (selectedObjects.Length As String)), " ", ClipboardExtension.GetText("$Clipboard_DestroyQuestion"))
		return false
	EndIf
	If isDestroyed || destroyInProgress || importInProgress
		return false
	EndIf
	destroyInProgress = true
	; Workshop::ScrapReference is the authoritative source of destruction events.
	; Queuing OnWorkshopObjectDestroyed manually before the native scrap raced
	; power-grid teardown and could notify workshop scripts twice.
	ClearSelection()
	; Preserve the full native array. The job owns pacing and target lifetimes.
	Int[] scrapResult = ClipboardExtension.ScrapObjectsPaced(referenceObject, selectedObjects)
	destroyInProgress = false
	If isDestroyed
		return false
	EndIf
	int submittedCount = 0
	bool completed = false
	If scrapResult != None && scrapResult.Length == 7
		submittedCount = scrapResult[3]
		completed = scrapResult[0] == 1 && scrapResult[2] == scrapResult[1] && scrapResult[5] == 0
	EndIf

	If !completed
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_DestroyIncomplete"), ClipboardExtension.GetText("$Clipboard_DestroyScrapCount", (submittedCount As String)), ClipboardExtension.GetText("$Clipboard_ReviewNativeLog"))
	ElseIf ShowFinishedDialogs()
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_DestroyFinished"), ClipboardExtension.GetText("$Clipboard_DestroyOriginalCount", (selectedObjects.Length As String)), ClipboardExtension.GetText("$Clipboard_DestroyScrapCount", (submittedCount As String)))
	EndIf
	return completed
EndFunction

; Move objects a fixed distance along a single axis.
Function MoveSelected(float xMod, float yMod, float zMod, ObjectReference positionReference)
	ClipboardExtension.MoveSelection(referenceObject,xMod,yMod,zMod);
	Utility.Wait(0.1)
	ClipboardExtension.UpdateSelectedWires(referenceObject);
	Utility.Wait(0.1)
	ClipboardExtension.SendWorkshopEventToSelectedObjects(referenceObject,"OnWorkshopObjectMoved")
	If ShowFinishedDialogs()
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_MoveFinished"))
	EndIf
EndFunction

; Rotate objects around the provided positionReference.
Function RotateSelected(float zAngleMod, ObjectReference positionReference)
	float originX = 0
	float originY = 0
	If positionReference
		originX = positionReference.X
		originY = positionReference.Y
	Else
		float[] center = ClipboardExtension.GetSelectionCenter(referenceObject)
		originX = center[0]
		originY = center[1]
	EndIf
	ClipboardExtension.RotateSelectionZ(referenceObject, zAngleMod, originX, originY);
	Utility.Wait(0.1)
	ClipboardExtension.UpdateSelectedWires(referenceObject);
	Utility.Wait(0.1)
	ClipboardExtension.SendWorkshopEventToSelectedObjects(referenceObject,"OnWorkshopObjectMoved")
	If ShowFinishedDialogs()
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_RotateFinished"))
	EndIf
EndFunction

; Scale objects well maintaining relative position. 
;   Min resulting scale is 0.01
;   Max resulting scale is 10.0
Function ScaleSelected(float scaleMod, bool maintainShape, bool inWorkshopMode)

	If !inWorkshopMode
		EnterWorkshopModeIfNeeded()
	EndIf

	ObjectReference[] selectedObjects = ClipboardExtension.GetSelectedObjectReferences(referenceObject);
	If !ClipboardExtension.TryScaleSelection(referenceObject,scaleMod,maintainShape)
		String errorText = ClipboardExtension.GetSelectionScaleError(referenceObject, scaleMod)
		If !errorText
			errorText = ClipboardExtension.GetText("$Clipboard_ScaleSelectionInvalid")
		EndIf
		CLIPBOARD_MENUS.ShowInformMenu(errorText)
		return
	EndIf
	Utility.Wait(0.1)
	ClipboardExtension.DisableObjects(selectedObjects);
	Utility.Wait(0.1)
	ClipboardExtension.EnableObjects(selectedObjects);
	Utility.Wait(0.1)
	ClipboardExtension.ApplyShaderEffectToSelection(referenceObject, SELECTION_EFFECT_LIST.GetAt(effectIndex));
	Utility.Wait(0.1)
	ClipboardExtension.SendWorkshopEventToSelectedObjects(referenceObject,"OnWorkshopObjectMoved")
	Utility.Wait(0.1)
	ClipboardExtension.UpdateSelectedWires(referenceObject);
	
	If ShowFinishedDialogs()
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_ScaleFinished"))
	EndIf
EndFunction

; Change Motion type of objects to effect their collision and physic behavior.
Function SetMotionTypeSelected(int motionType)
	ObjectReference[] selectedObjects = ClipboardExtension.GetSelectedObjectReferences(referenceObject);
	String progressLabel = ClipboardExtension.GetText("$Clipboard_SetMotionType")
	int index = 0
	While index < selectedObjects.Length
		selectedObjects[index].SetMotionType(motionType)
		index += 1
		CLIPBOARD_MENUS.ShowProgressUpdate(index, index, selectedObjects.Length, progressLabel)
	EndWhile
	If ShowFinishedDialogs()
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_MotionTypeFinished"))
	EndIf
EndFunction

; Marks objects as unowned, so they can be picked up without it being theft.
Function ClearOwnershipSelected()
	ObjectReference[] selectedObjects = ClipboardExtension.GetSelectedObjectReferences(referenceObject);
	String progressLabel = ClipboardExtension.GetText("$Clipboard_ClearOwnership")
	int index = 0
	While index < selectedObjects.Length
		selectedObjects[index].SetActorOwner(None)		
		index += 1
		CLIPBOARD_MENUS.ShowProgressUpdate(index, index, selectedObjects.Length, progressLabel)
	EndWhile
	If ShowFinishedDialogs()
		CLIPBOARD_MENUS.ShowInformMenu(ClipboardExtension.GetText("$Clipboard_OwnershipFinished"))
	EndIf
EndFunction

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardManager() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD
