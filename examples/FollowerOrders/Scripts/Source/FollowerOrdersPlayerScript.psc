ScriptName FollowerOrdersPlayerScript Extends ReferenceAlias
{Example for the Modifier Key Framework: attach to a player alias of a start-game-enabled quest.
Rule file: SKSE/Plugins/ModifierKeyFramework/FollowerOrders.json}

; e.g. your MCM toggle; 1 = the "Wait Here" order is available
GlobalVariable Property FollowerOrdersEnabled Auto

Event OnInit()
	Setup()
EndEvent

; Mod event registrations and SetRuleEnabled are not saved: redo them on every load
Event OnPlayerLoadGame()
	Setup()
EndEvent

Function Setup()
	If SKSE.GetPluginVersion("ModifierKeyFramework") <= 0
		Debug.Trace("FollowerOrders: Modifier Key Framework is not installed")
		Return
	EndIf
	RegisterForModEvent("FollowerOrders_Order", "OnOrder")
	ModifierKeyFramework.SetRuleEnabled("FollowerOrders", "orders", FollowerOrdersEnabled.GetValue() != 0)
EndFunction

; Call this when your MCM toggle changes
Function SetOrdersEnabled(Bool abEnabled)
	FollowerOrdersEnabled.SetValue(abEnabled As Int)
	ModifierKeyFramework.SetRuleEnabled("FollowerOrders", "orders", abEnabled)
EndFunction

; asRuleId: the rule's "id" ("orders"); afIsAlternate: 1.0 = the alternate action (key held)
Event OnOrder(String asEventName, String asRuleId, Float afIsAlternate, Form akTarget)
	Actor follower = akTarget As Actor
	If !follower || !afIsAlternate
		Return
	EndIf
	follower.SetActorValue("WaitingForPlayer", 1)
	Debug.Notification(follower.GetDisplayName() + " waits here.")
EndEvent
