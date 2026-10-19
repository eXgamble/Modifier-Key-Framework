#include "Dialogue.h"

namespace
{
	struct Pending
	{
		RE::ActorHandle            actor;
		std::vector<RE::TESTopic*> topics;
		bool                       endDialogue = false;
		bool                       chosen = false;   // the topic was picked
		std::string                openedMenu;       // the menu the topic opened (barter, training, ...)
		std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
		int                                   tries = 0;       // looks at the topic list so far
		bool                                  talked = false;  // the NPC started the topic's line
		std::optional<std::chrono::steady_clock::time_point> quietSince;  // the NPC stopped talking
		std::optional<std::chrono::steady_clock::time_point> closingSince;  // endDialogue: the topic's menu closed
		int                                                  closeTries = 0;  // onCancelPress calls so far
		bool                                                 reshown = false;  // the game showed the list again
	};

	// The conversation must open this soon after the action, or the pick is dropped (the NPC was busy, ...).
	constexpr auto kOpenWithin = std::chrono::seconds(5);
	// The hidden conversation is looked at 0.1 s apart: the topic is picked within kPickTries looks, and a picked
	// topic that opens no menu kQuietFor after its line ends (kNoLineFor if it says nothing) gives the conversation
	// back to the player.
	constexpr auto kTick = std::chrono::milliseconds(100);
	constexpr int  kPickTries = 40;
	constexpr int  kMaxTicks = 300;
	constexpr auto kQuietFor = std::chrono::seconds(2);
	constexpr auto kNoLineFor = std::chrono::seconds(5);
	// endDialogue: the close is asked again every kCloseRetry while the conversation stays open, and forced after
	// kCloseTries asks (no goodbye then). The menu stays hidden meanwhile.
	constexpr auto kCloseRetry = std::chrono::seconds(1);
	constexpr int  kCloseTries = 3;

	std::mutex             lock;
	std::optional<Pending> pending;
	std::atomic<int>       generation{ 0 };  // a newer conversation (or the end of this one) stops the looks
	std::optional<bool>    cursorWas;  // the mouse cursor's visibility before it was hidden (UI's turn only)

	RE::FxDelegateHandler::CallbackFn* topicClicked = nullptr;  // the dialogue menu's own "TopicClicked"
	RE::FxDelegateHandler*             topicHandler = nullptr;

	RE::GPtr<RE::DialogueMenu> Menu() { return RE::UI::GetSingleton()->GetMenu<RE::DialogueMenu>(); }

	// The dialogue menu's own topic callback: what a click on a topic runs (the list index as its one argument).
	void FindCallback()
	{
		const auto menu = Menu();
		if (!menu || !menu->fxDelegate) {
			return;
		}
		if (const auto defn = menu->fxDelegate->callbacks.Get(RE::GString("TopicClicked"))) {
			topicClicked = defn->callback;
			topicHandler = defn->handler.get();
		}
	}

	RE::GPtr<RE::IMenu> CursorMenu() { return RE::UI::GetSingleton()->GetMenu(RE::CursorMenu::MENU_NAME); }

	// The mouse cursor (keyboard and mouse only; a gamepad has none) hidden while the NPC speaks. UI's turn.
	void HideCursor()
	{
		const auto menu = CursorMenu();
		if (!menu || !menu->uiMovie) {
			return;
		}
		if (!cursorWas) {
			RE::GFxValue was;
			if (!menu->uiMovie->GetVariable(&was, "_root.mc_Cursor._visible") || !was.IsBool()) {
				return;  // not the cursor movie this expects: leave it alone
			}
			cursorWas = was.GetBool();
		}
		menu->uiMovie->SetVariable("_root.mc_Cursor._visible", RE::GFxValue(false));
	}

	// The cursor as it was, for the menu the topic opened or after the conversation. UI's turn.
	void ShowCursor()
	{
		if (!cursorWas) {
			return;
		}
		if (const auto menu = CursorMenu(); menu && menu->uiMovie) {
			menu->uiMovie->SetVariable("_root.mc_Cursor._visible", RE::GFxValue(*cursorWas));
		}
		cursorWas.reset();
	}

