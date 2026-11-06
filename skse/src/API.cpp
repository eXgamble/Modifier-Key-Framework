#include "ModifierKeyFrameworkAPI.h"

#include "Dialogue.h"
#include "Hooks.h"
#include "Hud.h"

// The C++ API for mods that draw the activation prompt themselves (include/ModifierKeyFrameworkAPI.h)
namespace
{
	void SetExternalHud(bool a_external)
	{
		Hud::SetExternal(a_external);
	}

	bool GetPrompt(MKF::PromptExtras* a_out)
	{
		return a_out && Hud::Describe(*a_out);
	}

	bool GetModifierButton(bool a_gamepad, char* a_out, std::uint32_t a_size)
	{
		if (!a_out || a_size == 0) {
			return false;
		}
		const auto name = Hud::ModifierButton(a_gamepad);
		strncpy_s(a_out, a_size, name.c_str(), _TRUNCATE);
		return !name.empty();
	}

	bool IsConversationHidden()
	{
		return Dialogue::Hidden();
	}

	RE::TESObjectREFR* Reference(std::uint32_t a_formID)
	{
		return a_formID ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID) : nullptr;
	}

	bool GetModifierActions(std::uint32_t a_target, MKF::ModifierActions* a_out)
	{
		if (!a_out) {
			return false;
		}
		*a_out = {};
		return Hooks::ModifierActions(Reference(a_target), *a_out) > 0;
	}

	bool RunModifierAction(std::uint32_t a_target, std::uint32_t a_slot)
	{
		return Hooks::RunModifierAction(Reference(a_target), a_slot);
	}

	constexpr MKF::API kAPI{ MKF::kAPIVersion, SetExternalHud, GetPrompt, GetModifierButton, IsConversationHidden,
		GetModifierActions, RunModifierAction };
}

extern "C" __declspec(dllexport) const MKF::API* MKF_GetAPI()
{
	return &kAPI;
}
