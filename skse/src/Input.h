#pragma once

// The shared modifier key (ModifierKeyFramework.ini). While it's held, rules use their alternate action.
namespace Input
{
	// Register for input events, and for the book menu closing (the prompt is rebuilt then). Call once the input
	// device manager exists (kDataLoaded).
	void Register();

	bool IsModifierHeld();

	// Rebuild the prompt of what's under the crosshair, on the game thread (safe from any thread)
	void QueuePromptRefresh();
}
