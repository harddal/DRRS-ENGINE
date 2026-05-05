#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "irrlicht.h"

#include "Engine/EngineState.h"
#include "Engine/Sound/SoundEngine.h"

struct SlideDefinition
{
    std::string imagePath;       // e.g. _asset_tex("intro/slide_engine")
    std::string soundPath;       // e.g. _asset_snd("intro/sting"), empty = silent
    float       displayTime;     // seconds to hold at full opacity
    float       fadeInDuration;  // seconds to fade in from black
    float       fadeOutDuration; // seconds to fade out to black
};

class IntroSlideshow
{
public:
    void init(std::vector<SlideDefinition> slides);
    void update();
    void render();
    void skip();
    void skipAll();
    bool isComplete() const { return m_complete; }

private:
    enum class Phase { FadeIn, Hold, FadeOut };

    void                              loadCurrentTexture();
    void                              advanceSlide();
    irr::core::rect<irr::s32>        computeLetterboxRect(irr::core::dimension2du texSize) const;
    irr::u32                          currentAlpha() const;

    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::vector<SlideDefinition>  m_slides;
    int                           m_index    = 0;
    Phase                         m_phase    = Phase::FadeIn;
    bool                          m_complete = false;
    irr::video::ITexture*         m_texture  = nullptr;
    SoundHandle                   m_soundHandle;
    TimePoint                     m_phaseStart;
};

class IntroState : public EngineState
{
public:
    explicit IntroState(ENGINE_STATE_ID id) : EngineState(id) {}

    void init(std::string args = "") override;
    void update(float dt) override;
    void updateUI(float dt) override;
    void destroy() override;

    void pause() override;
    void resume() override;

private:
    IntroSlideshow m_slideshow;

    bool m_skipKeyWasDown     = false;
    bool m_skipAllKeyWasDown  = false;
};
