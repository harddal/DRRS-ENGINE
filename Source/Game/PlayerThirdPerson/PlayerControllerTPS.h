#pragma once

#include "Game/IPlayerController.h"

#include "anax/anax.hpp"

#include "irrlicht.h"

#include <string>

struct MeshComponent;
class  AnimGraph;

// The third-person player controller.
//
// It shares NO logic with PlayerController. The only thing the two have in
// common is IPlayerController, which exists so the one global handle can point
// at either — see that header.
//
// Three things it does differently, and each is a trap if you assume otherwise:
//
//  1. TRANSFORM POSITION IS THE CAPSULE FOOT, not the capsule centre. The
//     first-person controller writes the PhysX capsule centre into the
//     transform, which is fine when there is no visible body. Here the
//     character mesh hangs off that transform, so a centre-anchored transform
//     would float the model half a capsule off the floor.
//
//  2. TRANSFORM ROTATION CARRIES YAW ONLY. Pitch belongs to the camera. Writing
//     the camera's full orientation into the body — which is exactly what the
//     first-person controller does — faceplants the character model.
//
//  3. THE CAMERA NODE IS POSED DIRECTLY, and CameraSystem is told to keep its
//     hands off via CameraComponent::controllerDriven. The component's 'offset'
//     is reinterpreted as the orbit pivot rather than an eye position; boom
//     length and collision pullback are computed here because they need a
//     raycast per frame.
class PlayerControllerTPS : public IPlayerController
{
public:
	PlayerControllerTPS() {}

	void init() override;
	void update(float dt) override;
	void updateUI(float dt) override;
	void destroy() override;

	// NO SUB-CONTROLLERS. This prototype has no weapon stack, no inventory and
	// no HUD, and says so honestly rather than handing back a half-wired one.
	//
	// Returning null here is legitimate — the interface documents it and every
	// caller null-checks the RESULT — and it is the reason those checks were
	// added rather than left as "the handle is never null in practice".
	//
	// A prototype that wants weapons constructs its own WeaponController and
	// returns it from here; WeaponController has no reference to PlayerController
	// or to any camera, so nothing stops that.
	HUDController*       hudController() override { return nullptr; }
	InventoryController* inventoryController() override { return nullptr; }
	WeaponController*    weaponController() override { return nullptr; }
	PlayerBreath*        breath() override { return nullptr; }

	void setOnLadder(bool on = true) override { m_onLadder = on; }
	void setIsSwimming(bool swimming = true) override { m_swimming = swimming; }
	void setIHeadUnderWater(bool under = true) override { m_headUnderWater = under; }
	void setWaterSurfaceY(float y) override { m_waterSurfaceY = y; }
	void lockPlayer(bool lock = true) override { m_locked = lock; }

	void setNoclip(bool on) override;
	bool isNoclip() const override { return m_noclip; }

	bool isMoving() override { return m_isMoving; }
	int  getCurrentHealth() override;
	int  getMaxHealth() override;
	bool isSwimming() override { return m_swimming; }
	bool isHeadUnderWater() override { return m_headUnderWater; }
	bool isBlocking() override { return m_isBlocking; }
	void setIsBlocking(bool blocking = true) override { m_isBlocking = blocking; }

	bool capturePlayerState(PlayerSaveState& out) const override;

private:
	// TAP FOR LIGHT, HOLD FOR HEAVY, on the same button.
	//
	// The light is a three-hit combo authored as one clip and played one
	// sub-range per hit, so a click during a follow-through cuts to the next
	// cut. The heavy is a single committed swing that cannot be chained.
	enum class AttackKind { Light, Heavy };

	void updateCamera(anax::Entity& player, float dt);

	// Owns m_bodyYaw. Split out of update() because the rule it implements is
	// no longer "face the way you are going": which yaw the body chases depends
	// on the locomotion mode, and while STANDING it chases nothing at all until
	// the camera has swung far enough to trigger an in-place turn.
	void updateFacing(float seconds, bool hasInput, float planarSpeed,
	                  const irr::core::vector3df& wish);

	// Decides WHICH clip and at what rate, and hands that to the entity's
	// AnimGraph. It no longer touches setFrameLoop at all — see the header
	// comment on AnimGraph and engine fork #6.
	//
	// Runs in the FIXED STEP, which is correct: picking a state is a logic
	// decision. Advancing the cursor and sampling the pose is not, and happens
	// once per rendered frame in AnimationSystem.
	void updateAnimation(anax::Entity& player, float planarSpeed);

	// The anim_layer_* console harness. Not gameplay: it exists so bone masks
	// and the additive path can be driven against any clip at runtime, because
	// the paladin set has no hit reactions and no authored additive clips to
	// point them at yet.
	void updateTestLayer(MeshComponent& mesh, AnimGraph& anim);

