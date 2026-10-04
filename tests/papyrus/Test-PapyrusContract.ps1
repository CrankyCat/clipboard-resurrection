<#
.SYNOPSIS
Verifies the maintained current native registration set against ClipboardExtension.psc.

.DESCRIPTION
Compares all native declarations in the maintained Papyrus interface with the
ordinary CommonLib bindings and latent bridge bindings. Every registration
must have an exact Papyrus declaration; internal C++ helpers are not public VM
bindings. No files are modified.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [string] $ProjectRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Join-Path $PSScriptRoot '../..'
}

$project = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$papyrusPath = Join-Path $project 'src\papyrus\ClipboardExtension.psc'
$pluginPath = Join-Path $project 'src\native\Clipboard.cpp'
$latentPath = Join-Path $project 'src\native\LatentBridge.cpp'
$serializationNamesPath = Join-Path $project 'src\native\SerializationNames.h'
$importFunctorsPath = Join-Path $project 'src\native\ImportFunctors.inl'
$importStatePath = Join-Path $project 'src\native\ImportJobState.h'
$arrayCompatPath = Join-Path $project 'src\native\LegacyPapyrusArray.h'
$managerPath = Join-Path $project 'src\papyrus\ClipboardManager.psc'
$cellMethodPath = Join-Path $project 'src\papyrus\ClipboardSelectionMethodByCell.psc'
$enginePath = Join-Path $project 'src\native\EngineAPI.cpp'
$runtimeCompatibilityPath = Join-Path $project 'src\native\RuntimeCompatibility.h'
$mcmConfigPath = Join-Path $project 'assets\MCM\Config\Clipboard\config.json'
$papyrusLocalizationPath = Join-Path $project 'localization\source\papyrus.en.json'
$mcmLocalizationPath = Join-Path $project 'localization\source\mcm.en.json'
$mcmDefaultsPath = Join-Path $project 'assets\MCM\Config\Clipboard\settings.ini'

foreach ($path in @($papyrusPath, $pluginPath, $latentPath, $importFunctorsPath, $importStatePath, $arrayCompatPath, $managerPath, $cellMethodPath, $enginePath, $runtimeCompatibilityPath, $mcmConfigPath, $mcmDefaultsPath, $papyrusLocalizationPath, $mcmLocalizationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required contract source was not found: $path"
    }
}

$papyrus = Get-Content -LiteralPath $papyrusPath -Raw
$plugin = Get-Content -LiteralPath $pluginPath -Raw
$latent = Get-Content -LiteralPath $latentPath -Raw
$serializationNames = Get-Content -LiteralPath $serializationNamesPath -Raw
$importFunctors = Get-Content -LiteralPath $importFunctorsPath -Raw
$importState = Get-Content -LiteralPath $importStatePath -Raw
$arrayCompat = Get-Content -LiteralPath $arrayCompatPath -Raw
$manager = Get-Content -LiteralPath $managerPath -Raw
$cellMethod = Get-Content -LiteralPath $cellMethodPath -Raw
$engine = Get-Content -LiteralPath $enginePath -Raw
$runtimeCompatibility = Get-Content -LiteralPath $runtimeCompatibilityPath -Raw
$mcmConfigText = Get-Content -LiteralPath $mcmConfigPath -Raw
$mcmDefaults = Get-Content -LiteralPath $mcmDefaultsPath -Raw
$mcmConfig = $mcmConfigText | ConvertFrom-Json

# Source catalogs retain the English behavioral labels while the modern package
# uses namespaced keys. Resolve those keys instead of weakening structural checks.
$contractEnglishText = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
foreach ($catalogPath in @($papyrusLocalizationPath, $mcmLocalizationPath)) {
    foreach ($entry in @(Get-Content -LiteralPath $catalogPath -Raw -Encoding UTF8 | ConvertFrom-Json)) {
        if ([string] $entry.key -cnotmatch '^\$Clipboard_[A-Za-z0-9_]+$' -or
            [string]::IsNullOrWhiteSpace([string] $entry.text) -or
            $contractEnglishText.ContainsKey([string] $entry.key)) {
            throw "Invalid or duplicate English localization entry in $catalogPath`: $($entry.key)"
        }
        $contractEnglishText.Add([string] $entry.key, [string] $entry.text)
    }
}
function Get-ContractEnglishText([string] $Text) {
    if ($Text.StartsWith('$')) {
        if (-not $contractEnglishText.ContainsKey($Text)) {
            throw "Contract presentation key has no English source: $Text"
        }
        return $contractEnglishText[$Text]
    }
    return $Text
}


# MCM grouping changes must preserve saved keys and action routes.
$importSectionIndex = -1
$importControlIndex = -1
$havokControlIndex = -1
for ($controlIndex = 0; $controlIndex -lt $mcmConfig.content.Count; ++$controlIndex) {
    $control = $mcmConfig.content[$controlIndex]
    if ($control.type -eq 'section' -and (Get-ContractEnglishText $control.text) -ceq 'Import Options') { $importSectionIndex = $controlIndex }
    if ($control.PSObject.Properties['id']) {
        if ($control.id -eq 'bAllowAllObjectsToBeImported:Selection') { $importControlIndex = $controlIndex }
        if ($control.id -eq 'bDisableHavokOnImportedObjects:Balance') { $havokControlIndex = $controlIndex }
    }
}
if ($importSectionIndex -lt 0 -or $importControlIndex -ne ($importSectionIndex + 1) -or
    $havokControlIndex -ne ($importControlIndex + 1)) {
    throw 'Import Options must contain the import toggle followed immediately by Havok, retaining saved keys.'
}

