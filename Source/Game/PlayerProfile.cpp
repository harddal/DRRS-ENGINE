#include "Game/PlayerProfile.h"

#include "Game/IPlayerController.h"
#include "Game/Player/PlayerController.h"
#include "Game/PlayerThirdPerson/PlayerControllerTPS.h"

#include "spdlog/spdlog.h"

namespace
{
	// THE FIRST ROW IS THE DEFAULT. Keep it that way — resolvePlayerProfile()
	// falls back to index 0 for an empty or unknown name, and every marker
	// authored before this field existed deserialises to empty.
	const PlayerProfile k_profiles[] =
	{
		{
			"fps",
			"player/player",
			1.75f,
			[]() -> std::unique_ptr<IPlayerController>
			{
				return std::unique_ptr<IPlayerController>(new PlayerController());
			}
		},
		{
			"tps",
			"player/player_tps",
			1.75f,   // same capsule as the first-person profile for now — see
			         // CCTSubsystem::init, which has one shared m_capsuleDesc
			[]() -> std::unique_ptr<IPlayerController>
			{
				return std::unique_ptr<IPlayerController>(new PlayerControllerTPS());
			}
		},
	};
}

const PlayerProfile& resolvePlayerProfile(const std::string& name)
{
	if (!name.empty())
	{
		for (const auto& profile : k_profiles)
		{
			if (name == profile.name)
				return profile;
		}

		spdlog::warn("resolvePlayerProfile(): unknown player profile '{}' — falling back to '{}'",
			name, k_profiles[0].name);
	}

	return k_profiles[0];
}
