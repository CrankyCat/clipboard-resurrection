Scriptname ClipboardExtension Native Hidden

Struct CombatEvent
	int objectCount
	int wireCount
	int pluginCount
	float centerX
	float centerY
	float centerZ
	float areaX
	float areaY
	float areaZ
	float minimumScale
	float averageScale
	float maximumScale
EndStruct

Struct SelectionDetails
	int objectCount
	int wireCount
	int pluginCount
	float centerX
	float centerY
	float centerZ
	float areaX
	float areaY
	float areaZ
	float minimumScale
	float averageScale
	float maximumScale
EndStruct

Struct ComponentEntry
	string name
	int formId
	int count
EndStruct

Struct PatternObjectEntry
	int pluginIndex
	int formId
	float scale
	float positionX
	float positionY
	float positionZ
	float angleX
	float angleY
	float angleZ
EndStruct

Struct PatternWireEntry
	int attachmentIndex1
	int attachmentIndex2
EndStruct

Struct PatternGeneralEntry
	string patternName
	string characterName
	int workshopId
	int workshopPlugin
	int pluginCount
	int wireCount
	int objectCount
EndStruct

Struct PatternReferenceEntry
	int cellId
	int cellPlugin
	float positionX
	float positionY
	float positionZ
	float angleX
	float angleY
	float angleZ
EndStruct

; One entry per physical pattern row. Only newObject is import-owned;
; wireObject may be an existing reference or an earlier row's new placement.
Struct ImportPlacementRow
	ObjectReference newObject
	ObjectReference wireObject
EndStruct

ImportPlacementRow[] Function PastePatternObjectsWithReuse(ObjectReference referenceObj, int slot) native global
; Native unpacking preserves large physical arrays without per-row Papyrus calls.
ObjectReference[] Function GetImportPlacementRows(ImportPlacementRow[] placement, Bool forWires) native global
Int Function GetImportPlacementReusedCount(ImportPlacementRow[] placement) native global

ComponentEntry[] Function GetPatternComponentCost(int slot) native global
ComponentEntry[] Function GetSelectionComponentCost(ObjectReference refObj) native global
ComponentEntry[] Function GetComponentCost(ObjectReference[] objs) native global
; Live engine inventory reads. Each batch covers at most 32 sources starting at
; startIndex; callers retain source order and refresh discovery after prompts.
Int Function CountComponentSources(Form componentForm, ObjectReference[] sources, Int startIndex = 0) native global
; Scalar -1 means unavailable, not zero inventory. Never credit an unreadable
; post-withdrawal balance or attempt another charge after such a failure.
Int Function CountComponentSource(Form componentForm, ObjectReference sourceRef) native global
; Optional phase timings reach Clipboard.log only while diagnostic logging is On.
Function ReportComponentCostTiming(String phase, Float elapsedSeconds, Int sourceCount, Int componentCount) native global

string Function GetKeyName(int keyCode) native global
Function ReloadSettings() native global

; Exclusive manual maintenance session. Tokens become invalid across loads.
; Candidate discovery returns [None] on failure; an empty array means no work.
Int Function BeginLegacyCleanup() native global
Bool Function IsLegacyCleanupActive(Int token = 0) native global
Function EndLegacyCleanup(Int token) native global
ObjectReference[] Function GetLegacyCleanupCandidates(Int token) native global
Bool Function IsLegacyCleanupReference(ObjectReference reference, Form expectedBase, Int token) native global

; Resolve game-language text with English fallback; numbered arguments are literal values.
String Function GetText(String key, String arg0 = "", String arg1 = "", String arg2 = "", String arg3 = "", String arg4 = "", String arg5 = "") native global

; Read-only: same exterior worldspace (or same interior cell) and actual workshop build volumes.
Bool Function IsPositionWithinWorkshop(ObjectReference workshop, Cell positionCell, Float x, Float y, Float z) native global

