#include "Settings.h"

#include <SimpleIni.h>

namespace
{
	constexpr auto INI_PATH = "Data/SKSE/Plugins/ModifierKeyFramework.ini";

	std::uint32_t modifierKey = 42;  // Left Shift
	std::int32_t  modifierKeyGamepad = 274;  // Left Shoulder
	std::string   alternateMarker = "+";
	bool          modifierIcon = true;

	const std::vector<std::string_view> supportedControls{ "Ready Weapon", "Jump", "Sneak", "Shout", "Auto-Move", "Toggle Always Run" };
	std::array<RE::BSFixedString, 2>    slotControls{ "Ready Weapon", "Jump" };

	std::string ReadControl(CSimpleIniA& a_ini, const char* a_key, std::string_view a_default)
	{
		const std::string value = a_ini.GetValue("Settings", a_key, std::string(a_default).c_str());
		const auto it = std::ranges::find_if(supportedControls, [&](std::string_view c) { return _stricmp(std::string(c).c_str(), value.c_str()) == 0; });
		if (it == supportedControls.end()) {
			logger::warn("{} = \"{}\" isn't a supported control, using {}", a_key, value, a_default);
			return std::string(a_default);
		}
		return std::string(*it);
	}
}

namespace Settings
{
	void Load()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(INI_PATH) < SI_OK) {
			logger::warn("{} not found, using defaults", INI_PATH);
		} else {
			modifierKey = static_cast<std::uint32_t>(ini.GetLongValue("Settings", "iModifierKey", 42));
			modifierKeyGamepad = static_cast<std::int32_t>(ini.GetLongValue("Settings", "iModifierKeyGamepad", 274));
			alternateMarker = ini.GetValue("Settings", "sAlternateMarker", "+");
			modifierIcon = ini.GetBoolValue("Settings", "bModifierIcon", true);
			const auto slot2 = ReadControl(ini, "sSlot2Control", "Ready Weapon");
			auto       slot3 = ReadControl(ini, "sSlot3Control", "Jump");
			if (slot3 == slot2) {
				slot3 = slot2 == "Jump" ? "Ready Weapon" : "Jump";
				logger::warn("sSlot2Control and sSlot3Control are both {}, slot 3 uses {}", slot2, slot3);
			}
			slotControls = { RE::BSFixedString(slot2), RE::BSFixedString(slot3) };
		}
		logger::info("Settings: modifier key {}, gamepad {}, alternate marker \"{}\", slot 2 {}, slot 3 {}", modifierKey, modifierKeyGamepad, alternateMarker, slotControls[0].c_str(), slotControls[1].c_str());
	}

	std::uint32_t ModifierKey() { return modifierKey; }
	std::int32_t  ModifierKeyGamepad() { return modifierKeyGamepad; }
	const std::string& AlternateMarker() { return alternateMarker; }
	bool               ModifierIcon() { return modifierIcon; }
	const RE::BSFixedString& SlotControl(std::size_t a_slot) { return slotControls[std::clamp<std::size_t>(a_slot, 2, 3) - 2]; }
	const std::vector<std::string_view>& SupportedSlotControls() { return supportedControls; }
}
