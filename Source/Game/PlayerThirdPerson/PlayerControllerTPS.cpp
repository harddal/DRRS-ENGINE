#include "PlayerControllerTPS.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Physics/PhysicsManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/World/WorldManager.h"

#include "Engine/World/Components/CameraComponent.h"
#include "Engine/World/Components/CCTComponent.h"
#include "Game/Components/DamageReceiverComponent.h"
#include "Engine/Animation/AnimMask.h"
#include "Engine/World/Components/AnimationComponent.h"
#include "Engine/World/Components/MeshComponent.h"
#include "Engine/World/Components/TransformComponent.h"

#include "Game/Player/PlayerSaveState.h"
#include "Game/GameplaySystem.h"
#include "Game/AI/AICoordinator.h"
#include "Engine/Engine.h"

#include <IMGUI/imgui.h>

#include "spdlog/spdlog.h"

#include <cmath>

using namespace irr;
using namespace irr::core;

namespace
{
	// Deliberately a private copy rather than a reference to the first-person
	// tuning globals. Those are one prototype's feel; this is another's, and the
	// two drifting apart is the point of the exercise.
	// TWO SPEED TIERS: the character WALKS by default and RUNS on the sprint
	// modifier. There is no third tier and no crouch modifier.
	//
	// Both numbers are picked against the clips rather than pulled round, and
	// that is the whole point of them: the Walk clip's measured ground speed is
	// 2.11 u/s and the run's is 4.57, so 2.2 and 6.0 play their own cycles at
	// rate 1.04 and 1.31 — near enough to 1.0 that the feet stay on the floor
	// at both tiers, with nothing clamped. Change either and check the rate it
	// implies against the measured table below before deciding it looks wrong.
	//
	// 6.0 is the old default speed, kept as the run tier so the fast tier still
	// covers ground at the rate the prototype was laid out for. The old 9.5
	// sprint is gone: nothing in the clip set can absorb it without skating.
	const float k_walkSpeed = 2.2f;   // units/sec, no modifier
	const float k_runSpeed  = 6.0f;   // units/sec, sprint held
	const float k_accel       = 40.0f;   // units/sec^2 toward the target speed
	const float k_friction    = 12.0f;   // units/sec^2 of braking with no input
	const float k_airAccel    =  8.0f;

	const float k_gravity         = 20.0f;
	const float k_fallGravityMult =  1.4f;
	const float k_jumpSpeed       = 10.0f;

	const float k_sensitivity = 0.09f;
	const float k_pitchMin    = -35.0f;  // looking up at the character
	const float k_pitchMax    =  70.0f;  // looking down on it

	// How fast the free-look offset eases back to zero once Left Control is
	// released. Same exponential-ease shape as the boom/shoulder smoothing,
	// picked fast enough that the camera settles back behind the character in
	// well under half a second rather than drifting there.
	const float k_freeLookReturnRate = 6.0f;

	// Boom. The camera sits this far back along the orbit direction unless
	// geometry gets in the way.
	const float k_boomLength    = 4.0f;
	const float k_boomRadius    = 0.25f;  // keeps the near plane out of the wall
	const float k_boomExtendRate = 6.0f;  // exponential rate, not a speed

	// Mouse-wheel zoom range while inspecting (Left Control held). Bounds the
	// UNOBSTRUCTED boom length (m_zoomBoom) rather than m_boom itself, so
	// collision pullback still applies on top of whatever zoom level is set.
	const float k_zoomBoomMin = 1.5f;   // close enough to fill the screen with the model
	const float k_zoomBoomMax = 10.0f;  // arbitrary headroom past the resting length
	const float k_zoomStep    = 0.5f;   // boom units per wheel notch

	// --- Over-the-shoulder framing -------------------------------------------
	// Shifts the ORBIT PIVOT, not just the camera: moving both the eye and the
	// point it looks at puts the character off-centre on screen and leaves the
	// view direction alone, which is the whole trick. Offsetting only the camera
	// would swing the aim across the character instead and the framing would
	// change with boom length.
	//
	// Applied ON TOP of the .ent's camera offset, which stays the base pivot —
	// the entity still owns "how high up the body does the camera orbit", and
	// these two own "where is the shoulder relative to that".
	//
	// Scale reference, since the numbers mean nothing on their own: the paladin
	// is 2.383 units tall, so 0.65 is a bit under a shoulder's width and 0.35 is
	// the distance from mid-chest to the base of the neck. At a 4-unit boom the
	// lateral shift moves the character about a tenth of the screen width off
	// centre. Push k_shoulderRight toward 1.0 for a more pronounced framing.
	const float k_shoulderRight = 0.65f;
	const float k_shoulderUp    = 0.35f;

	// How fast the character's own facing chases its target WHILE MOVING. Low
	// enough to read as a turn, high enough not to fight the camera.
	const float k_bodyTurnRate = 720.0f;  // deg/sec

	// --- Turning on the spot -------------------------------------------------
	// A standing character does NOT track the camera. It tolerates this much
	// divergence, and only then turns to face where the camera is looking,
	// playing the in-place turn clip while it does.
	//
	// Without the deadzone the body would micro-shuffle under every small mouse
	// correction, which is the entire reason a threshold exists rather than a
	// plain "always face the camera".
	const float k_standTurnTrigger = 50.0f;   // deg of divergence that starts a turn
	const float k_standTurnRelease =  8.0f;   // deg at which it stops again

	// Deliberately far slower than k_bodyTurnRate. THE TURN CLIPS CONTAIN NO
	// ROTATION — measured, see the clip notes below — so this rate is the only
	// thing the shuffling feet have to agree with. Turn faster than this and the
	// body pivots under stationary feet; slower and the camera drags the
	// character around behind it. 150 deg/sec turns the 50-degree trigger
	// divergence in a third of a second, against a 1.2s shuffle loop.
	const float k_turnInPlaceRate = 150.0f;   // deg/sec

	// PER-ASSET, AND MEASURED RATHER THAN ASSUMED. This was 180 on the reasoning
	// that the glTF importer mirrors Z (S = diag(1,1,-1,1)), so a character
	// authored facing +Z in glTF arrives facing -Z and wants the half turn.
	// The reasoning is right; the premise was wrong for THIS asset.
	//
	// gladiator.glb is authored facing -Z in glTF, so the mirror leaves it facing
	// +Z — which is already Irrlicht's yaw-0 forward, so the correct offset is 0
	// and the half turn made the character run backwards.
	//
	// How to measure it for a replacement body, because guessing cost a bug once:
	// take the lowest 1% of the mesh's POSITION accessor by Y (the feet) and
	// compare the Z extent either side of the origin. Toes reach further than
	// heels, so the larger extent is the authored facing. -Z in glTF (toes at -Z)
	// wants 0 here; +Z wants 180. Checked against suicide_cultist.glb, which
	// measures +Z and is the asset SuicideBomberBehavior drives with the base
	// CharacterBehavior::m_yawOffset of 180.
	//
	// This is the same quantity as CharacterBehavior::m_yawOffset, which is
	// per-entity and editable for exactly this reason — MeleeZombieBehavior
	// overrides it to 0 for zombie.b3d. A replacement player body almost
	// certainly needs this number changed with it.
	//
	// 2026-09-10: the body is now paladin.glb, and it was measured by the recipe
	// above rather than assumed. Its feet reach +0.181 toward +Z against -0.099
	// toward -Z, so the toes are at +Z and it wants the half turn — the opposite
	// of the gladiator, which measured -Z and wanted 0. That is the expected
	// result for anything built by Tools/mixamo_to_glb.py: suicide_cultist.glb
	// comes out of the same tool, measures +Z too, and runs on m_yawOffset 180.
	const float k_modelYawOffset = 180.0f;

	// Below this speed the character is travelling, but its velocity direction is
	// not yet worth trusting — see updateFacing.
	//
	// There is deliberately NO slew limit on the blend coordinate. One was tried;
	// simulating the integrator against a turning camera showed it made a violent
	// mouse flick WORSE, because lagging the coordinate behind a genuinely fast
	// direction change reports a larger strafe angle than is really happening. It
	// was damping a symptom of the wrap bug documented below, and once that was
	// fixed there was nothing left for it to damp.
	const float k_travelEpsilon = 0.01f;

	// Below this planar speed the character is standing still as far as the
	// animation is concerned. Above it, playback scales with actual speed.
	const float k_moveAnimThreshold = 0.35f;

	// -------------------------------------------------------------------------
	// THE CLIP SET
	// -------------------------------------------------------------------------
	// paladin.anim, written by Tools/mixamo_to_glb.py. Every name here is an
	// EXACT, CASE-SENSITIVE key into MeshComponent::findAnimation — these are
	// the Mixamo clip names as imported, not the lowercase 'idle'/'move'
	// aliases the .ent describes. Those aliases are what the first cut of this
	// controller asked for; they are now unused by it, and are left in the
	// sidecar because RenderSystem::playAnimation and script still accept them.
	const char* k_clipIdle        = "Idle";
	const char* k_clipWalk        = "Walk";
	const char* k_clipRun         = "Running_NoWeapon";
	const char* k_clipStrafeWalkL = "Left_Strafe_Walk";
	const char* k_clipStrafeWalkR = "Right_Strafe_Walk";
	const char* k_clipStrafeRunL  = "Left_Strafe_Running";
	const char* k_clipStrafeRunR  = "Right_Strafe_Running";
	const char* k_clipTurnLeft    = "TurnLeft_InPlace";
	const char* k_clipTurnRight   = "TurnRight_InPlace";
	const char* k_clipJumpStand   = "Jump_Standing";
	const char* k_clipJumpRun     = "Jump_Running";

	// The two swing clips. Both are GREAT SWORD animations — two-handed — which
	// is why the grip mask below is 'fingers' and not 'right_fingers'.
	//
	// Only defaults: the live names are 'tps_attack_light' / 'tps_attack_heavy',
	// so a re-export can swap either without touching this file. The STAGE TABLE
	// underneath them cannot move that way, because its frame numbers only mean
	// anything against one particular export — re-measure with
	// Tools/measure_attack_contact.py and update it here.
	const char* k_clipLightDefault = "Great_Sword_Light_Attack";
	const char* k_clipHeavyDefault = "Great_Sword_Heavy_Attack";

	// Great_Sword_Spell_Cast is imported and measured (36 frames, no root
	// motion) but deliberately unwired — there is no spell for it to cast yet.

	// 'Standing_Arguing' is the twelfth clip and is deliberately unused: it is a
	// 21-second conversation gesture, not locomotion. It is the obvious thing to
	// hang an idle break or a dialogue state off later.

	// --- THE STRAFE CLIPS ARE ROTATED FORWARD WALKS --------------------------
	// MEASURED 2026-09-14. These are not side-steps. Mixamo authored them by
	// turning the character and walking forward, and the turn is baked into the
	// animated joints:
	//
	//     clip                    hips world yaw   travel dir   travel vs body
	//     Walk                             -0.2         -0.1            0.1
	//     Right_Strafe_Walk               -84.0        -90.5           -6.5
	//     Left_Strafe_Walk                 83.7         90.0            6.3
	//     Right_Strafe_Running            -77.0        -89.4          -12.4
	//     Left_Strafe_Running              75.9         89.4           13.5
	//
	// So relative to its own body the character walks FORWARD in every one of
	// them. This cannot be corrected by rotating the clip: a rigid rotation moves
	// the body and the travel direction together, and the body-relative angle is
	// invariant. The clips are usable as they are — a character strafing right
	// turns to face right, which reads fine — but they come with a hard
	// consequence:
	//
	//   ANY WEIGHT ON A STRAFE CLIP SWINGS THE WHOLE BODY, up to 84 degrees at
	//   full weight and proportionally below that.
	//
	// which is why the directional blend must never be handed a spurious strafe
	// angle. See m_animFacing.

	// LEFT AND RIGHT SURVIVE THE IMPORT, so the clip names can be trusted. The
	// glTF importer mirrors Z (S = diag(1,1,-1,1)), and a mirror is exactly what
	// converts glTF's right-handed frame to Irrlicht's left-handed one: the
	// asset faces +Z in glTF and so arrives facing -Z (hence k_modelYawOffset
	// 180), while its left-hand geometry stays at +X — which IS the character's
	// left once it faces -Z under Irrlicht's yaw convention. Chirality is
	// preserved relative to the coordinate system, so 'Left_Strafe' really is
	// the character's left. If a future asset ever proves otherwise, the fix is
	// to swap the two names in this table and nowhere else.

	// --- Measured clip ground speeds -----------------------------------------
	// NOT guessed, and NOT the movement constants above. Every clip is authored
	// IN PLACE (hips return to their start — confirmed, so there is no root
	// drift to fight), so a clip's natural ground speed is the rate at which the
	// PLANTED foot travels backwards relative to the hips. Measured by
	// forward-kinematicing mixamorig:LeftToeBase / RightToeBase against
	// mixamorig:Hips per keyframe, taking the lower foot each frame and the
	// median of |d(rel)/dt|:
	//
	//     Walk                 2.11 u/s over a 1.033s cycle
	//     Running_NoWeapon     4.57 u/s over a 0.733s cycle
	//     Left/Right strafes   2.51 u/s (walk) and 4.18 u/s (run)
	//
	// These are in WORLD units: the Armature node carries the asset's 0.18412
	// scale, so the FK result is already in the units the controller moves in.
	// Anyone re-measuring from raw glTF accessor values must not apply that
	// scale a second time — doing so makes the paladin look like it takes
	// 10cm steps.
	//
	// Playback rate is speed/natural, which is what keeps the feet on the
	// ground — and it is why the two movement speeds were chosen against this
	// table. Every tier the player can actually reach lands inside the rate
	// clamps with room to spare:
	//
	//     walk 2.2  -> 1.04 forward, 0.88 strafing
	//     run  6.0  -> 1.31 forward, 1.44 strafing
	//
	// so nothing is clamped and nothing skates. The clamps below exist for the
	// speeds the player reaches in between, while accelerating or being shoved.
	const float k_walkClipSpeed       = 2.11f;
	const float k_runClipSpeed        = 4.57f;
	const float k_strafeWalkClipSpeed = 2.51f;
	const float k_strafeRunClipSpeed  = 4.18f;

	// CORROBORATED 2026-09-14 by Tools/measure_clip_phase.py, which re-derives
	// these by FK straight off the GLB: Walk 2.11 (exact), Running_NoWeapon 4.68,
	// strafe walk 2.48/2.50, strafe run 4.29. Within 2.5% of the hand-measured
	// numbers above, which are kept because the movement speeds were tuned
	// against them. That agreement is also what licenses the phase table below:
	// same FK pass, same run, so if one is right the other is too.
	//
	// The rate clamps that used to live here are gone. Playback rate is no longer
	// a per-clip quantity — it falls out of planarSpeed divided by the BLENDED
	// stride length, inside AnimGraph, which is what keeps the feet planted at
	// every speed rather than only at the two the tiers named. The safety rails
	// moved there with it.

	// --- Measured foot-plant phases ------------------------------------------
	// The fraction of each cycle at which the LEFT foot begins its stance,
	// measured by the same tool. Every cyclic clip in the locomotion blend is
	// driven from one shared phase accumulator offset by this number, so left
	// foot contact lines up across all of them.
	//
	// THIS IS WHAT FIXES THE SCISSOR. Cross-fading two cyclic clips whose cursors
	// sit at unrelated points averages left-foot-down against right-foot-down and
	// produces a pose with both feet somewhere in the middle — the moonwalk. It
	// is a distinct problem from the pose pop that phase 2 fixed, and phase 2 did
	// not touch it.
	//
	// Re-measure with:
	//   python Tools/measure_clip_phase.py <asset>.glb
	// and check its 'apart' column is near 0.50 on every cyclic clip before
	// believing any of it.
	const float k_phaseWalk        = 0.323f;
	const float k_phaseRun         = 0.318f;
	const float k_phaseStrafeWalkL = 0.414f;
	const float k_phaseStrafeWalkR = 0.379f;
	const float k_phaseStrafeRunL  = 0.381f;
	const float k_phaseStrafeRunR  = 0.381f;

	// THE TIER HYSTERESIS AND THE QUADRANT DWELL ARE GONE, NOT TUNED.
	//
	// k_runTierEnter/k_runTierExit and k_quadrantDwellMs existed to damp a
	// discrete switch: every tier change and every quadrant flap re-seeded the
	// clip to its first frame, so the code bought latency to make it happen less
	// often. The blend space below has no switch to damp — walk and run are
	// weighted continuously on speed, and the four directions bilinearly on the
	// body-space move angle — so leaving them in would only have reintroduced
	// the latency they were paying for. See the plan's "what not to do".
	//
	// The ONE threshold that survives, because it genuinely is a discrete choice:
	// there are two jump clips and nothing blends between them, so takeoff picks
	// one from the speed at the instant the feet leave the floor. Kept at the old
	// k_runTierEnter value so a running jump still triggers where it used to.
	const float k_runJumpSpeed = 3.4f;

