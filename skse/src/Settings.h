#pragma once

// Data/SKSE/Plugins/ModifierKeyFramework.ini
namespace Settings
{
	void Load();

	// DirectX scan code of the modifier key (default Left Shift), and the gamepad button in SKSE key
	// numbering (default 274 = Left Shoulder, -1 = none)
	std::uint32_t ModifierKey();
	std::int32_t  ModifierKeyGamepad();

	// Shown after the primary label when a target also has an alternate action, e.g. "Give Potion +"
	const std::string& AlternateMarker();

	// The game controls ("Ready Weapon", "Jump", ...) for modifier actions 2 and 3 (a_slot 2 or 3)
	const RE::BSFixedString& SlotControl(std::size_t a_slot);

	// The controls a slot may use: the ones whose game handler the framework can take over
	const std::vector<std::string_view>& SupportedSlotControls();
}
