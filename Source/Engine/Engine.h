#pragma once

#include <chrono>
#include <string>
#include <random>
#include <vector>

#include "Game/GameManager.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/Prop/PropManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/Renderer/Particle/ParticleManager.h"
#include "Engine/Script/ScriptManager.h"
#include "Engine/Sound/SoundManager.h"
#include "Engine/World/WorldManager.h"

#include "Engine/Resource/MaterialBuilder.h"

#include "EngineState.h"

#include "Engine/Interface/DebugConsole.h"

class Engine
{
protected:
    class StateManager
    {
    public:
        StateManager();
        ~StateManager();

        void update(irr::f32 dt);
        void updateUI(irr::f32 dt);

		std::string getStateInitArgs() { std::string temp = m_args; m_args = std::string(); return temp; }

		bool isNewState() { if (m_initNewState) { m_initNewState = false; return true; } return false; }
		
		void cycleNextState() { m_previous = m_current; m_current = m_next; m_next = ESID_INVALID; }

        ENGINE_STATE_ID current()  const { return static_cast<ENGINE_STATE_ID>(m_current);  }
		ENGINE_STATE_ID next()     const { return static_cast<ENGINE_STATE_ID>(m_next);     }
		ENGINE_STATE_ID previous() const { return static_cast<ENGINE_STATE_ID>(m_previous); }

        ENGINE_STATE_ID setState(ENGINE_STATE_ID state, const std::string& args = "");

    	// Both states must already be initialized
        ENGINE_STATE_ID setStatePauseResume(ENGINE_STATE_ID state, const std::string& args = "");

    	// Hard inits state, ignoring the queue
        ENGINE_STATE_ID initState(ENGINE_STATE_ID state, const std::string& args = "");
        ENGINE_STATE_ID destroyState(ENGINE_STATE_ID state);

        ENGINE_STATE_ID pauseState(ENGINE_STATE_ID state);
        ENGINE_STATE_ID resumeState(ENGINE_STATE_ID state);

        std::vector<std::unique_ptr<EngineState>> m_states;

        irr::u32 m_current, m_next, m_previous;

		bool m_initNewState;

		std::string m_args;
    };

	class RNG
	{
	public:
		RNG() : m_device()
		{
			std::generate_n(m_seed.data(), m_seed.size(), std::ref(m_device));
			m_sequence.generate(std::begin(m_seed), std::end(m_seed));
			m_mt19937.seed(m_sequence);
		}

		// Get a random integer from inclusive begin to end
		int getInt(int begin, int end)
		{
			std::uniform_int_distribution<int> number(begin, end);

			return number(m_mt19937);
		}

		// Get a random unsigned integer from inclusive begin to end
		unsigned int getIntUnsigned(unsigned int begin, unsigned int end)
		{
			std::uniform_int_distribution<unsigned int> number(begin, end);

			return number(m_mt19937);
		}

		// Get a random float from inclusive begin to end
		float getFloat(float begin, float end)
		{
			std::uniform_real_distribution<float> number(begin, end);

			return number(m_mt19937);
		}

		// Get a random double from inclusive begin to end
		double getDouble(double begin, double end)
		{
			std::uniform_real_distribution<double> number(begin, end);

			return number(m_mt19937);
		}

	private:
		std::mt19937 m_mt19937;
		std::random_device m_device;
		std::array<unsigned int, std::mt19937::state_size> m_seed;
		std::seed_seq m_sequence;

	};

public:
    Engine& operator =(const Engine&) = delete;

    Engine(const std::string& name = std::string(), const std::string& args = std::string());
    ~Engine();

	static void destroy() {
		delete s_Instance;
		s_Instance = nullptr;
	}

	void init();
	
    void update();

	void exit() { m_exit = true; }

	std::string getCmdLineArgs() { return m_cmdLineArgs; }
	
	double GetCounter();

    irr::u32 getCurrentTime() const { return static_cast<irr::u32>(m_simulationTime); }
    irr::f32 getDeltaTime() const { return m_deltaTime; }

    bool isGameMode() const { return m_isGameMode; }
    void setGameMode(bool mode = true) { m_isGameMode = mode; }

	bool isEditorMode() const { return m_isEditorMode; }
	void setEditorMode(bool mode = true) { m_isEditorMode = mode; }

	bool isDebugConsoleVisible() const { return m_drawConsole; }

	bool isDefaultStatsDrawingEnabled() const { return m_EnableDefaultStatsDrawing; }
	bool isDebugConsoleEnabled() const { return m_EnableDebugConsole; }
	bool isGameDebugFeaturesEnabled() const { return m_EnableGameDebugFeatures; }
	void setDefaultStatsDrawingEnabled(bool enabled = true) { m_EnableDefaultStatsDrawing = enabled; }
	void setDebugConsoleEnabled(bool enabled = true) { m_EnableDebugConsole = enabled; }
	void setGameDebugFeaturesEnabled(bool enabled = true) { m_EnableGameDebugFeatures = enabled; }

    StateManager*      stateManager()      { return &m_stateManager; }
    NavigationManager* navigationManager() { return &m_navigationManager; }
	RNG*		       rng()               { return &m_rng; }

    const MaterialBuilder& getMaterialBuilder() { return m_materialBuilder; }

	irr::f32 getPhysicsTime() { return m_physicsTime; }
	irr::f32 getRenderTime() { return m_renderTime; }

	void clearScene();

    static Engine* Get() { return s_Instance; }

protected:
	void StartCounter();

private:
    static Engine* s_Instance;
	
    MaterialBuilder m_materialBuilder;

    bool
		m_exit, m_isGameMode, m_isEditorMode, m_drawConsole,
		m_EnableDefaultStatsDrawing, m_EnableDebugConsole, m_EnableGameDebugFeatures;

	double PCFreq = 0.0;
	__int64 CounterStart = 0;

    irr::f32 m_currentTick, m_lastTick;
    irr::f32 m_deltaTime;

	// Fixed timestep variables (Gaffer on Games approach)
	const float m_fixedTimeStep = 0.01667f;      // 60Hz = 16.67ms in seconds
	float m_accumulator = 0.0f;                  // Time accumulator for fixed updates
	float m_interpolationAlpha = 0.0f;           // Render interpolation (0-1)
	const float m_maxFrameTime = 0.25f;          // Spiral of death protection (250ms max)
    float m_simulationTime = 0.0f;               // Actual simulation time in milliseconds (increments each fixed step)

	irr::f32 m_currentPhysicsTick, m_physicsTime;
	irr::f32 m_currentRenderTick, m_renderTime;

    std::string m_cmdLineArgs;

	GameManager m_gameManager;
    InputManager m_inputManager;
    RenderManager m_renderManager;
    PhysicsManager m_physicsManager;
    SoundManager m_soundManager;
    ScriptManager m_scriptManager;
	WorldManager m_worldManager;
    ParticleManager m_particleManager;
    PropManager m_propManager;
    NavigationManager m_navigationManager;

	RNG m_rng;

    DebugConsole m_debugConsole;
    StateManager m_stateManager;
};