	// --- Jump clip segments, MEASURED ----------------------------------------
	// Frame offsets INSIDE each jump clip, at the sidecar's 30fps. Taken from
	// the hips' vertical curve, which makes the phases unambiguous.
	//
	// A physics jump starts the instant the key goes down, so the clips cannot
	// be played from frame 0: Jump_Standing spends its first 23 frames (0.77s)
	// crouching for a leap that has already happened. Neither can they be played
	// to their end, because the character is airborne for as long as the arc
	// takes and a clip is a fixed length.
	//
	// So each is cut into a RISE and a LAND, and the gap between them is covered
	// by Irrlicht clamping a non-looping clip to its last frame — the held
	// falling pose costs no extra code at all. It is also what makes a long fall
	// off a ledge work: the same rise segment runs out and holds.
	//
	// Jump_Standing (74 frames): hips drop to a deep crouch at f14, extend
	// through standing height at f23, apex at f32, back through standing height
	// at f41, compress to f46, recovered by f58.
	//
	// Jump_Running (27 frames): already at takeoff on f0 (this clip has no
	// wind-up — it is the middle of a stride), apex f11, ground contact ~f20,
	// compression f23, recovered by f26.
	struct JumpSegments
	{
		int riseBegin;   // first frame of the launch
		int riseHold;    // last frame of the rise; the node clamps and holds here
		int landBegin;   // ground contact
		int landEnd;     // recovered to neutral
	};

	// riseBegin 20 rather than 23: three frames of leg extension sell the push
	// off, and the real arc rises for 0.5s against the clip's 0.4s, so the
	// couple of frames of slack are free.
	const JumpSegments k_jumpStanding = { 20, 36, 41, 58 };
	const JumpSegments k_jumpRunning  = {  0, 13, 17, 26 };

	// Which of the two jump clips plays is decided by k_runJumpSpeed, above.
	// That is the only speed boundary left in this file: everything else about
	// locomotion is a continuous blend now.

	// --- Cross-fade durations, PER TRANSITION --------------------------------
	// Not one global number. A transition's right length is a property of what
	// it is transitioning between: a stride change can take its time, an impact
	// cannot. Anything with a physical event in it — a takeoff, a landing —
	// has to be short or the character reads as wading.
	//
	// These are the seconds the OUTGOING clip takes to reach zero weight while
	// it carries on playing. Set anim_blend 0 to force every one of them to a
	// hard cut and get the old behaviour back for comparison.
	const float k_fadeLocomotion = 0.20f;   // walk <-> run, quadrant changes, idle -> moving
	const float k_fadeToIdle     = 0.30f;   // settling to a stop: the longest, and it shows
	const float k_fadeTakeoff    = 0.09f;   // ground -> air; the jump has already happened
	const float k_fadeToLand     = 0.12f;   // air -> land, i.e. the moment of impact

	// Locomotion mode, exposed through WorldManager's cvar store rather than
	// through a console command, so GameConsole needs no knowledge of this
	// class: a bare name prints and "name value" sets, which is the whole
	// interface a toggle needs. See init().
	const char* k_strafeCvar = "tp_strafe";

	// anim_blend 0 turns every cross-fade into a hard cut — exactly the phase 1
	// behaviour — so a suspected blend artifact can be bisected at runtime
	// without a rebuild. anim_debug 1 draws the player's active clip players,
	// their cursors and their weights.
	const char* k_animBlendCvar = "anim_blend";
	const char* k_animDebugCvar = "anim_debug";

	// --- Layers ---------------------------------------------------------------
	// Layer 0 carries the in-place turn. It is a MASKED OVERRIDE rather than a
	// whole-body state: the turn clips describe footwork and a spine counter-twist
	// under a body that updateFacing is rotating, and they have nothing to say
	// about the arms. Masking them out of the arms is what will let a weapon keep
	// aiming while the feet shuffle, and today it simply lets the idle underneath
	// keep driving them.
	const int k_layerTurn = 0;

	// Layer 1 is the anim_layer_* test harness: any clip, any mask, either mode,
	// set from the console without a rebuild. It is how masks and the additive
	// path get exercised against an asset set that has no hit reactions and no
	// authored additive clips to point them at yet.
	const int k_layerTest = 1;

	// Layer 2 holds the GRIP POSE: one frame of a clip whose hands are closed
	// round a weapon, masked to the finger chains and frozen there. It is what
	// lets the whole empty-handed Mixamo locomotion set be reused with a sword
	// in hand - the base clip keeps every other joint and only the fingers are
	// replaced. Stacked last, so it also wins over anything the test layer is
	// driving through the hands.
	const int k_layerGrip = 2;

	// Layer 3 is the SWING, stacked above the grip so the attack's own hands win
	// over the held grip pose while it plays. That ordering costs nothing and is
	// not arbitrary: both poses close the same fingers round the same sword —
	// measured, the attack clip's index curl is a flat 66.7 degrees from end to
	// end — so the two agree, and letting the swing own the hand means the grip
	// layer can never fight a clip that was authored holding the weapon.
	const int k_layerAttack = 3;

	const float k_fadeTurnIn  = 0.18f;
	const float k_fadeTurnOut = 0.25f;   // longer out: a turn should settle, not stop

	const char* k_animIKCvar          = "anim_ik";
	const char* k_animIKDrawCvar      = "anim_ik_draw";

	// --- The carried weapon --------------------------------------------------
	// Built by Tools/prepare_weapon_glb.py, which is where its scale and its
	// orientation come from: 1.75 units point to pommel, blade along +Y, and the
	// ORIGIN AT THE CENTRE OF THE GRIP. That last one is what makes the offsets
	// below small numbers to nudge rather than a reconstruction of wherever the
	// model's author put their world origin.
	const char* k_swordMesh  = "content/mesh/player/thirdperson/weapons/sword/sword.glb";
	const char* k_handJoint  = "mixamorig:RightHand";

	const char* k_swordCvar    = "tps_sword";
	const char* k_swordPosCvar = "tps_sword_pos";
	const char* k_swordRotCvar = "tps_sword_rot";
	const char* k_gripClipCvar = "tps_grip_clip";
	const char* k_gripMaskCvar = "tps_grip_mask";
	const char* k_swordUICvar  = "tps_sword_ui";

	// F3. F2 is the viewmodel debug, F8 the entity links, and F9/F10/F11 the
	// bounding boxes, debug sprites and entity labels — F3 is the lowest one
	// nothing in the engine, the editor or the game already claims, and F1 and
	// F12 are left alone because the OS and the overlay layer tend to want them.
	// KEYBOARD_KEY, NOT irr::EKEY_CODE. The engine carries its own sequential
	// key enum and InputManager maps it to VK_* itself, so irr::KEY_F3 (0x72)
	// would read a different key entirely — and unqualified KEY_F3 does not
	// even compile here, because 'using namespace irr' makes it ambiguous.
	const KEYBOARD_KEY k_swordUIKey = KEYBOARD_KEY::KEY_F3;

	// DERIVED FROM THE RIG, NOT GUESSED, AND STILL ONLY A STARTING POINT.
	//
	// Measured off paladin.glb's bind pose: the hand joint's local +Y runs down
	// the fingers (0, 1, 0.002 - Mixamo bones point +Y at their child), and the
	// axis a closed fist encircles, taken as (palm normal x finger direction),
	// is (0.885, 0, 0.466) in that joint's local frame. Rotating the sword's own
	// +Y onto it is X=-27.8, Y=0, Z=-90 in Irrlicht's rotation order, with the Z
	// component negated for glTF's mirror-across-Z import.
	//
	// The position is half a palm length (0.173 / 2) down that same +Y, which
	// moves the grip from the WRIST, where the joint actually sits, to the
	// middle of the palm.
	//
	// It is exposed as a cvar because a bind-pose derivation cannot know how
	// this particular model's hand is sculpted, and nudging it against the real
	// thing beats another round of arithmetic.
	// TUNED AGAINST THE RUNNING GAME on 2026-09-20 and baked back in from the F3
	// panel's "copy C++" button. The derivation below is what got it close; the
	// hand is sculpted, and these are where it actually sits.
	const char* k_swordPosDefault = "-0.0060 0.1260 -0.0320";
	const char* k_swordRotDefault = "-28.00 0.00 -90.00";

	// THE GRIP COMES FROM THE HEAVY ATTACK. Sword_And_Shield_Attack, which used
	// to supply it, is gone from the set — the great sword clips replaced it —
	// and a grip cvar left pointing at a clip the mesh no longer has is a warning
	// and an open hand, not an error anyone would notice.
	//
	// Great_Sword_Heavy_Attack is the right replacement on measurement rather
	// than by elimination: BOTH hands are closed (right index knuckle 112.8
	// degrees, left 47.0) and neither moves by so much as a degree across its 38
	// frames. A pose that never changes is exactly what a frozen grip wants.
	const char* k_gripClipDefault = "Great_Sword_Heavy_Attack";

	// --- The swing table -----------------------------------------------------
	// MEASURED OFF paladin.glb by Tools/measure_attack_contact.py, which forward-
	// kinematics the right hand and takes its speed RELATIVE TO THE HIPS per
	// frame — that is what separates the arm's own motion from the body's weight
	// shift underneath it. The peak is the frame the edge is travelling fastest,
	// which is where it passes through something at arm's length.
	//
	// EVERY NUMBER HERE BELONGS TO ONE EXPORT. Re-export either clip and they are
	// meaningless; run the tool again and paste what it prints.

	// One swing: a sub-range of a clip, and the frame inside it where the blade
	// lands. All three are LOCAL frames — offsets into the clip, not absolute
	// mesh frames. AnimGraph does that conversion in exactly one place.
	struct AttackStage
	{
		int begin;
		int end;
		int contact;
	};

	// THE LIGHT ATTACK IS A THREE-HIT COMBO, not one swing. Measured peaks at
	// frames 23, 51 and 78 of a 108-frame clip, separated by speed troughs at
	// 45 (0.61 u/s) and 70 (2.50) — those troughs are the weapon resetting
	// between cuts, and they are where the clip divides.
	//
	// Played as one 3.57-second animation a single click would take the player
	// away for three and a half seconds. Played as three sub-ranges, a click
	// during the follow-through cuts straight to the next cut, which is what a
	// combo IS — and it reuses the follow-up buffer already here for it.
	const AttackStage k_lightStages[] =
	{
		{  0,  45, 23 },
		{ 46,  70, 51 },
		{ 71, 107, 78 },
	};

	// The heavy is one committed swing: 38 frames, contact on 15, peak 7.02 u/s.
	const AttackStage k_heavyStages[] =
	{
		{ 0, 37, 15 },
	};

	// WHETHER THIS ATTACK MAY TAKE THE WHOLE BODY.
	//
	// The heavy may: measured, its hips drift is exactly zero, so a full-body
	// override just plays it where the character stands.
	//
	// THE LIGHT MAY NOT, and this is the one thing about the new clip set that
	// constrains the code rather than the other way round. Great_Sword_Light_
	// Attack was exported WITH ROOT MOTION — its hips travel +4.10 units forward
	// over the combo, about 1.1 u/s continuously, and every one of its three
	// stages carries part of that. Nothing here applies root motion, so a
	// full-body override would walk the character's mesh four units off the
	// capsule its collision actually lives in.
	//
	// Masked to 'upper_body' the problem disappears completely rather than being
	// worked around: that mask is rooted at Spine, so the Hips track — which is
	// the only joint carrying translation — is simply not one of the joints the
	// layer reaches, and the travel is discarded. The cost is that a standing
	// light attack cannot commit its legs and hips the way the heavy does.
	//
	// The real fix is an in-place re-export; see the note to the user.
	const bool k_lightAllowFullBody = false;
	const bool k_heavyAllowFullBody = true;

	// The light plays a touch fast and the heavy a touch slow, which is most of
	// what makes the two read as different weights of attack rather than as two
	// animations. Both are feel calls and both are cvars.
	const float k_lightRateDefault = 1.15f;
	const float k_heavyRateDefault = 1.0f;

	// HOW LONG THE BUTTON MUST BE HELD to mean "heavy". The light cannot fire
	// until this has elapsed without a release, so it is also the light attack's
	// input latency — which is the whole reason it is this short. Much under
	// 0.2s and a deliberate hold starts registering as a tap.
	const float k_attackHoldDefault = 0.22f;

	// IN FAST, OUT SLOW, and the fast one is the rule AnimGraph's own header
	// gives for anything with an impact in it: a swing that eases in over a
	// quarter second has already missed. The slow out lets the follow-through
	// settle back into locomotion instead of snapping.
	const float k_fadeAttackIn  = 0.08f;
	const float k_fadeAttackOut = 0.18f;

	// A click past this fraction of the swing is BUFFERED rather than dropped.
	// Opened just after the contact: a click before the blade has even landed is
	// the player mashing, and spending it would let the wind-up be skipped.
	const float k_attackBufferOpen = 0.55f;

	// The strike's shape. A REACH plus an ARC plus a height band, because that
	// is the shape a sword swing actually has — see performStrike for why this
	// is not the camera ray the first-person melee weapon uses.
	//
	// 2.2 units against a 2.357-unit character and a 1.75-unit sword held at
	// arm's length: the blade tip genuinely reaches that far at the contact
	// frame, so the number is the animation's and not a gameplay knob pulled to
	// feel generous.
	// 2.6 rather than the 2.2 a one-handed sword wanted: this is a GREAT sword,
	// and the reach is the animation's, not a generosity knob.
	const float k_attackReach    = 2.6f;
	const float k_attackArcDeg   = 100.0f;  // total, so 50 either side of facing
	const float k_attackHeight   = 2.0f;    // vertical band about the chest

	// The heavy is worth about two and a half light hits — it costs roughly
	// twice as long to land and cannot be chained.
	const int   k_lightDamage    = 30;
	const int   k_heavyDamage    = 75;

	// Where the swing is measured FROM, up the body from the transform (which is
	// the capsule foot on this controller, not its centre — see the header).
	const float k_attackOriginY  = 1.2f;

	const char* k_lightClipCvar   = "tps_attack_light";
	const char* k_heavyClipCvar    = "tps_attack_heavy";
	const char* k_lightRateCvar    = "tps_light_rate";
	const char* k_heavyRateCvar    = "tps_heavy_rate";
	const char* k_lightDamageCvar  = "tps_light_damage";
	const char* k_heavyDamageCvar  = "tps_heavy_damage";
	const char* k_attackHoldCvar   = "tps_attack_hold";
	const char* k_attackReachCvar  = "tps_attack_reach";
	const char* k_attackDebugCvar  = "tps_attack_debug";

	const char* k_animLayerClipCvar   = "anim_layer_clip";
	const char* k_animLayerMaskCvar   = "anim_layer_mask";
	const char* k_animLayerAddCvar    = "anim_layer_additive";
	const char* k_animLayerWeightCvar = "anim_layer_weight";

	// Vertical position smoothing, matched to the fixed step. Absorbs
	// stair-stepping and PhysX overlap recovery without lagging horizontal input.
	//
	// IT NO LONGER MOVES THE BODY. Lagging the body transform was the right
	// answer before foot IK and the wrong one after: the transform is the frame
	// the feet are placed in, so a lagged one puts the mesh somewhere the floor
	// is not for as long as the lag lasts - which is the exact window in which
	// anyone stepping onto a stair is looking at the feet. The body now sits on
	// the true capsule position and the lag is handed to the PELVIS (and to the
	// camera pivot), so the upper body still eases up the step while the feet
	// stay on the ground.
	const float k_verticalSmoothRate = 18.0f;

	// Cap on that pelvis lag. Anything past a full step is not step smoothing.
	const float k_maxStepLag = 0.5f;

	// A small axis cross, drawn on top of geometry. The whole point of these is
	// to be visible when the thing they mark is INSIDE something.
	void debugCross(const vector3df& p, float r, irr::video::SColor c)
	{
		RenderManager* rm = RenderManager::Get();
		if (!rm)
			return;

		rm->renderLine3DOverlayOnTop(Line3D(irr::core::line3df(p - vector3df(r, 0, 0), p + vector3df(r, 0, 0)), c));
		rm->renderLine3DOverlayOnTop(Line3D(irr::core::line3df(p - vector3df(0, r, 0), p + vector3df(0, r, 0)), c));
		rm->renderLine3DOverlayOnTop(Line3D(irr::core::line3df(p - vector3df(0, 0, r), p + vector3df(0, 0, r)), c));
	}

	void debugLine(const vector3df& a, const vector3df& b, irr::video::SColor c)
	{
		RenderManager* rm = RenderManager::Get();
		if (rm)
			rm->renderLine3DOverlayOnTop(Line3D(irr::core::line3df(a, b), c));
	}

	// getFootPosition() IS THE GROUND CONTACT. DO NOT TAKE contactOffset OFF IT.
	//
	// It is tempting to: a PhysX CCT is never allowed to touch what it stands on,
	// the sweep stops contactOffset short, so the capsule's geometric bottom does
	// float above the floor. But getFootPosition() has already accounted for it -
	// CctCapsuleController.cpp subtracts (contactOffset + radius + height/2) from
	// the centre, and PxController.h says so in as many words ("The foot position
	// takes the contact offset into account"). Subtracting it a second time drops
	// the character 0.1 units - about seven centimetres on this one - and sinks
	// the boots through the floor. Measured, not reasoned: with anim_ik 0 the
	// feet clipped into the ground by exactly that.
	float groundedFootY(physx::PxController* controller)
	{
		return static_cast<float>(controller->getFootPosition().y);
	}

