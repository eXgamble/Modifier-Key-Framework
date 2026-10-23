#include "Input.h"

#include "Rules.h"
#include "Settings.h"

namespace
{
	std::atomic<bool> keyboardHeld{ false };
	std::atomic<bool> gamepadHeld{ false };

	// Rebuild the prompt of whatever the player is looking at, so it switches the moment the key
	// goes down or up (NPCs, and objects when some rule is for objects).
	void RefreshPrompt()
	{
		const auto pick = RE::CrosshairPickData::GetSingleton();
		const auto target = pick ? pick->GetActiveTarget().get() : nullptr;
		if (target && (target->Is(RE::FormType::ActorCharacter) || Rules::HasObjectRules())) {
			if (const auto player = RE::PlayerCharacter::GetSingleton()) {
				player->UpdateCrosshairs();
			}
		}
	}

	// The game builds the prompt only when the crosshair lands on a new target, so a rule that changes with the
	// target's state goes stale while the player keeps looking at it: reading a book that's still under the
	// crosshair left "Read" up where Read Or Take's read-book rule says "Take" (found 2026-10-22). Rebuilt once
	// the book menu closes.
	class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static MenuSink* GetSingleton()
		{
			static MenuSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && !a_event->opening && a_event->menuName == RE::BookMenu::MENU_NAME) {
				SKSE::GetTaskInterface()->AddTask([] { RefreshPrompt(); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	class InputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputSink* GetSingleton()
		{
			static InputSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			for (auto event = *a_event; event; event = event->next) {
				const auto button = event->AsButtonEvent();
				if (!button) {
					continue;
				}
				// Same key numbering as SKSE/Papyrus: keyboard scan codes, mouse from 256, gamepad from 266
				auto key = button->GetIDCode();
				bool isGamepad = false;
				switch (event->GetDevice()) {
				case RE::INPUT_DEVICE::kMouse:
					key += SKSE::InputMap::kMacro_MouseButtonOffset;
					break;
				case RE::INPUT_DEVICE::kGamepad:
					key = SKSE::InputMap::GamepadMaskToKeycode(key);
					isGamepad = true;
					break;
				default:
					break;
				}

				const bool pressed = button->IsPressed();
				if (!isGamepad && key == Settings::ModifierKey()) {
					if (keyboardHeld.exchange(pressed) != pressed) {
						RefreshPrompt();
					}
				} else if (isGamepad && static_cast<std::int32_t>(key) == Settings::ModifierKeyGamepad()) {
					if (gamepadHeld.exchange(pressed) != pressed) {
						RefreshPrompt();
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

namespace Input
{
	void Register()
	{
		if (const auto inputManager = RE::BSInputDeviceManager::GetSingleton()) {
			inputManager->AddEventSink<RE::InputEvent*>(InputSink::GetSingleton());
			logger::info("Listening for the modifier key");
		} else {
			logger::error("No input device manager, the modifier key won't work");
		}
		if (const auto ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
		}
	}

	bool IsModifierHeld()
	{
		return keyboardHeld || gamepadHeld;
	}

	void QueuePromptRefresh()
	{
		SKSE::GetTaskInterface()->AddTask([] { RefreshPrompt(); });
	}
}
