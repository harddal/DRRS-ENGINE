#include "Engine/Engine.h"

#include <IMGUI/imgui.h>

#include "Debug/DebugState.h"
#include "Engine/Renderer/DecalManager.h"
#include "Editor/EditorState.h"
#include "Editor/EditorGameState.h"
#include "Game/IntroState.h"
#include "Game/MenuState.h"
#include "Game/GameState.h"
#include "Game/EndGameState.h"
#include "Resource/FilePaths.h"

#include "Utility/Utility.h"

#include "Game/DialogManager.h"

#include <windows.h>

Engine* Engine::s_Instance = nullptr;

Engine::Engine(const std::string& name, const std::string& args) :
	m_exit(false), m_isGameMode(false), m_isEditorMode(false), m_drawConsole(false),
	m_EnableDefaultStatsDrawing(false), m_EnableDebugConsole(false), m_EnableGameDebugFeatures(false),
	m_currentTick(0), m_lastTick(0), m_deltaTime(0.0f), m_renderManager(name, args)
{
    m_cmdLineArgs = args;

	setEditorMode(Utility::GetCmdlOptionExists(m_cmdLineArgs, "editor"));

	if (!isEditorMode())
	{
		if (Utility::GetCmdlOptionExists(m_cmdLineArgs, "console"))
		{
			m_EnableDebugConsole = true;
		}
		if (Utility::GetCmdlOptionExists(m_cmdLineArgs, "debug"))
		{
			m_EnableDefaultStatsDrawing = true;
			m_EnableDebugConsole = true;
			m_EnableGameDebugFeatures = true;
		}
	}
	else
	{
		// The editor is a dev environment — the in-game debug console (tilde)
		// is always available when playtesting via EditorGameState. Input
		// blocking while it is open is handled in update() (consoleBlocking).
		m_EnableDebugConsole = true;
	}

    if (s_Instance)
    {
        Utility::Error("Pointer to class \'Engine\' is invalid");
    }
    s_Instance = this;
	
    m_materialBuilder.buildMaterialTable();

	DialogManager::LoadDialogs();

	StartCounter();
}
Engine::~Engine()
{
    // BUG: Causes stack overflow on exit
    //delete s_Instance;
}

void Engine::init()
{
#ifndef NDEBUG
	setGameDebugFeaturesEnabled(true);
#endif

	if (isEditorMode())
	{
		m_stateManager.setState(ESID_EDITOR);
	}
	else
	{
		if (Utility::GetCmdlOptionExists(m_cmdLineArgs, "debug"))
		{
			m_stateManager.setState(ESID_DEBUG);
		}
		// Bypass intro
		else if (Utility::GetCmdlOptionExists(m_cmdLineArgs, "nointro"))
		{
			m_stateManager.setState(ESID_MENU);
		}
		// Normal startup
		else
		{
			m_stateManager.setState(ESID_INTRO);
		}
	}
}

