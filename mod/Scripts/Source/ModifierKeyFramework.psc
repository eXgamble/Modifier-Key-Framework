ScriptName ModifierKeyFramework Hidden
{Modifier Key Framework: Papyrus functions. Implemented by ModifierKeyFramework.dll (SKSE).

To check that the framework is installed without depending on this script:
	SKSE.GetPluginVersion("ModifierKeyFramework") > 0}

; The framework's version: major * 10000 + minor * 100 + patch (10000 = 1.0.0)
Int Function GetVersion() Global Native

; True while the modifier key (keyboard or gamepad button, ModifierKeyFramework.ini) is held
Bool Function IsModifierHeld() Global Native

; Switches one of your rules on or off. asFile is the rule file's name, with or without ".json"
; (any case); asRuleId is the rule's "id" (any case). Returns false if there's no such rule.
; Not saved: call it again on every game load (e.g. from OnPlayerLoadGame, with your MCM setting).
; A rule starts as its "enabled" value in the rule file (default true).
Bool Function SetRuleEnabled(String asFile, String asRuleId, Bool abEnabled) Global Native

; Whether a rule is currently on (false if there's no such rule)
Bool Function IsRuleEnabled(String asFile, String asRuleId) Global Native