$throttleControls = @($mcmConfig.content | Where-Object { $_.PSObject.Properties['id'] -and $_.id -eq 'bUseThrottling:Import' })
if ($throttleControls.Count -ne 1 -or
    $mcmConfig.content[$havokControlIndex + 1].id -ne 'bUseThrottling:Import' -or
    (Get-ContractEnglishText $throttleControls[0].text) -cne 'Use Throttling' -or
    $throttleControls[0].type -ne 'switcher' -or
    $throttleControls[0].valueOptions.sourceType -ne 'ModSettingBool' -or
    $throttleControls[0].action.type -ne 'CallGlobalFunction' -or
    $throttleControls[0].action.script -ne 'MCM' -or
    $throttleControls[0].action.function -ne 'SetModSettingBool' -or
    ($throttleControls[0].action.params -join '|') -cne 'Clipboard|bUseThrottling:Import|{value}' -or
    $mcmDefaults -notmatch '(?ms)^\[Import\]\s*(?:(?!^\[).)*?^bUseThrottling=0\s*$') {
    throw 'Use Throttling must be default-off under Import Options with its own Import setting and unchanged MCM action.'
}
foreach ($relative in @('assets/MCM/Settings/Clipboard.ini')) {
    $defaults = Get-Content -LiteralPath (Join-Path $project $relative) -Raw
    if ($defaults -notmatch '(?ms)^\[Import\]\s*(?:(?!^\[).)*?^bUseThrottling=0\s*$') {
        throw "Use Throttling must default to Off in $relative."
    }
}
$throttleHelp = Get-ContractEnglishText $throttleControls[0].help
if ($throttleHelp -notmatch '^Default off\.' -or $throttleHelp -notmatch 'scripting overloads' -or $throttleHelp -notmatch 'Papyrus stack dumps' -or
    $throttleHelp -notmatch 'Sim Settlements 2' -or $throttleHelp -notmatch 'next import') {
    throw 'Use Throttling must explain the Off scripting risk, SS2 interaction and next-import scope.'
}
if ($plugin -notmatch 'GetSettingValue\("Import", "bUseThrottling", "0"\)' -or
    $manager -match 'bUseThrottling') {
    throw 'Throttling must be sampled natively without adding a Papyrus branch around import completion.'
}
if ($plugin -match 'RuntimeVersion\(\)\s*(?:==|!=|<=|>=|<|>)' -or
    $plugin -match 'compatibleVersions\s*\[[^\]]+\]\s*=' -or
    $plugin -notmatch 'data\.structureIndependence\s*=\s*Clipboard::RuntimeCompatibility::kStructureFlags;' -or
    $runtimeCompatibility -notmatch '(?s)kStructureFlags\s*=\s*F4SE::PluginVersionData::kStructureIndependence_1_10_980Layout\s*\|\s*F4SE::PluginVersionData::kStructureIndependence_1_11_137Layout;' -or
    $plugin -notmatch 'data\.addressIndependence\s*=\s*F4SE::PluginVersionData::kAddressIndependence_Signatures;' -or
    $plugin -notmatch 'return Clipboard::RuntimeCompatibility::SupportsDeclaredLayout\(runtime\);' -or
    $runtimeCompatibility -notmatch '(?s)SupportsDeclaredLayout\(.*?REL::runtime_family\(runtime\).*?case REL::RuntimeFamily::kOG:.*?case REL::RuntimeFamily::kNG:.*?case REL::RuntimeFamily::kAE:.*?return true;.*?default:\s*return false;' -or
    $plugin -notmatch 'if \(!SupportsDeclaredLayout\(f4se->RuntimeVersion\(\)\)\)' -or
    $plugin -notmatch '(?s)ValidateAndHoldRuntimeCandidates\(.*?F4SE::Init\(f4se\).*?RuntimeSymbols::ResolveRequiredCommonLibSymbols\(\).*?EngineAPI::ResolveRequiredSymbols\(\)') {
    throw 'OG/NG/AE eligibility must use runtime-selected family/layout handling without an exact patch whitelist and retain database plus CommonLib dependency preflight/resolution.'
}

# Retired keys must have no runtime or UI/default effect, even if old user
# INIs still contain 0 or 1. Discovery must not depend on permissive policy.
$retiredDiscoveryKeys = 'bEnableToolCellObjectSelection|bEnableWorkshopCellObjectSelection'
if ($plugin -match $retiredDiscoveryKeys -or $mcmConfigText -match $retiredDiscoveryKeys -or
    $mcmDefaults -match $retiredDiscoveryKeys -or
    $plugin -match 'enableToolCellObjectSelection|enableWorkshopCellObjectSelection') {
    throw 'Retired cell-expansion keys still affect runtime discovery or packaged controls/defaults.'
}
$poolMatch = [regex]::Match($plugin, '(?s)static VMArray<TESObjectREFR\*> BuildSelectableObjectPool\(.*?(?=//Gets an array of all selectable objects)')
if (-not $poolMatch.Success -or
    $poolMatch.Value -notmatch 'if \(refObj->parentCell\)\s*\{\s*AppendObjectByCell\(&result, refObj->parentCell, automaticTransferPolicy, selectedRows\);' -or
    $poolMatch.Value -notmatch '(?s)TESObjectREFR\* workshop = .*?;\s*AppendWorkshopCellObjects\(\s*&result,\s*workshopObjects,\s*workshop,\s*automaticTransferPolicy,\s*selectedRows\);' -or
    $poolMatch.Value -match 'if\s*\(\s*!?\s*(automaticTransferPolicy|allowNormallyFiltered)' -or
    $plugin -notmatch 'return BuildSelectableObjectPool\(workshopObjects, refObj, true\);' -or
    $plugin -notmatch 'return BuildSelectableObjectPool\(workshopObjects, refObj, false\);') {
    throw 'Automatic and manual discovery no longer share unconditional cell sources with separate eligibility.'
}
if ($plugin -match 'WhirligigRadarStation\.esl' -or $mcmDefaults -match 'WhirligigRadarStation\.esl') {
    throw 'WhirligigRadarStation.esl was reintroduced into the native or packaged blacklist defaults.'
}
foreach ($policyKey in @('bAllowNormallyFilteredObjectsToBeSelectedAndExported', 'bAllowAllObjectsToBeImported')) {
    $policyControls = @($mcmConfig.content | Where-Object { $_.PSObject.Properties['id'] -and $_.id -eq ($policyKey + ':Selection') })
    if ($policyControls.Count -ne 1 -or $policyControls[0].type -ne 'switcher' -or
        $policyControls[0].valueOptions.sourceType -ne 'ModSettingBool' -or
        $policyControls[0].action.function -ne 'SetModSettingBool' -or
        @($policyControls[0].action.params)[1] -ne ($policyKey + ':Selection') -or
        $mcmDefaults -notmatch ('(?m)^' + [regex]::Escape($policyKey) + '=0\s*$')) {
        throw "The independent default-off Selection control is missing or misrouted: $policyKey"
    }
}