	// Parents the carried weapon to the hand joint and holds the grip pose that
	// closes the fingers round it. Both halves are console-tunable; see the
	// tps_sword_* / tps_grip_* block beside the tuning constants.
	void updateWeapon(MeshComponent& mesh, AnimGraph& anim);

	// The sword swing: reads the attack button, owns the swing timer, and fires
	// the contact-frame strike. Runs in the FIXED STEP and is called before
	// updateAnimation, which is what drives the layer from the state decided
	// here — the same split the rest of this controller uses, and the reason
	// updateAnimation does not read input.
	void updateAttack(anax::Entity& player, float seconds, float planarSpeed);

	// The strike itself, at the contact frame: an ARC in front of the body, not
	// a ray down the camera. See the call site for why.
	void performStrike(anax::Entity& player);

	// Start a swing NOW, or buffer it if one is already running. 'chain' asks a
	// light to continue the combo rather than restart it; it is ignored by the
	// heavy, which has only one stage.
	//
	// Returns false when the clip is missing, which is the only failure a caller
	// can do anything about.
	bool beginAttack(MeshComponent& mesh, AttackKind kind, bool chain, float planarSpeed);

	// The sword tuning panel: drag the attachment offset and orientation against
	// the running game instead of typing vectors at the console, and copy the
	// result back out as the two cvar lines (or as the C++ defaults) once it
	// looks right. Toggled with 'tps_sword_ui'.
	//
	// Called from updateUI and NOT from update(), like everything else that
	// draws — see the comment at the top of updateUI.
	void updateWeaponUI();

	// True while a debug panel has the mouse, which is the state every input
	// read in this controller is gated on ALONGSIDE m_locked.
	//
	// It is deliberately NOT folded into m_locked: that one is owned by outside
	// callers through lockPlayer(), and a script clearing it would otherwise
	// hand the camera back while the panel was still open.
	//
	// THE CAMERA STOPS BECAUSE OF WHAT THIS DOES NOT DO. The orbit block is the
	// only caller of InputManager::getMouseDelta(), and InputManager infers
	// "somebody is mouse-looking" from that call alone — skip it and the OS
	// cursor is un-clipped and released on the very next fixed step, with no
	// begin/end pair to leave unbalanced. See InputManager::update().
	bool uiHasMouse() const;

	// Every input gate in this controller is this, not m_locked alone.
	bool inputSuppressed() const { return m_locked || uiHasMouse(); }

	// getKeyPressOnce edge state for the panel's toggle key. Read with
	// ignore_process_flag = true, or the panel could not be closed once it had
	// switched input processing off.
	bool m_swordUIKey  = false;

	// Whether THIS controller is the one currently asserting the software
	// cursor. Without it, writing ImGui's MouseDrawCursor every frame would
	// stamp on the game console, which sets the same flag when it opens.
	bool m_swordUIHeld = false;

	// --- Attack --------------------------------------------------------------
	// The swing is a masked LAYER rather than a state, so it composes with
	// whatever the locomotion blend is already doing instead of replacing it.
	// The controller keeps its own clock rather than reading the layer's cursor:
	// the contact is a gameplay event and belongs on the fixed step, while the
	// layer's cursor advances once per RENDERED frame.
	bool  m_attackActive   = false;
	float m_attackTime     = 0.0f;   // seconds into the swing
	float m_attackDuration = 0.0f;   // seconds it will take at the rate chosen at its start
	float m_attackContact  = 0.0f;   // seconds at which the blade passes through
	float m_attackRate     = 1.0f;   // held for the whole swing; see updateAttack
	bool  m_attackStruck   = false;  // the contact has fired for THIS swing

	AttackKind m_attackKind  = AttackKind::Light;
	int        m_attackStage = 0;    // index into that kind's stage table

	// The sub-range this stage plays, as LOCAL frames inside its clip. Held for
	// the swing because updateAnimation hands them to the layer every frame and
	// must not hand it a different range half way through.
	int        m_attackBegin = 0;
	int        m_attackEnd   = -1;

	// Buffered follow-up, and WHICH kind was asked for. A click in the back half
	// of a swing is held and spent the instant that swing ends, because a melee
	// input dropped because it landed mid-animation reads as the button not
	// working — and a heavy charged during a light must not come out as a light.
	bool       m_attackQueued     = false;
	AttackKind m_attackQueuedKind = AttackKind::Light;

	// Whether the QUEUED light should continue the combo or restart it. A combo
	// only chains from the swing it is chaining off; a heavy in between, or
	// letting the whole thing play out, starts again at the first cut.
	bool       m_attackQueuedChain = false;

