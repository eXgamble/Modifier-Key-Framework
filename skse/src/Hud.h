#pragma once

// The action stack on the HUD: while the modifier key is held and the target's rule has more than one
// modifier action, the activation prompt shows one line per action, each with its own button,
// stacked like a controller's face buttons: slot 3 (Y) on top, slot 2 (X), then Activate (A), then
// the target's name.
namespace Hud
{
	// Watch for the HUD menu (kDataLoaded); the stack is drawn into the HUD's own prompt.
	void Register();

	// The extra lines for the target the prompt is being built for: labels of slots 2 and 3 (in slot
	// order). Game thread, from the prompt hook; an empty list clears the stack.
	void SetStack(RE::TESObjectREFR* a_target, std::vector<std::string> a_labels);
	void ClearStack();
}
