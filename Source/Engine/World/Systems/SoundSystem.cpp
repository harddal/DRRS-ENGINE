#include "SoundSystem.h"

#include "Engine/Engine.h"
#include "Game/IPlayerController.h"

void SoundComponent::add(sSoundData sound)
{
	sounds.push_back(sound);
}

void SoundSystem::onEntityAdded(anax::Entity& entity)
{
    auto& sounds = entity.getComponent<SoundComponent>().sounds;

    for (auto i = 0U; i < sounds.size(); i++) {
		auto src = SoundManager::Get()->sound()->getSoundSource(sounds[i].file.c_str(), false);
        if (src) {
			sounds[i].source = src;
        }
		else {
			sounds[i].source = SoundManager::Get()->sound()->addSoundSourceFromFile(sounds[i].file.c_str());
			if (!sounds[i].source) {
				spdlog::error("Failed to load sound file \'" + sounds[i].file + "\' in entity \'" + entity.getComponent<DescriptorComponent>().name + "\'");
			}
		}

        // A missing file leaves source null. play2D/play3D already treat that as
        // a silent no-op, so the entry is kept (its play flag still works) and
        // only these two setters need guarding -- they used to crash the spawn.
        if (sounds[i].source) {
            sounds[i].source->setDefaultMinDistance(sounds[i].minDist);
            sounds[i].source->setDefaultVolume(sounds[i].volume / 100.0f);
        }

		if (sounds[i].startPaused) {
		    sounds[i].play = false;
		}
		else {
		    sounds[i].play = true;
		}
        
    }
}

void SoundSystem::onEntityRemoved(anax::Entity& entity)
{
	for (auto& s : entity.getComponent<SoundComponent>().sounds) {
		if (s.sound) {
			s.sound->stop();
			s.sound->drop();
		}
	}
}

void SoundSystem::update()
{
    auto& entities = getEntities();
    std::vector<SoundHandle> toUnpause;

    for (auto& entity : entities) {
        auto& sounds = entity.getComponent<SoundComponent>().sounds;

        for (auto i = 0U; i < sounds.size(); i++) {
			if (sounds[i].is3D) {
				if (sounds[i].sound) {
					auto pos = entity.getComponent<TransformComponent>().getPosition();
					sounds[i].sound->setPosition(pos);
				}
			}

            if (sounds[i].play && !sounds[i].loop) {
                if (sounds[i].is3D && entity.hasComponent<TransformComponent>()) {
                    auto pos = entity.getComponent<TransformComponent>().getPosition();

                    if (sounds[i].sound) {
						sounds[i].sound->drop();
                    }
					sounds[i].sound = SoundManager::Get()->sound()->play3D(
						sounds[i].source, pos, sounds[i].loop, true, true);
					toUnpause.push_back(sounds[i].sound);
                }
                else {
                    if (sounds[i].sound) {
						sounds[i].sound->drop();
                    }
					sounds[i].sound = SoundManager::Get()->sound()->play2D(sounds[i].source, sounds[i].loop);
                }

                sounds[i].isPlaying = false;
            }
            else
			if (sounds[i].play && sounds[i].loop && !sounds[i].isPlaying) {
                if (sounds[i].is3D && entity.hasComponent<TransformComponent>()) {
                    auto pos = entity.getComponent<TransformComponent>().getPosition();

					if (sounds[i].sound) {
						sounds[i].sound->stop();
					}
					sounds[i].sound = SoundManager::Get()->sound()->play3D(
						sounds[i].source, pos, sounds[i].loop, true, true);
					toUnpause.push_back(sounds[i].sound);
                }
                else {
                    if (sounds[i].sound) {
						sounds[i].sound->stop();
                    }
					sounds[i].sound = SoundManager::Get()->sound()->play2D(sounds[i].source, sounds[i].loop);
                }

                sounds[i].isPlaying = true;
            }

			if (!sounds[i].loop) {
				sounds[i].play = false;
			}
        }
    }

    // Underwater muffle. Keyed off the EYES, not the chest: GameplaySystem sets
    // both flags from separate probes, and swimming with your head above the
    // surface should still sound dry. setUnderwater() edge-guards internally, so
    // pushing the state every frame is free.
    //
    // A null controller reads as dry, which is what lifts the filter on a
    // game->edit transition — though GameManager::destroy() clears it outright,
    // because this update only runs in game mode.
    SoundManager::Get()->sound()->setUnderwater(
        g_PlayerController && g_PlayerController->isHeadUnderWater());

    SoundManager::Get()->sound()->update3dAudio();

    for (auto& h : toUnpause)
        h->setPaused(false);
}