if ($papyrus -notmatch '(?im)^\s*ScriptName\s+ClipboardExtension\s+Native\s+Hidden\s*$') {
    throw 'ClipboardExtension.psc no longer declares the expected native hidden class.'
}
if ($plugin -notmatch 'constexpr\s+char\s+kPapyrusClassName\[\]\s*=\s*\{\s*"ClipboardExtension"\s*\}') {
    throw 'The current plugin no longer publishes ClipboardExtension as its Papyrus class name.'
}
if ($plugin -match '\.Push\s*\(\s*nullptr\s*\)') {
    throw 'A null pattern row is passed to VMArray::Push, which would drop the row and shift wire indices.'
}
if ($arrayCompat -notmatch 'UnpackVariable<::VMArray<RE::TESObjectREFR\*>>' -or
    $arrayCompat -notmatch 'if\s*\(\s*!array\s*\)\s*\{\s*return\s*\{\s*\}\s*;') {
    throw 'The native implementation no longer protects typed-null Papyrus ObjectReference arrays before CommonLib unpacking.'
}
if ($manager -notmatch 'PastePatternWiresForRows\s*\(\s*referenceObject\s*,\s*slot\s*,\s*wireRows\s*\)') {
    throw 'ClipboardManager no longer passes the row-aligned placement result to wire reconstruction.'
}
if ($engine -notmatch '(?s)TypeInfo\s+arrayType\s*\{[^}]*RawType::kVar[^}]*\}\s*;\s*arrayType\.SetArray\(true\)\s*;\s*if\s*\(\s*!a_vm->CreateArray\(arrayType') {
    throw 'CallFunctionNoWait no longer constructs its Papyrus argument payload as a Var[] array.'
}
if ($engine -notmatch '(?s)parameters->elements\[0\]\s*=\s*new\s+RE::BSScript::Variable\s*\(\s*std::move\(argumentValue\)\s*\)') {
    throw 'CallFunctionNoWait no longer stores a heap-owned Variable wrapper in its Var[] payload.'
}
if ($engine -notmatch '(?s)GetScriptObjectType\(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>\(\), referenceType\).*PapyrusArgument::RestoreDeclaredObjectType\(argumentValue, referenceType.get\(\)\).*parameters->elements\[0\]') {
    throw 'CallFunctionNoWait must preserve the declared ObjectReference argument type before Var[] wrapping.'
}
$callbackDispatch = [regex]::Match($engine, '(?s)bool DispatchReferenceCallback\(.*?(?=RE::TESObjectREFR\* GetObjectAtConnectPoint\()').Value
if ($callbackDispatch -notmatch '(?s)GetScriptObjectType\(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>\(\), referenceType\).*PapyrusArgument::RestoreDeclaredObjectType\(argumentValue, referenceType.get\(\)\)' -or
    $callbackDispatch -notmatch 'PapyrusDispatchABI::BorrowedFunction<Signature>' -or
    $callbackDispatch -notmatch 'DispatchMethodCall\(targetObject, a_function, arguments, a_callback\)' -or
    $latent -notmatch '(?s)BindLatent<ImportSummary, RE::TESObjectREFR\*, ReferenceArray>\(\s*a_vm, "InitializeImportedWorkshopObjects", EnqueueInitializeImportedWorkshopObjects, true\)' -or
    $latent -notmatch 'PreflightFactoryName<InitializeImportedWorkshopObjectsFunctor,\s*kInitializeImportedWorkshopObjectsLegacyName>' -or
    $latent -notmatch 'RegisterFactoryChecked<InitializeImportedWorkshopObjectsFunctor,\s*kInitializeImportedWorkshopObjectsLegacyName>') {
    throw 'Workshop initialization must retain typed latent results, its registered factory, declared ObjectReference arguments, and the callback-aware family dispatch adapter.'
}
if ($plugin -notmatch '(?s)!permissive\s*&&\s*formType\s*==\s*ENUM_FORM_ID::kLIGH\s*&&\s*!HasWorkshopConstructibleRecipe\(baseForm\)' -or
    $plugin -notmatch 'WorkshopWorkbenchType') {
    throw 'The current strict selection/import policy no longer distinguishes workshop-recipe-backed LIGH rows from ambient lights.'
}
if ($plugin -notmatch '(?s)!permissive\s*&&\s*formType\s*==\s*ENUM_FORM_ID::kLIGH\s*&&\s*!HasNonZeroObjectBounds\(baseForm\)') {
    throw 'The current default transfer policy no longer rejects zero-bounds LIGH base forms.'
}
if ($plugin -notmatch '(?s)!allowNormallyFilteredObjectsToBeSelectedAndExported\s*\)\s*\{.*?!HasWorkshopPlacementEvidence\(reference\).*?return\s+rejectedReference\s*;\s*\}\s*.*?return\s+false\s*;') {
    throw 'The current automatic selection/export policy no longer keeps strict workshop-placement evidence and a fully permissive authored-reference path.'
}
if ($plugin -match '(?s)baseForm->GetFormType\(\)\s*!=\s*ENUM_FORM_ID::kLIGH\s*&&\s*reference\s*&&\s*reference->Get3D\(\)\s*==\s*nullptr') {
    throw 'The current permissive automatic-selection path unexpectedly depends on loaded 3D.'
}
if ($plugin -notmatch 'bAllowNormallyFilteredObjectsToBeSelectedAndExported' -or
	$plugin -notmatch 'bAllowAllObjectsToBeImported' -or
	$plugin -notmatch '(?s)IsPluginBlackListed\(pluginName\)\s*\|\|\s*IsFormBlackListed\(baseForm\)' -or
	$plugin -notmatch 'formType\s*==\s*ENUM_FORM_ID::kTXST' -or
	$plugin -notmatch 'IsMarkerBaseForm') {
	throw 'The current permissive policy settings or their permanent deny-list precedence are incomplete.'
}
if ($plugin -match '(?s)formType\s*==\s*ENUM_FORM_ID::kSCOL.*?Transfer policy permanently rejected' -or
	$plugin -notmatch 'GetObjectsByWorkshopCells' -or
	$plugin -notmatch 'GetAssignedCellLocation' -or
	$plugin -notmatch 'EngineAPI::GetLocationReferenceID\(workshop\)' -or
	$plugin -notmatch 'assignedForm->Is\(ENUM_FORM_ID::kLCTN\)' -or
	$plugin -notmatch 'TESForm::GetAllForms\(\)' -or
	$plugin -notmatch 'BSAutoReadLock' -or
	$plugin -notmatch 'appendCandidate\(child, true\)' -or
	$plugin -notmatch 'WorkshopScope::ResolveMembership') {
	throw 'SCOL is not following ordinary bound-object policy, or workshop-cell enumeration is incomplete.'
}
if ($plugin -match '(?s)BGSConstructibleObject\*\s+conObj\s*=\s*baseForm\s*&&\s*baseForm->GetFormType\(\)\s*==\s*ENUM_FORM_ID::kLIGH' -or
    $plugin -notmatch '(?s)Cost and charging follow the exact import-admission decision above.*GetConstructibleObjectByCreatedObject_\(base,\s*baseForm\)') {
    throw 'Pattern component cost no longer follows import admission and any available recipe.'
}
if ($plugin -notmatch 'formType\s*==\s*ENUM_FORM_ID::kBNDS' -or
    $plugin -notmatch 'baseForm->IsDeleted\(\)' -or
    $plugin -notmatch 'baseForm->As<TESBoundObject>\(\)' -or
    $plugin -notmatch 'baseForm->GetFormType\(\)\s*==\s*ENUM_FORM_ID::kLVLN' -or
    $plugin -notmatch 'IsSupportedNPCBaseForm') {
    throw 'The current import path no longer enforces all structural form-safety guards.'
}
if ($papyrus -notmatch 'Function\s+GetManualSelectableObjectPool\(' -or
	$plugin -notmatch 'BindLegacyStatic<GetManualSelectableObjectPool>' -or
	$manager -notmatch 'ClipboardExtension\.GetManualSelectableObjectPool\(objs,\s*referenceObject\)') {
	throw 'The current manual gun candidate pool is not complete across Papyrus and native bindings.'
}
if ($papyrus -notmatch 'Function\s+GetObjectsByWorkshopCells\(' -or
	$plugin -notmatch 'BindLegacyStatic<GetObjectsByWorkshopCells>' -or
	$manager -notmatch 'ClipboardExtension\.GetObjectsByWorkshopCells\(objs,\s*workshopRef\)' -or
	$manager -notmatch 'Function\s+SelectAllInWorkshopCells\(') {
	throw 'The workshop-location cell selection route is incomplete across Papyrus and native bindings.'
}
if ($cellMethod -notmatch 'new\s+string\[2\]' -or
    $cellMethod -notmatch 'cellLabels\[0\]\s*=\s*ClipboardExtension\.GetText\("\$Clipboard_CellScopeWorkshop"\)' -or
    $cellMethod -notmatch 'cellLabels\[1\]\s*=\s*ClipboardExtension\.GetText\("\$Clipboard_CellScopeTool"\)' -or
    (Get-ContractEnglishText '$Clipboard_CellScopeWorkshop') -cne 'Workshop''s Cells' -or
    (Get-ContractEnglishText '$Clipboard_CellScopeTool') -cne 'Clipboard Tool''s Cell' -or
    $cellMethod -notmatch '(?s)If index == 0\s+found = GetClipboardManager\(\)\.SelectAllInWorkshopCells\(\)\s+ElseIf index == 1\s+found = GetClipboardManager\(\)\.SelectAllInCell\(GetParentTool\(\)\.GetParentCell\(\)\)' -or
    $cellMethod -match 'Player''s Cell') {
    throw 'The localized By Cell menu must retain Workshop''s Cells and Clipboard Tool''s Cell in order with their exact two selection routes.'
}
$pasteMatch = [regex]::Match($manager, '(?is)\bint\s+Function\s+PastePattern\([^\r\n]*\).*?\bEndFunction\b')
if (-not $pasteMatch.Success) { throw 'The public PastePattern manager entry point is missing.' }
$paste = $pasteMatch.Value
# Reused wire endpoints must never flow into import ownership or physics/animation work.
if ($paste -notmatch 'PastePatternObjectsWithReuse\(referenceObject, slot\)' -or
    $paste -notmatch 'newObjs = ClipboardExtension\.GetImportPlacementRows\(placement, false\)' -or $paste -notmatch 'wireRows = ClipboardExtension\.GetImportPlacementRows\(placement, true\)' -or
    $paste -notmatch 'If placedObjectCount <= 0 && reusedRowCount <= 0' -or
    $paste -notmatch 'placedObjectCount \+ reusedRowCount != generalEntry\.objectCount' -or
    $paste -match '(?:PrepareImportedRows|InitializeImportedWorkshopRows|ReconnectImportedPowerForRows|ConnectImportedPowerForRows|RefreshImportedPowerForRows|InitializePlacedAnimations|EnableObjects)\([^\r\n]*wireRows' -or
    $plugin -notmatch 'ResolvePapyrusStructure<ImportPlacementRow>' -or
    $plugin -notmatch 'return PlacePatternRows\(stackId, base, refObj, slot, false\)\.newRows;') {
    throw 'Duplicate wire endpoints must be separate from owned rows, including all-reused imports, while the legacy API remains unchanged.'
}

