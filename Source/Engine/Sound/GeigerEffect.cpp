#include "GeigerEffect.h"

#include "Engine/Sound/SoundManager.h"

#include <soloud_audiosource.h>

#include <atomic>
#include <cmath>
#include <random>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Per-sample Geiger counter synthesiser — Unity @kurtdekker approach ported
// to SoLoud's AudioSource/AudioSourceInstance pattern.
//
// Each "tick" is a decaying sinusoidal burst:
//   sample = gain * amplitude * sin(phase)
//   phase     += amplitude * 0.3   (slows as it decays)
//   amplitude *= 0.993             (decays to < 0.01 in ~460 samples, ~10 ms)
//
// A new tick is spawned with probability `chance` per sample on the audio
// thread.  chance = 1e-6 + 0.01 * strength^3 (unity cubic curve).
// ---------------------------------------------------------------------------

struct GeigerTick
{
    float amplitude = 1.0f;
    float phase     = 0.0f;
};

class GeigerAudioSourceInstance : public SoLoud::AudioSourceInstance
{
public:
    GeigerAudioSourceInstance(class GeigerAudioSource* parent);

    unsigned int getAudio(float* buf, unsigned int samplesToRead, unsigned int) override;
    bool hasEnded() override { return false; }

private:
    class GeigerAudioSource*      m_parent;
    std::vector<GeigerTick>       m_ticks;
    std::mt19937                  m_rng;
    std::uniform_real_distribution<float> m_uniform{ 0.0f, 1.0f };
};

class GeigerAudioSource : public SoLoud::AudioSource
{
public:
    GeigerAudioSource()
    {
        mBaseSamplerate = 44100.0f;
        mChannels       = 1;
        setLooping(true);
        setInaudibleBehavior(true, false);  // keep ticking even when inaudible
    }

    SoLoud::AudioSourceInstance* createInstance() override
    {
        return new GeigerAudioSourceInstance(this);
    }

    std::atomic<float> m_chance{ 1e-6f };
    std::atomic<float> m_gain  { 1.0f  };
};

GeigerAudioSourceInstance::GeigerAudioSourceInstance(GeigerAudioSource* parent)
    : m_parent(parent)
    , m_rng(std::random_device{}())
{
}

unsigned int GeigerAudioSourceInstance::getAudio(float* buf, unsigned int samplesToRead, unsigned int)
{
    const float gain   = m_parent->m_gain  .load(std::memory_order_relaxed);
    const float chance = m_parent->m_chance.load(std::memory_order_relaxed);

    for (unsigned int n = 0; n < samplesToRead; n++)
    {
        float sample = 0.0f;

        for (auto& t : m_ticks)
        {
			// Raise pitch → brighter, higher - pitched clicks
			// Lower pitch → duller, lower - pitched clicks
			// Lower decay toward 0.97 → shorter, snappier, less tonal
			// Raise decay toward 0.999 → longer ring, more tonal
			float pitch = 0.8f;
			float decay = 0.990f;

            sample      += gain * t.amplitude * sinf(t.phase);
            t.phase     += t.amplitude * pitch;
            t.amplitude *= decay;
        }

        buf[n] = sample;

        if (m_uniform(m_rng) < chance)
            m_ticks.push_back(GeigerTick{});

        m_ticks.erase(
            std::remove_if(m_ticks.begin(), m_ticks.end(),
                [](const GeigerTick& t){ return t.amplitude < 0.01f; }),
            m_ticks.end());
    }

    return samplesToRead;
}

// ---------------------------------------------------------------------------
// GeigerEffect
// ---------------------------------------------------------------------------

static GeigerEffect s_instance;
GeigerEffect* GeigerEffect::Get() { return &s_instance; }

void GeigerEffect::ensureSource()
{
    if (m_source) return;
    m_source = new GeigerAudioSource();
}

void GeigerEffect::setEnabled(bool b)
{
    m_enableRefCount += b ? 1 : -1;
    if (m_enableRefCount < 0) m_enableRefCount = 0;
    enabled = (m_enableRefCount > 0);

    auto& sl = SoundManager::Get()->sound()->soloud();

    if (enabled)
    {
        if (m_enableRefCount == 1)  // first enabler this session — start fresh
        {
            if (m_handle) { sl.stop(m_handle); m_handle = 0; }
            ensureSource();
            m_handle = sl.play(*m_source);
        }
    }
    else
    {
        if (m_handle) { sl.stop(m_handle); m_handle = 0; }
    }
}

void GeigerEffect::resetSession()
{
    // Called from BeginSoundSession when the game mode ends/restarts.
    // stopAllVoices() has already killed the SoLoud voice; just clear our state.
    m_enableRefCount = 0;
    m_handle         = 0;
    enabled          = false;
}

void GeigerEffect::setGain(float g)
{
    if (m_source)
        m_source->m_gain.store(g, std::memory_order_relaxed);
}

void GeigerEffect::setStrength(float s)
{
    strength = s;

    if (m_source)
    {
        const float bg   = 1e-6f;
        const float span = 0.01f;
        m_source->m_chance.store(bg + span * s * s * s, std::memory_order_relaxed);
    }
}