void Engine::update()
{
    while (m_renderManager.device()->run() && !m_exit)
    {
		// Title-bar X / ALT+F4 arrive here as a vetoable request (RenderManager
		// swallows the WM_CLOSE) so the editor can prompt about unsaved work.
		if (m_renderManager.consumeWindowCloseRequest())
			requestQuit();

		m_currentTick = static_cast<float>(GetCounter());

		// Calculate raw frame time in seconds
		float frameTime = (m_currentTick - m_lastTick) / 1000.0f;
		
		// Spiral of death protection: cap maximum frame time
		if (frameTime > m_maxFrameTime) {
			frameTime = m_maxFrameTime;
		}
		
		m_lastTick = m_currentTick;
		
		// Store variable frame time for UI updates (used after fixed loop)
		float variableDeltaMs = frameTime * 1000.0f;

		// --- Time scale ---------------------------------------------------
		// The hit-stop countdown runs on REAL frame time (outside the scaled
		// world clock) so a near-zero freeze always recovers on schedule.
		float effectiveScale = m_timeScale;
		if (m_hitStopRemainingMs > 0.0f)
		{
			m_hitStopRemainingMs -= frameTime * 1000.0f;
			effectiveScale = m_hitStopScale < m_timeScale ? m_hitStopScale : m_timeScale;
		}

		// Add (scaled) frame time to accumulator — fewer fixed steps per real
		// second slows logic + PhysX + simulation time coherently; rendering
		// below stays at full rate
		m_accumulator += frameTime * effectiveScale;
		
		// Initialization queue for new states
		if (m_stateManager.isNewState())
		{
			if (m_stateManager.next() != ESID_INVALID)
			{
				if (m_stateManager.current() != ESID_INVALID)
					m_stateManager.pauseState(m_stateManager.current());
				m_stateManager.initState(m_stateManager.next(), m_stateManager.getStateInitArgs());
				m_stateManager.cycleNextState();
			}
		}

		WorldManager::Get()->scriptSystem()->updateHotReloadWatch();

        m_renderManager.beginImGui();
        {
            if (m_isGameMode)
            {
				if (m_EnableDebugConsole)
				{
					static auto tilde_released = false;
					if (m_inputManager.getKeyRelease(KEY_TILDE, &tilde_released, true))
					{
						m_drawConsole = !m_drawConsole;

						InputManager::Get()->centerMouse();

						// Software cursor while the console is open (scrollback wheel/drag)
						ImGui::GetIO().MouseDrawCursor = m_drawConsole;

						m_gameConsole.clearInputBuffer();
					}
				}

				// Draw every frame (not just while open) so the slide-out
				// animation can finish after the console is toggled closed
				m_gameConsole.setOpen(m_drawConsole);
				m_gameConsole.draw(m_deltaTime);

				if (!m_drawConsole)
				{
					m_debugConsole.draw_stats();
				}
            }
			else if (m_drawConsole)
			{
				// Left game mode (e.g. ESC back to the editor) with the console
				// open — close it so its state and cursor don't leak into the editor
				m_drawConsole = false;
				m_gameConsole.setOpen(false);
				ImGui::GetIO().MouseDrawCursor = false;
			}
        }

		// Prepare write buffer for this frame's logic updates
		m_renderManager.clearGameWriteBuffer();
		bool logicUpdated = false;
		
		// Fixed timestep loop for physics and game logic
		const float fixedDeltaMs = m_fixedTimeStep * 1000.0f;
		while (m_accumulator >= m_fixedTimeStep)
		{
			logicUpdated = true;
			
			// Set delta time to fixed timestep so all game logic (weapons, scripts, etc.)
			// gets the correct fixed dt via getDeltaTime()
			m_deltaTime = fixedDeltaMs;
			
			// Increment simulation time for this fixed step
			m_simulationTime += fixedDeltaMs;
			
			// Update input at fixed timestep to match game logic sampling rate
			// Block keyboard input when ImGui has a text widget focused or console is open
			bool consoleBlocking = m_isGameMode && m_drawConsole;
			m_inputManager.update(!ImGui::GetIO().WantTextInput && !consoleBlocking);
			
			// Update world and game logic at fixed timestep
			m_worldManager.update(fixedDeltaMs);
			m_stateManager.update(fixedDeltaMs);
			
			// Physics update at fixed timestep
			if (m_isGameMode)
			{
				m_currentPhysicsTick = static_cast<float>(GetCounter());
				m_physicsManager.update(fixedDeltaMs);
				m_physicsTime = static_cast<float>(GetCounter()) - m_currentPhysicsTick;
			}
			
			m_accumulator -= m_fixedTimeStep;
		}
		
		// Only swap the render buffer when logic actually produced new content
		if (logicUpdated)
		{
			m_renderManager.swapGameBuffers();
		}
		
		// Calculate interpolation alpha for smooth rendering
		m_interpolationAlpha = m_accumulator / m_fixedTimeStep;
		
		// Restore variable frame time for UI updates and rendering
		m_deltaTime = variableDeltaMs;
		
		// Update UI with variable timestep (for smooth UI interactions)
		m_stateManager.updateUI(m_deltaTime);

		// Render — only when we have new content to display.
		// When VSync is on and no logic ran, skip the render pass to avoid black frames
		// caused by clearing the backbuffer with nothing new to draw.
		if (logicUpdated || !m_renderManager.Get()->getConfiguration().vSync)
		{
			m_currentRenderTick = static_cast<float>(GetCounter());
			m_renderManager.draw(m_deltaTime);
			m_renderTime = static_cast<float>(GetCounter()) - m_currentRenderTick;
		}

		// Frame rate limiting (spin-wait using high-performance counter)
		int frameLimit = m_renderManager.Get()->getConfiguration().frameLimit;
		if (!m_renderManager.Get()->getConfiguration().vSync && frameLimit > 0)
		{
			double targetFrameTimeMs = 1000.0 / static_cast<double>(frameLimit);
			double frameStartMs = static_cast<double>(m_currentTick);

			while ((GetCounter() - frameStartMs) < targetFrameTimeMs)
			{
				// Spin until target frame time reached
			}
		}
    }
}

Engine::StateManager::StateManager() : m_current(ESID_INVALID), m_next(ESID_INVALID), m_previous(ESID_INVALID), m_initNewState(false)
{
    m_states.push_back(std::make_unique<DebugState>(ESID_DEBUG));
    m_states.push_back(std::make_unique<IntroState>(ESID_INTRO));
    m_states.push_back(std::make_unique<MenuState>(ESID_MENU));
    m_states.push_back(std::make_unique<GameState>(ESID_GAME));
    m_states.push_back(std::make_unique<EndGameState>(ESID_ENDGAME));
    m_states.push_back(std::make_unique<EditorState>(ESID_EDITOR));
    m_states.push_back(std::make_unique<EditorGameState>(ESID_EDITORGAME));
}

