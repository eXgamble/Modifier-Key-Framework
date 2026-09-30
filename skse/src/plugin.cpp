// Modifier Key Framework: SKSE plugin.
// Mods declare activation rules for NPCs in JSON; the framework shows the prompt, runs the actions
// (SKSE mod events), handles the shared modifier key, and offers a small Papyrus API.

#include "Hooks.h"
#include "Input.h"
#include "Papyrus.h"
#include "Rules.h"
#include "Settings.h"

namespace
{
	// Own the log before SKSE::Init, and pass Init false so it doesn't truncate/replace it.
	void InitializeLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("Failed to find the SKSE log directory"sv);
		}
		*path /= "ModifierKeyFramework.log"sv;

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S:%e] [%l] %v"s);
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			Settings::Load();
			Rules::Load();
			Input::Register();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("ModifierKeyFramework loading on game runtime {}", a_skse->RuntimeVersion().string("."));

	SKSE::Init(a_skse, false);
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	SKSE::GetPapyrusInterface()->Register(Papyrus::Register);
	Hooks::Install();

	logger::info("ModifierKeyFramework loaded");
	return true;
}
