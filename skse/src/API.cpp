#include "ModifierKeyFrameworkAPI.h"

#include "Dialogue.h"
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

	constexpr MKF::API kAPI{ MKF::kAPIVersion, SetExternalHud, GetPrompt, GetModifierButton, IsConversationHidden };
}

extern "C" __declspec(dllexport) const MKF::API* MKF_GetAPI()
{
	return &kAPI;
}
