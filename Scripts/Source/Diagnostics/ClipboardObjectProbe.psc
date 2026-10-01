Scriptname ClipboardObjectProbe Hidden
; SPDX-License-Identifier: GPL-3.0-or-later
; Read-only console diagnostic. No attached script, quest, or Clipboard native required.
; cgf "ClipboardObjectProbe.Dump" "player_plot_01"

Function Dump(string sample = "unlabelled") Global
    ; Capture once, before any other calls: changing console selection later cannot retarget this run.
    ObjectReference target = Game.GetCurrentConsoleRef()
    if !target
        Debug.Notification("Clipboard probe: select a world object in the console first.")
        return
    endif
    bool opened = Debug.OpenUserLog("ClipboardObjectProbe")
    ; OpenUserLog returns false when already open. Keep it open so successive samples append.
    string token = sample + " ref=" + Hex(target.GetFormID()) + " t=" + Utility.GetCurrentRealTime()
    if !Debug.TraceUser("ClipboardObjectProbe", "[" + token + "] BEGIN schema=2")
        Debug.Notification("Clipboard probe: log unavailable. Check Papyrus logging settings.")
        return
    endif
    Emit(token, "game.version", Debug.GetVersionNumber())
    Emit(token, "f4se.version", F4SE.GetVersion() + "." + F4SE.GetVersionMinor() + "." + F4SE.GetVersionBeta())
    Emit(token, "sample.label", sample)
    Emit(token, "sample.meaning", "User label is ground truth; this probe makes no allow/block decision.")
    DumpForm(token, "reference", target)
    Form base = target.GetBaseObject()
    DumpForm(token, "base", base)
    if !base || target.IsDeleted()
        Emit(token, "ABORT", "Missing base or deleted reference; remaining getters skipped.")
        Emit(token, "END", "incomplete")
        return
    endif
    Emit(token, "base.papyrusClass", BaseClass(base))
    Emit(token, "base.recordSignature", "Not exposed by these APIs; verify base ID in xEdit, especially STAT versus SCOL.")
    Emit(token, "base.model", base.GetWorldModelPath())
    DumpPlugin(token, "base", base)
    DumpPlugin(token, "reference", target)
    Emit(token, "reference.created", target.IsCreated())
    Emit(token, "reference.disabled", target.IsDisabled())
    Emit(token, "reference.deleted", target.IsDeleted())
    Emit(token, "reference.destroyed", target.IsDestroyed())
    Emit(token, "reference.loaded3D", target.Is3DLoaded())
    Emit(token, "reference.activationBlocked", target.IsActivationBlocked())
    Emit(token, "reference.questItem", target.IsQuestItem())
    Emit(token, "reference.position", target.GetPositionX() + "," + target.GetPositionY() + "," + target.GetPositionZ())
    Emit(token, "reference.rotation", target.GetAngleX() + "," + target.GetAngleY() + "," + target.GetAngleZ())
    Emit(token, "reference.scale", target.GetScale())
    DumpForm(token, "cell", target.GetParentCell())
    DumpForm(token, "worldspace", target.GetWorldSpace())
    DumpForm(token, "location.current", target.GetCurrentLocation())
    DumpForm(token, "location.editor", target.GetEditorLocation())
    DumpForm(token, "owner.actorBase", target.GetActorOwner())
    DumpForm(token, "owner.actorRef", target.GetActorRefOwner())
    DumpForm(token, "owner.faction", target.GetFactionOwner())
    Emit(token, "owner.isOwnedByPlayer", target.IsOwnedBy(Game.GetPlayer()))
    DumpLink(token, "link.unkeyed", target.GetLinkedRef())
    Keyword workshopKey = Game.GetFormFromFile(0x00054BA6, "Fallout4.esm") as Keyword
    ProbeKeyword(token, target, "WorkshopItem", workshopKey)
    DumpKeywords(token, target, base)
    DumpScripts(token, "reference", target)
    DumpSS2(token, target)
    DumpAutoBeds(token, target)
    Emit(token, "END", "complete")
    Debug.Notification("Clipboard probe complete: " + sample)
EndFunction