String Function GetSettingValueString(string section, string propertyName, string defaultValue) native global
Int Function GetSettingValueInt(string section, string propertyName, int defaultValue) native global
Float Function GetSettingValueFloat(string section, string propertyName, float defaultValue) native global
Bool Function GetSettingValueBool(string section, string propertyName, bool defaultValue) native global

Function TransmitPowerInSelection(ObjectReference referenceObj) native global
ObjectReference[] Function PastePatternWires(ObjectReference referenceObj,int slot) native global
ObjectReference[] Function PastePatternWiresForRows(ObjectReference referenceObj,int slot,ObjectReference[] placedRows) native global
ObjectReference[] Function PastePatternObjects(ObjectReference referenceObj,int slot) native global
; Serialized native batches own transform snapshots, enabling, and loaded-3D Havok preparation.
; Result: status (1 complete, 0 partial, -1 failed/cancelled), placed, ready,
; Havok eligible, Havok handled, transforms restored, pending, failed, elapsed milliseconds.
Int[] Function PrepareImportedObjects(ObjectReference tool, ObjectReference[] rows, Bool disableHavok) native global
; Returns only loaded WorkshopStartPoweredOn references with positive generated power.
; Successful no-work is a one-element [None] marker; a None array means failure.
ObjectReference[] Function GetImportedPowerGenerators(ObjectReference[] rows) native global
; Per-reference animation capability, independent of generation, sound and form type.
; -1 unavailable, 0 normal model startup, 1 Workshop power sequences, 2 GenericMotor,
; 3 both. Sequence capability does not certify an accepted graph transition.
Int Function GetImportedAnimationKind(ObjectReference obj) native global
; Bounded native classification. Header: status (1 complete, -1 interrupted),
; original row count, unavailable count, elapsed ms; then physical row/kind pairs.
; A four-entry successful header is no work. None is failure. No animation is run.
Int[] Function GetImportedAnimationCandidates(ObjectReference tool, ObjectReference[] rows) native global
; Paced engine scrap. Status (1 submitted, 0 failures, -1 interrupted), distinct
; targets, visited, submitted, already deleted, failed, elapsed ms. Submission
; completion does not certify downstream script drainage. No replay after load.
Int[] Function ScrapObjectsPaced(ObjectReference tool, ObjectReference[] rows) native global
; After wire creation: status, candidates, points, matches, added, existing,
; pending, failed, elapsed milliseconds. Status uses the same 1/0/-1 convention.
; Skipped model points and rejected endpoints are diagnostics, not failed edges.
Int[] Function ReconnectImportedPower(ObjectReference tool, ObjectReference[] rows) native global
; Bounded workshop/object placed and moved calls, waiting for actual returns.
; Status, planned calls, dispatched, returned, failed, outstanding, skipped rows,
; completed rows, elapsed milliseconds, peak outstanding. Status 1 means all returned.
; Timeout reports incomplete; loading an unfinished job reports interrupted without replay.
Int[] Function InitializeImportedWorkshopObjects(ObjectReference tool, ObjectReference[] rows) native global
; Row-aware variants retain the above nine/ten aggregate fields, append a
; safe-to-continue flag, then one outcome per physical row: 0 missing, 1 complete,
; 2 failed, 3 pending. Unknown/cancelled/timed-out workshop work never permits continuation.
Int[] Function PrepareImportedRows(ObjectReference tool, ObjectReference[] rows, Bool disableHavok) native global
Int[] Function InitializeImportedWorkshopRows(ObjectReference tool, ObjectReference[] rows) native global
; Preserve physical holes and valid existing wire endpoints. Failed identities are
; removed even when a later duplicate row reused an earlier newly placed object.
ObjectReference[] Function GetSuccessfulImportRows(ObjectReference[] sources, ObjectReference[] originals, Int[] result, Bool workshop) native global
; Exclude failed new references from automatic conduit endpoints as well as sources.
Int[] Function ReconnectImportedPowerForRows(ObjectReference tool, ObjectReference[] rows, ObjectReference[] originals) native global
; Diagnostic split: connections adds snapped edges without explicit wireless
; refresh; refresh rechecks existing edges and never creates missing ones.
; Both preserve the existing nine-field summary and scoped import protections.
Int[] Function ConnectImportedPowerForRows(ObjectReference tool, ObjectReference[] rows, ObjectReference[] originals) native global
Int[] Function RefreshImportedPowerForRows(ObjectReference tool, ObjectReference[] rows, ObjectReference[] originals) native global
; Cancels native import/classification/scrap jobs for this tool; no rollback.
Function CancelImportedObjects(ObjectReference tool) native global
; UI-only progress across network processing and animation handling. The token
; prevents an interrupted/loaded old stack from advancing a newer UI session.
Int Function BeginImportedPowerProgress(ObjectReference tool, ObjectReference[] rows) native global
Function AdvanceImportedPowerProgress(ObjectReference tool, Int token, Int row) native global
Bool Function EndImportedPowerProgress(ObjectReference tool, Int token) native global
; Remove this tool's transient progress line; never cancel engine/script work.
Function ClearImportedProgress(ObjectReference tool) native global
ObjectReference[] Function UpdateSelectedWires(ObjectReference referenceObj) native global
Function ScrapSelection(ObjectReference referenceObj) native global
Function SendWorkshopEventToSelectedObjects(ObjectReference referenceObj, string eventFunctionName) native global
Function DisableObjects(ObjectReference[] objs) native global
Function EnableObjects(ObjectReference[] objs) native global
Function ScrapObjects(ObjectReference[] objs) native global
Function ScaleSelection(ObjectReference referenceObj,float scale, bool maintainShape) native global
; Exact decimal validation precedes float conversion; mutation rechecks current scales.
String Function GetSelectionScaleInputError(ObjectReference referenceObj, String value, Bool increase) native global
String Function GetSelectionScaleError(ObjectReference referenceObj, Float factor) native global
Bool Function TryScaleSelection(ObjectReference referenceObj, Float scale, Bool maintainShape) native global
SelectionDetails Function GetSelectionDetails(ObjectReference referenceObj) native global
; Geometry-only center; always returns X, Y, Z (zero for an empty selection).
float[] Function GetSelectionCenter(ObjectReference referenceObj) native global
Function RotateSelectionZ(ObjectReference referenceObj, float zRotation, float originX, float originY) native global
Function MoveSelection(ObjectReference referenceObj, float xMoveAmount, float yMoveAmount, float zMoveAmount) native global
int Function ClearSelection(ObjectReference referenceObj, Form effectShader) native global
bool Function Select(ObjectReference referenceObj, ObjectReference obj, Form effectShader) native global
int Function SelectAll(ObjectReference referenceObj, ObjectReference[] objs, Form effectShader) native global
bool Function Deselect(ObjectReference referenceObj, ObjectReference obj, Form effectShader) native global
int Function DeselectAll(ObjectReference referenceObj, ObjectReference[] objs, Form effectShader) native global
string[] Function GetSelectedPlugins(ObjectReference referenceObj) native global
string[] Function GetPlugins(ObjectReference referenceObj, ObjectReference[] objs) native global
ObjectReference[] Function GetSelectedObjectReferences(ObjectReference referenceObj) native global
int Function GetSelectionCount(ObjectReference referenceObj) native global
int Function GetSelectionWireCount(ObjectReference referenceObj) native global
ObjectReference Function GetSelectedObjectReference(int index, ObjectReference referenceObj) native global
bool Function IsSelectionFullyLoaded(ObjectReference referenceObj) native global
int Function GetFullyLoadedCount(ObjectReference[] objs) native global
int Function GetPatternCount(int firstSlot, int lastSlot) native global
PatternObjectEntry[] Function GetPatternObjects(int slot) native global
PatternWireEntry[] Function GetPatternWires(int slot) native global
int Function GetPatternImportWireCount(ObjectReference tool, int slot) native global
string[] Function GetPatternPlugins(int slot) native global
PatternGeneralEntry Function GetPatternGeneralInformation(int slot) native global
PatternReferenceEntry Function GetPatternReferenceInformation(int slot) native global
PatternGeneralEntry Function WritePatternFile(int slot, string patternName, string characterName, ObjectReference referenceObj) native global
Function ApplyShaderEffectToSelection(ObjectReference referenceObj, Form effectShader) native global
Function RemoveShaderEffectToSelection(ObjectReference referenceObj, Form effectShader) native global

