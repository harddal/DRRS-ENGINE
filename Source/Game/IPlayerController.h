#pragma once

#include <memory>

// The player-avatar controller interface.
//
// This exists for exactly one reason: `g_PlayerController` is dereferenced from
// engine code (Bindings_Game, GameConsole, WorldManager) and from game code that
// has no business knowing which prototype is running. Controller construction is
// chosen by the spawn marker (see MarkerComponent::profile and the profile table
// in GameplaySystem), so the concrete type is not known until a scene loads.
//
// It carries NO implementation and NO state. Every member is pure virtual. It is
// a shared *type*, not shared *logic* — a first-person and a third-person
// controller share none of the latter.
//
// The surface below is not a design; it is an inventory. It is precisely what
// external code already calls, and nothing more. Do not add a method here
// because a controller happens to have one — add it only when something outside
// the controller's own folder needs to call it through this pointer.
//
// NOTE ON DEFAULT ARGUMENTS. Several methods carry them. Default arguments on a
// virtual are bound STATICALLY, to the declared type of the pointer — an
// override that declares a different default silently changes behaviour
// depending on whether the call went through the base or the derived type. The
// defaults here and in every implementation must therefore stay identical.

class HUDController;
class InventoryController;
class PlayerBreath;
class WeaponController;

struct PlayerSaveState;

class IPlayerController
{
public:
	// Deleted through a base pointer by GameManager::destroy(), so this must be
	// virtual — without it the derived destructor never runs and every
	// sub-controller leaks its RenderManager registrations.
	virtual ~IPlayerController() = default;

	// --- Lifecycle -----------------------------------------------------------
	// Called only by GameManager and by the MT_PLAYER_START spawn case.
	virtual void init() = 0;
	virtual void update(float dt) = 0;
	virtual void updateUI(float dt) = 0;
	virtual void destroy() = 0;

	// --- Sub-controllers -----------------------------------------------------
	// These return concrete types on purpose: the HUD, the inventory and the
	// weapon stack are not first-person-specific and there is no second
	// implementation of any of them to abstract over. A controller that has no
	// use for one may return nullptr, which is why every caller must null-check
	// the RESULT and not merely the handle.
	virtual HUDController*       hudController() = 0;
	virtual InventoryController* inventoryController() = 0;
	virtual WeaponController*    weaponController() = 0;

	// Air supply / drowning. Reached from outside by GameplaySystem (the
	// CONTENT_NOAIR brush test), the HUD and the breath_* console commands.
	// Same null rule as above: a controller without one returns nullptr.
	virtual PlayerBreath*        breath() = 0;

	// --- Volume state --------------------------------------------------------
	// Pushed in by GameplaySystem's brush-volume tests once per frame, and by
	// the console. All of it is "what is the avatar standing in", which every
	// controller has an answer for even if that answer is a bare bool.
	virtual void setOnLadder(bool on = true) = 0;
	virtual void setIsSwimming(bool swimming = true) = 0;
	virtual void setIHeadUnderWater(bool under = true) = 0;

	// The world Y of the surface of the water volume the avatar is standing in,
	// pushed in alongside the two flags above. Swimming needs a waterline to
	// float against: without one the water has no top, and an avatar that stops
	// swimming simply hangs wherever it happened to be. Only meaningful while
	// isSwimming() is true.
	virtual void setWaterSurfaceY(float y) = 0;

	virtual void lockPlayer(bool lock = true) = 0;

	virtual void setNoclip(bool on) = 0;
	virtual bool isNoclip() const = 0;

	// --- Script queries ------------------------------------------------------
	// The AngelScript bindings in Bindings_Game.cpp. Non-const to match the
	// existing accessors; do not tidy that up without touching every override.
	virtual bool isMoving() = 0;
	virtual int  getCurrentHealth() = 0;
	virtual int  getMaxHealth() = 0;
	virtual bool isSwimming() = 0;
	virtual bool isHeadUnderWater() = 0;
	virtual bool isBlocking() = 0;
	virtual void setIsBlocking(bool blocking = true) = 0;

	// --- Save sidecar --------------------------------------------------------
	// Returns false when there is no live player to read, which keeps a save
	// made with no player loaded from writing a sidecar of zeroes.
	//
	// There is deliberately no applyPlayerState() counterpart here: the load
	// side is driven by the controller itself when the player spawns, so it has
	// never been called from outside.
	virtual bool capturePlayerState(PlayerSaveState& out) const = 0;
};

// The one live controller, or null between scenes. Constructed by the
// MT_PLAYER_START marker case, destroyed by GameManager::destroy().
//
// It is null more often than it used to be — construction is no longer
// unconditional — so check it before dereferencing.
extern std::unique_ptr<IPlayerController> g_PlayerController;
