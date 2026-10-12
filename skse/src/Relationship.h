#pragma once

// "info": "relationship" (rule format 5): how the people behind a target feel about the player, for the prompt's
// last line.
namespace Relationship
{
	// The relationship rank (Papyrus GetRelationshipRank: 4 Lover ... 0 Acquaintance ... -4 Archnemesis) of:
	// - an NPC: the NPC;
	// - a door: whoever owns the place it leads to (a house's owner, or the friendliest of its household);
	// - anything else: its owner, the same way.
	// nullopt when nobody owns it or they're all dead.
	std::optional<std::int32_t> Of(RE::TESObjectREFR* a_target);

	// The rank's name ("Friend"), from Interface/Translations/ModifierKeyFramework_<LANGUAGE>.txt
	std::string Name(std::int32_t a_rank);

	// Forget the actors found so far (a game was loaded or started)
	void Reset();
}
