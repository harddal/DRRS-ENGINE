#pragma once

#include <memory>

#include <cereal/cereal.hpp>

#include "Engine/Sound/SoundEngine.h"

struct SoundConfiguration
{
	float volume;

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(CEREAL_NVP(volume));
	}

	SoundConfiguration() : volume(1.0f) {}
};

class SoundManager
{
public:
	SoundManager& operator=(const SoundManager&) = delete;

    SoundManager();
    ~SoundManager();

	static void destroy() {
		delete s_Instance;
		s_Instance = nullptr;
	}

	SoundConfiguration getConfiguration() const { return m_configuration; }
	void saveConfiguration(SoundConfiguration configuration);

    SoundEngine* sound() const { return m_soundEngine; }

    static SoundManager* Get() { return s_Instance; }

private:
    static SoundManager* s_Instance;

    SoundEngine* m_soundEngine;

	SoundConfiguration m_configuration;

};
