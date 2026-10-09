#pragma once

// The action stack on the HUD: while the modifier key is held and the target's rule has more than one
// modifier action, the activation prompt shows one line per action, each with its own button,
// stacked like a controller's face buttons: slot 3 (Y) on top, slot 2 (X), then Activate (A), then
// the target's name.
namespace Hud
{
	// Watch for the HUD menu (kDataLoaded); the stack is drawn into the HUD's own prompt.
	void Register();

	// What the prompt being built for a target needs from the HUD: the modifier key's button after the
	// label (a_marker: the target has modifier actions, key not held), and/or the extra lines (labels of
	// slots 2 and 3, in slot order, key held). From the prompt hook.
	void SetPrompt(RE::TESObjectREFR* a_target, bool a_marker, std::vector<std::string> a_labels);
	void ClearPrompt();

	// The HUD part is in place (else the prompt hook falls back to the text marker)
	bool Ready();
}
