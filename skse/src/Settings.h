#pragma once

// Data/SKSE/Plugins/eXModifierFramework.ini
namespace Settings
{
	void Load();

	// DirectX scan code of the modifier key (default Left Shift), and the gamepad button (-1 = none)
	std::uint32_t ModifierKey();
	std::int32_t  ModifierKeyGamepad();

	// Shown after the primary label when a target also has an alternate action, e.g. "Give Potion +"
	const std::string& AlternateMarker();
}
