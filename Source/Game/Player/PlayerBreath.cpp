#include "PlayerBreath.h"

#include <algorithm>

bool PlayerBreath::s_enabled = true;

void PlayerBreath::reset()
{
	m_air            = maxAirSec;
	m_drownAccum     = 0.0f;
	m_drownTimer     = drownTickSec * 1000.0f;
	m_lowestFraction = 1.0f;
	m_airless        = false;
	m_drownedDamage  = 0;
}

void PlayerBreath::setAir(float seconds)
{
	m_air = std::min(std::max(seconds, 0.0f), maxAirSec);
	m_lowestFraction = std::min(m_lowestFraction, fraction());
}

PlayerBreath::TickResult PlayerBreath::tick(float dtMs, bool airless, bool alive, bool noclip)
{
	TickResult result;
	const float dtSec = dtMs / 1000.0f;

	auto fadeHud = [&](float target)
	{
		const float step = hudFadeSec > 0.0f ? dtSec / hudFadeSec : 1.0f;
		if (m_hudAlpha < target) m_hudAlpha = std::min(m_hudAlpha + step, target);
		else                     m_hudAlpha = std::max(m_hudAlpha - step, target);
	};

	if (!s_enabled)
	{
		reset();
		fadeHud(0.0f);
		return result;
	}

	// Noclip counts as breathing rather than as "frozen": the water flags keep
	// their last value while noclipping (GameplaySystem skips the water test),
	// so a player who noclips out of a pool would otherwise drown in mid-air.
	const bool drowning = airless && alive && !noclip;

	if (drowning)
	{
		m_air = std::max(m_air - dtSec, 0.0f);
		m_lowestFraction = std::min(m_lowestFraction, fraction());

		if (m_air <= 0.0f)
		{
			// Authored per second, dealt in whole points on a fixed cadence, so
			// the fractional remainder carries between chunks (same idea as the
			// CONTENT_HURT brushes). The first chunk lands one full tick after
			// the air runs out, not on the frame it does.
			m_drownAccum += drownDps * dtSec;
			m_drownTimer -= dtMs;

			if (m_drownTimer <= 0.0f)
			{
				m_drownTimer += drownTickSec * 1000.0f;

				const unsigned int points = static_cast<unsigned int>(m_drownAccum);
				m_drownAccum -= static_cast<float>(points);

				result.damage    = points;
				m_drownedDamage += points;
			}
		}
		else
		{
			m_drownTimer = drownTickSec * 1000.0f;
		}
	}
	else
	{
		// Breaking the surface after running low. Only on the EDGE, and only
		// alive — a corpse floating up does not gasp.
		if (m_airless && alive && m_lowestFraction < gaspThreshold)
			result.gasp = true;

		m_air = std::min(m_air + dtSec * refillRate, maxAirSec);

		// Dropped on exit so brief re-entries cannot bank damage and land it
		// all at once.
		m_drownAccum     = 0.0f;
		m_drownTimer     = drownTickSec * 1000.0f;
		m_lowestFraction = fraction();
	}

	m_airless = drowning;

	// Shown while holding breath and while it refills afterwards, so the bar
	// visibly tops up before fading instead of vanishing on the surface.
	fadeHud((alive && (drowning || m_air < maxAirSec)) ? 1.0f : 0.0f);

	return result;
}
