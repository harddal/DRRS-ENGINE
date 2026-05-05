#include "Game/IntroState.h"

#include <algorithm>

#include "Engine/Engine.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Resource/FilePaths.h"
#include "Engine/Sound/SoundManager.h"

void IntroState::init(std::string args)
{
	ImGui::GetIO().MouseDrawCursor = false;

	RenderManager::Get()->tonemapCallback()->exposure = 0.0f;

	m_skipKeyWasDown = false;
	m_skipAllKeyWasDown = false;

	m_slideshow.init({
		// { imagePath, soundPath, displayTime, fadeIn, fadeOut }
		{ "content/texture/intro/drrs_game_studio.png",  "content/sound/intro/logo_intro.wav", 3.0f, 0.5f, 0.5f },
		{ "content/texture/intro/engine_logo.png", "",     1.0f, 0.5f, 0.5f },
		{ "content/texture/intro/physx_logo.png",  "",     1.0f, 0.5f, 0.5f },
		{ "content/texture/intro/tech_logo_stack.png", "", 1.0f, 0.5f, 0.5f },
		});
}

void IntroState::update(float dt)
{
	m_slideshow.update();
	m_slideshow.render();

	if (m_slideshow.isComplete())
		Engine::Get()->stateManager()->setState(ESID_MENU);
}

void IntroState::updateUI(float dt)
{
	auto* input = InputManager::Get();

	// ESC — skip entire slideshow
	bool escDown = input->isKeyPressed(KEYBOARD_KEY::KEY_ESCAPE, true);
	if (escDown && !m_skipAllKeyWasDown)
		m_slideshow.skipAll();
	m_skipAllKeyWasDown = escDown;

	// Any other key or mouse button — advance one slide
	bool skipDown = input->isKeyPressed(KEYBOARD_KEY::KEY_RETURN, true)
		|| input->isKeyPressed(KEYBOARD_KEY::KEY_SPACE, true);
	/* || input->isMouseButtonPressed(0)
	 || input->isMouseButtonPressed(1);*/
	if (skipDown && !m_skipKeyWasDown)
		m_slideshow.skip();
	m_skipKeyWasDown = skipDown;
}

void IntroState::destroy()
{
	RenderManager::Get()->tonemapCallback()->exposure = 10.0f;
}

void IntroState::pause()
{
	RenderManager::Get()->tonemapCallback()->exposure = 10.0f;
}

void IntroState::resume()
{
}

void IntroSlideshow::init(std::vector<SlideDefinition> slides)
{
    m_slides    = std::move(slides);
    m_index     = 0;
    m_phase     = Phase::FadeIn;
    m_complete  = m_slides.empty();
    m_texture   = nullptr;
    m_phaseStart = Clock::now();

    if (!m_complete)
        loadCurrentTexture();
}

void IntroSlideshow::update()
{
    if (m_complete || m_slides.empty())
        return;

    const SlideDefinition& slide = m_slides[m_index];
    float elapsed = std::chrono::duration<float>(Clock::now() - m_phaseStart).count();

    switch (m_phase)
    {
    case Phase::FadeIn:
        if (elapsed >= slide.fadeInDuration)
        {
            m_phase      = Phase::Hold;
            m_phaseStart = Clock::now();
        }
        break;

    case Phase::Hold:
        if (elapsed >= slide.displayTime)
        {
            m_phase      = Phase::FadeOut;
            m_phaseStart = Clock::now();
        }
        break;

    case Phase::FadeOut:
        if (elapsed >= slide.fadeOutDuration)
            advanceSlide();
        break;
    }
}

void IntroSlideshow::render()
{
    if (m_complete || !m_texture)
        return;

    auto* rm = RenderManager::Get();

    irr::u32 alpha = currentAlpha();
    irr::core::rect<irr::s32> destRect = computeLetterboxRect(m_texture->getOriginalSize());

    rm->renderImage2DScaled(
        m_texture,
        destRect,
        irr::video::SColor(alpha, 255, 255, 255),
        true);
}

void IntroSlideshow::skip()
{
    if (m_complete)
        return;

    // If already fading out, advance immediately; otherwise jump to fade-out
    if (m_phase == Phase::FadeOut)
    {
        advanceSlide();
    }
    else
    {
        m_phase      = Phase::FadeOut;
        m_phaseStart = Clock::now();
    }
}

void IntroSlideshow::skipAll()
{
    m_complete = true;
    m_soundHandle.stop();
}

// ─── Private helpers ─────────────────────────────────────────────────────────

void IntroSlideshow::loadCurrentTexture()
{
    if (m_index >= (int)m_slides.size())
        return;

    const SlideDefinition& slide = m_slides[m_index];
    m_texture = RenderManager::Get()->driver()->getTexture(slide.imagePath.c_str());

    if (!slide.soundPath.empty())
        m_soundHandle = SoundManager::Get()->sound()->play2D(slide.soundPath.c_str());
}

void IntroSlideshow::advanceSlide()
{
    m_soundHandle.stop();
    ++m_index;

    if (m_index >= (int)m_slides.size())
    {
        m_complete = true;
        m_texture  = nullptr;
        return;
    }

    m_phase      = Phase::FadeIn;
    m_phaseStart = Clock::now();
    loadCurrentTexture();
}

irr::core::rect<irr::s32> IntroSlideshow::computeLetterboxRect(irr::core::dimension2du texSize) const
{
    const auto& cfg = RenderManager::Get()->getConfiguration();
    float scaleX = (float)cfg.width  / (float)texSize.Width;
    float scaleY = (float)cfg.height / (float)texSize.Height;
    float scale  = std::min(scaleX, scaleY);
    int dstW = (int)(texSize.Width  * scale);
    int dstH = (int)(texSize.Height * scale);
    int dstX = (cfg.width  - dstW) / 2;
    int dstY = (cfg.height - dstH) / 2;
    return { dstX, dstY, dstX + dstW, dstY + dstH };
}

irr::u32 IntroSlideshow::currentAlpha() const
{
    const SlideDefinition& slide = m_slides[m_index];
    float elapsed = std::chrono::duration<float>(Clock::now() - m_phaseStart).count();

    switch (m_phase)
    {
    case Phase::FadeIn:
    {
        float t = (slide.fadeInDuration > 0.0f) ? elapsed / slide.fadeInDuration : 1.0f;
        return (irr::u32)(255.0f * (std::min)(t, 1.0f));
    }
    case Phase::Hold:
        return 255u;
    case Phase::FadeOut:
    {
        float t = (slide.fadeOutDuration > 0.0f) ? elapsed / slide.fadeOutDuration : 1.0f;
        return (irr::u32)(255.0f * (std::max)(1.0f - t, 0.0f));
    }
    }
    return 255u;
}
