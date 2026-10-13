#include "Relationship.h"

#include "Rules.h"

namespace
{
	// Every actor reference in the game's form map (the persistent ones: anyone with a home), found once per game, and
	// each base's first reference (an NPC owner's reference without walking them all)
	std::vector<RE::FormID>                     actors;
	std::unordered_map<RE::TESNPC*, RE::FormID> refOfBase;
	bool                                        scanned = false;
	std::mutex                                  actorsLock;

	// An owner's rank, kept a while: a household walks every actor in the game (about 1 ms with 3300 of them, measured
	// 2026-10-12), and the prompt is built again on every press of the modifier and about once a second.
	struct CachedRank
	{
		std::optional<std::int32_t>           rank;
		std::chrono::steady_clock::time_point at;
	};
	std::unordered_map<RE::FormID, CachedRank> ownerRanks;
	std::mutex                                 ownerLock;
	constexpr auto                             kOwnerFresh = std::chrono::seconds(10);

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
						if (const auto base = static_cast<RE::Actor*>(form)->GetActorBase()) {
							refOfBase.try_emplace(base, id);
						}
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

	std::optional<std::int32_t> ComputeOwnerRank(RE::TESForm* a_owner)
	{
		if (const auto npc = a_owner ? a_owner->As<RE::TESNPC>() : nullptr) {
			// the owner's reference, to see they're alive; none found (never loaded): assume so
			Actors();
			RE::FormID id = 0;
			{
				std::scoped_lock lock{ actorsLock };
				if (const auto it = refOfBase.find(npc); it != refOfBase.end()) {
					id = it->second;
				}
			}
			if (const auto actor = id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr) {
				return Lives(actor) ? std::optional(RankOf(npc)) : std::nullopt;
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

	std::optional<std::int32_t> OwnerRank(RE::TESForm* a_owner)
	{
		if (!a_owner) {
			return std::nullopt;
		}
		const auto now = std::chrono::steady_clock::now();
		{
			std::scoped_lock lock{ ownerLock };
			if (const auto it = ownerRanks.find(a_owner->GetFormID()); it != ownerRanks.end() && now - it->second.at < kOwnerFresh) {
				return it->second.rank;
			}
		}
		const auto rank = ComputeOwnerRank(a_owner);
		std::scoped_lock lock{ ownerLock };
		ownerRanks[a_owner->GetFormID()] = { rank, now };
		return rank;
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
		{
			std::scoped_lock lock{ actorsLock };
			actors.clear();
			refOfBase.clear();
			scanned = false;
		}
		std::scoped_lock lock{ ownerLock };
		ownerRanks.clear();
	}
}