Function DumpAutoBeds(string token, ObjectReference target) Global
    ; Auto Beds owns children via PARENT -> child slot links, not SS2 child -> plot links.
    ; NumSlots is a stored Auto field in the inspected installed script.
    ScriptObject owner = target.CastAs("ukAutoBeds:ukTablePlaceAtMe")
    Emit(token, "autobeds.parentScript", owner != None)
    if !owner
        return
    endif
    Emit(token, "autobeds.NumSlots", owner.GetPropertyValue("NumSlots"))
    ; A generic Var property cannot be cast to Keyword[] by this compiler.
    ; Query the three slot keys verified in this exact sampled root's VMAD.
    ; This is a targeted measurement, not discovery for arbitrary Auto Beds roots.
    if !Game.IsPluginInstalled("ukAutoBeds.esp")
        Emit(token, "autobeds.status", "plugin unavailable")
        return
    endif
    if target.GetBaseObject() != Game.GetFormFromFile(0x001236EE, "ukAutoBeds.esp")
        Emit(token, "autobeds.status", "different parent base; sampled-root slot probes skipped")
        return
    endif
    Keyword[] slots = new Keyword[3]
    slots[0] = Game.GetFormFromFile(0x000965C5, "ukAutoBeds.esp") as Keyword
    slots[1] = Game.GetFormFromFile(0x000E34F3, "ukAutoBeds.esp") as Keyword
    slots[2] = Game.GetFormFromFile(0x00051EF2, "ukAutoBeds.esp") as Keyword
    Emit(token, "autobeds.scope", "sampled living-area root: table slot 3, lamp slot 8, rug slot 9")
    Emit(token, "autobeds.slotKeywordCount", slots.Length)
    int index = 0
    while index < slots.Length
        Keyword slotKeyword = slots[index]
        string fieldName = "autobeds.slot[" + index + "]"
        DumpForm(token, fieldName + ".keyword", slotKeyword)
        if slotKeyword
            ObjectReference child = target.GetLinkedRef(slotKeyword)
            DumpForm(token, fieldName + ".child", child)
            if child && !child.IsDeleted()
                DumpForm(token, fieldName + ".child.base", child.GetBaseObject())
            endif
        else
            Emit(token, fieldName + ".status", "keyword unavailable; link was not queried")
        endif
        index += 1
    endwhile
    Emit(token, "autobeds.status", "three selected parent slot queries finished; compare child IDs to labelled samples")
EndFunction

Function Emit(string token, string fieldKey, Var value) Global
    Debug.TraceUser("ClipboardObjectProbe", "[" + token + "] " + fieldKey + "=" + value)
EndFunction

Function DumpForm(string token, string fieldKey, Form value) Global
    if value
        Emit(token, fieldKey, value)
        Emit(token, fieldKey + ".id", Hex(value.GetFormID()))
        Emit(token, fieldKey + ".name", value.GetName())
        Emit(token, fieldKey + ".editorID", value.GetEditorID())
    else
        Emit(token, fieldKey, "None")
    endif
EndFunction

Function DumpLink(string token, string fieldKey, ObjectReference value) Global
    DumpForm(token, fieldKey, value)
    if value && !value.IsDeleted()
        DumpForm(token, fieldKey + ".base", value.GetBaseObject())
        Emit(token, fieldKey + ".isSimPlot", value.CastAs("SimSettlementsV2:ObjectReferences:SimPlot") != None)
        ScriptObject holder = value.CastAs("SimSettlementsV2:ObjectReferences:PlotLinkHolder")
        Emit(token, fieldKey + ".isPlotLinkHolder", holder != None)
        if holder
            ; Verified stored Auto property; never call a plot state-machine getter.
            Emit(token, fieldKey + ".holder.kPlotRef", holder.GetPropertyValue("kPlotRef"))
        endif
    endif
EndFunction

Function ProbeKeyword(string token, ObjectReference target, string fieldKey, Keyword value) Global
    DumpForm(token, "keyword." + fieldKey, value)
    if value
        Emit(token, "keyword." + fieldKey + ".onReference", target.HasKeyword(value))
        Emit(token, "keyword." + fieldKey + ".onBase", target.GetBaseObject().HasKeyword(value))
        DumpLink(token, "link." + fieldKey, target.GetLinkedRef(value))
    else
        Emit(token, "keyword." + fieldKey + ".status", "unavailable; not a negative match")
    endif
