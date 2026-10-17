#include "Hooks.h"

#include "Dialogue.h"
#include "Hud.h"
#include "Input.h"
#include "Relationship.h"
#include "Rules.h"
#include "Settings.h"

namespace
{
	// Set while the framework runs a vanilla activation itself (a slot action that "activate"s): the hooked Activate
	// passes it straight on instead of choosing an action again.
	thread_local bool runningVanilla = false;

	// The action Activate runs right now: the first modifier action the target offers while the modifier key is held,
	// otherwise the primary. index: the action's place in the rule, 0 = primary, 1-3 = modifier action, the same number
	// the mod event carries whichever slot the action ends up on.
	struct Current
	{
		const Rules::Action& action;
		std::int32_t         index;
	};

	std::int32_t IndexOf(const Rules::Rule& a_rule, const Rules::Action& a_action)
	{
		return &a_action == &a_rule.primary ? 0 : static_cast<std::int32_t>(&a_action - a_rule.alternates.data()) + 1;
	}

	Current CurrentAction(const Rules::Rule& a_rule, const std::vector<const Rules::Action*>& a_alternates)
	{
		if (!a_alternates.empty() && Input::IsModifierHeld()) {
			return { *a_alternates.front(), IndexOf(a_rule, *a_alternates.front()) };
		}
		return { a_rule.primary, 0 };
	}

	// The first line of the game's own prompt ("Search" of "Search\nWolf"). A prompt that is only the target's name (the
	// game offers no action on it right now) has no verb: empty, never the name as the action word.
	std::string VanillaVerb(const RE::BSString& a_text, RE::TESObjectREFR* a_target)
	{
		const std::string_view text{ a_text.c_str() ? a_text.c_str() : "" };
		const auto newline = text.find('\n');
		if (newline == std::string_view::npos) {
			const char* name = a_target->GetDisplayFullName();
			if (name && text == name) {
				return {};
			}
		}
		return std::string{ text.substr(0, newline) };
	}

	std::string GameString(const char* a_setting)
	{
		const auto setting = RE::GameSettingCollection::GetSingleton()->GetSetting(a_setting);
		const char* text = setting ? setting->GetString() : nullptr;
		return text ? text : "";
	}

	// Taking it (and so equipping or using it) is a crime: the label shows red, like the game's own "Steal"
	bool IsTheft(const Rules::Action& a_action, RE::TESObjectREFR* a_target)
	{
		switch (a_action.act) {
		case Rules::Do::kTake:
		case Rules::Do::kEquip:
		case Rules::Do::kEquipAndDraw:
		case Rules::Do::kUse:
			return !a_target->As<RE::Actor>() && a_target->IsCrimeToActivate();
		default:
			return false;
		}
	}

	// An action's first prompt line. Without a label: "take" uses the game's "Take" / "Steal", everything else keeps the
	// game's own first line ("Search" on a dead body, "Talk", "Read", ...).
	std::string LabelOf(const Rules::Action& a_action, RE::TESObjectREFR* a_target, const RE::BSString& a_vanilla)
	{
		const bool theft = IsTheft(a_action, a_target);
		if (a_action.label.empty()) {
			if (a_action.act == Rules::Do::kTake) {
				return GameString(theft ? "sSteal" : "sTake");
			}
			return VanillaVerb(a_vanilla, a_target);
		}
		return theft ? std::format("<font color='#FF0000'>{}</font>", a_action.Text()) : a_action.Text();
	}

