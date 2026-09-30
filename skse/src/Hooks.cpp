#include "Hooks.h"

#include "Rules.h"
#include "Settings.h"

namespace
{
	// The activation prompt: "<label>\n<name>", the same two lines as vanilla's "Talk\nLydia".
	// When the matched rule also has an alternate action, the primary label gets the marker from
	// the ini ("Give Potion +"), so players can tell a second action exists.
	struct GetActivateText
	{
		static bool thunk(RE::TESNPC* a_this, RE::TESObjectREFR* a_activator, RE::BSString& a_dst)
		{
			const bool result = func(a_this, a_activator, a_dst);

			auto actor = a_activator ? a_activator->As<RE::Actor>() : nullptr;
			if (const auto rule = Rules::Match(actor)) {
				std::string label = rule->primary.label;
				if (rule->alternate) {
					label += " " + Settings::AlternateMarker();
				}
				const char* name = actor->GetDisplayFullName();
				const std::string text = std::format("{}\n{}", label, name ? name : "");
				a_dst = text.c_str();
			}
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace Hooks
{
	void Install()
	{
		static std::once_flag once;
		std::call_once(once, []() {
			REL::Relocation<std::uintptr_t> npcVtbl{ RE::VTABLE_TESNPC[0] };
			GetActivateText::func = REL::Relocation<decltype(GetActivateText::thunk)>{ npcVtbl.write_vfunc(0x4C, GetActivateText::thunk) };
			logger::info("Hook installed: TESNPC::GetActivateText (0x4C)");
		});
	}
}