if ($paste -notmatch '(?s)PrepareImportedRows\(referenceObject,\s*newObjs,\s*disableImportedHavok\).*GetSuccessfulImportRows\(newObjs, newObjs, preparation, false\).*GetSuccessfulImportRows\(wireRows, newObjs, preparation, false\).*InitializeImportedWorkshopRows\(referenceObject,\s*preparedObjs\).*GetSuccessfulImportRows\(preparedObjs, preparedObjs, workshopResult, true\).*GetSuccessfulImportRows\(wireRows, preparedObjs, workshopResult, true\).*PastePatternWiresForRows\(referenceObject,slot,wireRows\).*ReconnectImportedPowerForRows\(referenceObject,\s*readyObjs,\s*newObjs\).*InitializePlacedAnimations\(readyObjs,\s*importPowerProgressToken\)' -or
    $paste -match 'SendWorkshopEventToSelectedObjects\s*\(') {
    throw 'Import must filter physical rows after preparation and actual-return workshop initialization, then wire, reconnect scoped power, and refresh only successful new objects.'
}
$workshopFailure = [regex]::Match($paste, '(?ims)^([ \t]+)If workshopResult\[10\] != 1[^\r\n]*\r?\n.*?^\1EndIf').Value
$rowPolicy = Get-Content -LiteralPath (Join-Path $project 'src\native\ImportRowResults.h') -Raw
if ($paste -notmatch 'workshopResult.Length != preparedObjs.Length \+ 11' -or
    $workshopFailure -notmatch 'workshopResult\[5\] != 0' -or
    $workshopFailure -notmatch 'CancelImportedObjects\(referenceObject\)' -or
    $workshopFailure -notmatch 'importInProgress = workshopResult\[5\] != 0' -or
    $workshopFailure -notmatch 'return placedObjectCount' -or
    $rowPolicy -notmatch '(?s)state.done && !state.cancelled && !state.interrupted && !state.timedOut &&\s*state.outstanding == 0 && state.Valid\(\)' -or
    $latent -notmatch 'ImportRows::Valid\(result, originals.size\(\), workshop\)') {
    throw 'Partial imports require validated row outcomes, terminal workshop completion, and a full stop for cancellation, timeout, interruption, outstanding or unknown calls.'
}
if ($paste -match 'WaitOnSelectionLoad\s*\(|GetPosition[XYZ]\s*\(|GetAngle[XYZ]\s*\(|TransmitPowerInSelection\s*\(' -or
    $paste -match '(?i)new\s+(?:Float|Bool)\s*\[\s*newObjs\.Length\s*\]|While\s+\w+\s*<\s*newObjs\.Length' -or
    $paste -notmatch 'placedObjectCount\s*=\s*preparation\[1\]' -or
    $paste -notmatch 'preparation.Length != newObjs.Length \+ 10' -or
    $paste -notmatch 'preparation\[9\] != 1' -or
    $paste -notmatch 'powerResult.Length != 9' -or $paste -match '\bIf loaded\b') {
    throw 'The row-aware import must use bounded native outcomes, validate preparation completion, preserve counts and avoid a global readiness gate on wiring.'
}
if ($plugin -notmatch '\(formType == ENUM_FORM_ID::kACTI \|\| formType == ENUM_FORM_ID::kMSTT\) && !HasNonZeroObjectBounds\(baseForm\)') {
    throw 'Zero-bounds ACTI and MSTT must remain unconditional transfer exclusions, independently of permissive LIGH policy.'
}
$managerDestroy = [regex]::Match($manager, '(?is)\bFunction\s+Destroy\(\).*?\bEndFunction\b').Value
$managerBeginDestroy = [regex]::Match($manager, '(?is)\bFunction\s+BeginDestroy\(\).*?\bEndFunction\b').Value
if ($paste -notmatch 'GetSettingValueBool\("Balance",\s*"bDisableHavokOnImportedObjects",\s*true\)' -or
    $paste -notmatch 'If importInProgress' -or $paste -notmatch 'importInProgress\s*=\s*true' -or
    $paste -notmatch 'importInProgress\s*=\s*false\s+return placedObjectCount' -or
    $managerDestroy -notmatch '(?s)BeginDestroy\(\).*?Self\.Delete\(\)' -or
    $managerBeginDestroy -notmatch '(?s)isDestroyed\s*=\s*true.*?CancelTimer\(SELECTION_EFFECT_TIMER_ID\).*?CancelImportedObjects\(referenceObject\).*?importInProgress\s*=\s*false' -or
    $managerBeginDestroy -match 'referenceObject\s*=\s*None' -or
    $manager -notmatch '(?s)Function StopDestroyedImport\(\).*?CancelImportedObjects\(referenceObject\).*?importInProgress\s*=\s*false') {
    throw 'The default-on Havok setting, tool cancellation, or complete-paste ownership guard is missing.'
}