	// The activation prompt: "<label>\n<the game's own second line>" (the name, plus whatever the game shows there).
	// When the matched rule also has a modifier action, the HUD draws the modifier key's button after the primary label
	// (or the ini's text marker goes on it); while the key is held the prompt shows the modifier actions instead.
	void Prompt(RE::TESObjectREFR* a_target, RE::BSString& a_dst)
	{
		const auto rule = Rules::Match(a_target);
		if (!rule) {
			Hud::ClearPrompt();
			return;
		}
		const auto alternates = Rules::Alternates(*rule, a_target);
		const auto [action, index] = CurrentAction(*rule, alternates);
		std::string label = LabelOf(action, a_target, a_dst);
		const bool noVerb = label.empty();  // the game's prompt is the name alone and stays so (no action line)
		// a modifier action exists: the HUD draws the modifier key's button after the label, or
		// (ini, or no HUD) the text marker goes on the label itself
		const bool marker = !alternates.empty() && index == 0;
		const bool iconMarker = marker && Settings::ModifierIcon() && Hud::Ready();
		if (marker && !iconMarker && !Settings::AlternateMarker().empty()) {
			label += " " + Settings::AlternateMarker();
		}
		// modifier held with more than one modifier action: the HUD shows the rest above
		std::vector<std::string> extra;
		if (index != 0) {
			for (std::size_t slot = 1; slot < alternates.size(); ++slot) {
				extra.push_back(LabelOf(*alternates[slot], a_target, a_dst));
			}
		}
		Hud::SetPrompt(a_target, iconMarker, std::move(extra));
		const std::string_view vanilla{ a_dst.c_str() ? a_dst.c_str() : "" };
		std::string            rest;
		if (const auto newline = vanilla.find('\n'); newline != std::string_view::npos) {
			rest = vanilla.substr(newline);
		} else {
			const char* name = a_target->GetDisplayFullName();
			rest = std::format("\n{}", name ? name : "");
		}
		// "info": the name line stays, the game's lines below it (a lock level, ...) make way: for nothing, or for an info
		// line with the game's diamond, like its own lock level line.
		if (action.info == Rules::Info::kNothing) {
			rest = rest.substr(0, rest.find('\n', 1));
		} else if (action.info == Rules::Info::kRelationship) {
			const auto rank = Relationship::Of(a_target);
			// the rule's own word for the rank (the first entry the rank reaches), or the game's rank name
			std::optional<std::string> word;
			if (rank && action.infoLabels.empty()) {
				word = Relationship::Name(*rank);
			} else if (rank) {
				const auto entry = std::ranges::find_if(action.infoLabels, [&](const Rules::RankLabel& a_entry) { return *rank >= a_entry.fromRank; });
				if (entry != action.infoLabels.end()) {
					word = Rules::LabelText(entry->label);
				}
			}
			if (word) {
				rest = std::format("{}\n<img src='DiamondMarker' width='10' height='15' align='baseline' vspace='5'>{}",
					rest.substr(0, rest.find('\n', 1)), *word);
			}
		}
		if (noVerb) {
			static std::set<RE::FormID> logged;  // once per target: the prompt is rebuilt every few frames
			if (logged.insert(a_target->GetFormID()).second) {
				logger::info("{:08X}: the game offers no action (its prompt is the name alone), so no action word", a_target->GetFormID());
			}
			// the name line first; a text marker (the label is only that now) goes on it
			std::string text = rest.substr(1);
			text.insert(std::min(text.find('\n'), text.size()), label);
			a_dst = text.c_str();
			return;
		}
		a_dst = (label + rest).c_str();
	}

	// Send a rule's SKSE mod event: the mod that owns the rule reacts in Papyrus
	// (RegisterForModEvent). Event(String eventName, String ruleId, Float action, Form target):
	// the rule's id, which action (0.0 primary, 1.0-3.0 the modifier actions in slot order, so 1.0 is
	// the single alternate as before), and the target as sender.
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

	// An action with topics: the conversation opens the vanilla way and the topic is picked in it.
	void PrepareTopic(const Rules::Rule& a_rule, const Rules::Action& a_action, std::int32_t a_index, RE::Actor* a_actor)
	{
		const char* name = a_actor->GetDisplayFullName();
		logger::info("{} ({:08X}): {} / {} talks ({})", name ? name : "?", a_actor->GetFormID(), a_rule.file, a_rule.id, a_index == 0 ? "primary" : std::format("modifier action {}", a_index));
		Dialogue::Prepare(a_actor, a_action.topics, a_action.endDialogue);
	}

