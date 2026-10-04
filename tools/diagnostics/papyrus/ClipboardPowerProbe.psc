Scriptname ClipboardPowerProbe Hidden
; SPDX-License-Identifier: GPL-3.0-or-later
; Read-only, one-reference snapshot. Requires the installed Clipboard native.
; Select a reference, then: cgf "ClipboardPowerProbe.Dump" "sample_label"

Function Dump(string sample = "sample") Global
    ; Capture once so later console selections cannot retarget a running sample.
    ObjectReference target = Game.GetCurrentConsoleRef()
    if !target
        Debug.MessageBox("Clipboard power probe: select an object with prid first.")
        return
    endif
    string refId = Hex(target.GetFormID())
    string token = sample + " ref=" + refId + " t=" + Utility.GetCurrentRealTime()
    bool opened = Debug.OpenUserLog("ClipboardPowerProbe")
    ; False can mean the log is already open. TraceUser determines availability.
    bool logged = Debug.TraceUser("ClipboardPowerProbe", "[" + token + "] BEGIN schema=1")
    Form base = target.GetBaseObject()
    if !base || target.IsDeleted()
        Emit(token, "END", "aborted: missing base or deleted reference")
        Debug.MessageBox("Clipboard power probe: " + refId + " has no base or is deleted. Remaining queries skipped.")
        return
    endif

    Emit(token, "sample.label", sample)
    Emit(token, "game.version", Debug.GetVersionNumber())
    Emit(token, "f4se.version", F4SE.GetVersion() + "." + F4SE.GetVersionMinor() + "." + F4SE.GetVersionBeta())
    Emit(token, "reference.id", refId)
    Emit(token, "base.id", Hex(base.GetFormID()))
    Emit(token, "base.editorID", base.GetEditorID())
    Emit(token, "base.model", base.GetWorldModelPath())
    Emit(token, "reference.disabled", target.IsDisabled())
    Emit(token, "reference.destroyed", target.IsDestroyed())
    Emit(token, "reference.position", target.GetPositionX() + "," + target.GetPositionY() + "," + target.GetPositionZ())
    Emit(token, "reference.scale", target.GetScale())

    bool loaded = target.Is3DLoaded()
    bool poweredBefore = target.IsPowered()
    int openState = target.GetOpenState()
    ; The marker distinguishes a missing/native-call failure from a returned 0.
    Emit(token, "classification.call", "begin")
    int animationKind = ClipboardExtension.GetImportedAnimationKind(target)
    Emit(token, "classification.returned", animationKind)
    bool poweredAfter = target.IsPowered()
    Emit(token, "reference.loaded3D", loaded)
    Emit(token, "reference.poweredBefore", poweredBefore)
    Emit(token, "reference.poweredAfter", poweredAfter)
    Emit(token, "reference.openState", openState)

    Actor actorTarget = target as Actor
    string actorSummary = "Actor: no"
    Emit(token, "reference.isActor", actorTarget != None)
    if actorTarget
        bool unconscious = actorTarget.IsUnconscious()
        bool dead = actorTarget.IsDead()
        bool aiEnabled = actorTarget.IsAIEnabled()
        Emit(token, "actor.unconscious", unconscious)
        Emit(token, "actor.dead", dead)
        Emit(token, "actor.aiEnabled", aiEnabled)
        actorSummary = "Actor: unconscious=" + unconscious + ", dead=" + dead + ", AI=" + aiEnabled
    endif

    Emit(token, "END", "complete")
    ; The console need not print a native return: this message is explicit.
    Debug.MessageBox("Clipboard power probe: " + sample + "\nRef " + refId + " / Base " + Hex(base.GetFormID()) + "\nPowered: " + poweredBefore + " -> " + poweredAfter + " / 3D: " + loaded + "\nAnimation kind: " + animationKind + " / Open state: " + openState + "\n" + actorSummary + "\nLog available: " + logged)
EndFunction

Function Emit(string token, string fieldName, Var value) Global
    Debug.TraceUser("ClipboardPowerProbe", "[" + token + "] " + fieldName + "=" + value)
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
