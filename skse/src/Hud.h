#pragma once

#include "ModifierKeyFrameworkAPI.h"

// The action stack on the HUD: while the modifier key is held and the target's rule has more than one
// modifier action, the activation prompt shows one line per action, each with its own button,
// stacked like a controller's face buttons: slot 3 (Y) on top, slot 2 (X), then Activate (A), then
// the target's name.
namespace Hud
{
	// Watch for the HUD menu (kDataLoaded); the stack is drawn into the HUD's own prompt.
	void Register();

	// What the prompt being built for a target needs from the HUD: the modifier key's button after the
	// label (a_marker: the target has modifier actions, key not held, and the button is drawn rather than the text
	// marker), and/or the extra lines (labels of slots 2 and 3, in slot order, key held). a_more and a_held are the plain
	// facts for a HUD another mod draws (API): the target has modifier actions and the key isn't held / the key is held.
	// From the prompt hook.
	void SetPrompt(RE::TESObjectREFR* a_target, bool a_marker, bool a_more, bool a_held, std::vector<std::string> a_labels);
	void ClearPrompt();

	// The HUD part is in place, or another mod draws the prompt (else the prompt hook falls back to the text marker)
	bool Ready();

	// API: another mod draws the prompt, so nothing of ours goes into the vanilla HUD
	void SetExternal(bool a_external);

	// API: the prompt's extras for the crosshair's current target; false when there are none
	bool Describe(MKF::PromptExtras& a_out);

	// API: the modifier key's button name on a device, as the game names it ("Shift", "360_LB")
	std::string ModifierButton(bool a_gamepad);
}
