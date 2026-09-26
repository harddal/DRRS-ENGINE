#pragma once

#include <string>
#include <unordered_map>
#include <memory>

#include <soloud.h>
#include <soloud_wav.h>
#include <soloud_biquadresonantfilter.h>

#include "irrlicht.h"

class SoundEngine;

// Wraps a SoLoud::Wav — drop-in for irrklang::ISoundSource*
class SoundSource
{
public:
	SoLoud::Wav wav;

	void setDefaultMinDistance(float dist) { wav.set3dMinMaxDistance(dist, 100000.0f); }
	void setDefaultVolume(float vol) { wav.setVolume(vol); }
	void drop() {}  // no-op: SoundEngine owns all sources
};

// Wraps a SoLoud voice handle — drop-in for irrklang::ISound*
// Supports pointer-like syntax: if (h), h->stop(), h->drop(), h = nullptr
class SoundHandle
{
	friend class SoundEngine;
public:
	SoundHandle() = default;
	SoundHandle(std::nullptr_t) {}
	SoundHandle& operator=(std::nullptr_t) { m_handle = 0; m_engine = nullptr; return *this; }

	explicit operator bool() const { return m_handle != 0; }
	SoundHandle* operator->() { return this; }

	void setPosition(float x, float y, float z);
	void setPosition(const irr::core::vector3df& pos) { setPosition(pos.X, pos.Y, pos.Z); }
	void stop();
	void setPaused(bool paused);
	void drop() { m_handle = 0; m_engine = nullptr; }
	SoLoud::handle handle() const { return m_handle; }

private:
	SoundHandle(SoLoud::handle h, SoundEngine* engine) : m_handle(h), m_engine(engine) {}

	SoLoud::handle m_handle = 0;
	SoundEngine*   m_engine = nullptr;
};

// Drop-in replacement for irrklang::ISoundEngine*
class SoundEngine
{
public:
	SoundEngine();
	~SoundEngine();

	// 2D (non-positional) playback
	// maxConcurrent: max simultaneous voices (0 = unlimited). When the limit is hit the oldest voice
	//   is stopped before the new one starts.
	// volume: per-voice scale (default 1.0). Reduce when stacking to prevent SoLoud's additive mixer
	//   from clipping, e.g. 0.35f with maxConcurrent=3 keeps the sum near 1.0.
	// poolGroup: optional shared-pool name. Voices from different source files that share the same
	//   group count against one concurrent limit (e.g. fire1/2/3.wav treated as a single pool).
	// playbackSpeed: relative play speed (1.0 = normal). Doubles as a pitch shift — 1.05 is ~a
	//   semitone up. Use small scatter (±0.03–0.08) to de-machine-gun repeated samples.
	SoundHandle play2D(const char*   file,   bool loop = false, int maxConcurrent = 0, float volume = -1.0f, const char* poolGroup = nullptr, bool startPaused = false, float playbackSpeed = 1.0f);
	SoundHandle play2D(SoundSource* source,  bool loop = false, int maxConcurrent = 0, float volume = -1.0f, const char* poolGroup = nullptr, bool startPaused = false, float playbackSpeed = 1.0f);

	// 3D (positional) playback — 'track' parameter accepted but ignored (always returns handle)
	SoundHandle play3D(const char*   file,   irr::core::vector3df pos, bool loop = false, bool startPaused = false, bool track = true, int maxConcurrent = 0, float volume = -1.0f, const char* poolGroup = nullptr, float playbackSpeed = 1.0f);
	SoundHandle play3D(SoundSource* source,  irr::core::vector3df pos, bool loop = false, bool startPaused = false, bool track = true, int maxConcurrent = 0, float volume = -1.0f, const char* poolGroup = nullptr, float playbackSpeed = 1.0f);

	// Randomized variant playback. 'basePath' has NO extension: "…/pistol/fire" plays a random
	// one of fire1.wav..fireN.wav (contiguous scan from 1, discovered once and cached), falling
	// back to plain fire.wav when no numbered variants exist. The previous pick is re-rolled
	// once so back-to-back repeats are rare. pitchJitter scatters playback speed by ±jitter.
	SoundHandle playRandomized2D(const char* basePath, float pitchJitter = 0.05f, int maxConcurrent = 0, float volume = -1.0f, const char* poolGroup = nullptr);
	SoundHandle playRandomized3D(const char* basePath, irr::core::vector3df pos, float pitchJitter = 0.05f, int maxConcurrent = 0, float volume = -1.0f, const char* poolGroup = nullptr);