# Keep the saved-call signature while refusing to invent pre-enable snapshots.
$legacyWaitSignature = 'bool Function WaitOnSelectionLoad(ObjectReference[] importedObjects = None, bool disableImportedHavok = false, Float[] targetPositionX = None, Float[] targetPositionY = None, Float[] targetPositionZ = None, Float[] targetAngleX = None, Float[] targetAngleY = None, Float[] targetAngleZ = None)'
$legacyWait = [regex]::Match($manager, '(?is)\bbool\s+Function\s+WaitOnSelectionLoad\([^\r\n]*\).*?\bEndFunction\b').Value
if ($manager -notmatch [regex]::Escape($legacyWaitSignature) -or
    $legacyWait -notmatch 'importedObjects\.Length\s*>\s*128' -or
    $legacyWait -notmatch 'new Bool\[128\]' -or
    $legacyWait -match '(?i)new\s+(?:Bool|Float)\s*\[[^\]]*\.Length' -or
    $legacyWait -notmatch 'importedObject\.SetMotionType\(importedObject\.Motion_Keyframed,\s*true\)' -or
    $legacyWait -notmatch 'importedObject\.SetPosition\(targetPositionX\[objectIndex\]' -or
    $legacyWait -notmatch 'importedObject\.SetAngle\(targetAngleX\[objectIndex\]') {
    throw 'The legacy wait signature or its bounded, snapshot-preserving fallback changed.'
}
foreach ($snapshotArray in @('targetPositionX', 'targetPositionY', 'targetPositionZ', 'targetAngleX', 'targetAngleY', 'targetAngleZ')) {
    if ($legacyWait -notmatch ($snapshotArray + '\s*==\s*None') -or
        $legacyWait -notmatch ($snapshotArray + '\.Length\s*!=\s*importedObjects\.Length')) {
        throw "The legacy wait must reject absent or incomplete original snapshots: $snapshotArray"
    }
}
if ($latent -notmatch '#include\s+"ImportFunctors\.inl"' -or
    $importFunctors -notmatch 'row\.eligible\s*=\s*_disableHavok\s*&&\s*!ref->As<RE::Actor>\(\)' -or
    $importFunctors -notmatch '(?s)row\.transform\s*=\s*\{\s*ref->data\.location\.x.*?ref->data\.angle\.z\s*\}.*?EngineAPI::Enable\(ref,\s*false\)' -or
    $importFunctors -notmatch '(?s)if\s*\(!ref->Get3D\(\)\).*?ApplyKeyframedMotion\(.*?MoveRefrToPosition\(' -or
    $importFunctors -notmatch '_state\.Save\(' -or $importFunctors -notmatch '_state\.Load\(' -or
    $importFunctors -notmatch 'WriteVariable\(intfc,\s*_references\)' -or
    $importFunctors -notmatch 'ReadVariable\(intfc,\s*_references\)' -or
    $importState -notmatch 'std::vector<Row>\s+rows' -or
    $importFunctors -notmatch 'visits\s*<\s*kRowsPerSlice' -or
    $importFunctors -notmatch 'SliceComplete\(visits,' -or
    $importState -notmatch 'elapsedMs\s*>=\s*kSliceBudgetMs' -or
    $importFunctors -notmatch 'ShouldReschedule\(') {
    throw 'Native import preparation lost actor exclusion, actual pre-enable snapshots, keyframe-before-restore order, serialized state, or bounded scheduling.'
}
if ($mcmDefaults -notmatch '(?ms)^\[Balance\]\s+.*^bDisableHavokOnImportedObjects=1\s*$') {
    throw 'The packaged Havok import setting no longer defaults to enabled under Balance.'
}
$havokControls = @($mcmConfig.content | Where-Object {
    $_.PSObject.Properties['id'] -and $_.id -eq 'bDisableHavokOnImportedObjects:Balance'
})
if ($havokControls.Count -ne 1 -or
    $havokControls[0].type -ne 'switcher' -or
    $havokControls[0].valueOptions.sourceType -ne 'ModSettingBool' -or
    $havokControls[0].action.function -ne 'SetModSettingBool' -or
    @($havokControls[0].action.params)[1] -ne 'bDisableHavokOnImportedObjects:Balance') {
    throw 'The MCM Havok import control is missing or no longer writes the Balance setting.'
}
if ($importFunctors -notmatch 'GetFormByID<RE::BGSKeyword>\(0x4455B\)' -or
    $importFunctors -notmatch 'GetFormByID<RE::ActorValueInfo>\(0x32E\)' -or
    $importFunctors -notmatch 'ref->Get3D\(\)\s*&&\s*ref->HasKeyword\(keyword,\s*nullptr\)\s*&&\s*ref->GetActorValue\(\*power\)\s*>\s*0\.0F') {
    throw 'The retained GetImportedPowerGenerators API must preserve its historical filtering contract.'
}
$animationPass = [regex]::Match($manager, '(?is)\bbool\s+Function\s+InitializePlacedAnimations\([^\r\n]*\).*?\bEndFunction\b').Value
if ($animationPass -notmatch 'While i < candidates\.Length' -or
    $animationPass -notmatch 'GetImportedAnimationCandidates\(referenceObject, placedObjects\)' -or
    $animationPass -match 'GetImportedAnimationKind\(placedObject\)' -or
    $animationPass -match 'GetImportedPowerGenerators|PlayAnimation\("Reset"\)|GetActorValue|\.Activate\(' -or
    $animationPass -notmatch 'placedObject\.IsPowered\(\)' -or
    $animationPass -notmatch 'placedObject\.PlayAnimation\(powerEvent\)' -or
    $animationPass -notmatch 'placedObject\.FanMotorOn\(motorOn\)' -or
    $animationPass -notmatch 'openState == 0 \|\| openState >= 3' -or
    $animationPass -notmatch 'return unavailableCount == 0 && !isDestroyed' -or
    $animationPass -match 'If !accepted') {
    throw 'Animation refresh must scan imported rows independently, use actual power and optional motor switch state, and not interpret graph rejection as visual failure.'
}
if ($importFunctors -notmatch '(?s)HasGenericMotor\(ref\).*?row\.eligible\s*=\s*false.*?ApplyKeyframedMotion' -or
    $legacyWait -notmatch '\(importedObject as GenericMotorScript\) == None') {
    throw 'Imported GenericMotorScript motion must survive native and retained legacy Havok preparation.'
}

