#include "Engine/Sound/SoundEngine.h"

#include <algorithm>
#include <cstdio>
#include <random>
#include <spdlog/spdlog.h>

// --- SoundHandle ---

void SoundHandle::setPosition(float x, float y, float z)
{
	if (m_handle && m_engine)
		m_engine->soloud().set3dSourcePosition(m_handle, x, y, -z);
}

void SoundHandle::stop()
{
	if (m_handle && m_engine)
		m_engine->soloud().stop(m_handle);
	m_handle = 0;
}

void SoundHandle::setPaused(bool paused)
{
	if (m_handle && m_engine)
		m_engine->soloud().setPause(m_handle, paused);
}

// --- SoundEngine ---

SoundEngine::SoundEngine()
{
	// 512-sample buffer (~11.6ms @ 44100Hz) keeps audio-onset jitter tight enough
	// that rapid-fire sounds at 250ms intervals stay perceptually in sync.
	// The default 2048-sample buffer (~46ms) produces audible timing drift at that rate.
	SoLoud::result result = m_soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::XAUDIO2, 44100, 512);
	if (result != SoLoud::SO_NO_ERROR)
	{
		spdlog::warn("SoLoud: XAudio2 init failed ({}), falling back to auto-detect", m_soloud.getErrorString(result));
		result = m_soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::AUTO, 44100, 512);
	}
	if (result != SoLoud::SO_NO_ERROR)
		spdlog::error("SoLoud: init failed: {}", m_soloud.getErrorString(result));
	else
		spdlog::info("SoLoud {}", SOLOUD_VERSION);

	// Default ceiling is 16 — far too low for a scene with rapid-fire weapons,
	// 3D shell bounces, and ambient sounds all stacking simultaneously.
	m_soloud.setMaxActiveVoiceCount(64);
}

SoundEngine::~SoundEngine()
{
	m_soloud.deinit();
}

SoundSource* SoundEngine::loadOrGetSource(const char* file)
{
	auto it = m_sources.find(file);
	if (it != m_sources.end())
		return it->second.get();

	auto src = std::make_unique<SoundSource>();
	SoLoud::result result = src->wav.load(file);
	if (result != SoLoud::SO_NO_ERROR)
	{
		spdlog::warn("SoundEngine: failed to load '{}'", file);
		return nullptr;
	}

	// SoLoud defaults to NO_ATTENUATION and minDist=1. Set inverse distance rolloff
	// with a larger min distance so sounds stay at full volume within a reasonable
	// radius before falling off. Override per-source with setDefaultMinDistance().
	src->wav.set3dAttenuation(SoLoud::AudioSource::INVERSE_DISTANCE, 2.0f);
	src->wav.set3dMinMaxDistance(30.0f, 100000.0f);

	SoundSource* ptr = src.get();
	m_sources.emplace(file, std::move(src));
	return ptr;
}

SoundSource* SoundEngine::getSoundSource(const char* file, bool preload)
{
	auto it = m_sources.find(file);
	if (it != m_sources.end())
		return it->second.get();
	if (preload)
		return loadOrGetSource(file);
	return nullptr;
}

SoundSource* SoundEngine::addSoundSourceFromFile(const char* file, bool preload)
{
	return loadOrGetSource(file);
}

// Prunes finished per-source voices then stops the oldest if at the limit.
void SoundEngine::enforceVoiceLimit(SoundSource* source, int maxConcurrent)
{
	if (maxConcurrent <= 0) return;

	auto& voices = m_voicePool[source];
	voices.erase(
		std::remove_if(voices.begin(), voices.end(),
			[this](SoLoud::handle h) { return !m_soloud.isValidVoiceHandle(h); }),
		voices.end());

	while ((int)voices.size() >= maxConcurrent)
	{
		m_soloud.stop(voices.front());
		voices.erase(voices.begin());
	}
}

// Prunes finished group voices then stops the oldest if at the limit.
// Voices from different source files that share the same group name count together.
void SoundEngine::enforceGroupVoiceLimit(const char* group, int maxConcurrent)
{
	if (maxConcurrent <= 0) return;

	auto& voices = m_groupPool[group];
	voices.erase(
		std::remove_if(voices.begin(), voices.end(),
			[this](SoLoud::handle h) { return !m_soloud.isValidVoiceHandle(h); }),
		voices.end());

	while ((int)voices.size() >= maxConcurrent)
	{
		m_soloud.stop(voices.front());
		voices.erase(voices.begin());
	}
}

SoundHandle SoundEngine::play2D(const char* file, bool loop, int maxConcurrent, float volume, const char* poolGroup, bool startPaused, float playbackSpeed)
{
	SoundSource* src = loadOrGetSource(file);
	if (!src) return {};
	return play2D(src, loop, maxConcurrent, volume, poolGroup, startPaused, playbackSpeed);
}

SoundHandle SoundEngine::play2D(SoundSource* source, bool loop, int maxConcurrent, float volume, const char* poolGroup, bool startPaused, float playbackSpeed)
{
	if (!source) return {};

	if (poolGroup)
		enforceGroupVoiceLimit(poolGroup, maxConcurrent);
	else
		enforceVoiceLimit(source, maxConcurrent);

	SoLoud::handle h = m_soloud.play(source->wav, volume, 0.0f, startPaused);
	if (loop) m_soloud.setLooping(h, true);
	if (playbackSpeed != 1.0f) m_soloud.setRelativePlaySpeed(h, playbackSpeed);

	if (maxConcurrent > 0)
	{
		if (poolGroup)
			m_groupPool[poolGroup].push_back(h);
		else
			m_voicePool[source].push_back(h);
	}

	return SoundHandle(h, this);
}