EndFunction

Function DumpKeywords(string token, ObjectReference target, Form base) Global
    ; F4SE Form.GetKeywords reads base-form keyword data. It is not a runtime keyword/link enumerator.
    Keyword[] keywords = base.GetKeywords()
    int count = keywords.Length
    Emit(token, "base.keywordCount", count)
    int i = 0
    while i < count && i < 128
        Keyword fieldKey = keywords[i]
        DumpForm(token, "base.keyword[" + i + "]", fieldKey)
        if fieldKey
            Emit(token, "base.keyword[" + i + "].onReference", target.HasKeyword(fieldKey))
        endif
        i += 1
    endwhile
    Emit(token, "base.keywordsTruncated", count > 128)
    LocationRefType[] locTypes = target.GetLocRefTypes()
    Emit(token, "reference.locRefTypeCount", locTypes.Length)
    i = 0
    while i < locTypes.Length && i < 128
        DumpForm(token, "reference.locRefType[" + i + "]", locTypes[i])
        i += 1
    endwhile
    Emit(token, "reference.locRefTypesTruncated", locTypes.Length > 128)
EndFunction

Function DumpScripts(string token, string fieldKey, ObjectReference target) Global
    ScriptObject workshop = target.CastAs("WorkshopObjectScript")
    Emit(token, fieldKey + ".script.WorkshopObjectScript", workshop != None)
    if workshop
        Emit(token, fieldKey + ".workshopID", workshop.GetPropertyValue("workshopID"))
        Emit(token, fieldKey + ".bAllowPlayerAssignment", workshop.GetPropertyValue("bAllowPlayerAssignment"))
        Emit(token, fieldKey + ".bDefaultPlayerOwnership", workshop.GetPropertyValue("bDefaultPlayerOwnership"))
    endif
    ScriptObject plot = target.CastAs("SimSettlementsV2:ObjectReferences:SimPlot")
    Emit(token, fieldKey + ".script.SimPlot", plot != None)
    if plot
        Emit(token, fieldKey + ".plot.bPlacedByPlayer", plot.GetPropertyValue("bPlacedByPlayer"))
        Emit(token, fieldKey + ".plot.bIsLayoutSpawned", plot.GetPropertyValue("bIsLayoutSpawned"))
        Emit(token, fieldKey + ".plot.iCurrentStage", plot.GetPropertyValue("iCurrentStage"))
        Emit(token, fieldKey + ".plot.kLinkedRefHolder", plot.GetPropertyValue("kLinkedRefHolder"))
        Emit(token, fieldKey + ".plot.CurrentLevelPlan", plot.GetPropertyValue("CurrentLevelPlan"))
    endif
    ScriptObject holder = target.CastAs("SimSettlementsV2:ObjectReferences:PlotLinkHolder")
    Emit(token, fieldKey + ".script.PlotLinkHolder", holder != None)
    if holder
        Emit(token, fieldKey + ".holder.kPlotRef", holder.GetPropertyValue("kPlotRef"))
    endif
EndFunction

Function DumpSS2(string token, ObjectReference target) Global
    if !Game.IsPluginInstalled("SS2.esm")
        Emit(token, "ss2.status", "SS2.esm not installed; SS2 probes unavailable")
        return
    endif
    ; Current SS2.esm QUST SS2_PlotManager, local 0xEB47. Resolve load order at runtime.
    Form managerForm = Game.GetFormFromFile(0x0000EB47, "SS2.esm")
    DumpForm(token, "ss2.manager", managerForm)
    if !managerForm
        Emit(token, "ss2.status", "manager missing; SS2 probes unavailable")
        return
    endif
    ScriptObject manager = managerForm.CastAs("SimSettlementsV2:Quests:PlotManager")
    if !manager
        Emit(token, "ss2.status", "manager script unavailable; SS2 probes unavailable")
        return
    endif
    ; These are verified stored Auto Const properties in the installed SS2 script.
    ; Resolve the actual VMAD values rather than assume an SS2 load-order prefix.
    string[] properties = new string[10]
    properties[0] = "PlotSpawnedKeyword"
    properties[1] = "PreventExportKeyword"
    properties[2] = "StageItemLinkKeyword"
    properties[3] = "StageModelLinkKeyword"
    properties[4] = "AccessoryLinkKeyword"
    properties[5] = "IndicatorLinkKeyword"
    properties[6] = "SecondaryAssignmentMarkerLinkKeyword"
    properties[7] = "SubSpawnLinkKeyword"
    properties[8] = "IdleSlotLinkKeyword"
    properties[9] = "WorkshopStackedItemParentKEYWORD"
    int i = 0
    while i < properties.Length
        Keyword fieldKey = manager.GetPropertyValue(properties[i]) as Keyword
        ProbeKeyword(token, target, "SS2." + properties[i], fieldKey)
        i += 1
    endwhile
    Emit(token, "ss2.status", "queries finished; check each keyword availability")
