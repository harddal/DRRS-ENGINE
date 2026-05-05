#pragma once

#include <string>

#include "anax/Component.hpp"
#include "Engine/Sound/SoundEngine.h"

#include "cereal/cereal.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"

struct sSoundData
{
    SoundHandle  sound;
    SoundSource* source;

    std::string file, name;

    bool
        play, 
        is3D, 
        startPaused, 
        loop, 
        isPlaying;

    float
        volume, minDist, maxDist;

    sSoundData() :
        source(nullptr),
        play(false), is3D(false), startPaused(true), loop(false), isPlaying(false),
        volume(100.0f), minDist(0.5f), maxDist(1.0f) {}
    sSoundData(std::string file, std::string name, bool is3D = false, bool loop = false,
        float volume = 100.0f, float minDist = 0.5f, float maxDist = 1.0f, bool startpaused = true)
    {
        source = nullptr;

        this->file = file;
        this->name = name;
        this->is3D = is3D;
        this->loop = loop;
        this->volume = volume;
        this->minDist = minDist;
        this->maxDist = maxDist;
        this->startPaused = startpaused;
    }

    template <class Archive>
    void serialize(Archive& archive)
    {
        archive(CEREAL_NVP(name), CEREAL_NVP(file), CEREAL_NVP(loop),
                CEREAL_NVP(minDist), CEREAL_NVP(volume), CEREAL_NVP(is3D),
                CEREAL_NVP(startPaused));
    }
};

struct SoundComponent : anax::Component
{
    std::vector<sSoundData> sounds;

	void add(sSoundData sound);

    void play(std::string name)
    { 
		for (auto i = 0U; i < sounds.size(); i++) {
		    if (sounds[i].name == name) {
		        sounds[i].play = true; 
		        return;
		    }
		}
    }

	template <class Archive>
	void serialize(Archive& archive)
	{
		archive(CEREAL_NVP(sounds));
	}
};
