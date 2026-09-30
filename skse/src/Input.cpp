#include "Input.h"

#include "Settings.h"

namespace
{
	std::atomic<bool> keyboardHeld{ false };
	std::atomic<bool> gamepadHeld{ false };

	// Rebuild the prompt of whatever the player is looking at, so it switches the moment the key
	// goes down or up (only for actors: the framework handles NPCs only).
	void RefreshPrompt()
	{
		const auto pick = RE::CrosshairPickData::GetSingleton();
		const auto target = pick ? pick->GetActiveTarget().get() : nullptr;
		if (target && target->Is(RE::FormType::ActorCharacter)) {
			if (const auto player = RE::PlayerCharacter::GetSingleton()) {
				player->UpdateCrosshairs();
			}
		}
	}

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
	}

	bool IsModifierHeld()
	{
		return keyboardHeld || gamepadHeld;
	}
}
