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

	// ---- Landing bob (damped spring) -------------------------------------
	// Pitch: positive = pitched down. k=200/c=16 → ζ≈0.57, returns in ~270ms
	// with a small upward overshoot — reads as a soft spring, not a hard jolt.
	float landingBobPos = 0.0f;   // Current pitch displacement (degrees)
	float landingBobVel = 0.0f;   // Rate of change (degrees/sec)

	// Y dip: camera drops in world-Y then returns. k=200/c=20 → ζ≈0.71,
	// critically damped — smooth return with no vertical bounce.
	float landingDipPos = 0.0f;   // Current Y offset (units, negative = down)
	float landingDipVel = 0.0f;   // Rate of change (units/sec)

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
	// Landing impact: displaces pitch downward by magnitude degrees and
	// lets the spring pull it back. Only triggers if the new impact would
	// produce more motion than the current spring state.
	// magnitude : peak downward pitch in degrees
	// ------------------------------------------------------------------
	void addLandingBob(float magnitude)
	{
		float currentEnergy = fabsf(landingBobPos) + fabsf(landingBobVel) * 0.05f;
		if (magnitude > currentEnergy)
		{
			landingBobPos = magnitude;          // pitch dips down
			landingBobVel = 0.0f;
			landingDipPos = -magnitude * 0.012f; // camera drops in Y
			landingDipVel = 0.0f;
		}
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
	void tick(float dt, float& outPitchOffset, float& outYawOffset, float& outRollOffset, float& outYOffset)
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

		// --- Integrate landing bob springs -----------------------------------
		auto integrateSpring = [&](float& pos, float& vel, float k, float c, float deadPos, float deadVel)
		{
			if (fabsf(pos) > deadPos || fabsf(vel) > deadVel)
			{
				float force = -k * pos - c * vel;
				vel += force * dtSec;
				pos += vel * dtSec;
				if (fabsf(pos) < deadPos && fabsf(vel) < deadVel)
				{
					pos = 0.0f;
					vel = 0.0f;
				}
			}
		};

		// Pitch bob: k=200, c=16 — slightly underdamped, subtle overshoot
		integrateSpring(landingBobPos, landingBobVel, 200.0f, 16.0f, 0.001f, 0.01f);
		// Y dip: k=200, c=20 — near-critically damped, no vertical bounce
		integrateSpring(landingDipPos, landingDipVel, 200.0f, 20.0f, 0.0001f, 0.001f);

		outPitchOffset = recoilPitch + shakeX + landingBobPos;
		outYawOffset   = recoilYaw;
		outRollOffset  = shakeZ;
		outYOffset     = landingDipPos;
	}
};

extern CameraFX g_CameraFX;