	// NOT named deg2rad/rad2deg: those are function-like MACROS in
	// Utility/Utility.h, which arrives here transitively. A function with a
	// macro's name expands into nonsense and reports as "type 'float'
	// unexpected" on the definition line, which reads like anything but a macro
	// collision.
	float toRadians(float d) { return d * static_cast<float>(irr::core::DEGTORAD64); }
	float toDegrees(float r) { return r * static_cast<float>(irr::core::RADTODEG64); }

	// Fold an angle into (-180, 180]. Written to survive ANY input magnitude:
	// yaw accumulates from raw mouse movement, so it genuinely does reach the
	// thousands in a long session, and the usual '+540 then fmod' shortcut only
	// holds while the argument stays above -540.
	float wrapAngle(float a)
	{
		a = fmodf(a, 360.0f);
		if (a >  180.0f) a -= 360.0f;
		if (a < -180.0f) a += 360.0f;
		return a;
	}

	// Shortest signed difference from 'from' to 'to'.
	float angleDelta(float from, float to)
	{
		return wrapAngle(to - from);
	}

	float clampf(float v, float lo, float hi)
	{
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	}

	// "x y z" or "x,y,z" from a cvar. A short or unparseable value leaves the
	// missing components at zero rather than rejecting the whole string: half a
	// vector typed into the console should move the thing half way, not look
	// like the command did nothing.
	vector3df parseVec3(const std::string& text)
	{
		vector3df out(0.0f, 0.0f, 0.0f);
		float* dst[3] = { &out.X, &out.Y, &out.Z };

		size_t i = 0;
		for (int n = 0; n < 3 && i < text.size(); ++n)
		{
			while (i < text.size() && (text[i] == ' ' || text[i] == ',' || text[i] == '	'))
				++i;
			if (i >= text.size())
				break;

			*dst[n] = static_cast<float>(atof(text.c_str() + i));

			while (i < text.size() && text[i] != ' ' && text[i] != ',' && text[i] != '	')
				++i;
		}
		return out;
	}
}

void PlayerControllerTPS::init()
{
	spdlog::info("PlayerControllerTPS: third-person prototype controller active");

	// CREATE, DON'T SET. setCVar would happily overwrite, and init() runs again
	// on every scene load — a player who turned strafe locomotion off would
	// find it back on after every load, which reads as the console command not
	// working rather than as a deliberate reset.
	if (!WorldManager::Get()->getCVarExists(k_strafeCvar))
		WorldManager::Get()->setCVar(k_strafeCvar, "1");

	if (!WorldManager::Get()->getCVarExists(k_animBlendCvar))
		WorldManager::Get()->setCVar(k_animBlendCvar, "1");

	if (!WorldManager::Get()->getCVarExists(k_animDebugCvar))
		WorldManager::Get()->setCVar(k_animDebugCvar, "0");

	// The layer test harness. Empty clip name = no test layer, which is the
	// point: it costs nothing until someone types a clip into it.
	//
	//   anim_layer_clip Standing_Arguing
	//   anim_layer_mask upper_body
	//
	// and the character gesticulates from the waist up while its legs keep
	// running the locomotion blend. Masks available: full, upper_body,
	// lower_body, no_arms, spine_up, arms, left_arm, right_arm, head, legs.
	// The probe draw is off by default - it is a diagnostic, not an overlay.
	if (!WorldManager::Get()->getCVarExists(k_animIKDrawCvar))
		WorldManager::Get()->setCVar(k_animIKDrawCvar, "0");

	// Foot IK on by default; anim_ik 0 is the bisect. It is a CORRECTION, so
	// turning it off must leave a character that still animates correctly — if
	// anything else changes when this is flipped, the IK is doing more than it
	// should be.
	//
	// Disabling IK for now until more work on it can be done to fix the bugs
	//if (!WorldManager::Get()->getCVarExists(k_animIKCvar))
		//WorldManager::Get()->setCVar(k_animIKCvar, "1");
	WorldManager::Get()->setCVar(k_animIKCvar, "0");

	if (!WorldManager::Get()->getCVarExists(k_animLayerClipCvar))
		WorldManager::Get()->setCVar(k_animLayerClipCvar, "");
	if (!WorldManager::Get()->getCVarExists(k_animLayerMaskCvar))
		WorldManager::Get()->setCVar(k_animLayerMaskCvar, "upper_body");
	if (!WorldManager::Get()->getCVarExists(k_animLayerAddCvar))
		WorldManager::Get()->setCVar(k_animLayerAddCvar, "0");
	if (!WorldManager::Get()->getCVarExists(k_animLayerWeightCvar))
		WorldManager::Get()->setCVar(k_animLayerWeightCvar, "1");

	// The carried weapon and the grip pose that closes the hand round it.
	//
	//   tps_sword 0/1            carry it or put it away
	//   tps_sword_pos "0 0.09 0" offset in the HAND JOINT's own axes, +Y = fingers
	//   tps_sword_rot "-28 0 -90" the same joint's frame, degrees
	//   tps_grip_clip <name>     clip whose FIRST FRAME supplies the finger pose
	//   tps_grip_mask fingers    or right_fingers for the sword hand alone
	//
	// tps_grip_clip HAS A DEFAULT, and it has to track the clip set: it is
	// whichever imported animation holds the weapon, because only its FINGERS
	// are ever read and the whole empty-handed locomotion set inherits the grip
	// from them. Today that is the great sword heavy attack, in two hands.
	if (!WorldManager::Get()->getCVarExists(k_swordCvar))
		WorldManager::Get()->setCVar(k_swordCvar, "1");
	if (!WorldManager::Get()->getCVarExists(k_swordPosCvar))
		WorldManager::Get()->setCVar(k_swordPosCvar, k_swordPosDefault);
	if (!WorldManager::Get()->getCVarExists(k_swordRotCvar))
		WorldManager::Get()->setCVar(k_swordRotCvar, k_swordRotDefault);
	if (!WorldManager::Get()->getCVarExists(k_gripClipCvar))
		WorldManager::Get()->setCVar(k_gripClipCvar, k_gripClipDefault);

	// fingers, not right_fingers — BOTH hands. This is a great sword and it is
	// held in two, which is the reverse of the one-handed case: closing only the
	// right hand would leave the left one open around a hilt it is visibly
	// gripping. Measured, the heavy clip closes both.
	if (!WorldManager::Get()->getCVarExists(k_gripMaskCvar))
		WorldManager::Get()->setCVar(k_gripMaskCvar, AnimMaskName::k_fingers);

	// --- The swing -----------------------------------------------------------
	//   tps_attack_light  <name>  the light (combo) clip
	//   tps_attack_heavy  <name>  the heavy clip
	//   tps_attack_hold   0.22    seconds of hold that mean "heavy"
	//   tps_light_rate    1.15    playback rate per kind
	//   tps_heavy_rate    1.0
	//   tps_light_damage  30      per connecting hit
	//   tps_heavy_damage  75
	//   tps_attack_reach  2.6     world units from the chest, along body facing
	//   tps_attack_debug  0       draw the arc and the entities it caught
	//
	// THE STAGE TABLE IS NOT A CVAR. Its frame numbers only mean anything
	// against one particular export, so pointing a clip cvar at a re-export
	// without re-measuring fires the strike at whatever that frame happens to be
	// in the new animation — a hit that lands visibly early or late rather than
	// an obvious failure. Run Tools/measure_attack_contact.py and edit the table.
	if (!WorldManager::Get()->getCVarExists(k_lightClipCvar))
		WorldManager::Get()->setCVar(k_lightClipCvar, k_clipLightDefault);
	if (!WorldManager::Get()->getCVarExists(k_heavyClipCvar))
		WorldManager::Get()->setCVar(k_heavyClipCvar, k_clipHeavyDefault);
	if (!WorldManager::Get()->getCVarExists(k_attackHoldCvar))
		WorldManager::Get()->setCVar(k_attackHoldCvar, std::to_string(k_attackHoldDefault));
	if (!WorldManager::Get()->getCVarExists(k_lightRateCvar))
		WorldManager::Get()->setCVar(k_lightRateCvar, std::to_string(k_lightRateDefault));
	if (!WorldManager::Get()->getCVarExists(k_heavyRateCvar))
		WorldManager::Get()->setCVar(k_heavyRateCvar, std::to_string(k_heavyRateDefault));
	if (!WorldManager::Get()->getCVarExists(k_lightDamageCvar))
		WorldManager::Get()->setCVar(k_lightDamageCvar, std::to_string(k_lightDamage));
	if (!WorldManager::Get()->getCVarExists(k_heavyDamageCvar))
		WorldManager::Get()->setCVar(k_heavyDamageCvar, std::to_string(k_heavyDamage));
	if (!WorldManager::Get()->getCVarExists(k_attackReachCvar))
		WorldManager::Get()->setCVar(k_attackReachCvar, std::to_string(k_attackReach));
	if (!WorldManager::Get()->getCVarExists(k_attackDebugCvar))
		WorldManager::Get()->setCVar(k_attackDebugCvar, "0");

	// OFF by default, because F3 opens it. The cvar is still the single source
	// of truth — the key flips it rather than shadowing it — so 'tps_sword_ui 1'
	// from the console and the key are the same switch, and neither can get out
	// of step with the other.
	if (!WorldManager::Get()->getCVarExists(k_swordUICvar))
		WorldManager::Get()->setCVar(k_swordUICvar, "0");
}

void PlayerControllerTPS::destroy()
{
	// No viewmodel or LDR-effect nodes to unregister — the first-person
	// controller's destroy() has real work to do because its sub-controllers
	// register those with the RenderManager; this prototype owns no such nodes.
	//
	// It does own the character's POSE, though. setExternalPose pins a node
	// until something clears it, so hand the body back to Irrlicht's own
	// playback on the way out; a node that outlives this controller (the editor
	// leaving play mode, say) would otherwise freeze on the last pose evaluated.
	//
	// GameManager::destroy() still calls this unconditionally, which is correct
	// and must stay that way — see the comment there.
	if (!WorldManager::Get())
		return;

	// The sword hangs off a bone of the character node, so a scene teardown
	// would take it with it - but destroy() also runs when the editor leaves
	// play mode with the scene still standing, and that path has to put it away
	// itself or a second entry into play mode attaches a second one.
	if (m_swordNode)
	{
		m_swordNode->remove();
		m_swordNode = nullptr;
	}
	m_swordOwner = nullptr;

	// And the swing, for the same reason: the clock is the controller's, not the
	// AnimGraph's, so a scene load or a trip out of play mode taken mid-swing
	// would otherwise come back with a timer still running against a layer that
	// no longer exists - and with a contact still pending against the old world.
	m_attackActive  = false;
	m_attackQueued  = false;
	m_attackStruck  = false;
	m_attackRestart = false;
	m_attackTime    = 0.0f;

	// The BUTTON too, or a press that was still down at teardown comes back on
	// the next entry into play mode looking like a hold that has already been
	// running for however long the scene took to load - which fires a heavy at
	// the player the instant they gain control.
	m_attackDown  = false;
	m_attackHeld  = 0.0f;
	m_attackSpent = false;

	// And the software cursor, if this controller is the one holding it up.
	// destroy() also runs when the editor leaves play mode, and ImGui's
	// MouseDrawCursor is global state that nothing else would ever clear — the
	// editor turns the OS cursor back on, so the result would be two of them.
	if (m_swordUIHeld)
	{
		ImGui::GetIO().MouseDrawCursor = false;
		m_swordUIHeld = false;
	}

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (player.isValid() && player.hasComponent<AnimationComponent>())
		player.getComponent<AnimationComponent>().graph.release();
}

void PlayerControllerTPS::setNoclip(bool on)
{
	m_noclip = on;
	if (on)
	{
		// Drop momentum and every latched volume state, so switching it back off
		// does not resume a fall or leave the player "on a ladder" in mid-air.
		m_velocity.set(0.0f, 0.0f, 0.0f);
		m_swimming = false;
		m_headUnderWater = false;
		m_onLadder = false;
	}
}

int PlayerControllerTPS::getCurrentHealth()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<DamageReceiverComponent>())
		return 0;

	return player.getComponent<DamageReceiverComponent>().health;
}

int PlayerControllerTPS::getMaxHealth()
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<DamageReceiverComponent>())
		return 0;

	return player.getComponent<DamageReceiverComponent>().threshold;
}

bool PlayerControllerTPS::capturePlayerState(PlayerSaveState& out) const
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<DamageReceiverComponent>())
		return false;   // no live player: refuse rather than write a sidecar of zeroes

	// Health is the only thing this prototype owns that has no entity to hang
	// off. Ammo, weapons, magazines, items and skills all belong to systems it
	// does not have, and the vectors are left empty rather than zero-filled —
	// applyPlayerState treats a short vector as "not mentioned".
	out.version = PlayerSaveState::CURRENT_VERSION;
	out.health = player.getComponent<DamageReceiverComponent>().health;

	return true;
}

