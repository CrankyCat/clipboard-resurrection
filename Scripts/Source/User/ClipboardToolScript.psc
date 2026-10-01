Scriptname ClipboardToolScript extends WorkshopObjectScript

Keyword Property BLOCK_PLAYER_ACTIVATION Auto Const

Message Property MESSAGE_MOVE_ONTO Auto Const

Event OnActivate( ObjectReference akActionRef)
	TranslateAroundSelf(Game.GetPlayer(), 0, -45, 0, 0, 0, 0, 1000)
EndEvent

WorkshopScript Function GetWorkshopById(int workshopID)
	return WorkshopParent.GetWorkshop(workshopID)
EndFunction

ObjectReference Function CreateActivator(Form objectForm, Message activationMessage = None, bool registerEvent = false)
	ObjectReference obj = Self.PlaceAtMe(objectForm,1,true,false,false)
	If activationMessage
		obj.SetActivateTextOverride(activationMessage)
	EndIf
	If registerEvent
		RegisterForRemoteEvent(obj, "OnActivate")
	EndIf
	return obj
EndFunction 

Function MoveAroundSelf(ObjectReference obj, float newX, float newY, float newZ, float newAngleX, float newAngleY, float newAngleZ, bool refresh)
	float sin = Math.sin(-Self.GetAngleZ())
	float cos = Math.cos(-Self.GetAngleZ())
	float relativeX = newX * cos - newY * sin
	float relativeY = newY * cos + newX * sin
	If refresh
		obj.Disable()
	EndIf
	obj.MoveTo(Self, relativeX, relativeY, newZ)
	obj.SetAngle(Self.GetAngleX() + newAngleX, Self.GetAngleY() + newAngleY, Self.GetAngleZ() + newAngleZ)
	If refresh
		obj.Enable()
	EndIf
EndFunction

Function TranslateAroundSelf(ObjectReference obj, float newX, float newY, float newZ,  float newAngleX, float newAngleY, float newAngleZ,  int speed)
	float sin = Math.sin(-Self.GetAngleZ())
	float cos = Math.cos(-Self.GetAngleZ())
	float relativeX = newX * cos - newY * sin + Self.GetPositionX()
	float relativeY = newY * cos + newX * sin + Self.GetPositionY()
	obj.TranslateTo(relativeX, relativeY, newZ + Self.GetPositionZ(), Self.GetAngleX() + newAngleX, Self.GetAngleY() + newAngleY, Self.GetAngleZ() + newAngleZ, speed)
EndFunction

Function OnSelectionChange()

EndFunction

bool Function InWorkshopMode()
	return false;
EndFunction

Function BlockTool()
	Self.AddKeyword(BLOCK_PLAYER_ACTIVATION)
EndFunction

Function UnblockTool()
	Self.RemoveKeyword(BLOCK_PLAYER_ACTIVATION)
EndFunction

Function HideTool()
	Self.Disable()
EndFunction

Function ShowTool()
	Self.Enable()
EndFunction

Function DestroyTool()
	Self.Delete()
EndFunction

; BEGIN GENERATED INTERNAL BUILD
int Function InternalBuild_ClipboardToolScript() Global
    return 122
EndFunction
; END GENERATED INTERNAL BUILD