	// "take", "equip", "equipAndDraw", "use": picked up the game's way (a crime where the game says so; the whole stack),
	// then equipped (ammo: the whole stack) or used (drunk, eaten, applied: one).
	void DoAction(const Rules::Rule& a_rule, Rules::Do a_act, std::int32_t a_index, RE::TESObjectREFR* a_target)
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto base = a_target->GetBaseObject();
		if (!base) {
			return;
		}
		const char* name = a_target->GetDisplayFullName();
		logger::info("{} ({:08X}): {} / {} does {} ({})", name ? name : "?", a_target->GetFormID(), a_rule.file, a_rule.id, static_cast<int>(a_act), a_index == 0 ? "primary" : std::format("modifier action {}", a_index));
		const std::int32_t count = std::max(1, a_target->extraList.GetCount());
		player->PickUpObject(a_target, count, false, true);
		if (a_act == Rules::Do::kTake) {
			return;
		}
		const bool consumable = base->Is(RE::FormType::AlchemyItem) || base->Is(RE::FormType::Ingredient);
		const auto equipCount = static_cast<std::uint32_t>(base->Is(RE::FormType::Ammo) ? count : 1);
		if (a_act == Rules::Do::kUse && consumable) {
			RE::ActorEquipManager::GetSingleton()->EquipObject(player, base, nullptr, 1);
			return;
		}
		RE::ActorEquipManager::GetSingleton()->EquipObject(player, base, nullptr, equipCount, nullptr, false, false, true, true);
		if (a_act == Rules::Do::kEquipAndDraw) {
			player->DrawWeaponMagicHands(true);
		}
	}

	// Run an action on a target the player activated (or pressed a slot control on). a_vanilla runs the game's own
	// activation. Returns what the activation returns.
	template <class Vanilla>
	bool Perform(const Rules::Rule& a_rule, const Rules::Action& a_action, std::int32_t a_index, RE::TESObjectREFR* a_target, Vanilla&& a_vanilla)
	{
		if (!a_action.event.empty()) {
			SendRuleEvent(a_rule, a_action, a_index, a_target);
			// "Not activated": with true, the game's activation code goes on to
			// mount a horse (riding isn't part of the base object's function, unlike dialogue)
			return false;
		}
		if (const auto actor = a_target->As<RE::Actor>(); actor && !a_action.topics.empty()) {
			PrepareTopic(a_rule, a_action, a_index, actor);
			return a_vanilla();
		}
		switch (a_action.act) {
		case Rules::Do::kTake:
		case Rules::Do::kEquip:
		case Rules::Do::kEquipAndDraw:
		case Rules::Do::kUse:
			DoAction(a_rule, a_action.act, a_index, a_target);
			return true;
		default:
			return a_vanilla();  // "activate", or a label only
		}
	}

	// Prompt and activation hooks for one kind of base object (NPCs, books, weapons, doors, ...). The player activating
	// a target a rule matches: run the rule's action instead of the vanilla one. Anyone else activating, or no matching
	// rule: vanilla.
	template <class T>
	struct ObjectHooks
	{
		static bool GetActivateText(RE::TESBoundObject* a_this, RE::TESObjectREFR* a_target, RE::BSString& a_dst)
		{
			const bool result = getText(a_this, a_target, a_dst);
			if (a_target) {
				Prompt(a_target, a_dst);
			}
			return result;
		}

		static bool Activate(RE::TESBoundObject* a_this, RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_activator, std::uint8_t a_arg3, RE::TESBoundObject* a_object, std::int32_t a_count)
		{
			const auto vanilla = [&] { return activate(a_this, a_target, a_activator, a_arg3, a_object, a_count); };
			if (!runningVanilla && a_activator && a_activator->IsPlayerRef() && a_target) {
				if (const auto rule = Rules::Match(a_target)) {
					Rules::ReportConflicts(a_target, rule);
					const auto [action, index] = CurrentAction(*rule, Rules::Alternates(*rule, a_target));
					return Perform(*rule, action, index, a_target, vanilla);
				}
			}
			return vanilla();
		}

		static inline REL::Relocation<decltype(GetActivateText)> getText;
		static inline REL::Relocation<decltype(Activate)>        activate;

		static void Install(std::string_view a_name)
		{
			REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
			getText = REL::Relocation<decltype(GetActivateText)>{ vtbl.write_vfunc(0x4C, GetActivateText) };
			activate = REL::Relocation<decltype(Activate)>{ vtbl.write_vfunc(0x37, Activate) };
			logger::info("Hooks installed: {} GetActivateText (0x4C), Activate (0x37)", a_name);
		}
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
		const auto rule = Rules::Match(target.get());
		const auto alternates = rule ? Rules::Alternates(*rule, target.get()) : std::vector<const Rules::Action*>{};
		if (alternates.size() <= alternate) {
			return false;
		}
		if (button->IsDown()) {
			Rules::ReportConflicts(target.get(), rule);
			const auto& action = *alternates[alternate];
			// the vanilla activation, bypassing the hook (which would pick the Activate button's action)
			Perform(*rule, action, IndexOf(*rule, action), target.get(), [&] {
				runningVanilla = true;
				const bool result = target->ActivateRef(RE::PlayerCharacter::GetSingleton(), 0, nullptr, std::max(1, target->extraList.GetCount()), false);
				runningVanilla = false;
				return result;
			});
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
			ObjectHooks<RE::TESNPC>::Install("TESNPC");
			// object targets (rule format 5): everything the player can pick up, plus doors and containers
			ObjectHooks<RE::TESObjectBOOK>::Install("TESObjectBOOK");
			ObjectHooks<RE::TESObjectWEAP>::Install("TESObjectWEAP");
			ObjectHooks<RE::TESObjectARMO>::Install("TESObjectARMO");
			ObjectHooks<RE::AlchemyItem>::Install("AlchemyItem");
			ObjectHooks<RE::IngredientItem>::Install("IngredientItem");
			ObjectHooks<RE::ScrollItem>::Install("ScrollItem");
			ObjectHooks<RE::TESObjectLIGH>::Install("TESObjectLIGH");
			ObjectHooks<RE::TESAmmo>::Install("TESAmmo");
			ObjectHooks<RE::TESObjectMISC>::Install("TESObjectMISC");
			ObjectHooks<RE::TESKey>::Install("TESKey");
			ObjectHooks<RE::TESSoulGem>::Install("TESSoulGem");
			ObjectHooks<RE::TESObjectDOOR>::Install("TESObjectDOOR");
			ObjectHooks<RE::TESObjectCONT>::Install("TESObjectCONT");

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