	// A topic action's conversation runs out of sight: no topic list, Exit button, speaker name or mouse cursor, only
	// the NPC's subtitle (when subtitles are on). The NPC still turns to the player and speaks. Set on every look: the
	// movie builds its clips a moment after the menu opens. UI's turn.
	void SetMenuVisible(bool a_visible)
	{
		const auto menu = Menu();
		if (!menu || !menu->uiMovie) {
			return;
		}
		for (const auto path : { "_root.DialogueMenu_mc.TopicListHolder._visible", "_root.DialogueMenu_mc.ExitButton._visible", "_root.DialogueMenu_mc.SpeakerName._visible" }) {
			menu->uiMovie->SetVariable(path, RE::GFxValue(a_visible));
		}
		if (a_visible) {
			ShowCursor();
		} else {
			HideCursor();
		}
	}

	// The pending topic's index in the dialogue list, if the list holds it now.
	std::optional<std::uint32_t> FindTopic(const std::vector<RE::TESTopic*>& a_topics, RE::ActorHandle a_actor)
	{
		const auto manager = RE::MenuTopicManager::GetSingleton();
		if (!manager || !manager->dialogueList || manager->speaker.get().get() != a_actor.get().get()) {
			return std::nullopt;
		}
		std::uint32_t index = 0;
		for (const auto dialogue : *manager->dialogueList) {
			if (dialogue && std::ranges::contains(a_topics, dialogue->parentTopic)) {
				return index;
			}
			++index;
		}
		return std::nullopt;
	}