EndFunction

Function DumpPlugin(string token, string fieldKey, Form value) Global
    int id = value.GetFormID()
    int slot = Math.LogicalAnd(Math.RightShift(id, 24), 0xFF)
    if slot == 0xFF
        Emit(token, fieldKey + ".originPlugin", "runtime-created reference/form; creator is unknown")
        return
    endif
    string filename = ""
    int localID = Math.LogicalAnd(id, 0x00FFFFFF)
    if slot == 0xFE
        int lightSlot = Math.LogicalAnd(Math.RightShift(id, 12), 0xFFF)
        localID = Math.LogicalAnd(id, 0xFFF)
        Game:PluginInfo[] lights = Game.GetInstalledLightPlugins()
        ; F4SE returns lightMods in array order, but PluginInfo.index is NOT the light index.
        ; Treat array slot as a candidate and require a FormFromFile identity round-trip.
        if lightSlot < lights.Length
            filename = lights[lightSlot].name
        endif
        Emit(token, fieldKey + ".lightIndex", lightSlot)
    else
        Game:PluginInfo[] plugins = Game.GetInstalledPlugins()
        int i = 0
        while i < plugins.Length && filename == ""
            if plugins[i].index == slot
                filename = plugins[i].name
            endif
            i += 1
        endwhile
    endif
    Emit(token, fieldKey + ".localID", Hex(localID))
    Emit(token, fieldKey + ".localIDDecimal", localID)
    if filename == ""
        Emit(token, fieldKey + ".originPlugin", "UNRESOLVED")
        return
    endif
    if Game.GetFormFromFile(localID, filename) != value
        Emit(token, fieldKey + ".originPlugin", "UNRESOLVED: candidate failed identity check: " + filename)
        return
    endif
    Emit(token, fieldKey + ".originPlugin", filename)
    string[] masters = Game.GetPluginDependencies(filename)
    Emit(token, fieldKey + ".directMasterCount", masters.Length)
    Emit(token, fieldKey + ".isSS2", filename == "SS2.esm")
    bool ss2Master = false
    int n = 0
    while n < masters.Length
        Emit(token, fieldKey + ".master[" + n + "]", masters[n])
        if masters[n] == "SS2.esm"
            ss2Master = true
        endif
        n += 1
    endwhile
    Emit(token, fieldKey + ".directlyMastersSS2", ss2Master)
EndFunction

string Function BaseClass(Form value) Global
    ; Derived Papyrus classes must be tested before their parents.
    if value as Furniture
        return "Furniture"
    elseif value as Activator
        return "Activator"
    elseif value as Container
        return "Container"
    elseif value as MovableStatic
        return "MovableStatic"
    elseif value as Static
        return "Static; does not prove STAT versus SCOL"
    elseif value as Light
        return "Light"
    else
        return "Other/unknown; see base form and xEdit"
    endif
EndFunction

string Function Hex(int value) Global
    string result = ""
    int shift = 28
    while shift >= 0
        int digit = Math.LogicalAnd(Math.RightShift(value, shift), 0xF)
        if digit < 10
            result += digit as string
        elseif digit == 10
            result += "A"
        elseif digit == 11
            result += "B"
        elseif digit == 12
            result += "C"
        elseif digit == 13
            result += "D"
        elseif digit == 14
            result += "E"
        else
            result += "F"
        endif
        shift -= 4
    endwhile
    return "0x" + result
EndFunction
