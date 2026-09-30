#include "Rules.h"

#include <nlohmann/json.hpp>

namespace
{
	using json = nlohmann::json;

	constexpr auto RULES_DIR = "Data/SKSE/Plugins/ModifierKeyFramework";

	std::vector<Rules::Rule> rules;  // sorted by priority, highest first

	std::string currentRule;  // "file / id" of the rule being read, for log messages

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
	Rules::Conditions ReadConditions(const json& a_block, std::string_view a_blockName, bool a_isRequirement)
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
		};

		for (const auto& [key, value] : a_block.items()) {
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

	std::optional<Rules::Action> ReadAction(const json& a_json)
	{
		if (!a_json.is_object() || !a_json.contains("label")) {
			return std::nullopt;
		}
		Rules::Action action;
		action.label = a_json.value("label", "");
		action.event = a_json.value("event", "");
		return action;
	}

	// Interface/Translations/<name>_<LANGUAGE>.txt into the game's translation table (SKSE itself
	// only loads files named after a plugin; a framework mod may have none). Each name once.
	void LoadTranslations(const std::string& a_name)
	{
		static std::set<std::string> loaded;
		if (loaded.insert(a_name).second) {
			SKSE::Translation::ParseTranslation(a_name);
		}
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

		LoadTranslations(root.value("translations", a_path.stem().string()));

		std::size_t loaded = 0;
		for (const auto& entry : root.value("rules", json::array())) {
			Rules::Rule rule;
			rule.file = file;
			rule.id = entry.value("id", "?");
			currentRule = std::format("{} / {}", file, rule.id);
			try {
				rule.priority = entry.value("priority", 0);
				if (entry.value("target", "npc") != "npc") {
					throw RuleError{ "only \"target\": \"npc\" is supported" };
				}
				auto primary = ReadAction(entry.value("primary", json{}));
				if (!primary) {
					throw RuleError{ "missing \"primary\" with a \"label\"" };
				}
				rule.primary = std::move(*primary);
				rule.alternate = ReadAction(entry.value("alternate", json{}));
				if (entry.contains("requires")) {
					rule.requirements = ReadConditions(entry["requires"], "requires", true);
				}
				if (entry.contains("not")) {
					rule.exclusions = ReadConditions(entry["not"], "not", false);
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

	// a_all: every set condition must hold ("requires"); otherwise: does any set condition hold ("not")
	bool Check(const Rules::Conditions& a_conditions, RE::Actor* a_actor, Inventory& a_inventory, bool a_all)
	{
		const auto state = a_actor->AsActorState();
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

		// cheapest first; each yields nullopt (not set), true (holds) or false
		const std::function<std::optional<bool>()> tests[] = {
			[&] { return flag(a_conditions.alive, [&] { return !a_actor->IsDead(); }); },
			[&] { return flag(a_conditions.teammate, [&] { return a_actor->IsPlayerTeammate(); }); },
			[&] { return flag(a_conditions.essential, [&] { return a_actor->IsEssential(); }); },
			[&] { return flag(a_conditions.protectedFlag, [&] { return a_actor->IsProtected(); }); },
			[&] { return flag(a_conditions.bleedingOut, [&] { return state && state->IsBleedingOut(); }); },
			[&] { return flag(a_conditions.unconscious, [&] { return state && state->IsUnconscious(); }); },
			[&] { return flag(a_conditions.sitting, [&] { return state && state->IsSitting(); }); },
			[&] { return flag(a_conditions.sleeping, [&] { return state && state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping; }); },
			[&] { return flag(a_conditions.inCombat, [&] { return a_actor->IsInCombat(); }); },
			[&] { return anyOf(a_conditions.npcs, [&](RE::TESNPC* a_npc) { return a_actor->GetActorBase() == a_npc || a_actor->GetTemplateBase() == a_npc; }); },
			[&] { return anyOf(a_conditions.races, [&](RE::TESRace* a_race) { return a_actor->GetRace() == a_race; }); },
			[&] { return anyOf(a_conditions.keywords, [&](RE::BGSKeyword* a_keyword) {
					  const auto race = a_actor->GetRace();
					  return a_actor->HasKeyword(a_keyword) || (race && race->HasKeyword(a_keyword));
				  }); },
			[&] { return anyOf(a_conditions.factions, [&](const Rules::FactionCondition& a_faction) {
					  return a_actor->IsInFaction(a_faction.faction) &&
				             (!a_faction.minRank || a_actor->GetFactionRank(a_faction.faction, a_actor->IsPlayerRef()) >= *a_faction.minRank);
				  }); },
			[&] { return anyOf(a_conditions.items, [&](RE::TESBoundObject* a_item) { return a_inventory.Carries(a_item); }); },
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
}

namespace Rules
{
	const std::string& Action::Text() const
	{
		if (!text) {
			std::string translated;
			if (!label.starts_with('$')) {
				text = label;
			} else if (SKSE::Translation::Translate(label, translated)) {
				text = std::move(translated);
			} else {
				logger::warn("No translation for label \"{}\" (Interface/Translations), shown as is", label);
				text = label;
			}
		}
		return *text;
	}

	void Load()
	{
		rules.clear();
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

	const Rule* Match(RE::Actor* a_actor)
	{
		if (!a_actor || rules.empty()) {
			return nullptr;
		}
		Inventory inventory(a_actor);
		for (const auto& rule : rules) {
			if (Check(rule.requirements, a_actor, inventory, true) && !Check(rule.exclusions, a_actor, inventory, false)) {
				return &rule;
			}
		}
		return nullptr;
	}

	void ReportConflicts(RE::Actor* a_actor, const Rule* a_winner)
	{
		static std::set<std::pair<const Rule*, const Rule*>> reported;

		Inventory inventory(a_actor);
		bool      after = false;
		for (const auto& rule : rules) {
			if (&rule == a_winner) {
				after = true;
				continue;
			}
			if (!after || rule.file == a_winner->file || reported.contains({ a_winner, &rule })) {
				continue;
			}
			if (Check(rule.requirements, a_actor, inventory, true) && !Check(rule.exclusions, a_actor, inventory, false)) {
				reported.insert({ a_winner, &rule });
				const char* name = a_actor->GetDisplayFullName();
				logger::info("Conflict on {} ({:08X}): {} / {} (priority {}) is used; {} / {} (priority {}) also matches and is ignored",
					name ? name : "?", a_actor->GetFormID(), a_winner->file, a_winner->id, a_winner->priority, rule.file, rule.id, rule.priority);
			}
		}
	}
}
