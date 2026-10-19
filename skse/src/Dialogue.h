#pragma once

// Dialogue topic actions (rule format 4): an action with "topic" starts the real conversation with the NPC and picks
// the first listed topic the NPC offers, as if the player had chosen it (the NPC turns to the player and says the
// topic's line, then e.g. opens the barter menu). The dialogue menu stays hidden meanwhile: the player sees the NPC
// turn and speak, then the menu. With "endDialogue", the conversation closes when the menu that topic opened (barter,
// training, gift, ...) closes, the way Tab does (the NPC says goodbye, the dialogue menu stays hidden); without it,
// the conversation shows then. If the topic never shows up or opens no
// menu, the conversation shows and the player goes on by hand.
namespace Dialogue
{
	// kDataLoaded: the menu sink.
	void Install();

	// The NPC offers the topic right now: its quest runs and one of its lines passes its conditions (the NPC speaking
	// to the player). Game thread.
	bool Offers(RE::Actor* a_actor, RE::TESTopic* a_topic);

	// Remember what to pick once the conversation with a_actor opens. The caller then starts the conversation.
	void Prepare(RE::Actor* a_actor, const std::vector<RE::TESTopic*>& a_topics, bool a_endDialogue);
}