Engine::StateManager::~StateManager()
{
    for (auto i = 0U; i < m_states.size(); i++) 
	{
        m_states[i]->destroy();
    }

    m_states.clear();
    std::vector<std::unique_ptr<EngineState>>().swap(m_states);
}

void Engine::StateManager::update(irr::f32 dt)
{
    if (m_current < ESID_INVALID) 
	{
        if (m_states[m_current]->hasInitialized()) 
		{
            m_states[m_current]->update(dt);
        }
    }
}

void Engine::StateManager::updateUI(irr::f32 dt)
{
    if (m_current < ESID_INVALID) 
	{
        if (m_states[m_current]->hasInitialized()) 
		{
            m_states[m_current]->updateUI(dt);
        }
    }
}

ENGINE_STATE_ID Engine::StateManager::setState(ENGINE_STATE_ID state, const std::string& args)
{
    if (state != ESID_INVALID)
	{
        if (m_states[state]->hasInitialized() &&
            static_cast<unsigned int>(state) != m_current)
        {
            destroyState(state);
        }

        m_initNewState = true;
		m_next = static_cast<unsigned int>(state);
		m_args = args;
    }

    return state;
}

ENGINE_STATE_ID Engine::StateManager::setStatePauseResume(ENGINE_STATE_ID state, const std::string& args)
{
    if (state != ESID_INVALID) 
	{
		if (!m_states[m_current]->hasInitialized())
		{
			spdlog::error("Current active state \'{0}\' is not initialized in StateManager::setStatePauseResume()", m_current);
			return ESID_INVALID;
		}
		if (!m_states[state]->hasInitialized())
		{
			spdlog::error("Next state \'{0}\' is not initialized in StateManager::setStatePauseResume()", static_cast<irr::u32>(state));
			return ESID_INVALID;
		}
    	
        m_states[m_current]->pause();
		m_states[m_current]->setPaused();

		m_states[state]->setPaused(false);
        m_states[state]->resume();

		m_current = static_cast<unsigned int>(state);

		m_args = args;
    }
	
    return state;
}

ENGINE_STATE_ID Engine::StateManager::initState(ENGINE_STATE_ID state, const std::string& args)
{
    if (state != ESID_INVALID) 
	{
        if (!m_states[state]->hasInitialized()) 
		{
            m_states[state]->init(args);
            m_states[state]->setInitialized();
        }
    }
    return state;
}

ENGINE_STATE_ID Engine::StateManager::destroyState(ENGINE_STATE_ID state)
{
    if (state != ESID_INVALID && m_states[state]->hasInitialized()) 
	{
    	if (m_states[state]->getID() == m_current)
    	{
			spdlog::error("StateManager: Cannot destroy currently active state");
			return state;
    	}
    	
        m_states[state]->setInitialized(false);
        m_states[state]->destroy();

		if (m_next == state)
		{
			m_next = ESID_INVALID;
		}
    	if (m_previous == state)
    	{
			m_previous = ESID_INVALID;
    	}
    }
    return state;
}

ENGINE_STATE_ID Engine::StateManager::pauseState(ENGINE_STATE_ID state)
{
    if (state != ESID_INVALID) 
	{
        m_states[state]->pause();
		m_states[state]->setPaused();
    }
    return state;
}

ENGINE_STATE_ID Engine::StateManager::resumeState(ENGINE_STATE_ID state)
{
    if (state != ESID_INVALID) 
	{
        m_states[state]->resume();
		m_states[state]->setPaused(false);
    }
    return state;
}

void Engine::StartCounter()
{
	LARGE_INTEGER li;
	if (!QueryPerformanceFrequency(&li))
		std::cout << "QueryPerformanceFrequency failed!\n";

	PCFreq = double(li.QuadPart) / 1000.0;

	QueryPerformanceCounter(&li);
	CounterStart = li.QuadPart;
}

double Engine::GetCounter()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return double(li.QuadPart - CounterStart) / PCFreq;
}

void Engine::clearScene()
{
	ParticleManager::Get()->clear();

	if (RenderManager::Get()->decals())
		RenderManager::Get()->decals()->clear();

	WorldManager::Get()->killAllEntities();

	// Safety net: any viewmodel/LDR-effect registration still alive here points
	// at a node deleted with the scene — drop them before the next render pass.
	RenderManager::Get()->clearEffectNodeRegistrations();

	// Mode-switch safety: never carry bullet time or an active hit-stop across scenes
	m_timeScale = 1.0f;
	m_hitStopRemainingMs = 0.0f;

	WorldManager::Get()->clearCVars();

	SoundManager::Get()->sound()->soloud().stopAll();

	PhysicsManager::Get()->destroyScene();
}