void PlayerControllerTPS::update(float dt)
{
	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid())
		return;

	auto& transform = player.getComponent<TransformComponent>();
	auto& cct       = player.getComponent<CCTComponent>();

	if (!cct.controller)
		return;

	const float seconds = dt / 1000.0f;

	if (m_firstUpdate)
	{
		// Seed from the live CCT so a freshly constructed instance does not read
		// a huge bogus delta on its first frame, and face the body the way the
		// spawn marker pointed it.
		m_smoothedY   = groundedFootY(cct.controller);
		m_stepLag     = 0.0f;
		m_bodyYaw  = wrapAngle(transform.rotation.Y);
		m_orbitYaw = m_bodyYaw;

		// Start at full extension. Left at zero the camera would sit in the
		// character's chest on frame one and ease outward, which reads as a bug.
		m_boom     = k_boomLength;
		m_zoomBoom = k_boomLength;
		m_shoulder = k_shoulderRight;

		// Seed the blend reference too, or the first frame reads the spawn yaw
		// as a strafe angle and swings the body (the strafe clips carry a baked
		// ~84-degree body yaw, so that is very visible).
		m_travelYaw   = m_bodyYaw;
		m_animFacing  = m_bodyYaw;
		m_travelValid = false;

		// A fresh controller is a teleport as far as the IK is concerned: its
		// smoothed foot offsets are from wherever the character used to be.
		if (player.hasComponent<AnimationComponent>())
			player.getComponent<AnimationComponent>().graph.resetFootIK();

		InputManager::Get()->centerMouse();
		m_firstUpdate = false;
	}

	// --- The debug panel's toggle --------------------------------------------
	// BEFORE every input read below, so the frame the panel opens is already a
	// frame this controller ignores rather than one that swings the camera as
	// the cursor appears.
	//
	// ignore_process_flag = true, which is not optional: the panel switches
	// input processing off while it is up, so a key read through the normal path
	// could open it and then never close it.
	if (InputManager::Get()->getKeyPressOnce(k_swordUIKey, &m_swordUIKey, true))
	{
		WorldManager::Get()->setCVar(k_swordUICvar, uiHasMouse() ? "0" : "1");

		// Re-anchor on both edges. Opening, it stops the pointer appearing
		// wherever the look happened to leave it; closing, it drops the delta
		// that walking the cursor across the panel would otherwise hand the
		// camera on the first frame back.
		InputManager::Get()->centerMouse();
	}

	// THE SOFTWARE CURSOR, not the OS one — game mode hides that (see
	// RenderManager) and the console draws its own the same way.
	//
	// Written only on the frames this controller is actually asserting it, and
	// once more on the frame it stops. Writing it unconditionally every frame
	// would clear the flag the game console sets when IT opens.
	if (uiHasMouse())
	{
		ImGui::GetIO().MouseDrawCursor = true;
		m_swordUIHeld = true;
	}
	else if (m_swordUIHeld)
	{
		ImGui::GetIO().MouseDrawCursor = false;
		m_swordUIHeld = false;
	}

	// --- Orbit input ---------------------------------------------------------
	// THE ONLY getMouseDelta() CALL IN THIS CONTROLLER, which is what makes
	// skipping it enough to release the OS cursor — see uiHasMouse().
	if (!inputSuppressed())
	{
		const vector2df mouseDelta = InputManager::Get()->getMouseDelta();

		// Left Control: inspect the model. The camera orbits on TOP of the
		// normal orbit angle instead of replacing it, so movement direction and
		// body facing — both of which read m_orbitYaw/m_orbitPitch directly —
		// never see the difference and the body holds still while the camera
		// swings around it.
		const bool freeLook = InputManager::Get()->isKeyPressed(KEYBOARD_KEY::KEY_LCONTROL);

		if (freeLook)
		{
			// Same sign convention as the normal branch below — see its comment.
			m_freeLookYaw   -= mouseDelta.X * k_sensitivity;
			m_freeLookPitch -= mouseDelta.Y * k_sensitivity;

			// Clamped against the SAME absolute pitch bounds as normal look, just
			// expressed relative to the frozen m_orbitPitch, so inspecting the
			// model can't tip the camera past straight-up/straight-down.
			m_freeLookPitch = clampf(m_freeLookPitch,
			                          k_pitchMin - m_orbitPitch,
			                          k_pitchMax - m_orbitPitch);
			m_freeLookYaw = wrapAngle(m_freeLookYaw);

			// Wheel zoom, gated on the same key as the orbit so it only acts
			// while inspecting. Scrolling forward (positive delta) zooms IN —
			// flip the sign here if a given mouse/driver reports it backwards.
			const float wheel = InputManager::Get()->getMouseWheelDelta();
			if (wheel != 0.0f)
				m_zoomBoom = clampf(m_zoomBoom - wheel * k_zoomStep, k_zoomBoomMin, k_zoomBoomMax);
		}
		else
		{
			// BOTH SUBTRACT. getMouseDelta() is negative when the mouse moves
			// right or down — InputManager flips the raw sign deliberately so
			// that every call site can keep using '-='. Adding here inverts the
			// axis.
			//
			// Positive pitch is "looking down" in Irrlicht, and here it also
			// swings the boom UP, so mouse-down puts the camera above the
			// character looking down at it.
			m_orbitYaw   -= mouseDelta.X * k_sensitivity;
			m_orbitPitch -= mouseDelta.Y * k_sensitivity;
			m_orbitPitch  = clampf(m_orbitPitch, k_pitchMin, k_pitchMax);

			// Both yaw values accumulate without bound otherwise, and float
			// precision on an angle degrades once it reaches the thousands.
			m_orbitYaw = wrapAngle(m_orbitYaw);

			// Control released: ease the inspection offset back to zero rather
			// than snapping, so the camera settles smoothly back behind the
			// character instead of jumping there in one frame.
			if (m_freeLookYaw != 0.0f || m_freeLookPitch != 0.0f)
			{
				const float f = 1.0f - expf(-k_freeLookReturnRate * seconds);
				m_freeLookYaw   += (0.0f - m_freeLookYaw)   * f;
				m_freeLookPitch += (0.0f - m_freeLookPitch) * f;

				if (fabsf(m_freeLookYaw)   < 0.01f) m_freeLookYaw   = 0.0f;
				if (fabsf(m_freeLookPitch) < 0.01f) m_freeLookPitch = 0.0f;
			}
		}
	}

	// --- Movement input, taken in CAMERA space -------------------------------
	// Forward is where the camera looks, not where the character faces. The
	// character then turns to follow, which is what makes it read as a
	// third-person control scheme rather than a tank one.
	float xMove = 0.0f, zMove = 0.0f;

	if (!inputSuppressed())
	{
		InputManager* in = InputManager::Get();
		if (in->isActionPressed("forward")  && !in->isActionPressed("backward")) zMove += 1.0f;
		if (in->isActionPressed("backward") && !in->isActionPressed("forward"))  zMove -= 1.0f;
		if (in->isActionPressed("strafel")  && !in->isActionPressed("strafer"))  xMove -= 1.0f;
		if (in->isActionPressed("strafer")  && !in->isActionPressed("strafel"))  xMove += 1.0f;
	}

	// Irrlicht's left-handed Y-up convention: yaw 0 looks down +Z.
	const float yawRad = toRadians(m_orbitYaw);
	const vector3df forward(sinf(yawRad), 0.0f, cosf(yawRad));
	const vector3df right(cosf(yawRad), 0.0f, -sinf(yawRad));

	vector3df wish = forward * zMove + right * xMove;
	const bool hasInput = wish.getLengthSQ() > 0.0001f;
	if (hasInput)
		wish.normalize();

	// The run modifier, read outside the movement block above because noclip
	// wants it too, and gated on m_locked because a locked player presses
	// nothing. CROUCH IS NOT READ HERE AT ALL — on the ground it does nothing
	// (this controller has no crouch, and the paladin has no crouch clip), and
	// in noclip it is the descend key, handled with the rest of the fly input.
	const bool wantRun = !inputSuppressed() && InputManager::Get()->isActionPressed("sprint");

	const float targetSpeed = wantRun ? k_runSpeed : k_walkSpeed;

	// Re-read every frame so the console toggle takes effect on the spot.
	m_strafeLocomotion = WorldManager::Get()->getCVarValue(k_strafeCvar) != "0";

	// Pushed rather than pulled: AnimGraph is engine-side and has no business
	// knowing WorldManager exists, and this is the only place that already reads
	// the cvar store every frame. Static, because it is a debug switch and not a
	// per-character property.
	AnimGraph::setBlendEnabled(WorldManager::Get()->getCVarValue(k_animBlendCvar) != "0");

	// --- Horizontal velocity -------------------------------------------------
	vector3df planar(m_velocity.X, 0.0f, m_velocity.Z);

	if (hasInput)
	{
		const float accel = m_grounded ? k_accel : k_airAccel;
		planar += wish * (accel * seconds);

		const float speed = planar.getLength();
		if (speed > targetSpeed)
			planar *= targetSpeed / speed;
	}
	else if (m_grounded)
	{
		// Brake only on the ground. Air friction would make jumps feel sticky
		// and remove any ability to steer a fall.
		const float speed = planar.getLength();
		const float drop  = k_friction * seconds;
		planar = (speed > drop) ? planar * ((speed - drop) / speed)
		                        : vector3df(0.0f, 0.0f, 0.0f);
	}

	m_velocity.X = planar.X;
	m_velocity.Z = planar.Z;

	// --- Jump and gravity ----------------------------------------------------
	// ONE JUMP PER PRESS. The press is consumed whether or not it produced a
	// jump — consuming it only on success means a key held through a fall is
	// still unconsumed on landing, and the player auto-hops the moment they
	// touch down.
	if (!inputSuppressed() && InputManager::Get()->isActionPressed("jump"))
	{
		if (!m_jumpConsumed)
		{
			m_jumpConsumed = true;

			if (m_grounded)
			{
				m_velocity.Y = k_jumpSpeed;
				m_grounded   = false;
			}
		}
	}
	else
	{
		m_jumpConsumed = false;
	}

	if (!m_noclip)
	{
		// Heavier on the way down: a symmetric arc reads as floaty.
		const float g = (m_velocity.Y <= 0.0f) ? k_gravity * k_fallGravityMult : k_gravity;
		m_velocity.Y -= g * seconds;
	}

	// --- Move the capsule ----------------------------------------------------
	physx::PxFilterData filterData;
	filterData.word0 = RHG_STATIC | RHG_DYNAMIC | RHG_CLIP_PLAYER;
	physx::PxControllerFilters filters(&filterData);

	if (m_noclip)
	{
		// Fly along the orbit direction, pitch included, with jump/crouch as
		// world up/down. setPosition ignores geometry, unlike move().
		const float pitchRad = toRadians(m_orbitPitch);
		vector3df fly(0.0f, 0.0f, 0.0f);

		if (!inputSuppressed())
		{
			InputManager* in = InputManager::Get();
			const vector3df look(sinf(yawRad) * cosf(pitchRad),
			                     -sinf(pitchRad),
			                     cosf(yawRad) * cosf(pitchRad));

			if (in->isActionPressed("forward"))  fly += look;
			if (in->isActionPressed("backward")) fly -= look;
			if (in->isActionPressed("strafer"))  fly += right;
			if (in->isActionPressed("strafel"))  fly -= right;
			if (in->isActionPressed("jump"))     fly.Y += 1.0f;
			if (in->isActionPressed("crouch"))   fly.Y -= 1.0f;
		}

		if (fly.getLengthSQ() > 0.0001f)
			fly.normalize();

		// NOT derived from targetSpeed. Noclip is a debug camera and crossing a
		// level at a walking 2.2 u/s is not what it is for, so it flies at the
		// RUN tier either way and the modifier doubles it from there.
		const float flySpeed = k_runSpeed * (wantRun ? 3.0f : 1.5f);
		const physx::PxExtendedVec3 pos = cct.controller->getPosition();
		cct.controller->setPosition(physx::PxExtendedVec3(
			pos.x + fly.X * flySpeed * seconds,
			pos.y + fly.Y * flySpeed * seconds,
			pos.z + fly.Z * flySpeed * seconds));

		m_velocity.set(0.0f, 0.0f, 0.0f);
		m_grounded  = false;
		m_smoothedY = groundedFootY(cct.controller);
	}
	else
	{
		cct.displacement = physx::PxVec3(
			m_velocity.X * seconds,
			m_velocity.Y * seconds,
			m_velocity.Z * seconds);

		const physx::PxControllerCollisionFlags flags =
			cct.controller->move(cct.displacement, 0.001f, seconds, filters);

		// Same three-frame debounce the first-person controller uses: a single
		// flickering collision flag would otherwise strobe the ground state, and
		// here that would also strobe the animation between idle and move.
		const int kRequiredAirborneFrames = 3;

		if (flags & physx::PxControllerCollisionFlag::eCOLLISION_DOWN)
		{
			m_grounded = true;
			m_airborneFrames = 0;

			// Stop accumulating downward speed while resting on the floor, or the
			// first step off a ledge inherits a large negative Y.
			if (m_velocity.Y < 0.0f)
				m_velocity.Y = 0.0f;
		}
		else if (++m_airborneFrames >= kRequiredAirborneFrames)
		{
			m_grounded = false;
		}

		// Head-bump: kill upward speed so the capsule does not hang under a
		// ceiling for the rest of the rise.
		if ((flags & physx::PxControllerCollisionFlag::eCOLLISION_UP) && m_velocity.Y > 0.0f)
			m_velocity.Y = 0.0f;
	}

	// --- Write the body pose -------------------------------------------------
	// THE FOOT, NOT THE CENTRE. The character mesh hangs off this transform.
	const physx::PxExtendedVec3 foot = cct.controller->getFootPosition();
	const vector3df footPos(static_cast<float>(foot.x),
	                        groundedFootY(cct.controller),
	                        static_cast<float>(foot.z));

	// In the air there is nothing to smooth - a fall is not a step - and letting
	// the lag accumulate over one would hand the IK a large pelvis offset the
	// instant the landing re-enabled it.
	if (m_grounded)
	{
		const float yFactor = 1.0f - expf(-k_verticalSmoothRate * seconds);
		m_smoothedY += (footPos.Y - m_smoothedY) * yFactor;
	}
	else
	{
		m_smoothedY = footPos.Y;
	}

	m_stepLag = clampf(m_smoothedY - footPos.Y, -k_maxStepLag, k_maxStepLag);

	// THE TRUE POSITION, NOT THE SMOOTHED ONE. See k_verticalSmoothRate.
	transform.setPosition(footPos);

	// YAW ONLY — pitch in the body transform faceplants the model. What the yaw
	// chases now depends on the locomotion mode; see updateFacing.
	const float planarSpeed = vector3df(m_velocity.X, 0.0f, m_velocity.Z).getLength();

	updateFacing(seconds, hasInput, planarSpeed, wish);

	transform.setRotation(vector3df(0.0f, m_bodyYaw + k_modelYawOffset, 0.0f));

	// The invisible raycast hitbox tracks the capsule CENTRE, which is where
	// PhysX keeps the controller position — not the foot written above.
	if (cct.hitboxNode)
	{
		auto* capsule = static_cast<physx::PxCapsuleController*>(cct.controller);
		const float r = static_cast<float>(capsule->getRadius());
		const float h = static_cast<float>(capsule->getHeight());

		// The CENTRE is contactOffset above the foot, not r + h/2 above it - see
		// groundedFootY. The cube was a tenth of a unit low before this.
		const float skin = static_cast<float>(cct.controller->getContactOffset());

		cct.hitboxNode->setPosition(
			vector3df(footPos.X, footPos.Y + skin + r + h * 0.5f, footPos.Z));
		cct.hitboxNode->setScale(vector3df(2.0f * r, h + 2.0f * r, 2.0f * r));
	}

	m_isMoving = planarSpeed > k_moveAnimThreshold;

	// BEFORE updateAnimation, and after updateFacing. It reads the attack button
	// and owns the swing clock; updateAnimation only drives the layer from the
	// state it leaves behind. It needs m_bodyYaw to be the CURRENT facing,
	// because the strike's arc is measured off it.
	updateAttack(player, seconds, planarSpeed);

	updateCamera(player, dt);
	updateAnimation(player, planarSpeed);
}

void PlayerControllerTPS::updateCamera(anax::Entity& player, float dt)
{
	if (!player.hasComponent<CameraComponent>())
		return;

	auto& cameraComponent = player.getComponent<CameraComponent>();
	if (!cameraComponent.camera)
		return;

	// This only does anything because player_tps.ent sets controllerDriven —
	// otherwise CameraSystem::update() overwrites both of these from the body
	// transform every frame and the camera rides inside the character's chest.
	auto& transform = player.getComponent<TransformComponent>();

	// 'offset' is the ORBIT PIVOT here, not an eye position. See the .ent.
	//
	// The free-look offset is folded in HERE ONLY. m_orbitYaw/m_orbitPitch stay
	// the angle movement and body facing read; effectiveYaw/effectivePitch are
	// what the camera actually renders from, so holding Left Control moves the
	// camera around the character without the character ever finding out.
	const float effectiveYaw   = m_orbitYaw   + m_freeLookYaw;
	const float effectivePitch = m_orbitPitch + m_freeLookPitch;
	const float yawRad   = toRadians(effectiveYaw);
	const float pitchRad = toRadians(effectivePitch);

	// The camera's right, level with the ground: the shoulder offset has to
	// follow the orbit, or it would read as a fixed world-space nudge and swing
	// from one shoulder to the other as the player turned.
	const vector3df camRight(cosf(yawRad), 0.0f, -sinf(yawRad));

	// THE SMOOTHED HEIGHT, not the body's. The body transform is now the true
	// capsule position so the foot IK has a floor it can trust; the camera still
	// wants the eased one, or every stair tread is a jolt.
	vector3df pivot = transform.position + cameraComponent.offset
	                + vector3df(0.0f, k_shoulderUp + m_stepLag, 0.0f);

	// CLAMP THE LATERAL SHIFT AGAINST GEOMETRY BEFORE APPLYING IT.
	//
	// This is not the same probe as the boom pullback below and cannot be
	// folded into it. k_shoulderRight is larger than the CCT capsule's 0.3
	// radius, so standing with the right shoulder against a wall puts the
	// shifted pivot INSIDE that wall — and a boom ray that starts inside
	// geometry has nothing in front of it to hit, so the pullback silently
	// does nothing and the camera renders from inside the world.
	//
	// Same RHG_CHARACTER exclusion as the boom probe, and for the same reason:
	// the player's own capsule is RHG_DYNAMIC and would otherwise be hit
	// immediately.
	float shoulder = k_shoulderRight;

	auto lateral = PhysicsManager::Get()->raycast(
		pivot, camRight, k_shoulderRight + k_boomRadius,
		RHG_STATIC | RHG_DYNAMIC | RHG_CLIP_PLAYER,
		RHG_CHARACTER);

	if (lateral.hit)
	{
		const auto hp = lateral.data.getAnyHit(0).position;
		const float d = (vector3df(hp.x, hp.y, hp.z) - pivot).getLength() - k_boomRadius;

		shoulder = clampf(d, 0.0f, k_shoulderRight);
	}

	// In immediately, out gradually — the boom's rule, applied to the framing
	// for the same reason. A wall brushed on the right would otherwise slide the
	// character to screen centre and snap him back the instant it cleared.
	if (shoulder < m_shoulder)
		m_shoulder = shoulder;
	else
		m_shoulder += (shoulder - m_shoulder) * (1.0f - expf(-k_boomExtendRate * (dt / 1000.0f)));

	pivot += camRight * m_shoulder;

	// Straight back along the look direction, then up by the pitch. This is the
	// negated forward vector, so a positive pitch lifts the camera and aims it
	// down at the character.
	const vector3df boomDir(
		-sinf(yawRad) * cosf(pitchRad),
		 sinf(pitchRad),
		-cosf(yawRad) * cosf(pitchRad));

	// m_zoomBoom rather than k_boomLength: the mouse-wheel zoom (Left Control
	// held) moves this target, and collision pullback below still shortens it
	// same as it always did the constant.
	float wanted = m_zoomBoom;

	// Collision pullback.
	//
	// RHG_CHARACTER IS EXCLUDED AND THAT IS LOAD-BEARING. The player's own CCT
	// capsule is tagged RHG_DYNAMIC so weapons and damage traces keep hitting it,
	// which means an unfiltered probe fired from the pivot hits the player
	// immediately and pins the camera inside their own chest. PhysX's filter
	// equation is a bitwise AND and cannot express "everything except", which is
	// exactly why PhysicsManager::raycast grew an excludeGroups prefilter.
	// 'wanted', not k_boomLength: zooming out with the mouse wheel can push the
	// target past the old fixed boom length, and a probe shorter than 'wanted'
	// would miss geometry beyond its range and let the camera clip through it.
	auto ray = PhysicsManager::Get()->raycast(
		pivot, boomDir, wanted + k_boomRadius,
		RHG_STATIC | RHG_DYNAMIC | RHG_CLIP_PLAYER,
		RHG_CHARACTER);

	if (ray.hit)
	{
		// Measured from the returned position rather than read off 'distance':
		// the query asks for ePOSITION | eNORMAL, and getAnyHit(0).position is
		// the accessor the rest of the codebase uses on this result.
		const auto hp = ray.data.getAnyHit(0).position;
		const float d = (vector3df(hp.x, hp.y, hp.z) - pivot).getLength() - k_boomRadius;

		if (d < wanted)
			wanted = (d > 0.0f) ? d : 0.0f;
	}

	// Pull IN immediately, push OUT gradually. Snapping outward as a corner
	// clears reads as a shove; snapping inward is invisible, because whatever
	// caused it is already filling the screen.
	if (wanted < m_boom)
		m_boom = wanted;
	else
		m_boom += (wanted - m_boom) * (1.0f - expf(-k_boomExtendRate * (dt / 1000.0f)));

	const vector3df camPos = pivot + boomDir * m_boom;

	cameraComponent.camera->setPosition(camPos);

	// Rotation is the LOOK direction, which is the opposite of the boom — the
	// camera sits behind the character and looks forward past it.
	cameraComponent.camera->setRotation(vector3df(effectivePitch, effectiveYaw, 0.0f));

	// THE TARGET IS SET HERE TOO, even though CameraSystem also sets it.
	//
	// Irrlicht does not build the view from the node's rotation at all —
	// CCameraSceneNode::render() calls buildCameraLookAtMatrixLH(Position,
	// Target, Up). Rotation only reaches the view indirectly, via the child
	// target node that CameraSystem re-resolves off the camera pose.
	//
	// And CameraSystem runs BEFORE this controller, not after: Engine::run()
	// ticks m_worldManager.update() — which owns CameraSystem — and only then
	// m_stateManager.update(), which owns GameManager and therefore this. So the
	// Target standing at render time was resolved from LAST tick's camera pose.
	//
	// On the first-person mount that is invisible: the camera barely translates
	// between ticks, so a stale Target costs one frame of rotational lag and
	// nothing else. Here the camera sweeps an arc of radius m_boom every tick, so
	// a stale Target paired with a fresh Position tilts the view off the boom
	// axis by the whole frame's mouse delta — the character slides off centre
	// while you turn and snaps back when you stop. Writing it here makes
	// position, rotation and target agree within the tick that renders them.
	//
	// The distance comes from the component so the .ent still governs it; the
	// fallback covers a camera authored with a zero target.
	const float lookDistance = cameraComponent.target.getLength();

	cameraComponent.camera->setTarget(
		camPos - boomDir * ((lookDistance > 1.0f) ? lookDistance : 100.0f));
}

