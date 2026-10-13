#include "Rules.h"

#include "Dialogue.h"

#include <nlohmann/json.hpp>

namespace
{
	using json = nlohmann::json;

	constexpr auto RULES_DIR = "Data/SKSE/Plugins/ModifierKeyFramework";

	// Rule files may say which format they're written for ("version"); newer ones are refused
	// instead of half-understood. Files without it are version 1.
	// 2 (1.1.0): "primary" and its "label" are optional (no label = the game's own prompt text).
	// 3 (1.2.0): "alternate" can be a list of up to 3 actions (buttons: Activate, then slots 2 and 3).
	// 4 (1.3.0): the "topic" condition, and actions that pick a dialogue topic ("topic", "endDialogue").
	// 5 (1.4.0): "target": "object" with object conditions, and "do" actions (the game's own take / equip / use / ...).
	// 6 (1.5.0): "info": "none" (nothing below the name line).
	constexpr std::int32_t RULE_FORMAT_VERSION = 6;
	constexpr std::size_t  MAX_ALTERNATES = 3;

	std::vector<Rules::Rule> rules;  // sorted by priority, highest first
	bool                     objectRules = false;

	std::string  currentRule;  // "file / id" of the rule being read, for log messages
	std::int32_t currentVersion = 1;  // the rule format of the file being read

	// Why a rule can't be used: logged once, the rule is skipped
	struct RuleError
	{
		std::string what;
	};

