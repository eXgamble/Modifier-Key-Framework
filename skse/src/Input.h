#pragma once

// The shared modifier key (ModifierKeyFramework.ini). While it's held, rules use their alternate action.
namespace Input
{
	// Register for input events. Call once the input device manager exists (kDataLoaded).
	void Register();

	bool IsModifierHeld();
}
