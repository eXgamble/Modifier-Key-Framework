#include "Settings.h"

#include <SimpleIni.h>

namespace
{
	constexpr auto INI_PATH = "Data/SKSE/Plugins/eXModifierFramework.ini";

	std::uint32_t modifierKey = 42;  // Left Shift
	std::int32_t  modifierKeyGamepad = -1;
	std::string   alternateMarker = "+";
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
			modifierKeyGamepad = static_cast<std::int32_t>(ini.GetLongValue("Settings", "iModifierKeyGamepad", -1));
			alternateMarker = ini.GetValue("Settings", "sAlternateMarker", "+");
		}
		logger::info("Settings: modifier key {}, gamepad {}, alternate marker \"{}\"", modifierKey, modifierKeyGamepad, alternateMarker);
	}

	std::uint32_t ModifierKey() { return modifierKey; }
	std::int32_t  ModifierKeyGamepad() { return modifierKeyGamepad; }
	const std::string& AlternateMarker() { return alternateMarker; }
}