	// "Plugin.esp|0x807" -> the form (local ID in that plugin, light plugins included).
	// A plugin that isn't loaded gives nullptr (the entry is dropped: soft dependencies work);
	// anything else that doesn't resolve is the rule author's mistake and throws.
	RE::TESForm* ResolveForm(const std::string& a_ref)
	{
		const auto bar = a_ref.find('|');
		if (bar == std::string::npos) {
			throw RuleError{ std::format("\"{}\": expected \"Plugin.esp|0xID\"", a_ref) };
		}
		const auto plugin = a_ref.substr(0, bar);
		RE::FormID localID = 0;
		try {
			localID = static_cast<RE::FormID>(std::stoul(a_ref.substr(bar + 1), nullptr, 16));
		} catch (...) {
			throw RuleError{ std::format("\"{}\": \"{}\" is not a hex form ID", a_ref, a_ref.substr(bar + 1)) };
		}
		const auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler->LookupModByName(plugin)) {
			return nullptr;
		}
		const auto form = dataHandler->LookupForm(localID, plugin);
		if (!form) {
			throw RuleError{ std::format("\"{}\": plugin \"{}\" has no form {:06X}", a_ref, plugin, localID) };
		}
		return form;
	}

	template <class T>
	T* ResolveAs(const std::string& a_ref, std::string_view a_kind)
	{
		const auto form = ResolveForm(a_ref);
		if (!form) {
			logger::info("{}: \"{}\": plugin not loaded, entry ignored", currentRule, a_ref);
			return nullptr;
		}
		const auto typed = form->As<T>();
		if (!typed) {
			throw RuleError{ std::format("\"{}\" is not {} (form type {})", a_ref, a_kind, static_cast<int>(form->GetFormType())) };
		}
		return typed;
	}

	// A form condition: one "Plugin.esp|0xID" string, or a list of them
	template <class T>
	void ReadForms(const json& a_value, std::string_view a_key, std::string_view a_kind, std::vector<T*>& a_out)
	{
		const auto list = a_value.is_array() ? a_value : json::array({ a_value });
		for (const auto& entry : list) {
			if (!entry.is_string()) {
				throw RuleError{ std::format("\"{}\": expected \"Plugin.esp|0xID\" or a list of them", a_key) };
			}
			if (const auto form = ResolveAs<T>(entry.get<std::string>(), a_kind)) {
				a_out.push_back(form);
			}
		}
	}

	// "faction": "Plugin.esp|0xID", { "form": "...", "minRank": 1 }, or a list of either
	void ReadFactions(const json& a_value, std::vector<Rules::FactionCondition>& a_out)
	{
		const auto list = a_value.is_array() ? a_value : json::array({ a_value });
		for (const auto& entry : list) {
			Rules::FactionCondition condition;
			std::string ref;
			if (entry.is_string()) {
				ref = entry.get<std::string>();
			} else if (entry.is_object() && entry.contains("form") && entry["form"].is_string()) {
				ref = entry["form"].get<std::string>();
				if (entry.contains("minRank")) {
					if (!entry["minRank"].is_number_integer()) {
						throw RuleError{ "\"faction\": \"minRank\" must be a whole number" };
					}
					condition.minRank = entry["minRank"].get<std::int32_t>();
				}
			} else {
				throw RuleError{ "\"faction\": expected \"Plugin.esp|0xID\", { \"form\": ..., \"minRank\": ... } or a list of them" };
			}
			condition.faction = ResolveAs<RE::TESFaction>(ref, "a faction");
			if (condition.faction) {
				a_out.push_back(condition);
			}
		}
	}

	// A "requires" or "not" block
	// "type": the object kinds by name
	constexpr std::pair<std::string_view, std::uint32_t> OBJECT_TYPES[] = {
		{ "book", Rules::kBook }, { "weapon", Rules::kWeapon }, { "armor", Rules::kArmor }, { "potion", Rules::kPotion },
		{ "food", Rules::kFood }, { "poison", Rules::kPoison }, { "ingredient", Rules::kIngredient }, { "scroll", Rules::kScroll },
		{ "torch", Rules::kTorch }, { "ammo", Rules::kAmmo }, { "misc", Rules::kMisc }, { "key", Rules::kKey },
		{ "soulGem", Rules::kSoulGem }, { "door", Rules::kDoor }, { "container", Rules::kContainer },
	};

	std::uint32_t ReadTypes(const json& a_value)
	{
		const auto    list = a_value.is_array() ? a_value : json::array({ a_value });
		std::uint32_t types = 0;
		for (const auto& entry : list) {
			const auto name = entry.is_string() ? entry.get<std::string>() : std::string{};
			const auto type = std::ranges::find_if(OBJECT_TYPES, [&](const auto& a_type) { return a_type.first == name; });
			if (type == std::end(OBJECT_TYPES)) {
				std::string known;
				for (const auto& [typeName, bit] : OBJECT_TYPES) {
					known += std::format("{}{}", known.empty() ? "" : ", ", typeName);
				}
				throw RuleError{ std::format("\"type\": {} is not one of {}", entry.dump(), known) };
			}
			types |= type->second;
		}
		return types;
	}

	Rules::Conditions ReadConditions(const json& a_block, std::string_view a_blockName, bool a_isRequirement, Rules::Target a_target)
	{
		Rules::Conditions conditions;
		if (!a_block.is_object()) {
			throw RuleError{ std::format("\"{}\" must be an object", a_blockName) };
		}

		const std::pair<std::string_view, std::optional<bool> Rules::Conditions::*> flags[] = {
			{ "alive", &Rules::Conditions::alive },
			{ "teammate", &Rules::Conditions::teammate },
			{ "essential", &Rules::Conditions::essential },
			{ "protected", &Rules::Conditions::protectedFlag },
			{ "bleedingOut", &Rules::Conditions::bleedingOut },
			{ "unconscious", &Rules::Conditions::unconscious },
			{ "sitting", &Rules::Conditions::sitting },
			{ "sleeping", &Rules::Conditions::sleeping },
			{ "inCombat", &Rules::Conditions::inCombat },
			{ "read", &Rules::Conditions::read },
			{ "spellTome", &Rules::Conditions::spellTome },
			{ "locked", &Rules::Conditions::locked },
			{ "requiresKey", &Rules::Conditions::requiresKey },
			{ "playerHasKey", &Rules::Conditions::playerHasKey },
			{ "owned", &Rules::Conditions::owned },
			{ "takeable", &Rules::Conditions::takeable },
			{ "playerSneaking", &Rules::Conditions::playerSneaking },
			{ "interior", &Rules::Conditions::interior },
		};
		// which target each condition is for (the rest work for both)
		constexpr std::string_view npcOnly[] = { "item", "faction", "race", "npc", "topic", "alive", "teammate", "essential", "protected",
			"bleedingOut", "unconscious", "sitting", "sleeping", "inCombat" };
		constexpr std::string_view objectOnly[] = { "type", "base", "destinationKeyword", "read", "spellTome", "locked", "requiresKey",
			"playerHasKey", "owned", "takeable" };
		constexpr std::string_view format5[] = { "type", "base", "destinationKeyword", "read", "spellTome", "locked", "requiresKey",
			"playerHasKey", "owned", "takeable", "playerSneaking", "interior" };

		for (const auto& [key, value] : a_block.items()) {
			if (std::ranges::contains(format5, key) && currentVersion < 5) {
				throw RuleError{ std::format("\"{}\" needs \"version\": 5", key) };
			}
			if (a_target == Rules::Target::kObject && std::ranges::contains(npcOnly, key)) {
				throw RuleError{ std::format("\"{}\" is for NPC rules, this rule's target is \"object\"", key) };
			}
			if (a_target == Rules::Target::kNPC && std::ranges::contains(objectOnly, key)) {
				throw RuleError{ std::format("\"{}\" is for object rules (\"target\": \"object\")", key) };
			}
			// a form list that lost every entry to missing plugins can never hold
			const auto requireSome = [&](std::size_t a_count) {
				if (a_isRequirement && a_count == 0) {
					throw RuleError{ std::format("\"{}\": none of the listed forms' plugins are loaded", key) };
				}
			};

			if (key == "item") {
				ReadForms(value, key, "an item", conditions.items);
				requireSome(conditions.items.size());
			} else if (key == "keyword") {
				ReadForms(value, key, "a keyword", conditions.keywords);
				requireSome(conditions.keywords.size());
			} else if (key == "faction") {
				ReadFactions(value, conditions.factions);
				requireSome(conditions.factions.size());
			} else if (key == "race") {
				ReadForms(value, key, "a race", conditions.races);
				requireSome(conditions.races.size());
			} else if (key == "npc") {
				ReadForms(value, key, "an NPC", conditions.npcs);
				requireSome(conditions.npcs.size());
			} else if (key == "type") {
				conditions.types = ReadTypes(value);
			} else if (key == "base") {
				ReadForms(value, key, "an object", conditions.bases);
				requireSome(conditions.bases.size());
			} else if (key == "destinationKeyword") {
				ReadForms(value, key, "a keyword", conditions.destinationKeywords);
				requireSome(conditions.destinationKeywords.size());
			} else if (key == "topic") {
				if (currentVersion < 4) {
					throw RuleError{ "\"topic\" needs \"version\": 4" };
				}
				ReadForms(value, key, "a dialogue topic", conditions.topics);
				requireSome(conditions.topics.size());
			} else if (const auto flag = std::ranges::find_if(flags, [&](const auto& a_flag) { return a_flag.first == key; }); flag != std::end(flags)) {
				if (!value.is_boolean()) {
					throw RuleError{ std::format("\"{}\" must be true or false", key) };
				}
				conditions.*(flag->second) = value.get<bool>();
			} else {
				throw RuleError{ std::format("unknown condition \"{}\" in \"{}\"", key, a_blockName) };
			}
		}
		return conditions;
	}

	// Every key of an object must be one of a_known (typos would otherwise be silently ignored)
	void CheckKeys(const json& a_object, std::initializer_list<std::string_view> a_known, std::string_view a_where)
	{
		for (const auto& [key, value] : a_object.items()) {
			if (std::ranges::find(a_known, key) == a_known.end()) {
				throw RuleError{ std::format("unknown key \"{}\" in {}", key, a_where) };
			}
		}
	}

	// "primary" / "alternate": { "label": "...", "event": "..." } (event optional; the primary's
	// label too: without one the prompt keeps the game's own text, e.g. "Search" on a dead body)
	// "do": the game's own actions
	constexpr std::pair<std::string_view, Rules::Do> DO_ACTIONS[] = {
		{ "activate", Rules::Do::kActivate }, { "take", Rules::Do::kTake }, { "equip", Rules::Do::kEquip },
		{ "equipAndDraw", Rules::Do::kEquipAndDraw }, { "use", Rules::Do::kUse },
	};

	Rules::Action ReadAction(const json& a_json, std::string_view a_name, bool a_labelRequired, Rules::Target a_target)
	{
		if (!a_json.is_object()) {
			throw RuleError{ std::format("\"{}\" must be an object", a_name) };
		}
		CheckKeys(a_json, { "label", "event", "topic", "endDialogue", "do", "info", "infoLabels" }, std::format("\"{}\"", a_name));
		Rules::Do act = Rules::Do::kNone;
		if (a_json.contains("do")) {
			if (currentVersion < 5) {
				throw RuleError{ std::format("\"{}\": \"do\" needs \"version\": 5", a_name) };
			}
			const auto name = a_json["do"].is_string() ? a_json["do"].get<std::string>() : std::string{};
			const auto found = std::ranges::find_if(DO_ACTIONS, [&](const auto& a_do) { return a_do.first == name; });
			if (found == std::end(DO_ACTIONS)) {
				throw RuleError{ std::format("\"{}\": \"do\" is one of activate, take, equip, equipAndDraw, use", a_name) };
			}
			act = found->second;
			if (a_target == Rules::Target::kNPC && act != Rules::Do::kActivate) {
				throw RuleError{ std::format("\"{}\": \"do\": \"{}\" is for object rules; NPC rules can only \"activate\"", a_name, name) };
			}
			if (a_json.contains("event") || a_json.contains("topic")) {
				throw RuleError{ std::format("\"{}\": give it one of \"event\", \"topic\" or \"do\"", a_name) };
			}
		}
		// take and activate have the game's own word ("Take" / "Steal", "Read", "Open", ...)
		if (act == Rules::Do::kTake || act == Rules::Do::kActivate) {
			a_labelRequired = false;
		}
		if (a_json.contains("label") ? !a_json["label"].is_string() || a_json["label"].get<std::string>().empty() : a_labelRequired) {
			throw RuleError{ std::format("\"{}\" needs a \"label\" (text)", a_name) };
		}
		if (a_json.contains("event") && !a_json["event"].is_string()) {
			throw RuleError{ std::format("\"{}\": \"event\" must be text", a_name) };
		}
		Rules::Action action;
		action.label = a_json.value("label", "");
		action.event = a_json.value("event", "");
		action.act = act;
		if (a_json.contains("info")) {
			if (currentVersion < 5) {
				throw RuleError{ std::format("\"{}\": \"info\" needs \"version\": 5", a_name) };
			}
			if (a_json["info"] == "relationship") {
				action.info = Rules::Info::kRelationship;
			} else if (a_json["info"] == "none") {
				if (currentVersion < 6) {
					throw RuleError{ std::format("\"{}\": \"info\": \"none\" needs \"version\": 6", a_name) };
				}
				action.info = Rules::Info::kNothing;
			} else {
				throw RuleError{ std::format("\"{}\": \"info\" is \"relationship\" or \"none\"", a_name) };
			}
		}
		if (a_json.contains("infoLabels")) {
			if (action.info != Rules::Info::kRelationship) {
				throw RuleError{ std::format("\"{}\": \"infoLabels\" needs \"info\": \"relationship\"", a_name) };
			}
			const auto& list = a_json["infoLabels"];
			if (!list.is_array() || list.empty()) {
				throw RuleError{ std::format("\"{}\": \"infoLabels\" must be a list of {{ \"fromRank\", \"label\" }}", a_name) };
			}
			for (const auto& entry : list) {
				if (!entry.is_object()) {
					throw RuleError{ std::format("\"{}\": each \"infoLabels\" entry must be an object", a_name) };
				}
				CheckKeys(entry, { "fromRank", "label" }, std::format("\"{}\" infoLabels entry", a_name));
				Rules::RankLabel rankLabel;
				if (!entry.contains("label") || !entry["label"].is_string() || entry["label"].get<std::string>().empty()) {
					throw RuleError{ std::format("\"{}\": each \"infoLabels\" entry needs a \"label\" (text)", a_name) };
				}
				rankLabel.label = entry["label"].get<std::string>();
				if (entry.contains("fromRank")) {
					if (!entry["fromRank"].is_number_integer() || entry["fromRank"].get<std::int32_t>() < -4 || entry["fromRank"].get<std::int32_t>() > 4) {
						throw RuleError{ std::format("\"{}\": \"fromRank\" is a whole number from -4 (Archnemesis) to 4 (Lover)", a_name) };
					}
					rankLabel.fromRank = entry["fromRank"].get<std::int32_t>();
				}
				action.infoLabels.push_back(std::move(rankLabel));
			}
			std::ranges::stable_sort(action.infoLabels, std::ranges::greater{}, &Rules::RankLabel::fromRank);
		}
		if (a_json.contains("topic") && a_target == Rules::Target::kObject) {
			throw RuleError{ std::format("\"{}\": \"topic\" is for NPC rules", a_name) };
		}
		if (a_json.contains("topic") || a_json.contains("endDialogue")) {
			if (currentVersion < 4) {
				throw RuleError{ std::format("\"{}\": \"topic\" and \"endDialogue\" need \"version\": 4", a_name) };
			}
			if (!action.event.empty()) {
				throw RuleError{ std::format("\"{}\": give it an \"event\" or a \"topic\", not both", a_name) };
			}
			if (!a_json.contains("topic")) {
				throw RuleError{ std::format("\"{}\": \"endDialogue\" needs a \"topic\"", a_name) };
			}
			ReadForms(a_json["topic"], "topic", "a dialogue topic", action.topics);
			if (action.topics.empty()) {
				throw RuleError{ std::format("\"{}\": none of the listed topics' plugins are loaded", a_name) };
			}
			if (a_json.contains("endDialogue")) {
				if (!a_json["endDialogue"].is_boolean()) {
					throw RuleError{ std::format("\"{}\": \"endDialogue\" must be true or false", a_name) };
				}
				action.endDialogue = a_json["endDialogue"].get<bool>();
			}
		}
		return action;
	}

	// "$Key" -> label, from the rule files' translation files. Kept here, not in the game's translation table: the
	// game pools its strings case-insensitively, so "Equip" would come back as an existing "EQUIP".
	std::unordered_map<std::string, std::string> translations;

	// Interface/Translations/<name>_<LANGUAGE>.txt (UTF-16 LE, "$Key<tab>Text" lines; loose or in a BSA). Each name once.
	void LoadTranslations(const std::string& a_name)
	{
		static std::set<std::string> loaded;
		if (!loaded.insert(a_name).second) {
			return;
		}
		const auto setting = RE::INISettingCollection::GetSingleton()->GetSetting("sLanguage:General");
		const std::string language = setting && setting->GetType() == RE::Setting::Type::kString ? setting->data.s : "ENGLISH";
		const auto        path = std::format("Interface\\Translations\\{}_{}.txt", a_name, language);
		RE::BSResourceNiBinaryStream stream{ path };
		if (!stream.good()) {
			return;
		}
		std::wstring  content;
		wchar_t       character = 0;
		std::uint64_t read = 0;
		while (true) {
			stream.stream->DoRead(&character, sizeof(character), read);
			if (read != sizeof(character)) {
				break;
			}
			content += character;
		}
		if (content.empty() || content.front() != 0xFEFF) {
			logger::error("{}: must be UTF-16 LE with a BOM, labels from it won't show", path);
			return;
		}
		std::size_t count = 0;
		for (std::size_t start = 1; start < content.size();) {
			auto end = content.find(L'\n', start);
			if (end == std::wstring::npos) {
				end = content.size();
			}
			std::wstring_view line{ content.data() + start, end - start };
			start = end + 1;
			if (line.ends_with(L'\r')) {
				line.remove_suffix(1);
			}
			const auto tab = line.find(L'\t');
			if (!line.starts_with(L'$') || tab == std::wstring_view::npos || tab < 2) {
				continue;
			}
			const auto key = SKSE::stl::utf16_to_utf8(line.substr(0, tab));
			const auto text = SKSE::stl::utf16_to_utf8(line.substr(tab + 1));
			if (key && text) {
				translations[*key] = *text;
				++count;
			}
		}
		logger::info("{}: {} label(s)", path, count);
	}

	void LoadFile(const std::filesystem::path& a_path)
	{
		const auto file = a_path.filename().string();
		json root;
		try {
			std::ifstream in(a_path);
			root = json::parse(in, nullptr, true, true);  // allow comments
		} catch (const std::exception& e) {
			logger::error("{}: not valid JSON ({})", file, e.what());
			return;
		}

		// the file as a whole: format version, known keys, a rule list
		try {
			if (!root.is_object()) {
				throw RuleError{ "expected an object with \"rules\"" };
			}
			CheckKeys(root, { "$schema", "version", "translations", "rules" }, "the file");
			if (root.contains("version")) {
				if (!root["version"].is_number_integer()) {
					throw RuleError{ "\"version\" must be a whole number" };
				}
				if (const auto version = root["version"].get<std::int32_t>(); version > RULE_FORMAT_VERSION) {
					throw RuleError{ std::format("written for rule format version {}, this Modifier Key Framework reads up to {}; update the framework", version, RULE_FORMAT_VERSION) };
				}
			}
			if (!root.contains("rules") || !root["rules"].is_array()) {
				throw RuleError{ "\"rules\" must be a list" };
			}
		} catch (const RuleError& e) {
			logger::error("{}: {}, file skipped", file, e.what);
			return;
		}

		const std::int32_t fileVersion = root.value("version", 1);
		currentVersion = fileVersion;
		LoadTranslations(root.value("translations", a_path.stem().string()));

		std::size_t loaded = 0;
		for (const auto& entry : root["rules"]) {
			Rules::Rule rule;
			rule.file = file;
			rule.id = entry.is_object() ? entry.value("id", "?") : "?";
			currentRule = std::format("{} / {}", file, rule.id);
			try {
				if (!entry.is_object()) {
					throw RuleError{ "a rule must be an object" };
				}
				CheckKeys(entry, { "id", "target", "requires", "not", "primary", "alternate", "priority", "enabled" }, "the rule");
				rule.priority = entry.value("priority", 0);
				if (entry.contains("enabled")) {
					if (!entry["enabled"].is_boolean()) {
						throw RuleError{ "\"enabled\" must be true or false" };
					}
					rule.enabled.value = entry["enabled"].get<bool>();
					rule.enabledInFile = rule.enabled.value;
				}
				if (const auto target = entry.value("target", "npc"); target == "object") {
					if (fileVersion < 5) {
						throw RuleError{ "\"target\": \"object\" needs \"version\": 5" };
					}
					rule.target = Rules::Target::kObject;
				} else if (target != "npc") {
					throw RuleError{ "\"target\" is \"npc\" or \"object\"" };
				}
				rule.primary = ReadAction(entry.value("primary", json::object()), "primary", false, rule.target);
				if (entry.contains("alternate")) {
					const auto& alternate = entry["alternate"];
					if (alternate.is_array()) {
						if (fileVersion < 3) {
							throw RuleError{ "a list of alternates needs \"version\": 3" };
						}
						if (alternate.empty() || alternate.size() > MAX_ALTERNATES) {
							throw RuleError{ std::format("\"alternate\" lists 1 to {} actions", MAX_ALTERNATES) };
						}
						for (std::size_t i = 0; i < alternate.size(); ++i) {
							rule.alternates.push_back(ReadAction(alternate[i], std::format("alternate {}", i + 1), true, rule.target));
						}
					} else {
						rule.alternates.push_back(ReadAction(alternate, "alternate", true, rule.target));
					}
				}
				if (rule.primary.label.empty() && rule.primary.event.empty() && rule.primary.topics.empty() && rule.primary.act == Rules::Do::kNone && rule.alternates.empty()) {
					throw RuleError{ "the rule changes nothing: give it a primary label or event, or an alternate" };
				}
				if (entry.contains("requires")) {
					rule.requirements = ReadConditions(entry["requires"], "requires", true, rule.target);
				}
				if (entry.contains("not")) {
					rule.exclusions = ReadConditions(entry["not"], "not", false, rule.target);
				}
			} catch (const RuleError& e) {
				logger::warn("{} / {}: {}, rule skipped", file, rule.id, e.what);
				continue;
			} catch (const std::exception& e) {
				logger::warn("{} / {}: {}, rule skipped", file, rule.id, e.what());
				continue;
			}

			rules.push_back(std::move(rule));
			++loaded;
		}
		logger::info("{}: {} rule(s) loaded", file, loaded);
	}

	// Lazily counted inventory, shared by every rule checked for one actor
	class Inventory
	{
	public:
		explicit Inventory(RE::Actor* a_actor) :
			actor(a_actor) {}

		bool Carries(RE::TESBoundObject* a_item)
		{
			if (!actor) {
				return false;
			}
			if (!counts) {
				counts = actor->GetInventoryCounts([](RE::TESBoundObject& a_object) {
					return std::ranges::any_of(rules, [&](const Rules::Rule& r) {
						return std::ranges::contains(r.requirements.items, &a_object) || std::ranges::contains(r.exclusions.items, &a_object);
					});
				});
			}
			const auto it = counts->find(a_item);
			return it != counts->end() && it->second > 0;
		}

	private:
		RE::Actor*                                         actor;
		std::optional<RE::TESObjectREFR::InventoryCountMap> counts;
	};

	// A door's destination: the location on the other side
	RE::BGSLocation* DestinationOf(RE::TESObjectREFR* a_door)
	{
		const auto linked = a_door->extraList.GetTeleportLinkedDoor().get();
		return linked ? linked->GetCurrentLocation() : nullptr;
	}

	bool PlayerHasKey(RE::TESObjectREFR* a_ref)
	{
		const auto lock = a_ref->GetLock();
		const auto key = lock ? lock->key : nullptr;
		if (!key) {
			return false;
		}
		const auto counts = RE::PlayerCharacter::GetSingleton()->GetInventoryCounts([&](RE::TESBoundObject& a_object) { return &a_object == key; });
		const auto it = counts.find(key);
		return it != counts.end() && it->second > 0;
	}

	// Can be picked up: an item (not a book flagged can't-take, a light only if carryable)
	bool Takeable(RE::TESObjectREFR* a_ref)
	{
		const auto type = Rules::TypeOf(a_ref);
		if (type == 0 || type == Rules::kDoor || type == Rules::kContainer) {
			return false;
		}
		const auto book = a_ref->GetBaseObject()->As<RE::TESObjectBOOK>();
		return !book || book->CanBeTaken();
	}

	// a_all: every set condition must hold ("requires"); otherwise: does any set condition hold ("not")
	bool Check(const Rules::Conditions& a_conditions, RE::TESObjectREFR* a_ref, Inventory& a_inventory, bool a_all)
	{
		const auto actor = a_ref->As<RE::Actor>();
		const auto state = actor ? actor->AsActorState() : nullptr;
		const auto base = a_ref->GetBaseObject();
		const auto book = base ? base->As<RE::TESObjectBOOK>() : nullptr;
		const auto flag = [](const std::optional<bool>& a_wanted, auto&& a_actual) -> std::optional<bool> {
			if (!a_wanted) {
				return std::nullopt;
			}
			return a_actual() == *a_wanted;
		};
		const auto anyOf = [](const auto& a_list, auto&& a_holds) -> std::optional<bool> {
			if (a_list.empty()) {
				return std::nullopt;
			}
			return std::ranges::any_of(a_list, a_holds);
		};

		// cheapest first; each yields nullopt (not set), true (holds) or false. NPC conditions are only ever set in NPC
		// rules and object conditions in object rules (the reader refuses the rest), so actor is set where it's used.
		const std::function<std::optional<bool>()> tests[] = {
			[&] { return a_conditions.types ? std::optional<bool>((Rules::TypeOf(a_ref) & a_conditions.types) != 0) : std::nullopt; },
			[&] { return anyOf(a_conditions.bases, [&](RE::TESBoundObject* a_base) { return base == a_base; }); },
			[&] { return flag(a_conditions.read, [&] { return book && book->IsRead(); }); },
			[&] { return flag(a_conditions.spellTome, [&] { return book && book->TeachesSpell(); }); },
			[&] { return flag(a_conditions.takeable, [&] { return Takeable(a_ref); }); },
			[&] { return flag(a_conditions.locked, [&] { return a_ref->IsLocked(); }); },
			[&] { return flag(a_conditions.requiresKey, [&] { return a_ref->IsLocked() && a_ref->GetLockLevel() == RE::LOCK_LEVEL::kRequiresKey; }); },
			[&] { return flag(a_conditions.playerSneaking, [&] { return RE::PlayerCharacter::GetSingleton()->IsSneaking(); }); },
			[&] { return flag(a_conditions.interior, [&] { const auto cell = a_ref->GetParentCell(); return cell && cell->IsInteriorCell(); }); },
			[&] { return flag(a_conditions.alive, [&] { return !actor->IsDead(); }); },
			[&] { return flag(a_conditions.teammate, [&] { return actor->IsPlayerTeammate(); }); },
			[&] { return flag(a_conditions.essential, [&] { return actor->IsEssential(); }); },
			[&] { return flag(a_conditions.protectedFlag, [&] { return actor->IsProtected(); }); },
			[&] { return flag(a_conditions.bleedingOut, [&] { return state && state->IsBleedingOut(); }); },
			[&] { return flag(a_conditions.unconscious, [&] { return state && state->IsUnconscious(); }); },
			[&] { return flag(a_conditions.sitting, [&] { return state && state->IsSitting(); }); },
			[&] { return flag(a_conditions.sleeping, [&] { return state && state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping; }); },
			[&] { return flag(a_conditions.inCombat, [&] { return actor->IsInCombat(); }); },
			[&] { return anyOf(a_conditions.npcs, [&](RE::TESNPC* a_npc) { return actor->GetActorBase() == a_npc || actor->GetTemplateBase() == a_npc; }); },
			[&] { return anyOf(a_conditions.races, [&](RE::TESRace* a_race) { return actor->GetRace() == a_race; }); },
			[&] { return anyOf(a_conditions.keywords, [&](RE::BGSKeyword* a_keyword) {
					  const auto race = actor ? actor->GetRace() : nullptr;
					  return a_ref->HasKeyword(a_keyword) || (race && race->HasKeyword(a_keyword));
				  }); },
			[&] { return anyOf(a_conditions.factions, [&](const Rules::FactionCondition& a_faction) {
					  return actor->IsInFaction(a_faction.faction) &&
				             (!a_faction.minRank || actor->GetFactionRank(a_faction.faction, actor->IsPlayerRef()) >= *a_faction.minRank);
				  }); },
			[&] { return flag(a_conditions.playerHasKey, [&] { return PlayerHasKey(a_ref); }); },
			[&] { return anyOf(a_conditions.destinationKeywords, [&](RE::BGSKeyword* a_keyword) {
					  const auto location = DestinationOf(a_ref);
					  return location && location->HasKeyword(a_keyword);
				  }); },
			[&] { return flag(a_conditions.owned, [&] { return a_ref->IsCrimeToActivate(); }); },
			[&] { return anyOf(a_conditions.items, [&](RE::TESBoundObject* a_item) { return a_inventory.Carries(a_item); }); },
			[&] { return anyOf(a_conditions.topics, [&](RE::TESTopic* a_topic) { return Dialogue::Offers(actor, a_topic); }); },
		};

		for (const auto& test : tests) {
			if (const auto result = test()) {
				if (a_all && !*result) {
					return false;
				}
				if (!a_all && *result) {
					return true;
				}
			}
		}
		return a_all;
	}

	// The rule is for this kind of reference: NPC rules for actors, object rules for everything else
	bool ForKind(const Rules::Rule& a_rule, bool a_isActor)
	{
		return (a_rule.target == Rules::Target::kNPC) == a_isActor;
	}
}