ObjectReference[] Function GetSelectableObjectPool(ObjectReference[] workshopObjs,ObjectReference refObj ) native global
ObjectReference[] Function GetManualSelectableObjectPool(ObjectReference[] workshopObjs,ObjectReference refObj ) native global

ConstructibleObject Function GetConstructibleObjectByCreatedObject(Form baseObj) native global

ObjectReference[] Function CreateSelectionBox(ObjectReference toolObj, Form wallForm, int xLength, int yLength, int zLength) native global

ObjectReference[] Function GetObjectsInBox(ObjectReference[] objs, ObjectReference toolObj, int xLength, int yLength, int zLength) native global
ObjectReference[] Function GetObjectsInSphere(ObjectReference[] objs, ObjectReference toolObj, int radius) native global
ObjectReference[] Function GetObjectsInCylinder(ObjectReference[] objs, ObjectReference toolObj, int radius) native global


ObjectReference[] Function GetObjectsByCell(Cell cell) native global
ObjectReference[] Function GetObjectsByWorkshopCells(ObjectReference[] workshopObjs, ObjectReference workshop) native global
ObjectReference[] Function FilterObjectsByPlugin(ObjectReference[] objs, string pluginName) native global
ObjectReference[] Function FilterObjectsByDistance(ObjectReference[] objs, ObjectReference origin, float maximum) native global