	// --- The button ----------------------------------------------------------
	// Tap-or-hold needs the button's own state, not a consume-on-press edge:
	// which attack a press means is not known until it is released or has been
	// held long enough, so the press cannot be spent when it arrives.
	bool  m_attackDown  = false;   // last frame's raw state, for the edges
	float m_attackHeld  = 0.0f;    // seconds this press has been down
	bool  m_attackSpent = false;   // this press already produced a swing

	// Set on the frame a swing starts, spent by updateAnimation. Re-issuing a
	// clip to a layer that is already running it does NOT re-seed the cursor, so
	// a second swing needs to say so explicitly — see AnimGraph::restartLayer.
	bool  m_attackRestart  = false;

	// WHICH MASK THIS SWING USES, chosen when it starts and HELD for its whole
	// length. A standing attack is worth committing the whole body to; one taken
	// at a run must leave the legs alone. Deciding it per frame instead would
	// swap the mask under the player the moment they let go of forward, and the
	// pelvis would jump the 0.17 units between the two stances mid-swing.
	const char* m_attackMask = nullptr;

	// WHICH CLIP this swing is playing, latched at its start for the same reason
	// the rate and the contact are: the clip name is a cvar so a re-exported or
	// renamed animation can be dropped in without a rebuild, and re-reading it
	// mid-swing would hand the layer a different clip half way through while the
	// contact time still described the old one.
	std::string m_attackClip;

	// --- Orbit ---------------------------------------------------------------
	// Owned exclusively; the camera node's rotation is never read back, so
	// nothing can accumulate into the aim.
	float m_orbitYaw   = 0.0f;
	float m_orbitPitch = 12.0f;   // start looking slightly down at the character

	// Camera-only offset applied ON TOP of m_orbitYaw/m_orbitPitch while Left
	// Control is held, for inspecting the model without turning it. Movement
	// direction and body facing read m_orbitYaw/m_orbitPitch alone, so this
	// offset never reaches them — see update()'s mouse-input block. Eases back
	// to zero once Control is released rather than snapping.
	float m_freeLookYaw   = 0.0f;
	float m_freeLookPitch = 0.0f;

	// Boom length actually in use. Lerps IN instantly and OUT slowly: snapping
	// out as you clear a corner reads as the camera being shoved, whereas
	// snapping in is invisible because the obstruction is already on screen.
	float m_boom = 0.0f;

	// The UNOBSTRUCTED boom length updateCamera lerps m_boom toward, before
	// collision pullback shortens it — i.e. what the mouse wheel zooms while
	// Left Control is held. Seeded to k_boomLength in the first-update block,
	// same as m_boom, because k_boomLength lives in the .cpp and can't seed a
	// header default member initializer.
	float m_zoomBoom = 4.0f;

	// Over-the-shoulder offset actually in use, smoothed on exactly the same
	// asymmetric rule and for the same reason — brushing a wall on the right
	// otherwise snaps the character to the centre of the screen and back.
	float m_shoulder = 0.0f;

	// --- Body ----------------------------------------------------------------
	// The character's own facing, and what actually gets written to the
	// transform. What it chases depends on m_strafeLocomotion; see updateFacing.
	float m_bodyYaw     = 0.0f;

	// THE YAW THE DIRECTIONAL BLEND IS MEASURED AGAINST — which is NOT
	// m_bodyYaw, and the difference is load-bearing.
	//
	// m_bodyYaw LAGS. Standing, it is allowed to sit up to k_standTurnTrigger
	// (50 degrees) off the camera before it corrects at all, and a camera whip
	// can put it 180 degrees out while it catches up at k_turnInPlaceRate. The
	// instant the player presses forward, the direction of travel is the
	// CAMERA's, so travel-minus-m_bodyYaw reads as a large strafe angle for the
	// 70ms or so the body needs to swing round.
	//
	// That was reading as the body snapping ~90 degrees right and back at the
	// start and end of every walk, because — MEASURED, see the clip notes —
	// the strafe clips carry a baked body yaw of about 84 degrees. Even half a
	// unit of strafe weight visibly swings the whole character.
	//
	// So the blend is measured against the yaw the body is CONVERGING TO. In
	// steady state the two are identical; they differ only during the catch-up,
	// which is exactly the transient that must not be read as a strafe. The cost
	// is up to 70ms of slight foot slide instead, which is invisible next to an
	// 84-degree swing.
	float m_animFacing  = 0.0f;

