#pragma once

#include <string>

// Slimmed to the stats overlay + player-info window. The console UI and
// command execution moved to Engine/Interface/GameConsole (Quake 3-style).
class DebugConsole
{
public:
    DebugConsole() : m_drawStats(false), m_drawPlayer(false)
    {
        clearInputBuffer();
    }

    void draw_stats();
    void drawPlayerInfo();

    void clearInputBuffer();

    void toggleDrawStats()
    {
        m_drawStats = !m_drawStats;
        if (m_drawStats) { m_drawPlayer = false; }
    }

    bool drawStats() { return m_drawStats; }

    void togglePlayerInfo()
    {
        m_drawPlayer = !m_drawPlayer;
        if (m_drawPlayer) { m_drawStats = false; }
    }

    bool playerInfo() { return m_drawPlayer; }

private:
    bool m_drawStats, m_drawPlayer;

    char m_inputBuffer[256];
};
