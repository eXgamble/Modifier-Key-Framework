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
	};

	// The conversation must open this soon after the action, or the pick is dropped (the NPC was busy, ...).
	constexpr auto kOpenWithin = std::chrono::seconds(5);

	std::mutex             lock;
	std::optional<Pending> pending;

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

	// Tried a few times after the menu opens: the topic list fills once the greeting starts.
	void TryChoose()
	{
		std::vector<RE::TESTopic*> topics;
		RE::ActorHandle            actor;
		{
			std::lock_guard guard(lock);
			if (!pending || pending->chosen) {
				return;
			}
			topics = pending->topics;
			actor = pending->actor;
		}
		if (const auto index = FindTopic(topics, actor)) {
			{
				std::lock_guard guard(lock);
				pending->chosen = true;
			}
			logger::info("Dialogue: topic picked (list entry {})", *index);
			Choose(*index);
		}
	}

	void Clear(const char* a_why)
	{
		std::lock_guard guard(lock);
		if (pending) {
			logger::debug("Dialogue: done ({})", a_why);
		}
		pending.reset();
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
					SKSE::GetTaskInterface()->AddUITask(FindCallback);  // the menu's own callbacks: on the UI's turn
					bool want;
					{
						std::lock_guard guard(lock);
						if (pending && std::chrono::steady_clock::now() - pending->at > kOpenWithin) {
							pending.reset();  // a conversation long after the action: not the one it started
						}
						want = pending && !pending->chosen;
					}
					if (want) {
						// skse-lint: ok one-shot: at most 40 tries 0.1 s apart after a topic action opens a conversation
						std::thread([] {
							for (int i = 0; i < 40; ++i) {
								std::this_thread::sleep_for(std::chrono::milliseconds(100));  // skse-lint: ok the bounded retry above
								SKSE::GetTaskInterface()->AddUITask(TryChoose);  // clicks the menu's topic: on the UI's turn
							}
						}).detach();
					}
				} else {
					Clear("the conversation closed");
				}
				return RE::BSEventNotifyControl::kContinue;
			}
			// the menu the chosen topic opens, then its closing: end the conversation
			std::lock_guard guard(lock);
			if (!pending || !pending->chosen || name == RE::HUDMenu::MENU_NAME || name == RE::CursorMenu::MENU_NAME || name == RE::FaderMenu::MENU_NAME ||
				name == RE::MessageBoxMenu::MENU_NAME || name == RE::TutorialMenu::MENU_NAME) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event->opening && pending->openedMenu.empty()) {
				pending->openedMenu = name.c_str();
			} else if (!a_event->opening && name == pending->openedMenu.c_str()) {
				if (pending->endDialogue) {
					logger::info("Dialogue: {} closed, the conversation ends", pending->openedMenu);
					RE::UIMessageQueue::GetSingleton()->AddMessage(RE::DialogueMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
				}
				pending.reset();
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