; Return an array of all ConstructibleObjects loaded in game.
ConstructibleObject[] Function GetAllConstructibleObjects() native global

; Reports code constants from every maintained PEX, independent of saved properties.
; Native deduplication limits records to once per script/build in this process.
bool Function ReportScriptBuild(string componentName, int build) Global Native

Function ReportBuildIdentity() Global
	int build0 = ClipboardButtonStand.InternalBuild_ClipboardButtonStand()
	if ReportScriptBuild("ClipboardButtonStand", build0)
		Debug.Trace("Clipboard build identity: script=ClipboardButtonStand internal=" + build0)
	endif
	int build1 = ClipboardCopyPylonScript.InternalBuild_ClipboardCopyPylonScript()
	if ReportScriptBuild("ClipboardCopyPylonScript", build1)
		Debug.Trace("Clipboard build identity: script=ClipboardCopyPylonScript internal=" + build1)
	endif
	int build2 = ClipboardExtension.InternalBuild_ClipboardExtension()
	if ReportScriptBuild("ClipboardExtension", build2)
		Debug.Trace("Clipboard build identity: script=ClipboardExtension internal=" + build2)
	endif
	int build3 = ClipboardManager.InternalBuild_ClipboardManager()
	if ReportScriptBuild("ClipboardManager", build3)
		Debug.Trace("Clipboard build identity: script=ClipboardManager internal=" + build3)
	endif
	int build4 = ClipboardQuest.InternalBuild_ClipboardQuest()
	if ReportScriptBuild("ClipboardQuest", build4)
		Debug.Trace("Clipboard build identity: script=ClipboardQuest internal=" + build4)
	endif
	int build5 = ClipboardSelectionMethod.InternalBuild_ClipboardSelectionMethod()
	if ReportScriptBuild("ClipboardSelectionMethod", build5)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionMethod internal=" + build5)
	endif
	int build6 = ClipboardSelectionMethodAll.InternalBuild_ClipboardSelectionMethodAll()
	if ReportScriptBuild("ClipboardSelectionMethodAll", build6)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionMethodAll internal=" + build6)
	endif
	int build7 = ClipboardSelectionMethodBox.InternalBuild_ClipboardSelectionMethodBox()
	if ReportScriptBuild("ClipboardSelectionMethodBox", build7)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionMethodBox internal=" + build7)
	endif
	int build8 = ClipboardSelectionMethodByCell.InternalBuild_ClipboardSelectionMethodByCell()
	if ReportScriptBuild("ClipboardSelectionMethodByCell", build8)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionMethodByCell internal=" + build8)
	endif
	int build9 = ClipboardSelectionMethodByPlugin.InternalBuild_ClipboardSelectionMethodByPlugin()
	if ReportScriptBuild("ClipboardSelectionMethodByPlugin", build9)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionMethodByPlugin internal=" + build9)
	endif
	int build10 = ClipboardSelectionMethodCylinder.InternalBuild_ClipboardSelectionMethodCylinder()
	if ReportScriptBuild("ClipboardSelectionMethodCylinder", build10)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionMethodCylinder internal=" + build10)
	endif
	int build11 = ClipboardSelectionMethodGun.InternalBuild_ClipboardSelectionMethodGun()
	if ReportScriptBuild("ClipboardSelectionMethodGun", build11)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionMethodGun internal=" + build11)
	endif
	int build12 = ClipboardSelectionMethodSphere.InternalBuild_ClipboardSelectionMethodSphere()
	if ReportScriptBuild("ClipboardSelectionMethodSphere", build12)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionMethodSphere internal=" + build12)
	endif
	int build13 = ClipboardSelectionWeapon.InternalBuild_ClipboardSelectionWeapon()
	if ReportScriptBuild("ClipboardSelectionWeapon", build13)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionWeapon internal=" + build13)
	endif
	int build14 = ClipboardSelectionWeaponHitEffect.InternalBuild_ClipboardSelectionWeaponHitEffect()
	if ReportScriptBuild("ClipboardSelectionWeaponHitEffect", build14)
		Debug.Trace("Clipboard build identity: script=ClipboardSelectionWeaponHitEffect internal=" + build14)
	endif
	int build15 = ClipboardToolScript.InternalBuild_ClipboardToolScript()
	if ReportScriptBuild("ClipboardToolScript", build15)
		Debug.Trace("Clipboard build identity: script=ClipboardToolScript internal=" + build15)
	endif
