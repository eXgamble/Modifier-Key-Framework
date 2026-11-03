#pragma once

// Modifier Key Framework's C++ API (1.9.0+), for a mod that draws the activation prompt itself: an HTML HUD or any
// HUD that hides the vanilla prompt. Copy this header into your project; it needs nothing else from MKF.
//
// The game's own prompt text keeps carrying MKF's label on its first line ("Give Potion" / "Lydia", a theft in a red
// FONT tag) as always. What MKF otherwise draws into the vanilla HUD comes from getPrompt instead: whether the modifier
// key's button belongs beside the Activate button, and the extra lines while the key is held.
//
//     const auto get = reinterpret_cast<MKF::GetAPI>(
//         GetProcAddress(GetModuleHandleW(L"ModifierKeyFramework.dll"), "MKF_GetAPI"));
//     const MKF::API* mkf = get ? get() : nullptr;   // from kPostLoad on
//     if (mkf && mkf->version >= 1) {
//         mkf->setExternalHud(true);
//     }
//
// Read getPrompt when the HUD's SetCrosshairTarget runs: MKF rebuilds the prompt (and the game calls it) whenever its
// part changes (a new target, the modifier key down or up, the target changing).
//
// Later versions only add members at the end of API, so check version before using one.

#include <cstdint>

namespace MKF
{
	inline constexpr std::uint32_t kAPIVersion = 2;

	struct PromptLine
	{
		char label[128];   // plain text, no HTML, in the player's language
		char control[64];  // the game control that runs it, e.g. "Ready Weapon", "Jump": its button is the player's binding
		bool crime;        // taking it is a crime: the game shows such labels red, like its own "Steal"
	};

	struct PromptExtras
	{
		std::uint32_t target;     // FormID of the crosshair target they belong to; 0 = MKF adds nothing to this prompt
		bool          more;       // key not held, and the target has modifier actions: the modifier key's button goes
		                          // beside the Activate button
		bool          held;       // key held: the game's first line is already the first modifier action (on Activate)
		std::uint32_t lineCount;  // while held: how many more lines go above it (0 to 2)
		PromptLine    lines[2];   // lines[0] = slot 2 (X on a controller), lines[1] = slot 3 (Y), stacked upward
	};

	struct API
	{
		std::uint32_t version;  // kAPIVersion of the MKF that answered

		// true: another HUD draws the prompt, so MKF stops drawing its buttons and lines into the vanilla HUD (and puts
		// no text marker on the label). false: MKF draws them again.
		void (*setExternalHud)(bool a_external);

		// What MKF adds to the prompt for the crosshair's current target. False (and a zeroed a_out) when it adds nothing.
		bool (*getPrompt)(PromptExtras* a_out);

		// The modifier key's button name as the game names it for a device ("Shift", "360_LB"), into a_out.
		bool (*getModifierButton)(bool a_gamepad, char* a_out, std::uint32_t a_size);

		// Version 2: true while the open conversation is a topic action's that MKF keeps out of sight ("endDialogue":
		// Buy / Sell, Train): the NPC turns and speaks, the topic's menu opens, then the conversation ends. A HUD that
		// draws the dialogue menu itself shows no topic list, speaker name or Exit button meanwhile, only the NPC's line.
		// It turns false with the menu still open when the pick goes wrong (the conversation then shows as usual), so
		// ask again rather than once per conversation. Any thread.
		bool (*isConversationHidden)();
	};

	using GetAPI = const API* (*)();
}
