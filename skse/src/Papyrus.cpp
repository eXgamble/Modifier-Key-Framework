#include "Papyrus.h"

#include "Input.h"
#include "Rules.h"

namespace
{
	constexpr auto SCRIPT = "ModifierKeyFramework";

	// major * 10000 + minor * 100 + patch, e.g. 10000 = 1.0.0
	std::int32_t GetFrameworkVersion(RE::StaticFunctionTag*)
	{
		const auto version = SKSE::PluginDeclaration::GetSingleton()->GetVersion();
		return version.major() * 10000 + version.minor() * 100 + version.patch();
	}

	bool IsModifierHeld(RE::StaticFunctionTag*)
	{
		return Input::IsModifierHeld();
	}

	bool SetRuleEnabled(RE::StaticFunctionTag*, RE::BSFixedString a_file, RE::BSFixedString a_ruleId, bool a_enabled)
	{
		const auto rule = Rules::Find(a_file.c_str(), a_ruleId.c_str());
		if (!rule) {
			logger::warn("SetRuleEnabled: no rule \"{}\" in \"{}\"", a_ruleId.c_str(), a_file.c_str());
			return false;
		}
		if (rule->enabled.value.exchange(a_enabled) != a_enabled) {
			logger::info("{} / {} {} by script", rule->file, rule->id, a_enabled ? "enabled" : "disabled");
			Input::QueuePromptRefresh();
		}
		return true;
	}

	bool IsRuleEnabled(RE::StaticFunctionTag*, RE::BSFixedString a_file, RE::BSFixedString a_ruleId)
	{
		const auto rule = Rules::Find(a_file.c_str(), a_ruleId.c_str());
		return rule && rule->enabled.value;
	}
}

namespace Papyrus
{
	bool Register(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("GetVersion", SCRIPT, GetFrameworkVersion, true);
		a_vm->RegisterFunction("IsModifierHeld", SCRIPT, IsModifierHeld, true);
		a_vm->RegisterFunction("SetRuleEnabled", SCRIPT, SetRuleEnabled);
		a_vm->RegisterFunction("IsRuleEnabled", SCRIPT, IsRuleEnabled, true);
		logger::info("Papyrus functions registered");
		return true;
	}
}