	// World yaw of the direction of travel, HELD when the character is not
	// travelling. atan2(0, 0) is 0, so a dead stop would otherwise report "due
	// north" and the blend would read the body's own yaw as a strafe angle.
	//
	// NOT the raw velocity angle — see updateFacing. Below walking speed the
	// velocity direction is dominated by the last single acceleration impulse and
	// swings with the camera, so the INPUT direction is mixed in there.
	float m_travelYaw   = 0.0f;

	// False while the character is not travelling. m_travelYaw holds its last
	// value then, purely so the anim_debug overlay has something to show; this
	// flag is what says not to believe it.
	bool  m_travelValid = false;

	irr::core::vector3df m_velocity = irr::core::vector3df(0.0f, 0.0f, 0.0f);

	bool  m_grounded        = false;
	int   m_airborneFrames  = 0;
	bool  m_jumpConsumed    = false;

	// The eased body height, and how far it currently sits from the true one.
	// The LAG is what is published: the body transform carries the true height
	// so the foot IK has a floor it can trust, and the lag is spent on the
	// pelvis and the camera pivot instead. See k_verticalSmoothRate.
	float m_smoothedY   = 0.0f;
	float m_stepLag     = 0.0f;
	bool  m_firstUpdate = true;

	// --- Locomotion mode -----------------------------------------------------
	// true  = the body faces the CAMERA and movement is read in body space, so
	//         the strafe clips carry sideways travel. This is what the paladin
	//         clip set is built for.
	// false = the body faces the way it is MOVING, and only the forward clips
	//         are ever needed.
	//
	// Driven by the 'tp_strafe' cvar, re-read every frame — it is a taste call
	// and the console is where taste calls get settled. See init().
	bool m_strafeLocomotion = true;

	// --- Animation state -----------------------------------------------------
	// The jump clips are NOT one clip each: they are a rise segment and a
	// landing segment with a held pose between them, because the character can
	// be airborne for any length of time and the clip cannot.
	enum class AnimPhase { Ground, Air, Land };

	AnimPhase m_animPhase     = AnimPhase::Ground;
	bool      m_wasGrounded   = true;
	bool      m_jumpIsRunning = false;   // which jump clip the current air phase uses

	int   m_turnDir       = 0;       // in-place turn: -1 left, 0 none, +1 right

	// m_moveQuadrant / m_quadrantTimer / m_runTier / m_movingAnim are GONE.
	// They were the latency the discrete clip switch had to buy to stop itself
	// flapping; the locomotion blend space has no switch, so there is nothing
	// left for them to damp and keeping them would only have re-added the delay.

	// WHICH CLIP IS LOADED IS NO LONGER TRACKED HERE. The active clip, its frame
	// range, the playback cursor and the warn-once guard for a missing name all
	// live in the entity's AnimGraph, because the NPCs need exactly the same
	// state and a second copy of it would diverge. Query it through
	// AnimationComponent::graph.

	// --- anim_debug mirrors --------------------------------------------------
	// Written by updateAnimation in the fixed step, read by updateUI once per
	// rendered frame. Debug only; nothing reads them for behaviour.
	float m_dbgRel       = 0.0f;
	float m_dbgSpeedW[3] = { 0.0f, 0.0f, 0.0f };   // idle, walk, run
	float m_dbgDirW[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };   // fwd, right, back, left

	// --- Carried weapon ------------------------------------------------------
	// A plain scene node parented to the hand BONE, not an entity. Parenting is
	// what keeps it lag-free: the bone's absolute transform is recomputed inside
	// the character's own render-time joint update, and a child of it is carried
	// along in the same pass. Copying the bone onto a separate entity's
	// transform instead would always be reading last frame's pose, which shows
	// up as the sword swimming in the hand through fast animation.
	irr::scene::IAnimatedMeshSceneNode* m_swordNode = nullptr;

	// The character node the sword is currently hung off. A scene load builds a
	// new one, and the stale bone pointer it leaves behind is not detectable
	// from the sword node alone.
	irr::scene::IAnimatedMeshSceneNode* m_swordOwner = nullptr;

	// Uniform scale the hand bone's absolute transform already carries, so the
	// attachment can divide it back out. NOT assumed to be 1: paladin.glb was
	// converted at 0.18412 and that factor lives on the skeleton root, so
	// everything hung off a joint inherits it. See updateWeapon().
	float m_swordBoneScale = 1.0f;

	// --- State pushed in from outside ----------------------------------------
	bool m_locked         = false;
	bool m_noclip         = false;
	bool m_swimming       = false;
	bool m_headUnderWater = false;
	// Pushed by GameplaySystem's water-volume test. Stored only to satisfy the
	// interface -- the third-person controller has no swim model of its own yet.
	float m_waterSurfaceY = 0.0f;
	bool m_onLadder       = false;
	bool m_isBlocking     = false;
	bool m_isMoving       = false;
};
