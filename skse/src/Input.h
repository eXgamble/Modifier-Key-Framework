#pragma once

// The shared modifier key (ModifierKeyFramework.ini). While it's held, rules use their alternate action.
namespace Input
{
	// Register for input events. Call once the input device manager exists (kDataLoaded).
	void Register();

	bool IsModifierHeld();

	// Rebuild the prompt of the NPC under the crosshair, on the game thread (safe from any thread)
	void QueuePromptRefresh();
}