$declarationPattern = '(?im)^\s*(?:[A-Za-z_][A-Za-z0-9_]*(?:\[\])?\s+)?Function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^\r\n]*\)[^\r\n]*\bnative\b'
$ordinaryPattern = 'BindLegacyStatic<[^>\r\n]+>\(\*vm,\s*"([A-Za-z_][A-Za-z0-9_]*)"\)'
$latentPattern = 'a_vm,\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*Enqueue[A-Za-z0-9_]+'

$declared = @([regex]::Matches($papyrus, $declarationPattern) | ForEach-Object { $_.Groups[1].Value })
$ordinary = @([regex]::Matches($plugin, $ordinaryPattern) | ForEach-Object { $_.Groups[1].Value })
$latentBindings = @([regex]::Matches($latent, $latentPattern) | ForEach-Object { $_.Groups[1].Value })

function Assert-Unique([string[]] $Names, [string] $Description) {
    $duplicates = @($Names | Group-Object -CaseSensitive | Where-Object Count -gt 1 | ForEach-Object Name)
    if ($duplicates.Count -ne 0) {
        throw "$Description contains duplicate names: $($duplicates -join ', ')"
    }
}

Assert-Unique $declared 'Papyrus native declaration set'
Assert-Unique $ordinary 'ordinary native binding set'
Assert-Unique $latentBindings 'latent native binding set'

if ($manager -notmatch 'GetSettingValueFloat\("Selection",\s*"fWideSelectionMaxDistance",\s*5000\.0\)' -or
	$manager -notmatch '(?s)If maxDistance < 0\.0\s+maxDistance = 5000\.0' -or
	$mcmDefaults -notmatch '(?m)^fWideSelectionMaxDistance=5000\.0\s*$' -or
	$plugin -notmatch 'WideSelection::UsesPersistentCellLimit' -or
	$plugin -notmatch 'cell->worldSpace->persistentCell' -or
	$plugin -notmatch '\}, requiresLimit, counts\)') {
	throw 'The persistent-storage-only 5,000-unit cap must match native policy, packaged defaults and Papyrus fallbacks.'
}
if ($manager -notmatch 'ClipboardExtension\.FilterObjectsByDistance\(objs,\s*referenceObject,\s*maxDistance\)' -or
	$plugin -notmatch 'WideSelection::Filter<VMArray<TESObjectREFR\*>>' -or
	$plugin -notmatch 'BindLegacyStatic<FilterObjectsByDistance>' -or
	$manager -match 'nearbyObjects\.Add\(' -or
	$manager -match 'remainingObjects\.Add\(' -or
	$manager -notmatch 'ClipboardExtension\.ScrapObjectsPaced\(referenceObject, selectedObjects\)') {
	throw 'Wide selection or destroy rebuilds large native snapshots through the 128-element Papyrus growth limit.'
}

