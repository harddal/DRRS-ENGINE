#pragma once
#include <soloud.h>

class GeigerAudioSource;  // synthesiser — defined in GeigerEffect.cpp

class GeigerEffect
{
public:
    static GeigerEffect* Get();

    // Called from script bindings.
    void setEnabled(bool b);   // refcounted: voice starts on 0→1, stops on 1→0
    void setStrength(float s);
    void setGain(float g);
    void resetSession();       // called from BeginSoundSession on game mode restart
    void update(float dt) {}   // no-op — synthesis runs on the SoLoud audio thread

    bool  enabled  = false;
    float strength = 0.0f;

private:
    void ensureSource();

    GeigerAudioSource* m_source        = nullptr;
    SoLoud::handle     m_handle        = 0;
    int                m_enableRefCount = 0;
};
