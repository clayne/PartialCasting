extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
#ifndef DEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= Version::PROJECT;
	*path += ".log"sv;
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef DEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

bool is_affected_spell([[maybe_unused]] RE::MagicCaster* mcaster, [[maybe_unused]] RE::SpellItem* spel)
{
	// TODO
	return true;
}

bool charge_time_conditions([[maybe_unused]] RE::MagicCaster* mcaster, [[maybe_unused]] RE::SpellItem* spel)
{
	return spel->GetChargeTime() >= 2 * mcaster->castingTimer;
}

class CastHook
{
public:
	static void Hook()
	{
		_InterruptCast = SKSE::GetTrampoline().write_call<5>(REL::ID(41362).address() + 0xca, InterruptCast);
		_Launch = SKSE::GetTrampoline().write_call<5>(REL::ID(33672).address() + 0x377, Launch);
	}

private:
	static void InterruptCast(RE::MagicCaster* mcaster, [[maybe_unused]] bool a_depleteEnergy)
	{
		using S = RE::MagicCaster::State;

		if (auto a = mcaster->GetCasterAsActor(); a && a->IsPlayerRef() && mcaster->state.any(S::kCharging, S::kUnk02)) {
			if (auto spel_ = mcaster->currentSpell; spel_ && spel_->As<RE::MagicItem>() &&
													spel_->GetDelivery() == RE::MagicSystem::Delivery::kAimed &&
													spel_->GetCastingType() == RE::MagicSystem::CastingType::kFireAndForget) {
				if (auto spel = spel_->As<RE::SpellItem>();
					spel && is_affected_spell(mcaster, spel) && charge_time_conditions(mcaster, spel)) {
					float old_timer = mcaster->castingTimer;
					mcaster->castingTimer = 0;
					// Update
					_generic_foo_<33622, void(RE::MagicCaster * a, float)>::eval(mcaster, 0);

					auto hand = mcaster->GetCastingSource();
					if (hand == RE::MagicSystem::CastingSource::kRightHand)
						a->NotifyAnimationGraph("MRh_SpellRelease_Event");
					if (hand == RE::MagicSystem::CastingSource::kLeftHand)
						a->NotifyAnimationGraph("MLh_SpellRelease_Event");

					mcaster->castingTimer = old_timer;
					return;
				}
			}
		}
		return _InterruptCast(mcaster, a_depleteEnergy);
	}

	static uint32_t* Launch(uint32_t* handle, RE::Projectile::LaunchData* ldata)
	{
		if (auto a = ldata->shooter; a && a->IsPlayerRef()) {
			if (auto mcaster = a->GetMagicCaster(ldata->castingSource);
				mcaster->castingTimer > 0 && mcaster->currentSpell && mcaster->currentSpell->As<RE::SpellItem>()) {
				float cast_time = mcaster->currentSpell->GetChargeTime();
				if (cast_time > 0 && 0.1 * cast_time < mcaster->castingTimer) {
					float mul = (cast_time - mcaster->castingTimer) / cast_time;
					ldata->scale *= mul;
					ldata->power *= mul;
				}
				mcaster->castingTimer = 0;
			}
		}
		return _Launch(handle, ldata);
	}

	static inline REL::Relocation<decltype(InterruptCast)> _InterruptCast;
	static inline REL::Relocation<decltype(Launch)> _Launch;
};

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		CastHook::Hook();

		break;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));
	if (!g_messaging) {
		logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
		return false;
	}

	logger::info("loaded");

	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(1 << 10);

	g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

	return true;
}
