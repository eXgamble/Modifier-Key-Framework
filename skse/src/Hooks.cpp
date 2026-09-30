#include "Hooks.h"

#include "Input.h"
#include "Rules.h"
#include "Settings.h"

namespace
{
	// The action a rule runs right now: its alternate while the modifier key is held (if it has one),
	// otherwise its primary.
	const Rules::Action& CurrentAction(const Rules::Rule& a_rule)
	{
		return (a_rule.alternate && Input::IsModifierHeld()) ? *a_rule.alternate : a_rule.primary;
	}

	// The activation prompt: "<label>\n<name>", the same two lines as vanilla's "Talk\nLydia".
	// When the matched rule also has an alternate action, the primary label gets the marker from
	// the ini ("Give Potion +"), so players can tell a second action exists; while the modifier
	// key is held the prompt shows the alternate label instead, without the marker.
	struct GetActivateText
	{
		static bool thunk(RE::TESNPC* a_this, RE::TESObjectREFR* a_activator, RE::BSString& a_dst)
		{
			const bool result = func(a_this, a_activator, a_dst);

			auto actor = a_activator ? a_activator->As<RE::Actor>() : nullptr;
			if (const auto rule = Rules::Match(actor)) {
				const auto& action = CurrentAction(*rule);
				std::string label = action.Text();
				if (rule->alternate && &action == &rule->primary && !Settings::AlternateMarker().empty()) {
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

	// Send a rule's SKSE mod event: the mod that owns the rule reacts in Papyrus
	// (RegisterForModEvent). Event(String eventName, String ruleId, Float isAlternate, Form target):
	// the rule's id, 1.0 for the alternate action (0.0 primary), and the NPC as sender.
	// Queued as a task, by handle, so it runs on the game thread.
	void SendRuleEvent(const Rules::Rule& a_rule, const Rules::Action& a_action, RE::TESObjectREFR* a_target)
	{
		const float isAlternate = &a_action == &a_rule.primary ? 0.0f : 1.0f;
		const char* name = a_target->GetDisplayFullName();
		logger::info("{} ({:08X}): {} / {} sends \"{}\" ({})", name ? name : "?", a_target->GetFormID(), a_rule.file, a_rule.id, a_action.event, isAlternate != 0.0f ? "alternate" : "primary");
		SKSE::GetTaskInterface()->AddTask([event = a_action.event, id = a_rule.id, isAlternate, handle = a_target->GetHandle()]() {
			auto ref = handle.get();
			if (!ref) {
				return;
			}
			SKSE::ModCallbackEvent modEvent{ event, id, isAlternate, ref.get() };
			SKSE::GetModCallbackEventSource()->SendEvent(&modEvent);
		});
	}

	// The player activating an NPC a rule matches: run the rule's action (its mod event) instead of
	// the vanilla one (dialogue). Anyone else activating, or no matching rule: vanilla.
	struct Activate
	{
		static bool thunk(RE::TESNPC* a_this, RE::TESObjectREFR* a_targetRef, RE::TESObjectREFR* a_activatorRef, std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_targetCount)
		{
			if (a_activatorRef && a_activatorRef->IsPlayerRef() && a_targetRef) {
				auto actor = a_targetRef->As<RE::Actor>();
				if (const auto rule = Rules::Match(actor)) {
					Rules::ReportConflicts(actor, rule);
					if (const auto& action = CurrentAction(*rule); !action.event.empty()) {
						SendRuleEvent(*rule, action, a_targetRef);
						return true;
					}
				}
			}
			return func(a_this, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);
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
			Activate::func = REL::Relocation<decltype(Activate::thunk)>{ npcVtbl.write_vfunc(0x37, Activate::thunk) };
			logger::info("Hook installed: TESNPC::Activate (0x37)");
		});
	}
}