// ---------------------------------------------------------------------------
// Facing
// ---------------------------------------------------------------------------

void PlayerControllerTPS::updateFacing(float seconds, bool hasInput, float planarSpeed,
                                      const vector3df& wish)
{
	const bool moving = hasInput && planarSpeed > k_moveAnimThreshold;

	// --- The direction of travel, as the BLEND should see it -----------------
	// Not the raw velocity angle. Velocity is the right source at speed —
	// momentum carried around a corner really is a slide and should animate as
	// one — but at low speed its direction is dominated by the last single
	// acceleration impulse. Starting to run while swinging the camera therefore
	// swung the measured direction with it for a frame or two, and with about 84
	// degrees of body yaw baked into the strafe clips, even a small strafe share
	// reads as a visible flick.
	//
	// So below walking speed the INPUT direction is mixed in. In strafe mode wish
	// is built from the camera yaw and the blend is measured against the camera
	// yaw, so the input contributes exactly the octant the player is pressing:
	// zero noise, zero lag, and exactly the intent. The hand-over to real
	// velocity is complete by k_walkSpeed, where the integrator has converged.
	float targetYaw = m_travelYaw;

	const bool travelling = (planarSpeed > k_travelEpsilon) || hasInput;

	if (travelling)
	{
		const float velYaw = (planarSpeed > k_travelEpsilon)
			? toDegrees(atan2f(m_velocity.X, m_velocity.Z))
			: m_travelYaw;

		if (hasInput && planarSpeed < k_walkSpeed)
		{
			const float wishYaw = toDegrees(atan2f(wish.X, wish.Z));
			const float t = clampf(planarSpeed / k_walkSpeed, 0.0f, 1.0f);

			targetYaw = wrapAngle(wishYaw + angleDelta(wishYaw, velYaw) * t);
		}
		else
		{
			targetYaw = velYaw;
		}

		m_travelYaw   = targetYaw;
		m_travelValid = true;
	}
	else
	{
		// Stopped. Hold the last direction for the overlay, but mark it stale so
		// the next move snaps instead of slewing.
		m_travelValid = false;
	}

	// --- What the directional blend measures against -------------------------
	// Deliberately NOT m_bodyYaw, and deliberately updated even while the body
	// is not chasing anything. See the member comment: m_bodyYaw lags by design,
	// and the blend must not read that lag as a strafe.
	//
	// Strafe mode: the body's target is always the camera, so that is the
	// reference. Pressing forward then gives a travel angle of zero from the
	// first frame, however far behind the body happens to be.
	//
	// Free mode: the body chases the direction of TRAVEL, so the reference is
	// the travel direction itself and the angle collapses to zero. That is the
	// correct answer for a mode whose whole premise is that the character only
	// ever animates forwards.
	m_animFacing = m_strafeLocomotion ? m_orbitYaw : m_travelYaw;

	if (moving)
	{
		// Any movement cancels an in-place turn outright, clip and all: the
		// footwork that clip describes is standing footwork.
		m_turnDir = 0;

		// THIS IS THE WHOLE DIFFERENCE BETWEEN THE TWO LOCOMOTION MODES.
		//
		// Strafe: the body holds the CAMERA's yaw, so the direction of travel
		// lands anywhere in the 360 degrees around it and the strafe clips have
		// something to describe.
		//
		// Free: the body turns to face the way it is GOING, which reads better
		// for open running but means the character can only ever animate
		// forwards — nothing is ever sideways relative to a body that has
		// already turned to follow it.
		//
		// m_animFacing IS that target, computed above. One source of truth on
		// purpose: the blend measures against the yaw the body is chasing, so if
		// these two ever disagreed the animation would describe a turn the body
		// was not making.
		const float delta = angleDelta(m_bodyYaw, m_animFacing);
		const float step  = k_bodyTurnRate * seconds;

		m_bodyYaw += (delta > step) ? step : ((delta < -step) ? -step : delta);
		m_bodyYaw  = wrapAngle(m_bodyYaw);
		return;
	}

	// --- Standing ------------------------------------------------------------
	// Deliberately the same in BOTH modes. A standing character that never
	// realigns ends up looking over its own shoulder indefinitely, and the
	// in-place turn clips exist precisely to cover the correction.
	//
	// Irrlicht's yaw increases from +Z toward +X, which is clockwise seen from
	// above — so a POSITIVE delta is a turn to the character's RIGHT. That is
	// the sign convention m_turnDir carries and updateAnimation reads.
	const float delta = angleDelta(m_bodyYaw, m_orbitYaw);

	if (m_turnDir == 0)
	{
		if (fabsf(delta) < k_standTurnTrigger)
			return;   // inside the deadzone: the body simply does not care

		m_turnDir = (delta > 0.0f) ? 1 : -1;
	}
	else if (fabsf(delta) <= k_standTurnRelease)
	{
		m_turnDir = 0;
		return;
	}
	else
	{
		// The player can swing the mouse back across the body mid-turn. Follow
		// where the camera is NOW rather than finishing the turn that was
		// started, or the character carries on rotating away from it.
		m_turnDir = (delta > 0.0f) ? 1 : -1;
	}

	const float step = k_turnInPlaceRate * seconds;

	m_bodyYaw += (delta > step) ? step : ((delta < -step) ? -step : delta);
	m_bodyYaw  = wrapAngle(m_bodyYaw);
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

void PlayerControllerTPS::updateAnimation(anax::Entity& player, float planarSpeed)
{
	if (!player.hasComponent<MeshComponent>())
		return;

	auto& mesh = player.getComponent<MeshComponent>();
	if (!mesh.node)
		return;

	// ADDED HERE, NOT IN init(). init() runs before the scene has spawned the
	// player entity, and the .ent deliberately does not carry the component —
	// nothing about a saved scene should decide whether a character is on the
	// blended path. AnimationSystem picks it up on the next world refresh, which
	// updateEntityQueues() does every fixed step.
	if (!player.hasComponent<AnimationComponent>())
	{
		player.addComponent<AnimationComponent>();

		// LOAD-BEARING. anax re-runs the system filters ONLY for entities sitting
		// in its activated queue, and addComponent on an already-live entity does
		// not put one there (World::refresh, World.cpp:128). Without this the
		// component exists and the controller happily drives it, but
		// AnimationSystem never sees the entity, so nothing ever advances the
		// cursor or calls setExternalPose — and the character stands frozen in
		// its bind pose. activate() is just a re-filter request; re-activating an
		// already-active entity is a no-op for every system it is already in.
		player.activate();
	}

	AnimGraph& anim = player.getComponent<AnimationComponent>().graph;

	// --- Phase ---------------------------------------------------------------
	// NOCLIP COUNTS AS GROUNDED. It is not, as far as the CCT is concerned —
	// setNoclip forces m_grounded false — and without this the character would
	// hold a falling pose for the whole time noclip was on.
	const bool grounded = m_grounded || m_noclip;

	if (!grounded && m_wasGrounded)
	{
		// Takeoff. WHICH jump clip is decided ONCE, here, from the speed at the
		// moment the feet leave the floor. Deciding it per frame would swap
		// clips in mid-air the moment a wall or air drag dropped the speed
		// under the threshold.
		m_jumpIsRunning = planarSpeed > k_runJumpSpeed;
		m_animPhase     = AnimPhase::Air;
	}
	else if (grounded && !m_wasGrounded)
	{
		// Touchdown. A landing at speed skips the recovery entirely — half a
		// second of absorbing the impact while the player is still holding
		// forward reads as the controls having been taken away.
		m_animPhase = (planarSpeed > k_moveAnimThreshold) ? AnimPhase::Ground
		                                                  : AnimPhase::Land;
	}

	m_wasGrounded = grounded;

	const JumpSegments& jump     = m_jumpIsRunning ? k_jumpRunning : k_jumpStanding;
	const char* const   jumpClip = m_jumpIsRunning ? k_clipJumpRun : k_clipJumpStand;

	// --- Layers, decided BEFORE the phase early-returns ----------------------
	// A layer is not a state and does not take part in the state machine below,
	// so it has to be driven on every path through this function. Decided here,
	// ahead of the Air and Land returns, a turn layer cannot survive a jump
	// taken mid-turn — which is exactly what would happen if the turn were
	// still handled down in the Ground branch.
	//
	// A MASKED OVERRIDE, not a whole-body state. The turn clips describe standing
	// footwork and a spine counter-twist under a body that updateFacing is
	// rotating (measured: the hips' yaw is flat across all 36 frames and only the
	// spine twists about 20 degrees and returns). They have nothing to say about
	// the arms, and replacing those was fine for a character holding nothing and
	// wrong the moment one holds a weapon. Masked to 'no_arms' they drive the
	// hips, legs and spine while the blend underneath keeps the arms.
	const bool turning = (m_animPhase == AnimPhase::Ground) &&
	                     (m_turnDir != 0) &&
	                     (planarSpeed <= k_moveAnimThreshold);

	if (turning)
	{
		anim.playLayer(mesh, k_layerTurn,
		               (m_turnDir > 0) ? k_clipTurnRight : k_clipTurnLeft,
		               AnimMaskName::k_noArms, ALM_OVERRIDE,
		               true, 1.0f, k_fadeTurnIn);
	}
	else
	{
		anim.stopLayer(k_layerTurn, k_fadeTurnOut);
	}

	updateTestLayer(mesh, anim);
	updateWeapon(mesh, anim);

	// --- The swing, also a layer ---------------------------------------------
	// Driven here with the others and for the same reason: it is not a state,
	// so nothing below this point in the function would reach it on the Air and
	// Land early-returns, and an attack begun on the ground would be stranded at
	// full weight for the whole of a jump taken during it.
	//
	// updateAttack decided all of this already. The only thing this does that it
	// could not is speak to the AnimGraph.
	if (m_attackActive)
	{
		// The STAGE's sub-range, not the whole clip: the light combo is three
		// cuts inside one 108-frame animation, and a click during a
		// follow-through has to be able to cut straight to the next one.
		anim.playLayer(mesh, k_layerAttack, m_attackClip.c_str(),
		               m_attackMask ? m_attackMask : AnimMaskName::k_upperBody,
		               ALM_OVERRIDE, false, m_attackRate, k_fadeAttackIn,
		               -1, m_attackBegin, m_attackEnd);

		// A SECOND SWING NEEDS TO SAY SO. Re-issuing a clip a layer is already
		// running deliberately leaves its cursor alone - that is what lets the
		// grip pose be re-requested every frame without restarting - so a
		// buffered follow-up would otherwise play the tail of the first swing
		// and stop.
		if (m_attackRestart)
		{
			anim.restartLayer(k_layerAttack);
			m_attackRestart = false;
		}
	}
	else
	{
		anim.stopLayer(k_layerAttack, k_fadeAttackOut);
	}

	// --- Foot IK weight ------------------------------------------------------
	// Grounded only. There is no floor to stand on in the air, and a landing is
	// the one moment the feet are SUPPOSED to be moving independently of it —
	// pinning them to the ground through a landing removes the impact entirely.
	//
	// A hard 0/1 here is fine: AnimFootIK ramps it internally, which is also why
	// the transition out of a jump does not need a number of its own.
	const bool wantIK = (m_animPhase == AnimPhase::Ground) &&
	                    (WorldManager::Get() &&
	                     WorldManager::Get()->getCVarValue(k_animIKCvar) != "0");

	anim.setFootIKWeight(wantIK ? 1.0f : 0.0f);

	// The step smoothing, applied to the pelvis instead of the body. See
	// k_verticalSmoothRate.
	anim.setPelvisOffset(m_stepLag);

	if (m_animPhase == AnimPhase::Air)
	{
		// Non-looping, so the player clamps at riseHold and holds the falling
		// pose for as long as the fall lasts — which is also what covers a plain
		// walk off a ledge, where there was no jump at all.
		anim.play(mesh, jumpClip, jump.riseBegin, jump.riseHold, false, 1.0f,
		          k_fadeTakeoff);
		return;
	}

	if (m_animPhase == AnimPhase::Land)
	{
		// Read the CLIP, not a timer. A wall-clock timer drifts out of step with
		// the pose the moment the world time scale moves (bullet time, hit
		// stop); the clip cursor is scaled by the same clock the pose is.
		//
		// ASK THE PLAYER WHETHER THE SEGMENT FINISHED rather than doing the
		// frame arithmetic here. The landing is non-looping, so its cursor
		// clamps at the end of the range play() was given and latches
		// 'finished' there — which IS the question, exactly, with no absolute
		// frame numbers left to get wrong.
		//
		// It used to compare anim.frame() against activeClipBase() + landEnd,
		// and that was a latent trap which a held weapon pose sprang: the base
		// came from the graph's name->range lookup CACHE, and ANY layer that
		// re-resolves its own clip overwrites that. Once the grip pose started
		// defaulting to a real clip it was re-resolved every single frame, so
		// the comparison was against the grip clip's base while the cursor was
		// the jump clip's. It could never come true, and the character held the
		// landing pose — which is what the player spawn settling onto the floor
		// puts it in — until they happened to move. See AnimGraph::activeClipBase.
		//
		// ORDER MATTERS HERE. finished() describes the DOMINANT slot of the
		// CURRENT state, and on the frame this phase is entered that is still
		// the airborne rise, which has been clamped at its hold frame for the
		// whole fall and so reports finished immediately. The landing has to be
		// PLAYING before its own cursor is worth asking about.
		if (planarSpeed > k_moveAnimThreshold)
		{
			// The player moved: cut the recovery short and fall through to the
			// locomotion blend, without starting a landing we would abandon on
			// the same frame.
			m_animPhase = AnimPhase::Ground;
		}
		else
		{
			// play() returns false only when the mesh has no such clip, which is
			// the one case where there is no landing to wait for at all.
			const bool playing = anim.play(mesh, jumpClip, jump.landBegin, jump.landEnd,
			                               false, 1.0f, k_fadeToLand);

			if (playing && !anim.finished())
				return;

			m_animPhase = AnimPhase::Ground;
		}
	}

	// --- The locomotion blend space ------------------------------------------
	// Idle, walk and run weighted continuously on speed; forward, both strafes
	// and backward weighted continuously on the direction of travel. Rebuilt from
	// scratch every fixed step and handed over whole — the shares are what move,
	// so no clip ever enters or leaves at a non-zero weight and there is nothing
	// to cross-fade WITHIN the set.
	//
	// Note this runs for a standing character too, at idle share 1.0. Standing is
	// not a separate state: making it one would put a cross-fade back at exactly
	// the idle/walk boundary the speed axis exists to make continuous.

	// Direction of travel relative to where the body is HEADING: 0 = straight
	// ahead, +90 = the character's right, 180 = backwards. Taken from the
	// VELOCITY and not from the input, so momentum carried around a corner
	// animates as the slide it actually is.
	//
	// Measured against m_animFacing rather than m_bodyYaw — see that member.
	// In free-facing mode the reference IS the travel direction, so 'rel'
	// collapses to zero and the blend resolves to the forward clips on its own.
	// There is deliberately no second code path for it.
	const float rel = wrapAngle(m_travelYaw - m_animFacing);
	m_dbgRel = rel;

	// --- 1D speed axis: idle -> walk -> run ----------------------------------
	// Three stops, at the speeds the CHARACTER moves at rather than at the speeds
	// the clips happen to describe. k_walkSpeed and k_runSpeed are exactly the
	// two tiers the controller can produce, so holding the walk key sits on the
	// walk stop and holding sprint sits on the run stop, with everything in
	// between — acceleration, being shoved, a slope — landing in the blend.
	float wIdle, wWalk, wRun;

	if (planarSpeed <= k_moveAnimThreshold)
	{
		wIdle = 1.0f; wWalk = 0.0f; wRun = 0.0f;
	}
	else if (planarSpeed < k_walkSpeed)
	{
		const float t = (planarSpeed - k_moveAnimThreshold) /
		                (k_walkSpeed - k_moveAnimThreshold);
		wIdle = 1.0f - t; wWalk = t; wRun = 0.0f;
	}
	else
	{
		const float t = clampf((planarSpeed - k_walkSpeed) / (k_runSpeed - k_walkSpeed),
		                       0.0f, 1.0f);
		wIdle = 0.0f; wWalk = 1.0f - t; wRun = t;
	}

	// --- 2D directional blend ------------------------------------------------
	// Four cardinals — forward, right strafe, backward, left strafe — and the
	// move angle interpolated between the two ADJACENT ones. That is the same
	// surface a bilinear blend on (forwardness, rightness) describes, computed
	// the short way: opposite cardinals are never both active, which is what lets
	// backward safely reuse the forward clip reversed.
	// FOLD INTO [0, 360) WITH A FLOOR, NOT WITH 'if (a < 0) a += 360'.
	//
	// This cost a bug. For any rel in roughly (-1e-5, 0), 'rel + 360.0f' ROUNDS
	// TO EXACTLY 360.0f in single precision — there is no float between
	// 359.99999 and 360 at that magnitude. The old code then took
	// d0 = int(360/90) & 3 = 0, which the mask made look safe, while
	// td = (360 - 0)/90 = 4.0 escaped entirely: wDir[fwd] = -3 and
	// wDir[right] = +4. The negative forward share was dropped by playBlend's
	// share > 0 test and the right strafe ran at four times full weight,
	// dominating the whole normalised blend.
	//
	// That is a ONE-FRAME full right strafe, firing only when rel lands in that
	// tiny negative window — i.e. intermittently, while walking straight
	// forward. With about 84 degrees of body yaw baked into the strafe clips it
	// reads as the character snapping sideways. Driving rel toward zero, which is
	// what the m_animFacing fix does, makes it MORE likely, not less.
	//
	// Deriving both the cell and the fraction from the same floor makes the two
	// impossible to disagree, whatever the rounding does.
	float a = rel + 360.0f;
	a -= floorf(a / 360.0f) * 360.0f;
	if (!(a >= 0.0f && a < 360.0f))
		a = 0.0f;                       // NaN, or anything else that got past the fold

	const float cell = a / 90.0f;                          // [0, 4)
	const int   d0   = static_cast<int>(cell) & 3;         // 0 fwd, 1 right, 2 back, 3 left
	const int   d1   = (d0 + 1) & 3;
	const float td   = cell - floorf(cell);                // [0, 1), by construction

	float wDir[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	wDir[d0] = 1.0f - td;
	wDir[d1] = td;

	m_dbgSpeedW[0] = wIdle; m_dbgSpeedW[1] = wWalk; m_dbgSpeedW[2] = wRun;
	for (int i = 0; i < 4; ++i) m_dbgDirW[i] = wDir[i];

	// Per-direction clip table. BACKWARDS IS THE FORWARD CLIP REVERSED: there is
	// no backpedal clip in the set, and reversing is not merely the cheap option
	// — it is the right one for the feet, because a body travelling backwards
	// needs its planted foot to travel FORWARD relative to the hips, which is
	// exactly what reversed playback produces. The arm swing is wrong, and that
	// is the price.
	struct DirClips
	{
		const char* walk;
		const char* run;
		float       walkPhase;
		float       runPhase;
		float       walkSpeed;
		float       runSpeed;
		bool        reverse;
	};

	static const DirClips k_dir[4] =
	{
		{ k_clipWalk,        k_clipRun,           k_phaseWalk,        k_phaseRun,
		  k_walkClipSpeed,   k_runClipSpeed,      false },                       // forward
		{ k_clipStrafeWalkR, k_clipStrafeRunR,    k_phaseStrafeWalkR, k_phaseStrafeRunR,
		  k_strafeWalkClipSpeed, k_strafeRunClipSpeed, false },                  // right
		{ k_clipWalk,        k_clipRun,           k_phaseWalk,        k_phaseRun,
		  k_walkClipSpeed,   k_runClipSpeed,      true  },                       // backward
		{ k_clipStrafeWalkL, k_clipStrafeRunL,    k_phaseStrafeWalkL, k_phaseStrafeRunL,
		  k_strafeWalkClipSpeed, k_strafeRunClipSpeed, false },                  // left
	};

	// At most five: idle, plus two adjacent directions at two speed tiers.
	AnimBlendEntry entries[5];
	int count = 0;

	if (wIdle > 0.0f)
	{
		// Idle is in the set but NOT in the sync group: it is a 17.7-second
		// standing loop with no gait to align, and a negative phase offset is how
		// AnimGraph is told to free-run a cursor for it.
		entries[count].clip         = k_clipIdle;
		entries[count].share        = wIdle;
		entries[count].phaseOffset  = -1.0f;
		entries[count].naturalSpeed = 0.0f;
		entries[count].reverse      = false;
		++count;
	}

	for (int i = 0; i < 2; ++i)
	{
		const int   d = (i == 0) ? d0 : d1;
		const float w = wDir[d];
		if (w <= 0.0f)
			continue;

		const DirClips& dc = k_dir[d];

		if (wWalk > 0.0f && count < 5)
		{
			entries[count].clip         = dc.walk;
			entries[count].share        = wWalk * w;
			entries[count].phaseOffset  = dc.walkPhase;
			entries[count].naturalSpeed = dc.walkSpeed;
			entries[count].reverse      = dc.reverse;
			++count;
		}

		if (wRun > 0.0f && count < 5)
		{
			entries[count].clip         = dc.run;
			entries[count].share        = wRun * w;
			entries[count].phaseOffset  = dc.runPhase;
			entries[count].naturalSpeed = dc.runSpeed;
			entries[count].reverse      = dc.reverse;
			++count;
		}
	}

	// planarSpeed goes in raw: AnimGraph derives the shared phase rate from it
	// and the blended stride length, so a walk at 2.2 and a run at 6.0 both come
	// out near playback rate 1.0 on their own clips and everything between them
	// interpolates instead of snapping.
	// The fade only applies on ENTRY to the blend state - from a turn, or from a
	// landing. Within the set nothing fades: the shares are the transition, so
	// slowing to a stop settles into idle at whatever rate the character actually
	// decelerates, which is what k_fadeToIdle used to approximate with a number.
	anim.playBlend(mesh, entries, count, planarSpeed,
	               (wIdle > 0.5f) ? k_fadeToIdle : k_fadeLocomotion);
}

// The anim_layer_* harness. Nothing in the game drives this; it exists so masks
// and the additive path can be exercised against a clip set that has no hit
// reactions and no authored additive clips yet.
void PlayerControllerTPS::updateTestLayer(MeshComponent& mesh, AnimGraph& anim)
{
	WorldManager* world = WorldManager::Get();
	if (!world)
		return;

	const std::string clip = world->getCVarValue(k_animLayerClipCvar);

	if (clip.empty())
	{
		anim.stopLayer(k_layerTest, 0.25f);
		return;
	}

	const std::string mask = world->getCVarValue(k_animLayerMaskCvar);
	const bool additive = world->getCVarValue(k_animLayerAddCvar) != "0";

	// A weight of 0 is a legitimate thing to ask for — it is how you fade the
	// layer to nothing without retyping the clip name — so it is applied as a
	// fade target rather than by dropping the layer.
	float weight = 1.0f;
	{
		const std::string w = world->getCVarValue(k_animLayerWeightCvar);
		if (!w.empty())
			weight = static_cast<float>(atof(w.c_str()));
	}

	if (weight <= 0.0f)
	{
		anim.stopLayer(k_layerTest, 0.25f);
		return;
	}

	anim.playLayer(mesh, k_layerTest, clip.c_str(),
	               mask.empty() ? AnimMaskName::k_full : mask.c_str(),
	               additive ? ALM_ADDITIVE : ALM_OVERRIDE,
	               true, 1.0f, 0.25f);
}

// The carried weapon, and the grip pose that closes the hand round it.
//
// THE POINT OF THIS FUNCTION is that the paladin's whole locomotion set was
// captured empty-handed. Rather than source a second set of idles, walks, runs,
// strafes, turns and jumps that happen to be holding a sword, the hand is
// closed by a masked layer over whichever of those clips is already playing,
// and the sword is parented to the bone underneath it. One pose covers every
// state the character will ever be in, including states added later.
void PlayerControllerTPS::updateWeapon(MeshComponent& mesh, AnimGraph& anim)
{
	WorldManager* world = WorldManager::Get();
	if (!world || !mesh.node)
		return;

	// A scene load builds a new character node and destroys the old one with
	// every bone hanging off it, the sword included. Nothing about the pointer
	// we are holding says so, so the OWNER is what gets compared.
	if (m_swordOwner != mesh.node)
	{
		m_swordNode  = nullptr;
		m_swordOwner = nullptr;
	}

	if (world->getCVarValue(k_swordCvar) == "0")
	{
		if (m_swordNode)
			m_swordNode->setVisible(false);

		anim.stopLayer(k_layerGrip, 0.2f);
		return;
	}

	if (!m_swordNode)
	{
		// EJUOR_READ, AND IT IS LOAD-BEARING. A character on the external-pose
		// path (engine fork #6) has its pose written straight into the shared
		// skinned mesh, and CAnimatedMeshSceneNode copies that back out onto the
		// bone scene nodes in READ mode and in no other. Leave the mode alone and
		// every joint node stays in the bind pose for the lifetime of the scene:
		// the character animates correctly, because the skinning never went
		// through those nodes, while anything parented to one hangs in the air
		// where the T-pose hand used to be.
		mesh.node->setJointMode(irr::scene::EJUOR_READ);

		irr::scene::IBoneSceneNode* hand = mesh.node->getJointNode(k_handJoint);
		if (!hand)
		{
			static bool warned = false;
			if (!warned)
			{
				warned = true;
				spdlog::warn("PlayerControllerTPS: no '{}' joint on the character - "
				             "nothing to hang a weapon on", k_handJoint);
			}
			return;
		}

		irr::scene::IAnimatedMesh* swordMesh = RenderManager::Get()->loadMesh(k_swordMesh);
		if (!swordMesh)
		{
			static bool warned = false;
			if (!warned)
			{
				warned = true;
				spdlog::warn("PlayerControllerTPS: could not load '{}'", k_swordMesh);
			}
			return;
		}

		m_swordNode = RenderManager::Get()->sceneManager()->addAnimatedMeshSceneNode(swordMesh, hand);
		if (!m_swordNode)
			return;

		m_swordOwner = mesh.node;

		// MATERIALS ARE SET UP BY HAND HERE because RenderSystem only ever sees
		// meshes that belong to an entity, and this one deliberately does not.
		// The SpecularColor alpha is the part that cannot be skipped: it is
		// where phong_perpixel reads metallic from, it defaults to 255, and a
		// fully metallic surface takes 1-metallic of its albedo - so a raw node
		// left alone renders BLACK rather than merely mis-lit.
		m_swordNode->setMaterialFlag(irr::video::EMF_NORMALIZE_NORMALS, true);
		m_swordNode->setMaterialFlag(irr::video::EMF_BILINEAR_FILTER, false);
		m_swordNode->setMaterialFlag(irr::video::EMF_TRILINEAR_FILTER, true);
		m_swordNode->setMaterialFlag(irr::video::EMF_ANISOTROPIC_FILTER, true);

		for (irr::u32 i = 0; i < m_swordNode->getMaterialCount(); ++i)
		{
			m_swordNode->getMaterial(i).Shininess = 0.0f;
			m_swordNode->getMaterial(i).SpecularColor.setAlpha(0);
			m_swordNode->getMaterial(i).DiffuseColor.setAlpha(255);
		}

		const irr::video::E_MATERIAL_TYPE shader = ShaderMaterialManager::get("phong_perpixel");
		if (shader != irr::video::EMT_SOLID)
			m_swordNode->setMaterialType(shader);

		spdlog::info("PlayerControllerTPS: sword attached to '{}'", k_handJoint);
	}

	m_swordNode->setVisible(true);

	// Re-applied EVERY FRAME, which is what makes tps_sword_pos/_rot tunable
	// from the console against the running game instead of against a rebuild.
	// It also sidesteps having to know whether the bone's absolute transform was
	// valid yet on the frame the node was created.
	irr::scene::ISceneNode* hand = m_swordNode->getParent();
	if (!hand)
		return;

	// THE SKELETON IS NOT UNIT SCALE. paladin.glb was converted at 0.18412 and
	// that factor sits on the skeleton root, so every bone's absolute transform
	// carries it and so would anything parented to one - a sword at 18% size,
	// with its offsets shrunk to match. Dividing it back out on the child leaves
	// the world orientation untouched, because a uniform scale commutes with the
	// rotation either side of it.
	const float boneScale = hand->getAbsoluteTransformation().getScale().X;
	m_swordBoneScale = (boneScale > 1e-6f) ? boneScale : 1.0f;

	m_swordNode->setScale(vector3df(1.0f / m_swordBoneScale));
	m_swordNode->setPosition(parseVec3(world->getCVarValue(k_swordPosCvar)) / m_swordBoneScale);
	m_swordNode->setRotation(parseVec3(world->getCVarValue(k_swordRotCvar)));

	// --- The grip pose -------------------------------------------------------
	// rate 0 and loop false: the layer holds the clip's FIRST FRAME and never
	// advances. A grip is a pose, not an animation, and freezing one frame of a
	// clip that happens to contain it costs nothing beyond the clip already
	// being in the mesh.
	const std::string gripClip = world->getCVarValue(k_gripClipCvar);
	if (gripClip.empty())
	{
		anim.stopLayer(k_layerGrip, 0.2f);
		return;
	}

	const std::string gripMask = world->getCVarValue(k_gripMaskCvar);

	anim.playLayer(mesh, k_layerGrip, gripClip.c_str(),
	               gripMask.empty() ? AnimMaskName::k_fingers : gripMask.c_str(),
	               ALM_OVERRIDE, false, 0.0f, 0.25f);
}

// ---------------------------------------------------------------------------
// The swing
// ---------------------------------------------------------------------------

// Reads the attack button and owns the swing clock.
//
// TAP FOR LIGHT, HOLD FOR HEAVY. That is one button with two meanings, and the
// meaning is not known at the moment of the press — so the press is NOT consumed
// when it arrives, the way the jump above consumes its own. It is held until the
// button is either released (a tap: light) or has been down past
// tps_attack_hold (a hold: heavy fires there and then, without waiting for the
// release, because waiting would make the heavy feel like it was ignored).
//
// The cost is that a light attack cannot fire until the hold threshold has
// passed, which is why that number is small. There is no way around it on one
// button: firing the light on the press and converting to a heavy afterwards
// would mean every heavy was preceded by a light the player did not ask for.
//
// THE SWING IS A LAYER, NOT A STATE, and that is the rest of the design. A state
// would replace the locomotion blend outright, which is right for a character
// standing still and wrong for one at a run: the legs would stop mid-stride to
// play a clip that has no stride in it. As a masked override it composes - the
// attack drives whatever the mask covers and the blend underneath keeps
// everything else - and it does not touch the locomotion state machine at all,
// so a jump, a landing or an in-place turn taken during a swing all still work.
void PlayerControllerTPS::updateAttack(anax::Entity& player, float seconds, float planarSpeed)
{
	WorldManager* world = WorldManager::Get();
	if (!world || !player.hasComponent<MeshComponent>())
		return;

	MeshComponent& mesh = player.getComponent<MeshComponent>();

	// Reading the SAME cvar updateWeapon reads, rather than a second one, is
	// what stops the two disagreeing about whether the character is armed - a
	// swing with the sword put away would be a punch with the wrong animation.
	const bool armed = world->getCVarValue(k_swordCvar) != "0";

	const bool down = armed && !inputSuppressed() && !m_noclip &&
	                  InputManager::Get()->isMouseButtonPressed(MOUSE_BUTTON::MB_LEFT);

	float hold = static_cast<float>(atof(world->getCVarValue(k_attackHoldCvar).c_str()));
	if (!(hold > 0.0f))
		hold = k_attackHoldDefault;

	// --- Advance the swing already running -----------------------------------
	// BEFORE the button is read, so a contact and the click that chains off it
	// cannot land in the wrong order within one step.
	if (m_attackActive)
	{
		m_attackTime += seconds;

		// THE STRIKE FIRES ON THE CONTACT FRAME, NOT ON THE BUTTON. A melee hit
		// resolved on the click lands before the blade has left the shoulder,
		// and reads as the damage being disconnected from the animation - which
		// it is.
		if (!m_attackStruck && m_attackTime >= m_attackContact)
		{
			m_attackStruck = true;
			performStrike(player);
		}
	}

	// --- The button ----------------------------------------------------------
	const bool pressEdge   =  down && !m_attackDown;
	const bool releaseEdge = !down &&  m_attackDown;
	m_attackDown = down;

	if (pressEdge)
	{
		m_attackHeld  = 0.0f;
		m_attackSpent = false;
	}

	bool wantAttack = false;
	AttackKind wantKind = AttackKind::Light;

	if (down && !m_attackSpent)
	{
		m_attackHeld += seconds;

		if (m_attackHeld >= hold)
		{
			// Held long enough: the heavy comes out now. Spent, so keeping the
			// button down cannot produce a second one - a heavy needs a fresh
			// press, which is also what stops a hold from auto-repeating.
			wantAttack    = true;
			wantKind      = AttackKind::Heavy;
			m_attackSpent = true;
		}
	}
	else if (releaseEdge && !m_attackSpent)
	{
		// Let go before the threshold: it was a tap.
		wantAttack = true;
		wantKind   = AttackKind::Light;
	}

	if (releaseEdge)
	{
		m_attackHeld  = 0.0f;
		m_attackSpent = false;
	}

	// --- Route it ------------------------------------------------------------
	if (m_attackActive)
	{
		if (wantAttack)
		{
			// A request during a swing is BUFFERED rather than dropped, but only
			// from the back of the swing: earlier than that it is the player
			// mashing, and spending it would let the wind-up be skipped.
			//
			// It is held even before the window opens, though, because a heavy
			// charged through the end of a light is the most obvious thing a
			// player will try and the least forgivable to drop.
			const bool windowOpen = m_attackTime >= m_attackDuration * k_attackBufferOpen;

			if (windowOpen || wantKind == AttackKind::Heavy)
			{
				m_attackQueued     = true;
				m_attackQueuedKind = wantKind;

				// A light chains off a light, and only off a light. Anything
				// else starts the combo again at its first cut.
				m_attackQueuedChain = (wantKind == AttackKind::Light) &&
				                      (m_attackKind == AttackKind::Light);
			}
		}

		if (m_attackTime < m_attackDuration)
			return;

		m_attackActive = false;

		// Finished. A buffered request starts on this same frame rather than
		// letting the layer fade out and back in, which would put a visible
		// hitch between two swings that should read as one combination.
		if (m_attackQueued)
		{
			m_attackQueued = false;
			beginAttack(mesh, m_attackQueuedKind, m_attackQueuedChain, planarSpeed);
		}

		return;
	}

	if (wantAttack && armed)
		beginAttack(mesh, wantKind, false, planarSpeed);
}

// Start a swing, or the next stage of one.
bool PlayerControllerTPS::beginAttack(MeshComponent& mesh, AttackKind kind,
                                      bool chain, float planarSpeed)
{
	WorldManager* world = WorldManager::Get();
	if (!world)
		return false;

	const bool light = (kind == AttackKind::Light);

	std::string clipName = world->getCVarValue(light ? k_lightClipCvar : k_heavyClipCvar);
	if (clipName.empty())
		clipName = light ? k_clipLightDefault : k_clipHeavyDefault;

	const sAnimationData* clip = mesh.findAnimation(clipName);
	if (!clip)
	{
		// Warn once PER NAME, not once ever: the name is a cvar, so a typo
		// corrected at the console must be able to report success, and the next
		// bad name must still be able to report itself.
		static std::string warned;
		if (warned != clipName)
		{
			warned = clipName;
			spdlog::warn("PlayerControllerTPS: no '{}' clip on the character - "
			             "nothing to swing", clipName);
		}
		return false;
	}

	const AttackStage* stages = light ? k_lightStages : k_heavyStages;
	const int stageCount = light ? static_cast<int>(sizeof(k_lightStages) / sizeof(k_lightStages[0]))
	                             : static_cast<int>(sizeof(k_heavyStages) / sizeof(k_heavyStages[0]));

	// Chaining walks the combo forward and STOPS at the last cut rather than
	// wrapping: a fourth click during the third hit restarts the combo, which is
	// what the player means, instead of silently replaying the finisher.
	int stage = 0;
	if (chain && light && m_attackKind == AttackKind::Light)
		stage = (m_attackStage + 1 < stageCount) ? m_attackStage + 1 : 0;

	const AttackStage& st = stages[stage];

	const float fps = (mesh.fps > 0) ? static_cast<float>(mesh.fps) : 30.0f;

	// CLAMPED AGAINST THE CLIP ACTUALLY LOADED. The table above is measured
	// against one export; a shorter re-export would otherwise put a stage past
	// the end of the clip, where it would never fire at all.
	const int clipFrames = clip->frames.Y - clip->frames.X;

	int begin   = (st.begin   < clipFrames) ? st.begin   : 0;
	int end     = (st.end     <= clipFrames && st.end > begin) ? st.end : clipFrames;
	int contact = (st.contact > begin && st.contact < end) ? st.contact
	                                                       : (begin + (end - begin) / 2);

	const float frames = static_cast<float>(end - begin);
	if (frames <= 0.0f)
		return false;

	// LATCHED, not re-read per frame. The rate sets both the duration and the
	// contact time, and moving it mid-swing would move a contact the swing may
	// already have gone past - which would either fire it twice or never.
	m_attackRate = static_cast<float>(
		atof(world->getCVarValue(light ? k_lightRateCvar : k_heavyRateCvar).c_str()));
	if (m_attackRate <= 0.01f)
		m_attackRate = light ? k_lightRateDefault : k_heavyRateDefault;

	m_attackClip     = clipName;
	m_attackKind     = kind;
	m_attackStage    = stage;
	m_attackBegin    = begin;
	m_attackEnd      = end;
	m_attackDuration = frames / (fps * m_attackRate);
	m_attackContact  = static_cast<float>(contact - begin) / (fps * m_attackRate);
	m_attackTime     = 0.0f;
	m_attackStruck   = false;
	m_attackActive   = true;
	m_attackRestart  = true;

	// FULL BODY WHEN STANDING, UPPER BODY WHEN MOVING, and held either way.
	//
	// A clip with no root motion may take the whole body when the character is
	// planted: that is what gives a standing heavy its weight shift. At a run
	// the same pelvis drop would fight the locomotion blend for the legs and
	// win, so the mask is pulled back to the spine and up and the legs keep
	// running underneath. Airborne counts as moving: there is no stance to
	// commit to in the air.
	//
	// A clip that DOES carry root motion is never allowed the whole body - see
	// k_lightAllowFullBody, which is what keeps the light combo's four units of
	// forward travel off the character.
	const bool mayCommit = light ? k_lightAllowFullBody : k_heavyAllowFullBody;
	const bool planted   = (planarSpeed <= k_moveAnimThreshold) && m_grounded;

	m_attackMask = (planted && mayCommit) ? AnimMaskName::k_full
	                                      : AnimMaskName::k_upperBody;
	return true;
}

// The strike itself.
//
// AN ARC IN FRONT OF THE BODY, NOT A RAY DOWN THE CAMERA. The first-person
// melee weapon casts two units straight out of the eye, which is right when the
// camera IS the character's head. Here it is on a boom behind and above the
// shoulder, and the body is not even facing the same way - it is allowed to lag
// the camera by up to k_standTurnTrigger while standing. A camera ray would let
// the player kill something they are standing beside by looking at it, and miss
// the thing the sword visibly passes through.
//
// It also sidesteps the node-id lookup such a ray would need: matching a scene
// node back to an entity through getID() is unreliable, because ids default to
// 0 and 0 is a valid entity id.
void PlayerControllerTPS::performStrike(anax::Entity& player)
{
	WorldManager* world = WorldManager::Get();
	if (!world || !world->gameplaySystem() || !player.hasComponent<TransformComponent>())
		return;

	const vector3df origin = player.getComponent<TransformComponent>().getPosition() +
	                         vector3df(0.0f, k_attackOriginY, 0.0f);

	// THE BODY'S FACING, NOT THE CAMERA'S, and without k_modelYawOffset - that
	// offset is an asset correction applied to the mesh, and folding it in here
	// would swing the hitbox away from the sword by however wrong this
	// particular model's authored facing happens to be.
	const float yawRad = toRadians(m_bodyYaw);
	const vector3df facing(sinf(yawRad), 0.0f, cosf(yawRad));

	float reach = static_cast<float>(atof(world->getCVarValue(k_attackReachCvar).c_str()));
	if (reach <= 0.01f)
		reach = k_attackReach;

	const bool light = (m_attackKind == AttackKind::Light);

	int damage = atoi(world->getCVarValue(light ? k_lightDamageCvar
	                                            : k_heavyDamageCvar).c_str());
	if (damage <= 0)
		damage = light ? k_lightDamage : k_heavyDamage;

	const bool draw = world->getCVarValue(k_attackDebugCvar) != "0";

	// cos of the HALF angle, so the arc test is one dot product per candidate.
	const float cosHalfArc = cosf(toRadians(k_attackArcDeg * 0.5f));

	if (draw)
	{
		// The arc as three lines - the facing and both edges. Enough to see
		// where the swing thought it was pointing without drawing a fan.
		const float half = toRadians(k_attackArcDeg * 0.5f);
		const float offsets[3] = { -half, 0.0f, half };

		for (int i = 0; i < 3; ++i)
		{
			const vector3df dir(sinf(yawRad + offsets[i]), 0.0f, cosf(yawRad + offsets[i]));
			RenderManager::Get()->renderLine3DOverlayOnTop(
			    Line3D(irr::core::line3df(origin, origin + dir * reach),
			           irr::video::SColor(255, 255, 220, 60)));
		}
	}

	HIT_RESULT best = HIT_RESULT::NONE;

	for (auto entity : world->world()->getEntities())
	{
		if (!entity.hasComponent<DescriptorComponent>() ||
		    !entity.hasComponent<DamageReceiverComponent>() ||
		    !entity.hasComponent<TransformComponent>())
			continue;

		DescriptorComponent& desc = entity.getComponent<DescriptorComponent>();

		// NOT THE PLAYER. Filtering on the TYPE rather than on the entity id
		// also covers the invisible hitbox the CCT carries, which is a separate
		// node with the same owner.
		if (desc.type == ET_PLAYER || !desc.isAlive)
			continue;

		DamageReceiverComponent& drc = entity.getComponent<DamageReceiverComponent>();
		if (drc.health <= 0 || drc.deathResolved)
			continue;

		const vector3df target = entity.getComponent<TransformComponent>().getPosition();
		vector3df to = target - origin;

		// The height band is tested FIRST and on its own, so a sword cannot
		// reach something standing on a crate overhead.
		if (fabsf(to.Y) > k_attackHeight)
			continue;

		to.Y = 0.0f;

		const float distance = to.getLength();
		if (distance > reach || distance < 0.0001f)
			continue;

		to /= distance;
		if (to.dotProduct(facing) < cosHalfArc)
			continue;

		// LOS LAST, because it is the only expensive test here and the cheap
		// ones have already thrown out almost everything. AICoordinator's is the
		// ONE implementation of it - it steps the ray origin past the caller's
		// own bounding box, which a raw raycast does not do for anything but
		// ET_PLAYER, and a second copy would be a second thing to get wrong.
		if (!AICoordinator::hasLineOfSight(player, target))
			continue;

		// The wound is put on the RIM of the target facing the swing rather than
		// at its origin, so the blood comes off the surface the blade met
		// instead of out of the middle of the chest.
		const vector3df wound = target - to * 0.25f + vector3df(0.0f, k_attackOriginY, 0.0f);

		const HIT_RESULT result = world->gameplaySystem()->damageEntity(
		    desc.id, static_cast<unsigned int>(damage), DAMAGE_TYPE::DEFAULT,
		    DamageContext::fromImpact(wound, -to, to));

		if (result == HIT_RESULT::KILL)
			best = HIT_RESULT::KILL;
		else if (result == HIT_RESULT::HIT && best == HIT_RESULT::NONE)
			best = HIT_RESULT::HIT;

		if (draw)
		{
			RenderManager::Get()->renderLine3DOverlayOnTop(
			    Line3D(irr::core::line3df(origin, wound),
			           irr::video::SColor(255, 255, 60, 60)));
		}
	}

	// ONE hit-stop for the whole swing, not one per entity caught. A sword that
	// sweeps three targets would otherwise stack three freezes and stop the game
	// dead; the heaviest result is the one worth feeling.
	// A heavy bites harder than a light, and a kill harder than a hit. Four
	// numbers rather than two, because a heavy that felt the same as a light
	// would undo most of what separating them was for.
	if (best != HIT_RESULT::NONE && Engine::Get())
	{
		const float ms = (best == HIT_RESULT::KILL) ? (light ?  70.0f : 110.0f)
		                                            : (light ?  35.0f :  60.0f);
		Engine::Get()->requestHitStop(ms);
	}
}

// The sword tuning panel.
//
// Everything here writes a CVAR rather than a member, because updateWeapon
// re-reads those every frame — that is what makes the panel and the console two
// views of the same number instead of two sources for it, and it is why a drag
// here moves the sword on the same frame with no apply step.
bool PlayerControllerTPS::uiHasMouse() const
{
	WorldManager* world = WorldManager::Get();
	return world && world->getCVarValue(k_swordUICvar) != "0";
}

void PlayerControllerTPS::updateWeaponUI()
{
	WorldManager* world = WorldManager::Get();
	if (!world || world->getCVarValue(k_swordUICvar) == "0")
		return;

	// Formats a vector back into the "x y z" the cvar holds. %.4f throughout:
	// the position is in world units on a 2.3-unit character, so the fourth
	// decimal is about a tenth of a millimetre and nothing useful is lost.
	struct Local
	{
		static void setVec3(WorldManager* w, const char* cvar, const vector3df& v)
		{
			char buf[96];
			snprintf(buf, sizeof(buf), "%.4f %.4f %.4f", v.X, v.Y, v.Z);
			w->setCVar(cvar, buf);
		}
	};

	ImGui::SetNextWindowBgAlpha(0.85f);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("sword", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		bool carried = world->getCVarValue(k_swordCvar) != "0";
		if (ImGui::Checkbox("carry (tps_sword)", &carried))
			world->setCVar(k_swordCvar, carried ? "1" : "0");

		ImGui::SameLine();
		ImGui::TextDisabled(m_swordNode ? "attached" : "not attached");

		// The scale the hand bone already carries, divided back out on the
		// child. Shown because it is the number that explains an 18%-size sword
		// if the division is ever lost — see updateWeapon.
		ImGui::TextDisabled("joint %s   bone scale %.5f", k_handJoint, m_swordBoneScale);

		ImGui::TextDisabled("F3 or 'tps_sword_ui 0' to close  -  movement and camera are held");

		ImGui::Separator();

		// --- Attachment ------------------------------------------------------
		// IN THE HAND JOINT'S OWN AXES, not the world's: +Y runs down the
		// fingers on a Mixamo rig, so nudging Y slides the grip along the palm.
		vector3df pos = parseVec3(world->getCVarValue(k_swordPosCvar));
		vector3df rot = parseVec3(world->getCVarValue(k_swordRotCvar));

		ImGui::TextUnformatted("position  (hand-joint axes, +Y down the fingers)");
		if (ImGui::DragFloat3("##pos", &pos.X, 0.002f, -1.0f, 1.0f, "%.4f"))
			Local::setVec3(world, k_swordPosCvar, pos);

		ImGui::TextUnformatted("rotation  (degrees, same frame)");
		if (ImGui::DragFloat3("##rot", &rot.X, 0.5f, -180.0f, 180.0f, "%.2f"))
			Local::setVec3(world, k_swordRotCvar, rot);

		// A drag is for finding the value; these are for landing on it.
		ImGui::TextDisabled("ctrl+click a field to type it exactly");

		if (ImGui::Button("reset to derived"))
		{
			world->setCVar(k_swordPosCvar, k_swordPosDefault);
			world->setCVar(k_swordRotCvar, k_swordRotDefault);
		}

		ImGui::SameLine();
		if (ImGui::Button("copy cvars"))
		{
			char buf[256];
			snprintf(buf, sizeof(buf),
			         "tps_sword_pos \"%.4f %.4f %.4f\"\ntps_sword_rot \"%.2f %.2f %.2f\"\n",
			         pos.X, pos.Y, pos.Z, rot.X, rot.Y, rot.Z);
			ImGui::SetClipboardText(buf);
		}

		// The same two numbers as the source lines they will eventually replace,
		// so a tuned result can be pasted into the constants and stop being a
		// setting that only exists in someone's config.
		ImGui::SameLine();
		if (ImGui::Button("copy C++"))
		{
			char buf[256];
			snprintf(buf, sizeof(buf),
			         "const char* k_swordPosDefault = \"%.4f %.4f %.4f\";\n"
			         "const char* k_swordRotDefault = \"%.2f %.2f %.2f\";\n",
			         pos.X, pos.Y, pos.Z, rot.X, rot.Y, rot.Z);
			ImGui::SetClipboardText(buf);
		}

		// --- Grip ------------------------------------------------------------
		if (ImGui::CollapsingHeader("grip pose"))
		{
			// The clip whose FIRST FRAME closes the fingers. Free text because
			// it is any clip on the character, not a fixed list.
			char clipBuf[64];
			const std::string clip = world->getCVarValue(k_gripClipCvar);
			snprintf(clipBuf, sizeof(clipBuf), "%s", clip.c_str());

			if (ImGui::InputText("clip", clipBuf, sizeof(clipBuf),
			                     ImGuiInputTextFlags_EnterReturnsTrue))
				world->setCVar(k_gripClipCvar, clipBuf);

			ImGui::TextDisabled("empty = no grip layer; press Enter to apply");

			// The finger masks first, because those are the ones that make
			// sense here — the rest are listed so a wrong choice can be SEEN to
			// be wrong rather than guessed at.
			static const char* k_maskNames[] =
			{
				AnimMaskName::k_rightFingers, AnimMaskName::k_fingers,
				AnimMaskName::k_rightArm,     AnimMaskName::k_arms,
				AnimMaskName::k_upperBody,    AnimMaskName::k_full
			};

			const std::string current = world->getCVarValue(k_gripMaskCvar);
			if (ImGui::BeginCombo("mask", current.c_str()))
			{
				for (int i = 0; i < IM_ARRAYSIZE(k_maskNames); ++i)
				{
					const bool selected = (current == k_maskNames[i]);
					if (ImGui::Selectable(k_maskNames[i], selected))
						world->setCVar(k_gripMaskCvar, k_maskNames[i]);
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		// --- Swing -----------------------------------------------------------
		if (ImGui::CollapsingHeader("swing"))
		{
			// Both clips, looked up live rather than remembered from the last
			// swing, so a freshly imported animation reports itself correctly
			// before it has ever been played once.
			anax::Entity& player = world->managerSystem()->getEntityByName("player");

			MeshComponent* mc = (player.isValid() && player.hasComponent<MeshComponent>())
			                        ? &player.getComponent<MeshComponent>() : nullptr;

			const float fps = (mc && mc->fps > 0) ? static_cast<float>(mc->fps) : 30.0f;

			struct Local
			{
				// One clip row: a name field that applies on Enter, and the
				// clip's real length beside it — or a loud complaint if the mesh
				// has nothing by that name, which is the failure that otherwise
				// shows up only as an open hand or a swing that never comes out.
				static void clipRow(WorldManager* w, MeshComponent* mc,
				                    const char* label, const char* cvar,
				                    const AttackStage* stages, int stageCount,
				                    float fps)
				{
					const std::string name = w->getCVarValue(cvar);

					char buf[64];
					snprintf(buf, sizeof(buf), "%s", name.c_str());

					if (ImGui::InputText(label, buf, sizeof(buf),
					                     ImGuiInputTextFlags_EnterReturnsTrue))
						w->setCVar(cvar, buf);

					const sAnimationData* clip = mc ? mc->findAnimation(name) : nullptr;
					if (!clip)
					{
						ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
						                   "  no such clip on the character");
						return;
					}

					const int frames = clip->frames.Y - clip->frames.X;
					ImGui::TextDisabled("  %d frames at %.0f fps", frames + 1, fps);

					// The stage table, read-only and on purpose: these are
					// MEASURED properties of one export, not settings. Editing
					// them by hand here would invite exactly the silent
					// mismatch the tool exists to prevent.
					for (int i = 0; i < stageCount; ++i)
						ImGui::TextDisabled("  hit %d: frames %d..%d, contact %d%s",
						                    i + 1, stages[i].begin, stages[i].end,
						                    stages[i].contact,
						                    (stages[i].end > frames) ? "   PAST END OF CLIP" : "");
				}
			};

			const int lightCount = static_cast<int>(sizeof(k_lightStages) / sizeof(k_lightStages[0]));
			const int heavyCount = static_cast<int>(sizeof(k_heavyStages) / sizeof(k_heavyStages[0]));

			Local::clipRow(world, mc, "light clip", k_lightClipCvar,
			               k_lightStages, lightCount, fps);
			Local::clipRow(world, mc, "heavy clip", k_heavyClipCvar,
			               k_heavyStages, heavyCount, fps);

			ImGui::TextDisabled("stage frames are measured - re-run "
			                    "Tools/measure_attack_contact.py after a re-export");

			ImGui::Separator();

			float hold       = static_cast<float>(atof(world->getCVarValue(k_attackHoldCvar).c_str()));
			float lightRate  = static_cast<float>(atof(world->getCVarValue(k_lightRateCvar).c_str()));
			float heavyRate  = static_cast<float>(atof(world->getCVarValue(k_heavyRateCvar).c_str()));
			float reach      = static_cast<float>(atof(world->getCVarValue(k_attackReachCvar).c_str()));
			int   lightDmg   = atoi(world->getCVarValue(k_lightDamageCvar).c_str());
			int   heavyDmg   = atoi(world->getCVarValue(k_heavyDamageCvar).c_str());

			// Also the light attack's input latency - it cannot fire until this
			// has passed without a release. Said out loud, because "the light
			// feels sluggish" and "the heavy is hard to trigger" are the same
			// slider pulled in opposite directions.
			if (ImGui::DragFloat("hold = heavy", &hold, 0.005f, 0.08f, 1.0f, "%.3f s"))
				world->setCVar(k_attackHoldCvar, std::to_string(hold));
			ImGui::TextDisabled("  also the light attack's input delay");

			// Rates are latched when a swing STARTS, so a drag mid-swing takes
			// effect on the next one. Said out loud because the sword not
			// reacting to the slider looks like the slider being broken.
			if (ImGui::DragFloat("light rate", &lightRate, 0.01f, 0.25f, 3.0f, "%.2f"))
				world->setCVar(k_lightRateCvar, std::to_string(lightRate));
			if (ImGui::DragFloat("heavy rate", &heavyRate, 0.01f, 0.25f, 3.0f, "%.2f"))
				world->setCVar(k_heavyRateCvar, std::to_string(heavyRate));

			if (ImGui::DragInt("light damage", &lightDmg, 1.0f, 1, 500))
				world->setCVar(k_lightDamageCvar, std::to_string(lightDmg));
			if (ImGui::DragInt("heavy damage", &heavyDmg, 1.0f, 1, 500))
				world->setCVar(k_heavyDamageCvar, std::to_string(heavyDmg));

			if (ImGui::DragFloat("reach", &reach, 0.02f, 0.5f, 6.0f, "%.2f"))
				world->setCVar(k_attackReachCvar, std::to_string(reach));

			bool drawArc = world->getCVarValue(k_attackDebugCvar) != "0";
			if (ImGui::Checkbox("draw the strike arc", &drawArc))
				world->setCVar(k_attackDebugCvar, drawArc ? "1" : "0");

			ImGui::Separator();

			if (m_attackActive)
				ImGui::Text("%s  hit %d  %.2f / %.2f s  %s%s",
				            (m_attackKind == AttackKind::Light) ? "LIGHT" : "HEAVY",
				            m_attackStage + 1,
				            m_attackTime, m_attackDuration,
				            m_attackStruck ? "struck" : "...",
				            m_attackQueued ? "  [queued]" : "");
			else
				ImGui::TextDisabled("not swinging");

			// The raw button state, which is what makes a tap-or-hold mistake
			// diagnosable: you can see whether the press was even registered and
			// how far through the hold it got.
			ImGui::TextDisabled("button %s  held %.3f s%s",
			                    m_attackDown ? "down" : "up",
			                    m_attackHeld,
			                    m_attackSpent ? "  (spent)" : "");
		}
	}
	ImGui::End();
}

void PlayerControllerTPS::updateUI(float dt)
{
	// No HUD yet. Anything drawn here MUST be drawn from updateUI and not from
	// update(): update() runs inside the engine's fixed-step loop and executes
	// zero times on some rendered frames, which strobes ImGui windows.
	//
	// The anim_debug overlay below is the reason that rule matters here and not
	// only in principle: it reads weights that change every rendered frame, so
	// drawn from update() it would strobe AND show stale numbers.
	if (!WorldManager::Get())
		return;

	// BEFORE the anim_debug gate: the sword panel has its own toggle and is not
	// part of that overlay.
	updateWeaponUI();

	if (WorldManager::Get()->getCVarValue(k_animDebugCvar) == "0")
		return;

	anax::Entity& player = WorldManager::Get()->managerSystem()->getEntityByName("player");
	if (!player.isValid() || !player.hasComponent<AnimationComponent>())
		return;

	const AnimGraph& anim = player.getComponent<AnimationComponent>().graph;

	ImGui::SetNextWindowBgAlpha(0.75f);
	if (ImGui::Begin("anim_debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("blend %s   state %s",
		            AnimGraph::blendEnabled() ? "on" : "OFF (hard cuts, no sync)",
		            m_animPhase == AnimPhase::Air  ? "Air"
		          : m_animPhase == AnimPhase::Land ? "Land" : "Ground");

		// The shared cyclic phase. Every synced player below samples its own
		// frame range at (phase + its own foot-plant offset), so this one number
		// is what keeps a walk->run cross-fade from scissoring.
		ImGui::Text("phase %.3f   rate %.2f cyc/s", anim.phase(), anim.phaseRate());

		// --- Foot IK ---------------------------------------------------------
		// 'offset' is how far each ankle is being moved vertically, in world
		// units, and 'hip' how far the pelvis has dropped to keep the legs from
		// over-extending. On flat ground with the feet planted these should all
		// sit near zero: a large steady offset on level floor means soleOffset is
		// wrong for this rig, not that the IK is working hard.
		{
			float offL = 0.0f, offR = 0.0f, stL = 0.0f, stR = 0.0f;
			bool  gndL = false, gndR = false;

			const AnimFootIK& ik = anim.footIK();
			if (ik.valid())
			{
				ik.footInfo(0, offL, gndL, stL);
				ik.footInfo(1, offR, gndR, stR);

				// 'stance' is how planted the CLIP has each foot: 1 while it is
				// down, 0 through the swing. A foot with stance 0 must never show
				// a negative offset - that was the bug.
				ImGui::Text("ik w %.2f  hip %+.3f  reach %+.3f  lag %+.3f",
				            ik.weight(), ik.hipDrop(), -ik.hipExtra(), m_stepLag);
				ImGui::Text("sole %.4f  toe %.4f", ik.soleOffset(), ik.toeOffset());
				ImGui::Text("   L %+.3f stance %.2f%s   R %+.3f stance %.2f%s",
				            offL, stL, gndL ? "" : " (air)",
				            offR, stR, gndR ? "" : " (air)");

				// --- The probes, drawn in the world ----------------------------
				// A foot in the wrong place looks the same whether the probe
				// missed, hit the far side of a step, or hit something vertical
				// and was thrown away. The only way to tell is to look at where
				// the rays went, so: grey ray, GREEN cross for a hit that was
				// used, RED for one rejected as too steep, YELLOW for where the
				// ankle is being sent, and a white line from the animated ankle
				// to that target - the length of the white line IS the
				// correction.
				if (WorldManager::Get()->getCVarValue(k_animIKDrawCvar) != "0")
				{
					const irr::video::SColor kRay   (255,  90,  90,  90);
					const irr::video::SColor kUsed  (255,  40, 230,  60);
					const irr::video::SColor kReject(255, 235,  50,  50);
					const irr::video::SColor kTarget(255, 245, 220,  40);
					const irr::video::SColor kAnkle (255,  60, 190, 245);
					const irr::video::SColor kMove  (255, 255, 255, 255);

					for (int side = 0; side < 2; ++side)
					{
						const AnimFootIK::FootDebug& d = ik.footDebug(side);

						const AnimFootIK::Probe* probes[2] = { &d.ankle, &d.toe };

						for (int i = 0; i < 2; ++i)
						{
							const AnimFootIK::Probe& pr = *probes[i];
							if (!pr.fired)
								continue;

							debugLine(pr.from, pr.from - vector3df(0.0f, pr.length, 0.0f), kRay);

							if (pr.valid)
							{
								debugCross(pr.hit, 0.05f, pr.used ? kUsed : kReject);

								// The normal is what decides used/rejected, so
								// show it rather than only its verdict.
								debugLine(pr.hit, pr.hit + pr.normal * 0.20f,
								          pr.used ? kUsed : kReject);
							}
						}

						debugCross(d.ankleWorld,  0.035f, kAnkle);
						debugCross(d.targetWorld, 0.05f,  kTarget);
						debugLine (d.ankleWorld,  d.targetWorld, kMove);
					}
				}
			}
			else
			{
				ImGui::Text("ik    unavailable on this rig");
			}
		}

		ImGui::Separator();

		// The blend-space coordinates. 'rel' is the direction of travel relative
		// to where the body is heading, and it is the number to watch if the
		// character ever swings sideways when it should not: the strafe clips
		// carry a baked ~84-degree body yaw, so a spurious rel of even 45 turns
		// the whole character. Walking straight forward must hold rel near 0 and
		// dir fwd near 1.000 THROUGHOUT, including the first and last frames of
		// the move.
		ImGui::Text("body %6.1f  facing %6.1f  travel %6.1f  rel %6.1f",
		            m_bodyYaw, m_animFacing, m_travelYaw, m_dbgRel);
		ImGui::Text("speed  idle %.3f  walk %.3f  run %.3f",
		            m_dbgSpeedW[0], m_dbgSpeedW[1], m_dbgSpeedW[2]);
		ImGui::Text("dir    fwd %.3f  right %.3f  back %.3f  left %.3f",
		            m_dbgDirW[0], m_dbgDirW[1], m_dbgDirW[2], m_dbgDirW[3]);

		ImGui::Separator();

		if (ImGui::BeginTable("players", 6, ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("clip");
			ImGui::TableSetupColumn("cursor");
			ImGui::TableSetupColumn("range");
			ImGui::TableSetupColumn("sync");
			ImGui::TableSetupColumn("share");
			ImGui::TableSetupColumn("weight");
			ImGui::TableHeadersRow();

			AnimPlayerDebug info;
			for (int i = 0; i < anim.debugPlayerCount(); ++i)
			{
				if (!anim.debugPlayer(i, info))
					continue;

				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				// A player marked '>' belongs to the CURRENT state. Everything
				// else on this list is a state on its way out, still playing and
				// still contributing to the pose, which is the whole point.
				ImGui::Text("%s%s", info.current ? "> " : "  ", info.clip.c_str());

				ImGui::TableNextColumn(); ImGui::Text("%.1f", info.cursor);
				ImGui::TableNextColumn(); ImGui::Text("%d-%d%s", info.begin, info.end,
				                                      info.loop ? " L" : "");
				// 'sync' means the cursor comes from the shared phase rather than
				// from its own clock. A locomotion clip that is NOT synced is a
				// bug (or anim_blend 0); idle is legitimately free-running.
				ImGui::TableNextColumn(); ImGui::Text("%s", info.synced ? "yes" : "free");
				ImGui::TableNextColumn(); ImGui::Text("%.3f", info.share);
				ImGui::TableNextColumn(); ImGui::Text("%.3f", info.weight);
			}

			ImGui::EndTable();
		}

		// --- The swing -------------------------------------------------------
		// The layer itself shows up in the list below; this is the controller's
		// own clock, which is what the CONTACT is timed against.
		if (m_attackActive)
		{
			ImGui::Separator();
			ImGui::Text("attack  %.3f / %.3f s  contact %.3f %s  mask %s  rate %.2f%s",
			            m_attackTime, m_attackDuration, m_attackContact,
			            m_attackStruck ? "HIT" : "...",
			            m_attackMask ? m_attackMask : "-",
			            m_attackRate,
			            m_attackQueued ? "  [queued]" : "");
		}

		// --- Layers ----------------------------------------------------------
		// Stacked ON TOP of everything above, in index order. 'mask' is which
		// joints the layer reaches; 'add' means it composes with what is under it
		// rather than replacing it.
		AnimLayerDebug layer;
		bool anyLayer = false;

		for (int i = 0; i < anim.debugLayerCount(); ++i)
		{
			if (!anim.debugLayer(i, layer))
				continue;

			if (!anyLayer)
			{
				ImGui::Separator();
				ImGui::Text("layers");
				anyLayer = true;
			}

			ImGui::Text("  %d %-20s %-11s %s w %.3f -> %.0f  cursor %.1f",
			            i, layer.clip.c_str(), layer.mask.c_str(),
			            layer.additive ? "add" : "over",
			            layer.weight, layer.target, layer.cursor);
		}
	}
	ImGui::End();
}