	// Ends the conversation the way the player's Tab does (the movie's own onCancelPress: the list fades out, then the
	// game closes the menu), so the NPC says goodbye. Kept out of sight: the dialogue menu comes back on top when the
	// topic's menu closes. UI's turn.
	void CloseHidden(int a_try)
	{
		const auto menu = Menu();
		if (!menu || !menu->uiMovie) {
			return;
		}
		SetMenuVisible(false);
		RE::GFxValue state;
		menu->uiMovie->GetVariable(&state, "_root.DialogueMenu_mc.eMenuState");
		logger::info("Dialogue: closing the conversation like Tab (ask {}, menu state {})", a_try, state.IsNumber() ? state.GetNumber() : -1.0);
		if (a_try > kCloseTries || !menu->uiMovie->Invoke("_root.DialogueMenu_mc.onCancelPress", nullptr, nullptr, 0)) {
			logger::info("Dialogue: the conversation would not close like Tab, closed without a goodbye");
			RE::UIMessageQueue::GetSingleton()->AddMessage(RE::DialogueMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}
	}

	// endDialogue, after the topic's menu closed: the conversation stays hidden until it is gone (the game shows the
	// topic list again when the dialogue menu is back on top), and the close is asked again if it stays open. UI's
	// turn. Returns false when there is nothing left to look at.
	bool CloseTick()
	{
		const auto menu = Menu();
		if (menu && menu->uiMovie) {
			RE::GFxValue shown;
			if (menu->uiMovie->GetVariable(&shown, "_root.DialogueMenu_mc.TopicListHolder._visible") && shown.IsBool() && shown.GetBool()) {
				std::lock_guard guard(lock);
				if (pending && !pending->reshown) {
					pending->reshown = true;
					logger::info("Dialogue: the game showed the topic list again while closing, hidden");
				}
			}
		}
		SetMenuVisible(false);
		int ask = 0;
		{
			std::lock_guard guard(lock);
			if (!pending || !pending->closingSince) {
				return false;
			}
			if (std::chrono::steady_clock::now() - *pending->closingSince > kCloseRetry * pending->closeTries) {
				ask = ++pending->closeTries;
			}
		}
		if (ask) {
			CloseHidden(ask);
		}
		return ask <= kCloseTries;
	}

	void Choose(std::uint32_t a_index)
	{
		const auto menu = Menu();
		if (!menu || !topicClicked) {
			return;
		}
		RE::GFxValue          arg(static_cast<double>(a_index));
		const RE::FxDelegateArgs args(RE::GFxValue(), topicHandler, menu->uiMovie.get(), &arg, 1);
		topicClicked(args);
	}

	// The pick went wrong: the conversation shows as usual and the player goes on by hand. UI's turn.
	void GiveBack(const char* a_why)
	{
		{
			std::lock_guard guard(lock);
			pending.reset();
		}
		logger::info("Dialogue: {}, the conversation shows", a_why);
		SetMenuVisible(true);
	}

	// One look at the hidden conversation, on the UI's turn. Picks the topic once the list holds it (the list fills
	// once the greeting starts), then waits for the menu the topic's line opens. Returns false when there is nothing
	// left to look at.
	bool Tick()
	{
		std::vector<RE::TESTopic*> topics;
		RE::ActorHandle            actor;
		bool                       chosen = false;
		int                        tries = 0;
		bool                       closing = false;
		{
			std::lock_guard guard(lock);
			if (pending && pending->closingSince) {
				closing = true;
			} else if (!pending || !pending->openedMenu.empty()) {
				return false;  // over, or the topic's menu is open: the menu sink takes it from here
			} else {
				chosen = pending->chosen;
				tries = ++pending->tries;
				topics = pending->topics;
				actor = pending->actor;
			}
		}
		if (closing) {
			return CloseTick();
		}
		SetMenuVisible(false);
		if (!chosen) {
			if (const auto index = FindTopic(topics, actor)) {
				{
					std::lock_guard guard(lock);
					if (!pending) {
						return false;
					}
					pending->chosen = true;
					pending->at = std::chrono::steady_clock::now();
				}
				logger::info("Dialogue: topic picked (list entry {})", *index);
				Choose(*index);
			} else if (tries >= kPickTries) {
				GiveBack("the topic never showed up in the list");
				return false;
			}
			return true;
		}
		// picked: the NPC says the line, then the line's script opens the menu
		const auto  manager = RE::MenuTopicManager::GetSingleton();
		const bool  talking = manager && manager->currentTopicInfo;
		const auto  now = std::chrono::steady_clock::now();
		const char* why = nullptr;
		{
			std::lock_guard guard(lock);
			if (!pending || !pending->openedMenu.empty()) {
				return false;
			}
			if (talking) {
				pending->talked = true;
				pending->quietSince.reset();
			} else if (pending->talked) {
				if (!pending->quietSince) {
					pending->quietSince = now;
				} else if (now - *pending->quietSince > kQuietFor) {
					why = "the topic's line opened no menu";
				}
			} else if (now - pending->at > kNoLineFor) {
				why = "the topic said nothing and opened no menu";
			}
		}
		if (why) {
			GiveBack(why);
			return false;
		}
		return true;
	}

	void Clear(const char* a_why)
	{
		std::lock_guard guard(lock);
		if (pending) {
			logger::debug("Dialogue: done ({})", a_why);
		}
		pending.reset();
	}

	// Looks at the hidden conversation every kTick on the UI's turn, until Tick has nothing left to look at, a newer
	// round of looks starts or the conversation closes.
	void StartLooks()
	{
		const int mine = ++generation;
		// skse-lint: ok one-shot: at most kMaxTicks looks 0.1 s apart while a topic action's conversation runs
		std::thread([mine] {
			for (int i = 0; i < kMaxTicks && generation == mine; ++i) {
				std::this_thread::sleep_for(kTick);  // skse-lint: ok the bounded looks above
				SKSE::GetTaskInterface()->AddUITask([mine] {
					if (generation == mine && !Tick()) {
						++generation;  // nothing left to look at: the remaining looks stop
					}
				});
			}
		}).detach();
	}

	struct MenuSink : RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto& name = a_event->menuName;
			if (name == RE::DialogueMenu::MENU_NAME) {
				if (a_event->opening) {
					bool want;
					{
						std::lock_guard guard(lock);
						if (pending && std::chrono::steady_clock::now() - pending->at > kOpenWithin) {
							pending.reset();  // a conversation long after the action: not the one it started
						}
						want = pending && !pending->chosen;
					}
					// the menu's own callbacks and its visibility: on the UI's turn
					SKSE::GetTaskInterface()->AddUITask([want] {
						FindCallback();
						if (want) {
							SetMenuVisible(false);
						}
					});
					if (want) {
						StartLooks();
					}
				} else {
					Clear("the conversation closed");
					++generation;  // its looks stop
					SKSE::GetTaskInterface()->AddUITask(ShowCursor);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
			// the menu the chosen topic opens, then its closing: end the conversation
			std::lock_guard guard(lock);
			if (!pending || !pending->chosen || pending->closingSince || name == RE::HUDMenu::MENU_NAME || name == RE::CursorMenu::MENU_NAME || name == RE::FaderMenu::MENU_NAME ||
				name == RE::MessageBoxMenu::MENU_NAME || name == RE::TutorialMenu::MENU_NAME) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event->opening && pending->openedMenu.empty()) {
				// a menu the player works in (barter, training, gift, ...), not an overlay that comes and goes
				// meanwhile (the barter menu's LoadWaitSpinner, other mods' HUD overlays). The cursor alone doesn't
				// tell: with a gamepad the barter and training menus have none, and the training menu has no input
				// context either; both use the menu controls, which the spinner doesn't.
				const auto menu = RE::UI::GetSingleton()->GetMenu(name);
				const bool works = menu && (menu->UsesCursor() || menu->UsesMenuContext() || menu->inputContext.get() != RE::IMenu::Context::kNone);
				logger::info("Dialogue: {} opened (flags {:X}, input context {}): {}", name.c_str(), menu ? menu->menuFlags.underlying() : 0,
					menu ? static_cast<int>(menu->inputContext.get()) : -1, works ? "the topic's menu" : "passing by");
				if (!works) {
					return RE::BSEventNotifyControl::kContinue;
				}
				pending->openedMenu = name.c_str();
				SKSE::GetTaskInterface()->AddUITask(ShowCursor);
			} else if (!a_event->opening && name == pending->openedMenu.c_str()) {
				if (pending->endDialogue) {
					// hidden at once and asked to close like Tab; the looks keep it hidden until it is gone
					logger::info("Dialogue: {} closed, the conversation ends", pending->openedMenu);
					pending->closingSince = std::chrono::steady_clock::now();
					pending->closeTries = 1;
					SKSE::GetTaskInterface()->AddUITask([] { CloseHidden(1); });
					StartLooks();
				} else {
					logger::info("Dialogue: {} closed, the conversation shows", pending->openedMenu);
					SKSE::GetTaskInterface()->AddUITask([] { SetMenuVisible(true); });
					pending.reset();
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

namespace Dialogue
{
	void Install()
	{
		static MenuSink sink;
		RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
	}

	bool Offers(RE::Actor* a_actor, RE::TESTopic* a_topic)
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		// running the way Papyrus means it: CommonLib's IsRunning() alone is only "not stopping", true for a quest that
		// never started
		const auto quest = a_topic ? a_topic->ownerQuest : nullptr;
		if (!a_actor || !a_topic || (quest && !(quest->IsEnabled() && quest->IsRunning()))) {
			return false;
		}
		for (std::uint32_t i = 0; i < a_topic->numTopicInfos; ++i) {
			const auto info = a_topic->topicInfos[i];
			if (info && info->objConditions.IsTrue(a_actor, player)) {
				return true;
			}
		}
		return false;
	}

	void Prepare(RE::Actor* a_actor, const std::vector<RE::TESTopic*>& a_topics, bool a_endDialogue)
	{
		std::lock_guard guard(lock);
		pending = Pending{ a_actor->GetHandle(), a_topics, a_endDialogue };
	}
}
