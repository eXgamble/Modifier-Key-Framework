#pragma once

// Activation rules, one JSON file per mod in Data/SKSE/Plugins/eXModifierFramework/*.json:
//
// {
//   "rules": [{
//     "id": "downed",
//     "target": "npc",
//     "requires": { "item": "Death Timer - Immersive Bleedout.esp|0x807", "alive": true },
//     "primary":   { "label": "Give Potion", "event": "DeathTimer_GivePotion" },
//     "alternate": { "label": "Search",      "event": "DeathTimer_Search" },
//     "priority": 50
//   }]
// }
//
// "requires" and "alternate" are optional. When several rules match, the highest priority wins.
namespace Rules
{
	struct Action
	{
		std::string label;
		std::string event;
	};

	struct Rule
	{
		std::string                 file;  // for log messages
		std::string                 id;
		RE::TESBoundObject*         requiredItem = nullptr;
		std::optional<bool>         requiredAlive;
		std::int32_t                priority = 0;
		Action                      primary;
		std::optional<Action>       alternate;
	};

	// Reads every rule file. Needs kDataLoaded (form look-ups).
	void Load();

	// The highest-priority rule the actor matches, or nullptr.
	const Rule* Match(RE::Actor* a_actor);
}