namespace Rules
{
	std::string Translate(const std::string& a_key, std::string_view a_fallback)
	{
		const auto found = translations.find(a_key);
		return found != translations.end() ? found->second : std::string{ a_fallback };
	}

	std::string LabelText(const std::string& a_label)
	{
		std::string translated;
		if (!a_label.starts_with('$')) {
			return a_label;
		}
		if (const auto found = translations.find(a_label); found != translations.end()) {
			return found->second;
		}
		if (SKSE::Translation::Translate(a_label, translated)) {  // a key from a file SKSE loaded itself
			return translated;
		}
		static std::set<std::string> warned;
		if (warned.insert(a_label).second) {
			logger::warn("No translation for label \"{}\" (Interface/Translations), shown as is", a_label);
		}
		return a_label;
	}

	const std::string& Action::Text() const
	{
		if (!text) {
			text = LabelText(label);
		}
		return *text;
	}

	void Load()
	{
		rules.clear();
		LoadTranslations("ModifierKeyFramework");  // the framework's own words (relationship ranks)
		std::error_code ec;
		if (!std::filesystem::is_directory(RULES_DIR, ec)) {
			logger::info("No rule folder ({}), nothing to do", RULES_DIR);
			return;
		}
		// Files in name order (case-insensitive), so equal priorities always resolve the same way:
		// by file name, then by position in the file (the sort below is stable)
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(RULES_DIR, ec)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				files.push_back(entry.path());
			}
		}
		const auto lower = [](const std::filesystem::path& a_path) {
			auto name = a_path.filename().string();
			std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return name;
		};
		std::ranges::sort(files, {}, lower);
		for (const auto& file : files) {
			LoadFile(file);
		}
		std::ranges::stable_sort(rules, std::ranges::greater{}, &Rule::priority);
		objectRules = std::ranges::any_of(rules, [](const Rule& r) { return r.target == Target::kObject; });

		// Rules from different files sharing a priority: fine, but say who wins a tie
		for (auto first = rules.begin(); first != rules.end();) {
			const auto last = std::ranges::find_if(first, rules.end(), [&](const Rule& r) { return r.priority != first->priority; });
			if (std::ranges::any_of(first, last, [&](const Rule& r) { return r.file != first->file; })) {
				std::string order;
				for (auto it = first; it != last; ++it) {
					order += std::format("{}{} / {}", order.empty() ? "" : ", ", it->file, it->id);
				}
				logger::info("Priority {} is shared by several mods; when more than one matches, the first wins: {}", first->priority, order);
			}
			first = last;
		}
		logger::info("{} rule(s) active", rules.size());
	}

	const Rule* Match(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref || rules.empty()) {
			return nullptr;
		}
		const auto actor = a_ref->As<RE::Actor>();
		if (!actor && !objectRules) {
			return nullptr;
		}
		Inventory inventory(actor);
		for (const auto& rule : rules) {
			if (rule.enabled.value && ForKind(rule, actor != nullptr) && Check(rule.requirements, a_ref, inventory, true) &&
				!Check(rule.exclusions, a_ref, inventory, false)) {
				return &rule;
			}
		}
		return nullptr;
	}

	void ResetEnabled()
	{
		std::size_t changed = 0;
		for (auto& rule : rules) {
			if (rule.enabled.value.exchange(rule.enabledInFile) != rule.enabledInFile) {
				++changed;
			}
		}
		if (changed) {
			logger::info("{} rule switch(es) back to their file's value for the new game or load", changed);
		}
	}

	bool HasObjectRules()
	{
		return objectRules;
	}

	std::uint32_t TypeOf(RE::TESObjectREFR* a_ref)
	{
		const auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base) {
			return 0;
		}
		switch (base->GetFormType()) {
		case RE::FormType::Book:
			return kBook;
		case RE::FormType::Weapon:
			return kWeapon;
		case RE::FormType::Armor:
			return kArmor;
		case RE::FormType::AlchemyItem:
			{
				const auto item = base->As<RE::AlchemyItem>();
				return item->IsPoison() ? kPoison : item->IsFood() ? kFood : kPotion;
			}
		case RE::FormType::Ingredient:
			return kIngredient;
		case RE::FormType::Scroll:
			return kScroll;
		case RE::FormType::Light:
			return base->As<RE::TESObjectLIGH>()->CanBeCarried() ? kTorch : 0;
		case RE::FormType::Ammo:
			return kAmmo;
		case RE::FormType::Misc:
			return kMisc;
		case RE::FormType::KeyMaster:
			return kKey;
		case RE::FormType::SoulGem:
			return kSoulGem;
		case RE::FormType::Door:
			return kDoor;
		case RE::FormType::Container:
			return kContainer;
		default:
			return 0;
		}
	}

	std::vector<const Action*> Alternates(const Rule& a_rule, RE::TESObjectREFR* a_ref)
	{
		const auto                 actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
		std::vector<const Action*> available;
		for (const auto& action : a_rule.alternates) {
			if (action.topics.empty() || std::ranges::any_of(action.topics, [&](RE::TESTopic* a_topic) { return Dialogue::Offers(actor, a_topic); })) {
				available.push_back(&action);
			}
		}
		return available;
	}

	Rule* Find(std::string_view a_file, std::string_view a_id)
	{
		const auto equalNoCase = [](std::string_view a_left, std::string_view a_right) {
			return std::ranges::equal(a_left, a_right, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
		};
		for (auto& rule : rules) {
			const std::string_view file = rule.file;
			const auto             stem = file.substr(0, file.size() - 5);  // without ".json"
			if ((equalNoCase(file, a_file) || equalNoCase(stem, a_file)) && equalNoCase(rule.id, a_id)) {
				return &rule;
			}
		}
		return nullptr;
	}

	void ReportConflicts(RE::TESObjectREFR* a_ref, const Rule* a_winner)
	{
		static std::set<std::pair<const Rule*, const Rule*>> reported;

		const auto actor = a_ref->As<RE::Actor>();
		Inventory  inventory(actor);
		bool      after = false;
		for (const auto& rule : rules) {
			if (&rule == a_winner) {
				after = true;
				continue;
			}
			if (!after || !rule.enabled.value || !ForKind(rule, actor != nullptr) || rule.file == a_winner->file || reported.contains({ a_winner, &rule })) {
				continue;
			}
			if (Check(rule.requirements, a_ref, inventory, true) && !Check(rule.exclusions, a_ref, inventory, false)) {
				reported.insert({ a_winner, &rule });
				const char* name = a_ref->GetDisplayFullName();
				logger::info("Conflict on {} ({:08X}): {} / {} (priority {}) is used; {} / {} (priority {}) also matches and is ignored",
					name ? name : "?", a_ref->GetFormID(), a_winner->file, a_winner->id, a_winner->priority, rule.file, rule.id, rule.priority);
			}
		}
	}
}
