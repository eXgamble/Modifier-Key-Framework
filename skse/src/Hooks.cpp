#include "Hooks.h"

#include "Hud.h"
#include "Input.h"
#include "Rules.h"
#include "Settings.h"

namespace
{
	// The action Activate runs right now: the first modifier action while the modifier key is held
	// (if the rule has one), otherwise the primary. index: 0 = primary, 1-3 = modifier action (slot).
	struct Current
	{
		const Rules::Action& action;
		std::int32_t         index;
	};

	Current CurrentAction(const Rules::Rule& a_rule)
	{
		if (!a_rule.alternates.empty() && Input::IsModifierHeld()) {
			return { a_rule.alternates.front(), 1 };
		}
		return { a_rule.primary, 0 };
	}

	// The first line of the game's own prompt ("Search" of "Search\nWolf")
	std::string VanillaVerb(const RE::BSString& a_text)
	{
		const std::string_view text{ a_text.c_str() ? a_text.c_str() : "" };
		return std::string{ text.substr(0, text.find('\n')) };
	}

	// The activation prompt: "<label>\n<name>", the same two lines as vanilla's "Talk\nLydia".
	// When the matched rule also has an alternate action, the primary label gets the marker from
	// the ini ("Give Potion +"), so players can tell a second action exists; while the modifier
	// key is held the prompt shows the alternate label instead, without the marker.
	// A primary without a label keeps the game's own first line ("Search" on a dead body, "Talk", ...).
	struct GetActivateText
	{
		static bool thunk(RE::TESNPC* a_this, RE::TESObjectREFR* a_activator, RE::BSString& a_dst)
		{
			const bool result = func(a_this, a_activator, a_dst);

			auto actor = a_activator ? a_activator->As<RE::Actor>() : nullptr;
			const auto rule = Rules::Match(actor);
			if (!rule) {
				Hud::ClearPrompt();
			} else {
				const auto [action, index] = CurrentAction(*rule);
				std::string label = action.label.empty() ? VanillaVerb(a_dst) : action.Text();
				// a modifier action exists: the HUD draws the modifier key's button after the label, or
				// (ini, or no HUD) the text marker goes on the label itself
				const bool marker = !rule->alternates.empty() && index == 0;
				const bool iconMarker = marker && Settings::ModifierIcon() && Hud::Ready();
				if (marker && !iconMarker && !Settings::AlternateMarker().empty()) {
					label += " " + Settings::AlternateMarker();
				}
				// modifier held with more than one modifier action: the HUD shows the rest above
				std::vector<std::string> extra;
				if (index == 1) {
					for (std::size_t slot = 1; slot < rule->alternates.size(); ++slot) {
						extra.push_back(rule->alternates[slot].Text());
					}
				}
				Hud::SetPrompt(actor, iconMarker, std::move(extra));
				const char* name = actor->GetDisplayFullName();
				const std::string text = std::format("{}\n{}", label, name ? name : "");
				a_dst = text.c_str();
			}
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Send a rule's SKSE mod event: the mod that owns the rule reacts in Papyrus
	// (RegisterForModEvent). Event(String eventName, String ruleId, Float action, Form target):
	// the rule's id, which action (0.0 primary, 1.0-3.0 the modifier actions in slot order, so 1.0 is
	// the single alternate as before), and the NPC as sender.
	// Queued as a task, by handle, so it runs on the game thread.
	void SendRuleEvent(const Rules::Rule& a_rule, const Rules::Action& a_action, std::int32_t a_index, RE::TESObjectREFR* a_target)
	{
		const float isAlternate = static_cast<float>(a_index);
		const char* name = a_target->GetDisplayFullName();
		logger::info("{} ({:08X}): {} / {} sends \"{}\" ({})", name ? name : "?", a_target->GetFormID(), a_rule.file, a_rule.id, a_action.event, a_index == 0 ? "primary" : std::format("modifier action {}", a_index));
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
					if (const auto [action, index] = CurrentAction(*rule); !action.event.empty()) {
						SendRuleEvent(*rule, action, index, a_targetRef);
						// "Not activated": with true, the game's activation code goes on to
						// mount a horse (riding isn't part of this function, unlike dialogue)
						return false;
					}
				}
			}
			return func(a_this, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Slots 2 and 3 (X and Y on a controller): game controls from the ini (default Ready Weapon and
	// Jump). While the modifier key is held over a target whose rule has that slot, pressing the control
	// runs the action and the game doesn't do its normal job (draw the weapon, jump). Hooked on the
	// handlers' CanProcess, so the player's own key bindings (keyboard or gamepad) apply as they are.
	bool TakeSlot(RE::InputEvent* a_event, const RE::BSFixedString& a_handlerControl)
	{
		// every handler is asked about every event: each only takes its own control
		const auto button = a_event ? a_event->AsButtonEvent() : nullptr;
		if (!button || button->QUserEvent() != a_handlerControl || !Input::IsModifierHeld()) {
			return false;
		}
		const auto& control = button->QUserEvent();
		std::size_t alternate;
		if (control == Settings::SlotControl(2)) {
			alternate = 1;
		} else if (control == Settings::SlotControl(3)) {
			alternate = 2;
		} else {
			return false;  // not a slot control (ini)
		}
		const auto pick = RE::CrosshairPickData::GetSingleton();
		const auto target = pick ? pick->GetActiveTarget().get() : nullptr;
		const auto actor = target ? target->As<RE::Actor>() : nullptr;
		const auto rule = Rules::Match(actor);
		if (!rule || rule->alternates.size() <= alternate) {
			return false;
		}
		if (button->IsDown()) {
			Rules::ReportConflicts(actor, rule);
			if (const auto& action = rule->alternates[alternate]; !action.event.empty()) {
				SendRuleEvent(*rule, action, static_cast<std::int32_t>(alternate) + 1, target.get());
			}
		}
		return true;  // the whole press (down, held, up) belongs to the action
	}

	// One per game handler a slot control can belong to (Settings::SupportedSlotControls)
	template <class Handler, RE::BSFixedString RE::UserEvents::*Control>
	struct SlotControl
	{
		static bool thunk(RE::PlayerInputHandler* a_this, RE::InputEvent* a_event)
		{
			const auto events = RE::UserEvents::GetSingleton();
			if (events && TakeSlot(a_event, events->*Control)) {
				return false;
			}
			return func(a_this, a_event);
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ Handler::VTABLE[0] };
			func = REL::Relocation<decltype(thunk)>{ vtbl.write_vfunc(0x1, thunk) };
		}
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

			// the controls modifier actions 2 and 3 can use (ini sSlot2Control / sSlot3Control)
			SlotControl<RE::ReadyWeaponHandler, &RE::UserEvents::readyWeapon>::Install();
			SlotControl<RE::JumpHandler, &RE::UserEvents::jump>::Install();
			SlotControl<RE::SneakHandler, &RE::UserEvents::sneak>::Install();
			SlotControl<RE::ShoutHandler, &RE::UserEvents::shout>::Install();
			SlotControl<RE::AutoMoveHandler, &RE::UserEvents::autoMove>::Install();
			SlotControl<RE::ToggleRunHandler, &RE::UserEvents::toggleRun>::Install();
			logger::info("Hooks installed: controls for modifier actions 2 and 3");
		});
	}
}