EndFunction

; Clipboard-owned modal input. Tokens are session-scoped opaque strings.
; State: 0 unknown/interrupted, 1 queued, 2 ready, 3 accepted, 4 cancelled,
; 5 failed, 6 interrupted. Results remain available until acknowledged.
bool Function IsOwnedInputAvailable() Global Native
string Function BeginOwnedInput(ObjectReference owner, string header, string defaultValue, int inputType, int maxChars, int minimum = 0, int maximum = 0) Global Native
int Function GetOwnedInputState(string token) Global Native
string Function GetOwnedInputResult(string token) Global Native
bool Function IsOwnedInputFinished(string token) Global Native
bool Function AcknowledgeOwnedInput(string token) Global Native
Function CancelOwnedInput(ObjectReference owner) Global Native
Function AbandonOwnedInput(string token) Global Native

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardExtension() Global
    return 128
EndFunction
; END GENERATED INTERNAL BUILD

; Always-on warnings/errors, independent of the optional diagnostic setting.
Function ReportScriptIssue(string issueText, bool isError = false) Global Native

Function LogIssue(string issueText, bool isError = false) Global
    ReportScriptIssue(issueText, isError)
    If isError
        Debug.Trace(issueText, 2)
    Else
        Debug.Trace(issueText, 1)
    EndIf
EndFunction
