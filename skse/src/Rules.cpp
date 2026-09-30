#include "Rules.h"

#include <nlohmann/json.hpp>

namespace
{
	constexpr auto RULES_DIR = "Data/SKSE/Plugins/eXModifierFramework";

	std::vector<Rules::Rule> rules;  // sorted by priority, highest first

	// "Plugin.esp|0x807" -> the form (local ID in that plugin, light plugins included).
	// On failure, a_why says which part didn't resolve.
	RE::TESBoundObject* ResolveForm(const std::string& a_ref, std::string& a_why)
	{
		const auto bar = a_ref.find('|');
		if (bar == std::string::npos) {
			a_why = "expected \"Plugin.esp|0xID\"";
			return nullptr;
		}
		const auto plugin = a_ref.substr(0, bar);
		RE::FormID localID = 0;
		try {
			localID = static_cast<RE::FormID>(std::stoul(a_ref.substr(bar + 1), nullptr, 16));
		} catch (...) {
			a_why = std::format("\"{}\" is not a hex form ID", a_ref.substr(bar + 1));
			return nullptr;
		}
		const auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler->LookupModByName(plugin)) {
			a_why = std::format("plugin \"{}\" is not loaded", plugin);
			return nullptr;
		}
		const auto form = dataHandler->LookupForm(localID, plugin);
		if (!form) {
			a_why = std::format("plugin \"{}\" has no form {:06X}", plugin, localID);
			return nullptr;
		}
		const auto object = form->As<RE::TESBoundObject>();
		if (!object) {
			a_why = std::format("form {:08X} in \"{}\" is not an item (form type {})", form->GetFormID(), plugin, static_cast<int>(form->GetFormType()));
		}
		return object;
	}

	std::optional<Rules::Action> ReadAction(const nlohmann::json& a_json)
	{
		if (!a_json.is_object() || !a_json.contains("label")) {
			return std::nullopt;
		}
		return Rules::Action{ a_json.value("label", ""), a_json.value("event", "") };
	}

	void LoadFile(const std::filesystem::path& a_path)
	{
		const auto file = a_path.filename().string();
		nlohmann::json root;
		try {
			std::ifstream in(a_path);
			root = nlohmann::json::parse(in, nullptr, true, true);  // allow comments
		} catch (const std::exception& e) {
			logger::error("{}: not valid JSON ({})", file, e.what());
			return;
		}

		std::size_t loaded = 0;
		for (const auto& entry : root.value("rules", nlohmann::json::array())) {
			Rules::Rule rule;
			rule.file = file;
			rule.id = entry.value("id", "?");
			rule.priority = entry.value("priority", 0);

			if (entry.value("target", "npc") != "npc") {
				logger::warn("{} / {}: only \"target\": \"npc\" is supported, skipped", file, rule.id);
				continue;
			}
			auto primary = ReadAction(entry.value("primary", nlohmann::json{}));
			if (!primary) {
				logger::warn("{} / {}: missing \"primary\" with a \"label\", skipped", file, rule.id);
				continue;
			}
			rule.primary = std::move(*primary);
			rule.alternate = ReadAction(entry.value("alternate", nlohmann::json{}));

			const auto requirements = entry.value("requires", nlohmann::json::object());
			if (requirements.contains("item")) {
				const auto ref = requirements["item"].get<std::string>();
				std::string why;
				rule.requiredItem = ResolveForm(ref, why);
				if (!rule.requiredItem) {
					logger::warn("{} / {}: item \"{}\" not usable: {}, skipped", file, rule.id, ref, why);
					continue;
				}
			}
			if (requirements.contains("alive")) {
				rule.requiredAlive = requirements["alive"].get<bool>();
			}

			rules.push_back(std::move(rule));
			++loaded;
		}
		logger::info("{}: {} rule(s) loaded", file, loaded);
	}
}

namespace Rules
{
	void Load()
	{
		rules.clear();
		std::error_code ec;
		if (!std::filesystem::is_directory(RULES_DIR, ec)) {
			logger::info("No rule folder ({}), nothing to do", RULES_DIR);
			return;
		}
		for (const auto& entry : std::filesystem::directory_iterator(RULES_DIR, ec)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				LoadFile(entry.path());
			}
		}
		std::ranges::stable_sort(rules, std::ranges::greater{}, &Rule::priority);
		logger::info("{} rule(s) active", rules.size());
	}

	const Rule* Match(RE::Actor* a_actor)
	{
		if (!a_actor || rules.empty()) {
			return nullptr;
		}
		const bool dead = a_actor->IsDead();

		// Inventory is only counted once, and only if some rule needs an item
		std::optional<RE::TESObjectREFR::InventoryCountMap> counts;
		const auto carries = [&](RE::TESBoundObject* a_item) {
			if (!counts) {
				counts = a_actor->GetInventoryCounts([](RE::TESBoundObject& a_object) {
					return std::ranges::any_of(rules, [&](const Rule& r) { return r.requiredItem == &a_object; });
				});
			}
			const auto it = counts->find(a_item);
			return it != counts->end() && it->second > 0;
		};

		for (const auto& rule : rules) {
			if (rule.requiredAlive && *rule.requiredAlive == dead) {
				continue;
			}
			if (rule.requiredItem && !carries(rule.requiredItem)) {
				continue;
			}
			return &rule;
		}
		return nullptr;
	}
}
