#pragma once

#include <cstdlib>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// CameraFX  —  lightweight "mailbox" for procedural camera effects.
//
// Weapons / systems write impulses (addRecoil / addShake).
// PlayerController reads and decays them each frame in update().
// ---------------------------------------------------------------------------

struct CameraFX
{
	// ---- Recoil -----------------------------------------------------------
	float recoilPitch      = 0.0f;   // Current accumulated pitch offset (degrees)
	float recoilYaw        = 0.0f;   // Current accumulated yaw offset (degrees)
	float recoilDecaySpeed = 8.0f;   // Exponential decay rate (multiplier per second)

	// ---- Screen shake -----------------------------------------------------
	float shakeAmount   = 0.0f;   // Peak magnitude of current shake (degrees)
	float shakeDuration = 0.0f;   // Total duration of current shake event (ms)
	float shakeTimer    = 0.0f;   // Elapsed time since shake started (ms)

	// ------------------------------------------------------------------
	// Call from a weapon on fire to kick the camera.
	// pitch : upward kick in degrees (negative = up in Irrlicht)
	// yaw   : sideways kick in degrees (positive = right)
	// ------------------------------------------------------------------
	void addRecoil(float pitch, float yaw = 0.0f)
	{
		recoilPitch += pitch;
		recoilYaw   += yaw;
	}

	// ------------------------------------------------------------------
	// Call from a weapon on impact to trigger screen shake.
	// Only upgrades an existing shake if the new one is stronger.
	// amount   : peak shake magnitude in degrees
	// duration : how long the shake lasts in milliseconds
	// ------------------------------------------------------------------
	void addShake(float amount, float duration)
	{
		if (amount > shakeAmount)
		{
			shakeAmount   = amount;
			shakeDuration = duration;
			shakeTimer    = 0.0f;
		}
	}

	// ------------------------------------------------------------------
	// Called by PlayerController::update() every frame.
	// Outputs X (pitch), Y (yaw), and Z (roll) offsets in degrees.
	// dt : delta time in milliseconds
	// ------------------------------------------------------------------
	void tick(float dt, float& outPitchOffset, float& outYawOffset, float& outRollOffset)
	{
		float dtSec = dt / 1000.0f;

		// --- Decay recoil (exponential) ------------------------------------
		recoilPitch -= recoilPitch * recoilDecaySpeed * dtSec;
		recoilYaw   -= recoilYaw   * recoilDecaySpeed * dtSec;

		if (fabsf(recoilPitch) < 0.01f) recoilPitch = 0.0f;
		if (fabsf(recoilYaw)   < 0.01f) recoilYaw   = 0.0f;

		// --- Compute shake offset ------------------------------------------
		float shakeX = 0.0f;
		float shakeZ = 0.0f;

		if (shakeTimer < shakeDuration)
		{
			shakeTimer += dt;
			float progress     = shakeTimer / shakeDuration;
			float currentMag   = shakeAmount * (1.0f - progress); // linear decay

			// Random unit in [-1, 1]
			auto randUnit = []() -> float {
				return (static_cast<float>(rand() % 2001) - 1000.0f) / 1000.0f;
			};

			shakeX = randUnit() * currentMag;
			shakeZ = randUnit() * currentMag * 0.35f; // subtle roll component
		}
		else
		{
			shakeAmount = 0.0f; // reset when done
		}

		outPitchOffset = recoilPitch + shakeX;
		outYawOffset   = recoilYaw;
		outRollOffset  = shakeZ;
	}
};

extern CameraFX g_CameraFX;