	// Source management — same file is only loaded once
	SoundSource* getSoundSource(const char* file, bool preload = false);
	SoundSource* addSoundSourceFromFile(const char* file, bool preload = true);

	// Master volume (0.0 - 1.0)
	void setSoundVolume(float volume);

	// Call once per frame after updating all 3D source positions
	void update3dAudio();

	// Set listener transform for 3D audio (call before update3dAudio each frame)
	void setListenerPosition(float px, float py, float pz,
		float lookX, float lookY, float lookZ,
		float upX = 0.f, float upY = 1.f, float upZ = 0.f);

	// velocity is ignored (SoLoud does not use it)
	void setListenerPosition(irr::core::vector3df pos, irr::core::vector3df look, irr::core::vector3df /*velocity*/, irr::core::vector3df up)
	{
		setListenerPosition(pos.X, pos.Y, pos.Z, look.X, look.Y, look.Z, up.X, up.Y, up.Z);
	}

	// --- Underwater muffle ---------------------------------------------------
	// A lowpass biquad occupies SoLoud's global (master) filter slot for the whole
	// life of the engine; submerging only fades its wet mix in. The filter is
	// never attached or detached at runtime -- setGlobalFilter() destroys and
	// rebuilds the filter instance, which pops audibly and has to be raced
	// against the mixer thread.
	//
	// Being global, this muffles EVERYTHING, 2D included: music, UI clicks and the
	// player's own weapon sounds go under with the 3D world. That is deliberate --
	// it is what submersion sounds like, and it is how Source-era games do it.
	void setUnderwater(bool underwater, float fadeSeconds = 0.25f);
	bool isUnderwater() const { return m_underwater; }

	// Tuning knobs, exposed for the 'snd_underwater' console command. cutoffHz is
	// the lowpass corner (lower = more muffled); resonance is the biquad Q, which
	// adds the slight bottled ring around the corner. Safe to call while
	// submerged -- the change is audible immediately.
	void setUnderwaterParams(float cutoffHz, float resonance);
	float underwaterCutoff()    const { return m_underwaterCutoff; }
	float underwaterResonance() const { return m_underwaterResonance; }

	void stopHandle(SoLoud::handle h) { if (h) m_soloud.stop(h); }
	void stopAllVoices();   // stop all playing voices without unloading sources
	void removeAllSoundSources();
	void drop() {}  // compatibility shim — SoundManager::~SoundManager calls drop()

	SoLoud::Soloud& soloud() { return m_soloud; }

private:
	SoundSource* loadOrGetSource(const char* file);
	void enforceVoiceLimit(SoundSource* source, int maxConcurrent);
	void enforceGroupVoiceLimit(const char* group, int maxConcurrent);

	// Discovered numbered-variant sets, keyed by extensionless base path
	struct VariantSet
	{
		std::vector<SoundSource*> sources;
		int lastIndex = -1;   // avoid immediate repeats
	};
	VariantSet& getVariantSet(const char* basePath);
	SoundSource* pickVariant(const char* basePath);
	static float jitteredSpeed(float pitchJitter);

	// Global filter slot the underwater lowpass lives in. Slot 0 of 8
	// (FILTERS_PER_STREAM); the rest stay free for future master effects.
	static constexpr unsigned int k_underwaterSlot = 0;

	SoLoud::Soloud m_soloud;

	// Declared after m_soloud so it is destroyed FIRST -- but ~Soloud deletes the
	// filter INSTANCE, which holds a back-pointer to this parent. ~SoundEngine
	// therefore clears the slot explicitly before deinit(); do not rely on
	// member order here.
	SoLoud::BiquadResonantFilter m_underwaterFilter;
	bool  m_underwater          = false;
	float m_underwaterCutoff    = 800.0f;   // Hz
	float m_underwaterResonance = 2.0f;

	std::unordered_map<std::string, std::unique_ptr<SoundSource>> m_sources;
	std::unordered_map<SoundSource*, std::vector<SoLoud::handle>>  m_voicePool;
	std::unordered_map<std::string,  std::vector<SoLoud::handle>>  m_groupPool;
	std::unordered_map<std::string,  VariantSet>                   m_variantSets;
};
