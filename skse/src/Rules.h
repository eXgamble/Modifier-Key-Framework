#pragma once

// Activation rules, one JSON file per mod in Data/SKSE/Plugins/ModifierKeyFramework/*.json:
//
// {
//   "rules": [{
//     "id": "downed",
//     "target": "npc",
//     "requires": { "item": "Death Timer - Immersive Bleedout.esp|0x807", "alive": true },
//     "not":      { "faction": "Skyrim.esm|0x5C84E" },
//     "primary":   { "label": "Give Potion", "event": "DeathTimer_GivePotion" },
//     "alternate": { "label": "Search",      "event": "DeathTimer_Search" },
//     "priority": 50
//   }]
// }
//
// Labels can be "$Key"s, looked up in Interface/Translations/<name>_<LANGUAGE>.txt, where <name> is
// the rule file's name, or the file's top-level "translations" value.
//
// "requires", "not" and "alternate" are optional. Every condition in "requires" must hold, none in
// "not" may. A form condition can list several forms: any one of them counts. When several rules
// match, the highest priority wins.
namespace Rules
{
	struct Action
	{
		std::string label;  // plain text, or "$Key" from Interface/Translations
		std::string event;

		// The label to show: a "$Key" translated (on first use, then cached), plain text as is
		const std::string& Text() const;

	private:
		mutable std::optional<std::string> text;
	};

	struct FactionCondition
	{
		RE::TESFaction*             faction = nullptr;
		std::optional<std::int32_t> minRank;
	};

	// One "requires" or "not" block. Form lists: any one of them counts. Unset = not checked.
	struct Conditions
	{
		std::vector<RE::TESBoundObject*> items;
		std::vector<RE::BGSKeyword*>     keywords;
		std::vector<FactionCondition>    factions;
		std::vector<RE::TESRace*>        races;
		std::vector<RE::TESNPC*>         npcs;
		std::optional<bool>              alive;
		std::optional<bool>              teammate;
		std::optional<bool>              essential;
		std::optional<bool>              protectedFlag;
		std::optional<bool>              bleedingOut;
		std::optional<bool>              unconscious;
		std::optional<bool>              sitting;
		std::optional<bool>              sleeping;
		std::optional<bool>              inCombat;
	};

	struct Rule
	{
		std::string           file;  // for log messages
		std::string           id;
		Conditions            requirements;
		Conditions            exclusions;
		std::int32_t          priority = 0;
		Action                primary;
		std::optional<Action> alternate;
	};

	// Reads every rule file. Needs kDataLoaded (form look-ups).
	void Load();

	// The highest-priority rule the actor matches, or nullptr.
	const Rule* Match(RE::Actor* a_actor);

	// Logs (once per pair) each rule from another file that also matches the actor but lost to
	// a_winner. Called on activation only, not for every prompt update.
	void ReportConflicts(RE::Actor* a_actor, const Rule* a_winner);
}
