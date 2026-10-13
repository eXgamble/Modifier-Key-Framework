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
// "alternate" can also be a list of up to 3 actions (rule format 3): while the modifier key is held
// they get the buttons Activate, slot 2 and slot 3 (ini; X and Y on a controller), in that order.
//
// Rule format 4: a "topic" condition (the NPC offers one of these dialogue topics right now, e.g. "What have you got
// for sale?"), and actions with "topic": instead of an event, the action starts the conversation and picks the first
// listed topic the NPC offers, as if the player had chosen it. "endDialogue": true closes the conversation when the
// menu that topic opened (barter, training, ...) closes. A modifier action with topics shows only while the NPC offers
// one of them; the ones left move up a slot.
//
// Rule format 5: "target": "object" for things in the world (books, items, doors, containers), with object conditions
// ("type", "base", "read", "locked", ...), and actions that do one of the game's own things ("do": "take", "equip",
// "equipAndDraw", "use", "activate") instead of sending an event. An action's "info": "relationship" shows, while it's
// the action on Activate, how the people behind the target feel about the player instead of the game's last prompt line
// (a door's lock level, ...): the game's rank names, or the rule's own words per rank ("infoLabels": [{ "fromRank": 1,
// "label": "Friend" }, { "label": "Unwelcome" }]: the first entry whose fromRank the rank reaches).
//
// "requires", "not" and "alternate" are optional. Every condition in "requires" must hold, none in
// "not" may. A form condition can list several forms: any one of them counts. When several rules
// match, the highest priority wins.
namespace Rules
{
	enum class Target
	{
		kNPC,
		kObject
	};

	// "do": one of the game's own actions, run by the framework
	enum class Do
	{
		kNone,
		kActivate,      // the vanilla activation (Talk, Read, Open, Unlock, ...)
		kTake,          // pick it up (a crime where the game says so)
		kEquip,         // pick it up and equip it
		kEquipAndDraw,  // pick it up, equip it, draw it
		kUse            // pick it up and use it: drink, eat, apply a poison (or equip what can't be used)
	};

	// "info": what the prompt's last line shows instead of the game's own (a lock level, ...)
	enum class Info
	{
		kNone,
		kRelationship  // how the NPC, or the people living behind a door, or an object's owner, feel about the player
	};

	// "type" of an object target (bit flags)
	enum ObjectType : std::uint32_t
	{
		kBook = 1 << 0,
		kWeapon = 1 << 1,
		kArmor = 1 << 2,
		kPotion = 1 << 3,
		kFood = 1 << 4,
		kPoison = 1 << 5,
		kIngredient = 1 << 6,
		kScroll = 1 << 7,
		kTorch = 1 << 8,
		kAmmo = 1 << 9,
		kMisc = 1 << 10,
		kKey = 1 << 11,
		kSoulGem = 1 << 12,
		kDoor = 1 << 13,
		kContainer = 1 << 14
	};

	// "infoLabels": the word shown from a relationship rank up (4 Lover ... 0 Acquaintance ... -4 Archnemesis)
	struct RankLabel
	{
		std::int32_t fromRank = -4;
		std::string  label;  // plain text, or "$Key"
	};

	struct Action
	{
		std::string label;  // plain text, or "$Key" from Interface/Translations
		std::string event;
		std::vector<RE::TESTopic*> topics;  // format 4: pick one of these in dialogue instead of an event
		bool                       endDialogue = false;
		Do                         act = Do::kNone;  // format 5: a game action instead of an event
		Info                       info = Info::kNone;  // format 5
		std::vector<RankLabel>     infoLabels;          // highest fromRank first; empty = the game's rank names

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
		std::vector<RE::TESTopic*>       topics;
		std::optional<bool>              alive;
		std::optional<bool>              teammate;
		std::optional<bool>              essential;
		std::optional<bool>              protectedFlag;
		std::optional<bool>              bleedingOut;
		std::optional<bool>              unconscious;
		std::optional<bool>              sitting;
		std::optional<bool>              sleeping;
		std::optional<bool>              inCombat;
		// object targets (format 5)
		std::uint32_t                    types = 0;  // ObjectType bits, 0 = any
		std::vector<RE::TESBoundObject*> bases;
		std::vector<RE::BGSKeyword*>     destinationKeywords;  // a door's destination location
		std::optional<bool>              read;
		std::optional<bool>              spellTome;
		std::optional<bool>              locked;
		std::optional<bool>              requiresKey;
		std::optional<bool>              playerHasKey;
		std::optional<bool>              owned;  // taking or using it is a crime
		std::optional<bool>              takeable;  // can be picked up (not a book that can't be taken, a fixed light, ...)
		// either target
		std::optional<bool>              playerSneaking;
		std::optional<bool>              interior;  // the target is in an interior cell
	};

	// Switched from Papyrus (any thread), read by the prompt / activation (game thread)
	struct EnabledFlag
	{
		std::atomic<bool> value{ true };

		EnabledFlag() = default;
		EnabledFlag(const EnabledFlag& a_other) :
			value(a_other.value.load()) {}
		EnabledFlag& operator=(const EnabledFlag& a_other)
		{
			value = a_other.value.load();
			return *this;
		}
	};

	struct Rule
	{
		std::string           file;  // for log messages
		std::string           id;
		Target                target = Target::kNPC;
		Conditions            requirements;
		Conditions            exclusions;
		std::int32_t          priority = 0;
		Action                primary;
		std::vector<Action>   alternates;  // modifier actions in button order: Activate, slot 2, slot 3 (0-3 of them)
		EnabledFlag           enabled;  // "enabled" in JSON (default true), then Papyrus
		bool                  enabledInFile = true;  // "enabled" in JSON: what each load goes back to
	};

	// A "$Key" from the framework's own or a rule file's translation file, or a_fallback
	std::string Translate(const std::string& a_key, std::string_view a_fallback);

	// A label to show: a "$Key" translated (any translation file), plain text as is
	std::string LabelText(const std::string& a_label);

	// Reads every rule file. Needs kDataLoaded (form look-ups).
	void Load();

	// A rule by its file ("DeathTimer" or "DeathTimer.json") and id, both in any case (Papyrus strings
	// are case-insensitive: the game may hand over a differently cased copy), or nullptr
	Rule* Find(std::string_view a_file, std::string_view a_id);

	// The highest-priority rule the reference matches (an NPC rule for an actor, an object rule for anything else), or
	// nullptr.
	const Rule* Match(RE::TESObjectREFR* a_ref);

	// Is there any rule for objects (a quick way out for the object hooks)
	bool HasObjectRules();

	// The rule's modifier actions the target offers right now, in slot order (an action with topics only while the actor
	// offers one of them).
	std::vector<const Action*> Alternates(const Rule& a_rule, RE::TESObjectREFR* a_ref);

	// The object type bits of a reference's base (one bit, 0 for anything else)
	std::uint32_t TypeOf(RE::TESObjectREFR* a_ref);

	// kPreLoadGame / kNewGame: every rule back to its "enabled" value from its file. Papyrus switches aren't saved (the
	// documented contract: mods set them again on every load), so one save's switch must not carry into the next.
	void ResetEnabled();

	// Logs (once per pair) each rule from another file that also matches the actor but lost to
	// a_winner. Called on activation only, not for every prompt update.
	void ReportConflicts(RE::TESObjectREFR* a_ref, const Rule* a_winner);
}