SoundHandle SoundEngine::play3D(const char* file, irr::core::vector3df pos, bool loop, bool startPaused, bool track, int maxConcurrent, float volume, const char* poolGroup, float playbackSpeed)
{
	SoundSource* src = loadOrGetSource(file);
	if (!src) return {};
	return play3D(src, pos, loop, startPaused, track, maxConcurrent, volume, poolGroup, playbackSpeed);
}

SoundHandle SoundEngine::play3D(SoundSource* source, irr::core::vector3df pos, bool loop, bool startPaused, bool track, int maxConcurrent, float volume, const char* poolGroup, float playbackSpeed)
{
	if (!source) return {};

	if (poolGroup)
		enforceGroupVoiceLimit(poolGroup, maxConcurrent);
	else
		enforceVoiceLimit(source, maxConcurrent);

	SoLoud::handle h = m_soloud.play3d(source->wav, pos.X, pos.Y, -pos.Z,
		0.f, 0.f, 0.f,   // velocity
		volume,
		startPaused);
	if (loop) m_soloud.setLooping(h, true);
	if (playbackSpeed != 1.0f) m_soloud.setRelativePlaySpeed(h, playbackSpeed);

	if (maxConcurrent > 0)
	{
		if (poolGroup)
			m_groupPool[poolGroup].push_back(h);
		else
			m_voicePool[source].push_back(h);
	}

	return SoundHandle(h, this);
}

// --- Randomized variant playback ---

// Quiet existence probe — matches SoLoud's stdio loading, avoids the
// loadOrGetSource() warning while scanning for variants that may not exist.
static bool fileExists(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

SoundEngine::VariantSet& SoundEngine::getVariantSet(const char* basePath)
{
	auto it = m_variantSets.find(basePath);
	if (it != m_variantSets.end())
		return it->second;

	VariantSet set;

	// Contiguous scan: base1.wav, base2.wav, ... stop at the first gap
	for (int i = 1;; i++)
	{
		std::string candidate = std::string(basePath) + std::to_string(i) + ".wav";
		if (!fileExists(candidate))
			break;
		if (SoundSource* src = loadOrGetSource(candidate.c_str()))
			set.sources.push_back(src);
	}

	// No numbered variants — fall back to the plain un-numbered file
	if (set.sources.empty())
	{
		std::string plain = std::string(basePath) + ".wav";
		if (SoundSource* src = fileExists(plain) ? loadOrGetSource(plain.c_str()) : nullptr)
			set.sources.push_back(src);
		else
			spdlog::warn("SoundEngine: no variants or fallback found for '{}'", basePath);
	}
	else if (set.sources.size() > 1)
	{
		spdlog::info("SoundEngine: '{}' -> {} variants", basePath, set.sources.size());
	}

	return m_variantSets.emplace(basePath, std::move(set)).first->second;
}

SoundSource* SoundEngine::pickVariant(const char* basePath)
{
	VariantSet& set = getVariantSet(basePath);
	if (set.sources.empty())
		return nullptr;

	static std::mt19937 rng{ std::random_device{}() };
	int index = (int)(rng() % set.sources.size());

	// Re-roll once to make back-to-back repeats rare
	if ((int)set.sources.size() > 1 && index == set.lastIndex)
		index = (int)(rng() % set.sources.size());

	set.lastIndex = index;
	return set.sources[index];
}

float SoundEngine::jitteredSpeed(float pitchJitter)
{
	if (pitchJitter <= 0.0f)
		return 1.0f;
	static std::mt19937 rng{ std::random_device{}() };
	std::uniform_real_distribution<float> dist(-pitchJitter, pitchJitter);
	return 1.0f + dist(rng);
}

SoundHandle SoundEngine::playRandomized2D(const char* basePath, float pitchJitter, int maxConcurrent, float volume, const char* poolGroup)
{
	SoundSource* src = pickVariant(basePath);
	if (!src) return {};
	return play2D(src, false, maxConcurrent, volume, poolGroup, false, jitteredSpeed(pitchJitter));
}

SoundHandle SoundEngine::playRandomized3D(const char* basePath, irr::core::vector3df pos, float pitchJitter, int maxConcurrent, float volume, const char* poolGroup)
{
	SoundSource* src = pickVariant(basePath);
	if (!src) return {};
	return play3D(src, pos, false, false, true, maxConcurrent, volume, poolGroup, jitteredSpeed(pitchJitter));
}

void SoundEngine::setSoundVolume(float volume)
{
	m_soloud.setGlobalVolume(volume);
}

void SoundEngine::update3dAudio()
{
	m_soloud.update3dAudio();
}

void SoundEngine::setListenerPosition(float px, float py, float pz,
	float lookX, float lookY, float lookZ,
	float upX, float upY, float upZ)
{
	// Negate Z to convert from Irrlicht's left-handed coordinate system to SoLoud's
	// right-handed system. Applied consistently to listener and sources so relative
	// positions are correct and the derived right vector (cross(at,up)) points +X.
	m_soloud.set3dListenerParameters(px, py, -pz, lookX, lookY, -lookZ, upX, upY, -upZ);
}

void SoundEngine::stopAllVoices()
{
	m_soloud.stopAll();
}

void SoundEngine::removeAllSoundSources()
{
	m_soloud.stopAll();
	m_sources.clear();
	// These hold raw SoundSource* / voice handles into the sources we just freed
	m_variantSets.clear();
	m_voicePool.clear();
	m_groupPool.clear();
}
