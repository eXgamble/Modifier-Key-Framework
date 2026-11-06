#pragma once

#include "ModifierKeyFrameworkAPI.h"

namespace Hooks
{
	// Vtable hooks on TESNPC (every NPC). Engine code only, safe to install during SKSEPlugin_Load.
	void Install();

	// For the C++ API (version 3): the modifier actions the rules give a target, as plain labels, and running one
	// (slot 1-3) as the player's own press would. Running changes the world: SKSE task only.
	std::uint32_t ModifierActions(RE::TESObjectREFR* a_target, MKF::ModifierActions& a_out);
	bool          RunModifierAction(RE::TESObjectREFR* a_target, std::uint32_t a_slot);
}
