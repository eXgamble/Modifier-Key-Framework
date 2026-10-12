#include "Relationship.h"

#include "Rules.h"

namespace
{
	// Every actor reference in the game's form map (the persistent ones: anyone with a home), found once per game
	std::vector<RE::FormID> actors;
	bool                    scanned = false;
	std::mutex              actorsLock;

	std::vector<RE::FormID> Actors()
	{
		std::scoped_lock lock{ actorsLock };
		if (!scanned) {
			scanned = true;
			const auto [forms, formsLock] = RE::TESForm::GetAllForms();
			RE::BSReadLockGuard guard{ formsLock };
			if (forms) {
				for (const auto& [id, form] : *forms) {
					if (form && form->Is(RE::FormType::ActorCharacter)) {
						actors.push_back(id);
					}
				}
			}
			logger::info("Relationship: {} actors", actors.size());
		}
		return actors;
	}

	std::int32_t RankOf(RE::TESNPC* a_npc)
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto playerBase = player ? player->GetActorBase() : nullptr;
		const auto relationship = a_npc && playerBase ? RE::BGSRelationship::GetRelationship(a_npc, playerBase) : nullptr;
		// level: 0 Lover ... 4 Acquaintance ... 8 Archnemesis; no relationship = Acquaintance
		return relationship ? 4 - static_cast<std::int32_t>(relationship->level.underlying()) : 0;
	}

	bool Lives(RE::Actor* a_actor)
	{
		return a_actor && !a_actor->IsDead() && !a_actor->IsDisabled();
	}

	// The owner of a door's destination, or of the reference itself: its own owner, else its cell's
	RE::TESForm* OwnerOf(RE::TESObjectREFR* a_target)
	{
		RE::NiPointer<RE::TESObjectREFR> place{ a_target };
		if (const auto base = a_target->GetBaseObject(); base && base->Is(RE::FormType::Door)) {
			place = a_target->extraList.GetTeleportLinkedDoor().get();
			if (!place) {
				return nullptr;
			}
		}
		if (const auto owner = place->extraList.GetOwner()) {
			return owner;
		}
		const auto cell = place->GetParentCell();
		return cell ? cell->GetOwner() : nullptr;
	}

	std::optional<std::int32_t> OwnerRank(RE::TESForm* a_owner)
	{
		if (const auto npc = a_owner ? a_owner->As<RE::TESNPC>() : nullptr) {
			// the owner's reference, to see they're alive; none found (never loaded): assume so
			for (const auto id : Actors()) {
				const auto actor = RE::TESForm::LookupByID<RE::Actor>(id);
				if (actor && actor->GetActorBase() == npc) {
					return Lives(actor) ? std::optional(RankOf(npc)) : std::nullopt;
				}
			}
			return RankOf(npc);
		}
		const auto faction = a_owner ? a_owner->As<RE::TESFaction>() : nullptr;
		if (!faction) {
			return std::nullopt;
		}
		// a household: the friendliest grown-up (Simply Knock lets a friend answer first, children only when no one
		// else is home)
		std::optional<std::int32_t> adults;
		std::optional<std::int32_t> children;
		for (const auto id : Actors()) {
			const auto actor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!Lives(actor) || actor->IsPlayerRef() || !actor->IsInFaction(faction)) {
				continue;
			}
			auto& best = actor->IsChild() ? children : adults;
			best = std::max(best.value_or(-4), RankOf(actor->GetActorBase()));
		}
		return adults ? adults : children;
	}
}

namespace Relationship
{
	std::optional<std::int32_t> Of(RE::TESObjectREFR* a_target)
	{
		if (!a_target) {
			return std::nullopt;
		}
		if (const auto actor = a_target->As<RE::Actor>()) {
			return RankOf(actor->GetActorBase());
		}
		return OwnerRank(OwnerOf(a_target));
	}

	std::string Name(std::int32_t a_rank)
	{
		static constexpr std::array<std::pair<const char*, const char*>, 9> NAMES{ {
			{ "$MKF_Archnemesis", "Archnemesis" },
			{ "$MKF_Enemy", "Enemy" },
			{ "$MKF_Foe", "Foe" },
			{ "$MKF_Rival", "Rival" },
			{ "$MKF_Acquaintance", "Acquaintance" },
			{ "$MKF_Friend", "Friend" },
			{ "$MKF_Confidant", "Confidant" },
			{ "$MKF_Ally", "Ally" },
			{ "$MKF_Lover", "Lover" },
		} };
		const auto& [key, fallback] = NAMES[static_cast<std::size_t>(std::clamp(a_rank, -4, 4) + 4)];
		return Rules::Translate(key, fallback);
	}

	void Reset()
	{
		std::scoped_lock lock{ actorsLock };
		actors.clear();
		scanned = false;
	}
}