# The batching and localization APIs are additive. Matching declaration/binding counts alone
# would not catch an existing name renamed in both places.
$exportBody = [regex]::Match($plugin, '(?s)std::optional<PatternGeneralEntry> WritePatternFile\(.*?(?=bool ReportScriptBuild\()').Value
if (!$exportBody -or $exportBody -match 'IsReferenceBlockedForAutomaticTransfer' -or
    $exportBody -notmatch 'IsBaseFormBlockedForTransfer\(GetBaseForm\(selectedObject\), false, true\)' -or
    $exportBody -notmatch '(?s)selectedObjects.Length\(\) == 0.*?return failure;.*?CollectExistingPower.*?AtomicFile::Write') {
    throw 'Export must honor manual selection and reject an empty snapshot before collecting wires or replacing a slot.'
}
if ($manager -notmatch 'GetPatternImportWireCount\(referenceObject, slot\)' -or
    $manager -notmatch 'newWires.Length < expectedImportWires' -or
    $plugin -notmatch 'basic_file_sink_mt>\(path->string\(\), true\)') {
    throw 'Relocated external-wire warning counts or fresh-process log truncation are missing.'
}
$legacyNativeNames = @(
    'ApplyShaderEffectToSelection', 'ClearSelection', 'CreateSelectionBox', 'Deselect', 'DeselectAll',
    'DisableObjects', 'EnableObjects', 'FilterObjectsByDistance', 'FilterObjectsByPlugin',
    'GetAllConstructibleObjects', 'GetComponentCost', 'GetConstructibleObjectByCreatedObject',
    'GetFullyLoadedCount', 'GetKeyName', 'GetManualSelectableObjectPool', 'GetObjectsByCell',
    'GetObjectsByWorkshopCells', 'GetObjectsInBox', 'GetObjectsInCylinder', 'GetObjectsInSphere',
    'GetPatternComponentCost', 'GetPatternCount', 'GetPatternGeneralInformation', 'GetPatternObjects',
    'GetPatternPlugins', 'GetPatternReferenceInformation', 'GetPatternWires', 'GetPlugins',
    'GetSelectableObjectPool', 'GetSelectedObjectReference', 'GetSelectedObjectReferences',
    'GetSelectedPlugins', 'GetSelectionComponentCost', 'GetSelectionCount', 'GetSelectionDetails',
    'GetSelectionWireCount', 'GetSettingValueBool', 'GetSettingValueFloat', 'GetSettingValueInt',
    'GetSettingValueString', 'IsSelectionFullyLoaded', 'MoveSelection', 'PastePatternObjects',
    'PastePatternWires', 'PastePatternWiresForRows', 'ReloadSettings', 'RemoveShaderEffectToSelection',
    'RotateSelectionZ', 'ScaleSelection', 'ScrapObjects', 'ScrapSelection', 'Select', 'SelectAll',
    'SendWorkshopEventToSelectedObjects', 'TransmitPowerInSelection', 'UpdateSelectedWires', 'WritePatternFile'
)
$newNativeSignatures = @{
    GetSelectionCenter = 'float[] Function GetSelectionCenter(ObjectReference referenceObj) native global'
    CountComponentSources = 'Int Function CountComponentSources(Form componentForm, ObjectReference[] sources, Int startIndex = 0) native global'
    CountComponentSource = 'Int Function CountComponentSource(Form componentForm, ObjectReference sourceRef) native global'
    ReportComponentCostTiming = 'Function ReportComponentCostTiming(String phase, Float elapsedSeconds, Int sourceCount, Int componentCount) native global'
    GetPatternImportWireCount = 'int Function GetPatternImportWireCount(ObjectReference tool, int slot) native global'
    PrepareImportedRows = 'Int[] Function PrepareImportedRows(ObjectReference tool, ObjectReference[] rows, Bool disableHavok) native global'
    InitializeImportedWorkshopRows = 'Int[] Function InitializeImportedWorkshopRows(ObjectReference tool, ObjectReference[] rows) native global'
    GetSuccessfulImportRows = 'ObjectReference[] Function GetSuccessfulImportRows(ObjectReference[] sources, ObjectReference[] originals, Int[] result, Bool workshop) native global'
    ReconnectImportedPowerForRows = 'Int[] Function ReconnectImportedPowerForRows(ObjectReference tool, ObjectReference[] rows, ObjectReference[] originals) native global'
    ConnectImportedPowerForRows = 'Int[] Function ConnectImportedPowerForRows(ObjectReference tool, ObjectReference[] rows, ObjectReference[] originals) native global'
    RefreshImportedPowerForRows = 'Int[] Function RefreshImportedPowerForRows(ObjectReference tool, ObjectReference[] rows, ObjectReference[] originals) native global'
    GetImportPlacementRows = 'ObjectReference[] Function GetImportPlacementRows(ImportPlacementRow[] placement, Bool forWires) native global'
    GetImportPlacementReusedCount = 'Int Function GetImportPlacementReusedCount(ImportPlacementRow[] placement) native global'
    PastePatternObjectsWithReuse = 'ImportPlacementRow[] Function PastePatternObjectsWithReuse(ObjectReference referenceObj, int slot) native global'
    GetText = 'String Function GetText(String key, String arg0 = "", String arg1 = "", String arg2 = "", String arg3 = "", String arg4 = "", String arg5 = "") native global'
    PrepareImportedObjects = 'Int[] Function PrepareImportedObjects(ObjectReference tool, ObjectReference[] rows, Bool disableHavok) native global'
    GetImportedPowerGenerators = 'ObjectReference[] Function GetImportedPowerGenerators(ObjectReference[] rows) native global'
    GetImportedAnimationKind = 'Int Function GetImportedAnimationKind(ObjectReference obj) native global'
    GetImportedAnimationCandidates = 'Int[] Function GetImportedAnimationCandidates(ObjectReference tool, ObjectReference[] rows) native global'
    ScrapObjectsPaced = 'Int[] Function ScrapObjectsPaced(ObjectReference tool, ObjectReference[] rows) native global'
    ReconnectImportedPower = 'Int[] Function ReconnectImportedPower(ObjectReference tool, ObjectReference[] rows) native global'
    CancelImportedObjects = 'Function CancelImportedObjects(ObjectReference tool) native global'
    InitializeImportedWorkshopObjects = 'Int[] Function InitializeImportedWorkshopObjects(ObjectReference tool, ObjectReference[] rows) native global'
}
foreach ($name in $legacyNativeNames) {
    if ($declared -cnotcontains $name) { throw "The additive import contract removed a preserved native: $name" }
}
foreach ($name in $newNativeSignatures.Keys) {
    if ($papyrus -notmatch [regex]::Escape($newNativeSignatures[$name])) {
        throw "The additive native has an unexpected Papyrus signature: $name"
    }
    if (@('GetSelectionCenter', 'CountComponentSources', 'CountComponentSource', 'ReportComponentCostTiming', 'GetPatternImportWireCount', 'CancelImportedObjects', 'GetText', 'GetImportedAnimationKind', 'GetImportPlacementRows', 'GetImportPlacementReusedCount', 'GetSuccessfulImportRows') -ccontains $name) {
        if ($ordinary -cnotcontains $name) { throw "This native must remain an ordinary binding: $name" }
    }
    elseif ($latentBindings -cnotcontains $name) {
        throw "The bounded import operation must remain latent: $name"
    }
}
foreach ($name in @('ScrapObjectsPaced', 'GetImportedAnimationCandidates', 'PastePatternObjectsWithReuse', 'PrepareImportedRows', 'InitializeImportedWorkshopRows', 'ReconnectImportedPowerForRows', 'ConnectImportedPowerForRows', 'RefreshImportedPowerForRows')) {
    if ($serializationNames -notmatch [regex]::Escape('"Clipboard.' + $name + 'Functor"') -or
        $serializationNames -match [regex]::Escape('"ClipboardResurrection.' + $name + 'Functor"')) {
        throw "New performance jobs must have canonical-only serialized names: $name"
    }
    foreach ($method in @('PreflightFactoryName', 'RegisterFactoryChecked')) {
        if ($latent -notmatch [regex]::Escape("$method<$($name)Functor>(registry)")) {
            throw "A performance factory lacks checked registration: $name / $method"
        }
    }
}
foreach ($name in @(
    'CreateSelectionBox', 'ScrapSelection', 'ScrapObjects', 'SendWorkshopEventToSelectedObjects',
    'EnableObjects', 'DisableObjects', 'ScaleSelection', 'TransmitPowerInSelection',
    'PastePatternObjects', 'PastePatternWires', 'PastePatternWiresForRows',
    'PrepareImportedObjects', 'GetImportedPowerGenerators', 'ReconnectImportedPower', 'InitializeImportedWorkshopObjects'
)) {
    if ($serializationNames -notmatch [regex]::Escape('"Clipboard.' + $name + 'Functor"') -or
        $serializationNames -notmatch [regex]::Escape('"ClipboardResurrection.' + $name + 'Functor"')) {
        throw "A canonical serialized name or preserved read alias is absent: $name"
    }
    $key = switch ($name) {
        'SendWorkshopEventToSelectedObjects' { 'SendWorkshopEvent' }
        'TransmitPowerInSelection' { 'TransmitPower' }
        default { $name }
    }
    foreach ($method in @('PreflightFactoryName', 'RegisterFactoryChecked')) {
        if ($latent -notmatch [regex]::Escape("$method<$($key)Functor, k$($key)LegacyName>(registry)")) {
            throw "A serialized factory or alias lacks checked registration: $name / $method"
        }
    }
}
if ($latent -match '"ClipboardResurrection\.' -or
    $importFunctors -match '"ClipboardResurrection\.') {
    throw 'Current functors must use canonical names from SerializationNames.h; legacy names are read aliases only.'
}
$cleanupSignatures = @{
    BeginLegacyCleanup = 'Int Function BeginLegacyCleanup() native global'
    IsLegacyCleanupActive = 'Bool Function IsLegacyCleanupActive(Int token = 0) native global'
    EndLegacyCleanup = 'Function EndLegacyCleanup(Int token) native global'
    GetLegacyCleanupCandidates = 'ObjectReference[] Function GetLegacyCleanupCandidates(Int token) native global'
    IsLegacyCleanupReference = 'Bool Function IsLegacyCleanupReference(ObjectReference reference, Form expectedBase, Int token) native global'
}
foreach ($name in $cleanupSignatures.Keys) {
    if ($papyrus -notmatch [regex]::Escape($cleanupSignatures[$name]) -or $ordinary -cnotcontains $name) {
        throw "The cleanup maintenance contract must retain its ordinary signature: $name"
    }
}
$cleanupControls = @($mcmConfig.content | Where-Object { $_.PSObject.Properties['id'] -and $_.id -eq 'cleanUpOldPlatforms' })
if ($cleanupControls.Count -ne 1 -or $cleanupControls[0].type -ne 'button' -or
    $cleanupControls[0].action.type -ne 'CallFunction' -or
    $cleanupControls[0].action.form -ine 'Clipboard.esp|3790b' -or
    $cleanupControls[0].action.function -cne 'CleanUpOldPlatforms') {
    throw 'The cleanup action must call the existing Clipboard quest without changing its form identity.'
}
$progressSignatures = @{
    BeginImportedPowerProgress = 'Int Function BeginImportedPowerProgress(ObjectReference tool, ObjectReference[] rows) native global'
    AdvanceImportedPowerProgress = 'Function AdvanceImportedPowerProgress(ObjectReference tool, Int token, Int row) native global'
    EndImportedPowerProgress = 'Bool Function EndImportedPowerProgress(ObjectReference tool, Int token) native global'
    ClearImportedProgress = 'Function ClearImportedProgress(ObjectReference tool) native global'
}
foreach ($name in $progressSignatures.Keys) {
    if ($papyrus -notmatch [regex]::Escape($progressSignatures[$name]) -or $ordinary -cnotcontains $name) {
        throw "The progress UI contract must retain its ordinary signature: $name"
    }
}
$ownedInputSignatures = @{
    IsOwnedInputAvailable = 'bool Function IsOwnedInputAvailable() Global Native'
    BeginOwnedInput = 'string Function BeginOwnedInput(ObjectReference owner, string header, string defaultValue, int inputType, int maxChars, int minimum = 0, int maximum = 0) Global Native'
    GetOwnedInputState = 'int Function GetOwnedInputState(string token) Global Native'
    GetOwnedInputResult = 'string Function GetOwnedInputResult(string token) Global Native'
    IsOwnedInputFinished = 'bool Function IsOwnedInputFinished(string token) Global Native'
    AcknowledgeOwnedInput = 'bool Function AcknowledgeOwnedInput(string token) Global Native'
    CancelOwnedInput = 'Function CancelOwnedInput(ObjectReference owner) Global Native'
    AbandonOwnedInput = 'Function AbandonOwnedInput(string token) Global Native'
}
foreach ($name in $ownedInputSignatures.Keys) {
    if ($papyrus -notmatch [regex]::Escape($ownedInputSignatures[$name]) -or $ordinary -cnotcontains $name) {
        throw "Owned-input contract must retain its ordinary signature: $name"
    }
}
if ($papyrus -notmatch [regex]::Escape('Function ReportScriptIssue(string issueText, bool isError = false) Global Native') -or
    $ordinary -cnotcontains 'ReportScriptIssue') {
    throw 'Script warnings/errors must retain their ordinary native logging bridge.'
}
$scaleSignatures = @{
    GetSelectionScaleInputError = 'String Function GetSelectionScaleInputError(ObjectReference referenceObj, String value, Bool increase) native global'
    GetSelectionScaleError = 'String Function GetSelectionScaleError(ObjectReference referenceObj, Float factor) native global'
    TryScaleSelection = 'Bool Function TryScaleSelection(ObjectReference referenceObj, Float scale, Bool maintainShape) native global'
}
foreach ($name in $scaleSignatures.Keys) {
    if ($papyrus -notmatch [regex]::Escape($scaleSignatures[$name])) {
        throw "Exact scale validation signature changed: $name"
    }
}
foreach ($method in @('PreflightFactoryName', 'RegisterFactoryChecked')) {
    if ($latent -notmatch [regex]::Escape("$method<TryScaleSelectionFunctor>(registry)")) {
        throw 'Checked scaling must register its own additive serialized factory.'
    }
}
if ($latent -notmatch 'BindLatent<bool, RE::TESObjectREFR\*, float, bool>\(a_vm, "TryScaleSelection"' -or
    $manager -notmatch '(?s)If !ClipboardExtension.TryScaleSelection\(referenceObject,scaleMod,maintainShape\).*?return\s+EndIf\s+Utility.Wait\(0.1\)') {
    throw 'Rejected scale changes must return a boolean and stop before refresh or completion.'
}
if ($declared.Count -ne 103) {
    throw "Expected 103 Papyrus native declarations; found $($declared.Count)."
}
if ($ordinary.Count -ne 79) {
	throw "Expected 79 ordinary native bindings; found $($ordinary.Count)."
}
if ($latentBindings.Count -ne 24) {
    throw "Expected 24 latent native bindings; found $($latentBindings.Count)."
}
if ($plugin -notmatch ('Bound ' + $ordinary.Count + ' ordinary and ' + $latentBindings.Count + ' latent ClipboardExtension natives')) {
	throw 'The registration summary does not match the verified binding counts.'
}

$overlap = @($ordinary | Where-Object { $latentBindings -ccontains $_ })
if ($overlap.Count -ne 0) {
    throw "Native names are bound by both registration paths: $($overlap -join ', ')"
}

$allBindings = @($ordinary + $latentBindings)
$missing = @($declared | Where-Object { $allBindings -cnotcontains $_ } | Sort-Object)
if ($missing.Count -ne 0) {
    throw "Papyrus natives lack native bindings: $($missing -join ', ')"
}

$extra = @($allBindings | Where-Object { $declared -cnotcontains $_ } | Sort-Object)
if ($extra.Count -ne 0) {
	throw "Native bindings without Papyrus declarations: $($extra -join ', ')"
}

Write-Output "Papyrus contract accepted: $($declared.Count) declarations, $($ordinary.Count) ordinary bindings, and $($latentBindings.Count) latent bindings."
