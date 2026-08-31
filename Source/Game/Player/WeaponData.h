#pragma once

#include "Engine/Engine.h"
#include "Engine/Renderer/RenderManager.h"

#include "Engine/World/Components.h"

#include "Engine/Resource/FilePaths.h"

#include "Game/Components/DamageReceiverComponent.h"

#define _weapon_crosshair_size 64
#define _weapon_crosshair2x_size 128
#define _weapon_crosshair3x_size 256
#define _weapon_crosshair_center_position irr::core::vector2d<irr::s32>((RenderManager::Get()->getConfiguration().width / 2) - (_weapon_crosshair_size / 2), (RenderManager::Get()->getConfiguration().height / 2) - (_weapon_crosshair_size / 2))
#define _weapon_crosshair2x_center_position irr::core::vector2d<irr::s32>((RenderManager::Get()->getConfiguration().width / 2) - (_weapon_crosshair2x_size / 2), (RenderManager::Get()->getConfiguration().height / 2) - (_weapon_crosshair2x_size / 2))
#define _weapon_crosshair3x_center_position irr::core::vector2d<irr::s32>((RenderManager::Get()->getConfiguration().width / 2) - (_weapon_crosshair3x_size / 2), (RenderManager::Get()->getConfiguration().height / 2) - (_weapon_crosshair3x_size / 2))

enum PLAYER_WEAPON
{
	WEAP_NONE,
	WEAP_MELEE,
	WEAP_REVOLVER,
	WEAP_SHOTGUN,
	WEAP_HEAVYRIFLE,
	WEAP_CROSSBOW,
	WEAP_LMG,
	WEAP_SNIPER,
	WEAP_LAUNCHER,
	WEAP_DUALSMG,
	WEAP_SKULLSTAFF,
	WEAP_SAWNOFFS,
	WEAP_PITCHFORK,
	WEAP_RIFLE,
	WEAP_SMG,
	WEAP_COUNT
};

// --- Selection buckets -------------------------------------------------------
// Half-Life 2 style weapon slots: a number key selects a CATEGORY, and pressing
// it again cycles the weapons inside it. The pack outgrew one-key-per-weapon at
// fifteen weapons and ten keys, and category slots are the answer that does not
// run out again.
//
// The value IS the key the player presses, so WEAPCAT_MELEE is bucket 1. Keep
// them contiguous from 1 and keep WEAPCAT_COUNT last.
enum WEAPON_CATEGORY
{
	WEAPCAT_NONE = 0,  // empty hands; not a bucket, and reached with 0
	WEAPCAT_MELEE,     // 1  knife, pitchfork
	WEAPCAT_SIDEARM,   // 2  revolver
	WEAPCAT_SHOTGUN,   // 3  shotgun, sawn-offs
	WEAPCAT_AUTOMATIC, // 4  smg, rifle, heavy rifle, dual smgs
	WEAPCAT_HEAVY,     // 5  lmg, sniper
	WEAPCAT_EXPLOSIVE, // 6  grenade launcher
	WEAPCAT_EXOTIC,    // 7  crossbow, skull staff
	WEAPCAT_COUNT
};

// Which bucket a weapon sits in. Defined ONCE, here, rather than as a key->weapon
// table inside the input handler: a table in the input code would be a second
// place that enumerates weapons, and it would rot exactly the way the console's
// hardcoded "give <0-12>" usage string did. Adding a weapon means touching this
// file, which is the same file its ammo type already lives in.
WEAPON_CATEGORY weaponCategory(PLAYER_WEAPON weapon);

// For the selection HUD. weaponIconPath() returns nullptr when a weapon has no
// icon, so the HUD can fall back to text rather than drawing a broken texture.
const char* weaponDisplayName(PLAYER_WEAPON weapon);
const char* weaponIconPath(PLAYER_WEAPON weapon);

// THE ORDER OF THIS ENUM IS A FILE FORMAT. Pickup .ent files store ammoType as
// a raw integer, and player.sav stores the reserve array indexed by it, so
// reordering silently reassigns every placed pickup and every existing save.
// Append only, and keep AMMO_COUNT last.
enum AMMO_TYPE
{
	AMMO_NONE,    // no reserve at all: melee weapons, and pickups granting no ammo
	AMMO_LIGHT,   // dual SMGs, pistol
	AMMO_HEAVY,   // heavy rifle, LMG — the belt drains a pool the rifle also needs
	AMMO_MAGNUM,  // revolver
	AMMO_SHELL,   // shotgun, sawn-offs
	AMMO_MATCH,   // sniper
	AMMO_BOLT,    // crossbow
	AMMO_GRENADE, // launcher
	AMMO_ENERGY,  // legacy sci-fi set
	AMMO_ROCKET,  // legacy sci-fi set
	AMMO_COUNT
};

// Sentinel for a pickup's ammoType/ammoAmount properties: "ask the weapon".
// Deliberately NOT a member of AMMO_TYPE — it is never a valid index into the
// reserve array. AMMO_NONE keeps its own distinct meaning on a pickup: hand over
// the weapon and no ammunition whatsoever.
#define _ammo_auto (-1)

// Which pool a weapon draws from. AMMO_NONE means it never reloads from a
// reserve — melee, and anything not deliberately given a pool.
AMMO_TYPE weaponAmmoType(PLAYER_WEAPON weapon);

// Reserve rounds a pickup of this weapon hands over when its ammoAmount is
// _ammo_auto. Lives here rather than in twelve .ent files so retuning one
// weapon's generosity is a one-line edit.
int weaponPickupAmmo(PLAYER_WEAPON weapon);

// Cap on the pool itself. AMMO_NONE reports 0; drawFromReserve() treats that
// pool as infinite rather than empty, so melee weapons need no special case.
int ammoReserveMax(AMMO_TYPE type);

// Magazine contents, in a shape general enough to serialize every weapon in the
// pack without the save code knowing about any of them.
//
// Four slots because one int is lossy: the sawn-offs genuinely track two guns
// independently, and the revolver needs its live/spent chamber split. The charge
// is separate because mana is a float and is not ammunition — it regenerates, so
// it has no pool and no pickup, but it still has to survive a checkpoint.
struct WeaponMagState
{
	int   slots[4] = { -1, -1, -1, -1 }; // -1 = slot unused by this weapon
	float charge   = -1.0f;              // -1 = weapon has no charge resource
};

struct BurnDecal
{
	irr::scene::ISceneNode* node = nullptr;
	float spawnTime = 0.0f;
	float maxLifetime = 1500.0f;
	unsigned int id = 0; // For debugging
};

struct WeaponProjectile
{
	anax::Entity entity;  // Store entity handle directly HACK: Needs to use the Entities ID not a copy, es no bueno!
	entityid id = _entity_null_value; // This is the fix ^ however not implemented NOIMP
	entityid targetId = _entity_null_value;
	bool useTracking = false;
	bool isTrackingActive = false; // Is it actively allowed to turn yet?
	float distanceTraveled = 0.0f; // Track distance for launch phase
	float trackingStartDistance = 3.0f; // Distance before tracking engages
	irr::core::vector3df velocity;
	irr::core::vector3df previousPosition;  // Position from last frame for swept collision
	float speed = 50.0f;
	float lifetime = 0.0f;
	float maxLifetime = 5000.0f; // 5 seconds
	irr::core::vector3df initialRotation;  // Rotation to apply once node is created
	bool orientationApplied = false;       // Track if rotation has been applied
	irr::scene::IParticleSystemSceneNode* trailParticles = nullptr; // Trail effect (Irrlicht, legacy)
	SPK::System* sparkSystem = nullptr; // Trail/bolt effect (SPARK)
	SoundHandle flyingSound; // Looping 3D flight sound
	bool isBouncing  = false; // If true, reflects off surfaces and detonates on timer instead of impact
	int  bounceCount = 0;    // Number of times this projectile has bounced
	bool isStuck     = false; // Embedded in a surface: frozen in place, running out its lifetime
};

class PlayerWeapon
{
public:
	virtual void precache() = 0;
	virtual void init()     = 0;
	virtual void update()   = 0;
	virtual void persist()  = 0;
	virtual void destroy()  = 0;

	virtual void equip()    = 0;
	virtual void unequip()  = 0;
	virtual void idle()     = 0;
	virtual void move()     = 0;
	virtual void fire()     = 0;
	virtual void reload()   = 0;

	// Animated unequip support — weapons that want a transition animation override these.
	// Default: instant hide (same as unequip()).
	virtual void startUnequip() { unequip(); }
	virtual bool isUnequipping() const { return false; }

	// Composes sway + view kick and writes the viewmodel node transform once per
	// frame. Called by WeaponController after update(); weapons must not write
	// m_mesh.node position/rotation themselves or the two will fight.
	virtual void updateWeaponSway(float dt);

	// Instant viewmodel kick (position snap-back / rotation punch) recovered by an
	// exponential spring inside updateWeaponSway(). Call from fire()/swing paths.
	void addViewKick(const irr::core::vector3df& posKick, const irr::core::vector3df& rotKick);
	void resetViewKick();

	// --- Clip playback speed -------------------------------------------------
	// Multiplier over m_mesh.fps for the clip that is playing. Set it alongside
	// playAnimation() and put it back to 1.0 when leaving that clip.
	//
	// Frame-triggered GAMEPLAY logic is unaffected, since it keys off
	// getFrameNr(). Frame-triggered SOUND is not: a cue's lead-in is fixed in
	// real time, so the number of animation frames it spans changes with speed.
	// Use soundLeadFrames() rather than a hardcoded frame count for those.
	void setClipSpeed(float multiplier);

	// Animation frames of lead a cue needs for its transient to land ON a given
	// frame, from the measured time between the start of the .wav and that
	// transient. Converted at the CURRENT playback speed, so speeding a clip up
	// cannot silently desync the sounds attached to it.
	int soundLeadFrames(float transientSeconds) const;

	// --- Clip stabilisation --------------------------------------------------
	// Cancels part of a clip's gross screen-space motion, for animations that
	// swing the whole weapon further than reads well in first person.
	//
	// It works by nailing a REFERENCE POINT on the gun rather than by countering
	// the root transform: measure how far that point drifts from its rest
	// position and counter-translate the viewmodel node by a fraction of the
	// drift. A pure translation absorbs both the root's travel AND the much
	// larger swing that a root ROTATION imparts to a point out along the barrel.
	//
	// Deliberately not a counter-rotation: the node's origin is not the gun's
	// pivot, so rotating it back would move the gun again and drag the arms with
	// it. The gun still visibly tilts here, which is what sells the action
	// working — it just stops wandering off screen.
	void enableClipStabilization(const char* jointName, const irr::core::vector3df& localOffset);

	// The strength this weapon wants when it stabilises: 0 = off, 1 = the
	// reference point is pinned dead still. Expect to want less than 1 — fully
	// pinned, the arms appear to do all the moving and it reads uncanny.
	//
	// Kept separate from the currently-active amount so the F2 slider edits
	// something durable: states re-apply the amount on every transition, and a
	// slider writing the active value directly would be wiped by the next pump.
	void  setStabilizationTuneAmount(float amount) { m_stabTuneAmount = amount; }
	float stabilizationTuneAmount() const          { return m_stabTuneAmount; }

	// Active amount for the clip that is playing. Set per animation state, using
	// stabilizationTuneAmount() as the "on" value.
	void setStabilizationAmount(float amount) { m_stabAmount = amount; }

	// Records where the reference point sits at the REST pose. Must be called
	// with the weapon idle and its joints live — not from init(), where the node
	// is hidden and the joint transforms are stale. Cheap, and the rest pose
	// never changes, so call it once behind stabilizationRestValid().
	void captureStabilizationRest();
	bool stabilizationRestValid() const { return m_stabRestValid; }

	// Live tuning hooks for the viewmodel debug window (F2)
	float&               debugStabilizationAmount() { return m_stabTuneAmount; }
	irr::core::vector3df& debugStabilizationOffset() { return m_stabLocalOffset; }
	bool                 hasClipStabilization() const { return m_stabBone != nullptr; }

	// --- Rigid glTF part addressing ------------------------------------------
	// glTF weapons have no scene node per moving part. GltfImport gives every
	// glTF node an Irrlicht joint and hands non-skinned geometry to it as buffer
	// indices in SJoint::AttachedMeshes, so a part like a shell or a magazine is
	// addressed by JOINT NAME, and "hiding" it means swapping the material on its
	// buffers — there is no node to call setVisible() on.
	//
	// This exists because these weapon packs reuse one prop mesh for several
	// jobs: the shotgun's single 'slug' is both the case flicked out by the pump
	// and the fresh shell thumbed into the loading port, so it has to be hidden
	// and shown per frame or it teleports across the screen.
	struct MeshPart
	{
		std::vector<irr::u32>       buffers;                         // indices into the viewmodel's mesh buffers
		irr::video::E_MATERIAL_TYPE material = irr::video::EMT_SOLID; // real material, restored on show
		irr::scene::IBoneSceneNode* bone     = nullptr;               // transform probe; draws nothing itself
		bool                        visible  = true;

		// The joint that actually MOVES this part, which is NOT `bone`.
		//
		// GltfImport attaches geometry to the joint of the node that carries the
		// mesh — the leaf, e.g. "bullet_sniper_0" — and that leaf has no animation
		// channels of its own. Its motion comes entirely from its parent, so
		// bone->getPosition() is a CONSTANT and any displacement test against it
		// silently never fires. (bone->getAbsoluteTransformation() is animated and
		// is fine; it is the LOCAL transform that is dead.)
		//
		// animBone is that parent — "bullet" — whose local position is keyed and
		// therefore actually moves. Use partOffset() rather than reading either
		// of these directly.
		irr::scene::ISceneNode*     animBone = nullptr;
	};

	// How far a part currently sits from wherever it rests, in its own parent's
	// space — so it measures the PART moving, not the gun being swung around.
	// Returns zero when the part never resolved.
	irr::core::vector3df partPosition(const MeshPart& part) const
	{
		return part.animBone ? part.animBone->getPosition() : irr::core::vector3df(0.0f, 0.0f, 0.0f);
	}

	// Matches the first joint whose name starts with jointNamePrefix AND carries
	// geometry — the prefix form survives a re-export renaming the "_weapon_0"
	// leaf. Call AFTER the node's materials are assigned: it caches the material
	// so setMeshPartVisible() has something to restore. Returns false and warns
	// if nothing matched.
	bool resolveMeshPart(const char* jointNamePrefix, MeshPart& outPart);

	void setMeshPartVisible(MeshPart& part, bool visible);

	// World transform of the part, for spawning something exactly where it is.
	// Forces the joint up to date, so call after animateJoints().
	bool meshPartWorldTransform(MeshPart& part, irr::core::matrix4& outWorld);

	// Per-axis scale that makes a stand-in mesh of size targetExtent match the
	// on-screen size of this part. Axes are matched by RANK (shortest to
	// shortest) rather than by index, because the glTF to Irrlicht conversion may
	// permute them; and per-axis rather than one ratio, because a stand-in rarely
	// shares the part's aspect ratio — matching only the long axis leaves it
	// visibly too fat, and bulk goes as the square of the diameter.
	irr::core::vector3df matchPartScale(const MeshPart& part,
	                                    const irr::core::vector3df& targetExtent) const;

	// --- Procedural idle breathing -------------------------------------------
	// Small, never-repeating hold-steady motion for weapons whose idle clip is a
	// single frame or absent. Call enableIdleBreathing() once in init(); the
	// motion is then folded into updateWeaponSway()'s single transform write, so
	// it composes with sway and kick instead of fighting them. Weapons still must
	// not touch m_mesh.node's position/rotation themselves.
	//
	// scale is a master multiplier over every amplitude in IdleBreath, so the
	// common case is tuned with one number: 0.5 for a braced two-hand hold,
	// ~1.5-2 for a heavy weapon held one-handed.
	void enableIdleBreathing(float scale = 1.0f);

	// Current breathing displacement. updateWeaponSway() applies this for you;
	// it is exposed so a weapon can inspect or damp it (e.g. while aiming down
	// sights) without reimplementing the curve.
	void idleBreathOffsets(irr::core::vector3df& outPosition,
	                       irr::core::vector3df& outRotation) const;

	// Hit-confirmation feedback. Weapons pass the HIT_RESULT from
	// GameplaySystem::damageEntity(); HIT flashes the hitmarker + plays a tick
	// (rate-limited), KILL shows a larger red marker + kill sound. NONE is a no-op.
	static void registerHitFeedback(HIT_RESULT result);

	// Draws the fading hitmarker over the crosshair. Called once per frame by
	// WeaponController::update() — weapons never call this themselves.
	static void drawHitFeedback();

	// Shared draw/holster handling sounds. Every weapon transitions the same way,
	// so the paths live here instead of being repeated (and drifting) per weapon.
	// Call from equip() and startUnequip() respectively — NOT from unequip(),
	// which also runs as the instant-hide teardown and would double up.
	// Both resolve through playRandomized2D, so dropping equip1.wav/equip2.wav
	// next to equip.wav upgrades every weapon to variants with no code change.
	static void playEquipSound();
	static void playUnequipSound();

	// Cue for a reload the player asked for and cannot have. Silence reads as a
	// dropped input, which sends them pressing reload again instead of going to
	// look for ammunition.
	static void playEmptyReserveSound();

	// Preloads the two sounds above. Called once by WeaponController alongside the
	// per-weapon precache() pass, so no single weapon has to own assets they all
	// share — and so the first draw of the session doesn't hitch on a disk read.
	static void precacheSharedSounds();

	// World point under the crosshair: camera-center ray hit, or the ray's far end.
	irr::core::vector3df getCrosshairAimPoint(float maxRange = 1000.0f);

	// Converging-aim direction: from 'origin' (typically the muzzle bone, which sits
	// below-right of screen centre) toward the crosshair aim point, so muzzle-origin
	// rays/projectiles land on the crosshair instead of running parallel to the
	// camera ray. Falls back to camera forward when the aim point is too close to
	// converge on safely.
	irr::core::vector3df getAimDirection(const irr::core::vector3df& origin, float maxRange = 1000.0f);

	// Debug accessors for the viewmodel positioning tool in WeaponController
	irr::core::vector3df& debugViewPositionOffset() { return m_viewPositionOffset; }
	irr::core::vector3df& debugViewRotationOffset() { return m_viewRotationOffset; }
	irr::scene::IAnimatedMeshSceneNode* debugViewmodelNode() { return m_mesh.node; }
	const std::string& debugWeaponName() const { return m_descriptor.name; }

	// Weapons that compose a WeaponEffects return it so the viewmodel debug
	// window can tune the muzzle offset live. Models without a FIRESPOT empty
	// need that offset dialled in by eye; guessing it in code is what put the
	// revolver's flash inside its own grip.
	virtual class WeaponEffects* debugEffects() { return nullptr; }

	// Projectile weapons expose their arc knobs the same way and for the same
	// reason: launch speed and gravity decide whether a shot reads as a bolt or a
	// mortar, and that is judged by eye, not from the source. Pointers rather
	// than values so the debug window edits the live members in place.
	//
	// Returns false for hitscan weapons, which leaves the block out of the UI.
	struct BallisticTuning
	{
		float* speed       = nullptr;
		float* gravity     = nullptr;
		float* maxAimRange = nullptr; // how far out the arc is solved to land
	};
	virtual bool debugBallistics(BallisticTuning&) { return false; }

	// --- Identity ------------------------------------------------------------
	// Set by each weapon in init(). Everything that maps a weapon to a pool, a
	// save record or an ownership flag goes through this rather than through the
	// weapon's index in WeaponController::m_player_weapon — the registration
	// order there matches this enum today only by coincidence, and one weapon
	// registered out of order would shift every index under an old save.
	PLAYER_WEAPON weaponType() const { return m_weapon_type; }

	// Rounds in the gun, for the HUD. -1 means "this weapon has no ammunition
	// readout" — melee and the pitchfork — and the HUD draws nothing.
	//
	// Deliberately its own accessor rather than reading slot 0 of saveMagState():
	// the staff keeps its SELECTED SPELL in that slot, and a readout wired to the
	// save shape would have displayed the spell index as a round count.
	virtual int displayAmmo() const { return -1; }

	// --- Save state ----------------------------------------------------------
	// Weapons that hold rounds override these; the default pair is a no-op, which
	// is correct for melee and for anything that has nothing to remember.
	virtual void saveMagState(WeaponMagState&) const {}
	virtual void loadMagState(const WeaponMagState&) {}

	// Rounds left in the pool this weapon feeds from. Reported as INT_MAX for
	// AMMO_NONE so "is there anything to reload with" reads the same everywhere.
	int reserveRemaining() const;

protected:
	// Moves up to 'want' rounds out of the shared reserve and into the magazine,
	// returning what was ACTUALLY moved so a weapon can tell a full reload from a
	// partial one. The LMG needs that distinction — its belt arc is drawn from
	// the round count, so a short draw renders a visibly short belt for free.
	//
	// Weapons on AMMO_NONE get 'want' back unchanged, which is what keeps melee
	// out of this entirely.
	int drawFromReserve(int want);

	// Drives the viewmodel node from the clip named in m_mesh.animationList, so
	// frame ranges are declared once in init() instead of being repeated (and
	// drifting) at every call site. Loop mode comes from the clip's own flag.
	// Returns false — leaving the current loop untouched — when the clip is missing.
	bool playAnimation(const std::string& name);

	bool picked_up;

	irr::core::vector3df m_viewPositionOffset, m_viewRotationOffset, m_viewScaleOffset;

	// Every weapon sets this in init(). The old m_current_ammo/m_ammo_type pair
	// that sat here was written and read by nobody: rounds live in each weapon's
	// own magazine member, and the pool is looked up with weaponAmmoType().
	PLAYER_WEAPON m_weapon_type = WEAP_NONE;

	DescriptorComponent m_descriptor;
	MeshComponent m_mesh;

	// Weapon sway variables
	irr::core::vector3df m_lastCameraRotation;
	irr::core::vector3df m_swayOffset;       // smoothed sway displacement (lags camera)
	float m_swayAmount = 0.02f;
	float m_swaySmoothing = 0.1f;

	// View kick spring state (shared by all weapons)
	irr::core::vector3df m_kickPos;          // positional kick offset
	irr::core::vector3df m_kickRot;          // rotational kick offset (degrees)
	float m_kickPosRecovery = 15.0f;         // spring rate, per second
	float m_kickRotRecovery = 10.0f;         // spring rate, per second

	// Idle breathing tuning. Defaults are a relaxed two-handed hold; positions are
	// in viewmodel units (same space as m_viewPositionOffset), rotations degrees.
	struct IdleBreath
	{
		bool  enabled = false;
		float scale   = 1.0f;             // master multiplier over everything below

		// Breathing — the dominant slow rise and fall of the chest.
		float breathRate  = 0.23f;        // Hz, about 14 breaths per minute
		float breathPosY  = 0.0035f;      // vertical travel
		float breathPitch = 0.30f;        // degrees; muzzle rides the breath

		// Postural drift — the slower wander of someone not braced on anything.
		float driftRate = 0.13f;          // Hz
		float driftPosX = 0.0028f;
		float driftPosZ = 0.0015f;
		float driftYaw  = 0.42f;          // degrees
		float driftRoll = 0.22f;          // degrees
	};

	IdleBreath m_idleBreath;
	float m_idleBreathTime = 0.0f;       // seconds, advanced by updateWeaponSway()

	// Clip stabilisation state. m_stabRest and the per-frame reference are both in
	// VIEWMODEL-NODE-LOCAL space (model units), which is what makes the maths
	// independent of the node position being written this frame.
	irr::scene::IBoneSceneNode* m_stabBone = nullptr;
	irr::core::vector3df m_stabLocalOffset;   // reference point within the bone, model units
	irr::core::vector3df m_stabRest;          // reference at the rest pose, node-local
	irr::core::vector3df m_stabOffset;        // resulting counter-translation, parent space
	float m_stabAmount     = 0.0f; // active this frame
	float m_stabTuneAmount = 0.0f; // the weapon's chosen "on" strength
	bool  m_stabRestValid  = false;

	void updateClipStabilization();

};

// Shader callback for plasma ball animation
class PlasmaShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
	virtual void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData)
	{
		// Pass time to shader for animation
		irr::f32 time = Engine::Get()->getDeltaTime() / 1000.0f; // Convert to seconds
		services->setPixelShaderConstant("uTime", &time, 1);

		// Get depth texture from deferred renderer
		//auto* depthTexture = RenderManager::Get()->renderer()->getMRT(2);

		// Set texture samplers
		irr::s32 tex0 = 0;
		services->setPixelShaderConstant("texture1", &tex0, 1);

		irr::s32 depthTex = 1;
		services->setPixelShaderConstant("uDepthTexture", &depthTex, 1);

		// Pass screen dimensions for depth sampling
		irr::core::dimension2du screenSize = RenderManager::Get()->driver()->getScreenSize();
		irr::f32 screenDims[2] = { (irr::f32)screenSize.Width, (irr::f32)screenSize.Height };
		services->setPixelShaderConstant("uScreenSize", screenDims, 2);

		// Pass camera far plane for linear depth calculation in vertex shader
		irr::scene::ICameraSceneNode* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
		if (cam) {
			irr::f32 farDist = cam->getFarValue();
			services->setVertexShaderConstant("CamFar", &farDist, 1);
		}
	}
};

// Shader callback for impact burn effect with time-based animation
class ImpactBurnShaderCallback : public irr::video::IShaderConstantSetCallBack
{
public:
	float maxLifetime = 1500.0f; // Total lifetime in ms

	virtual void OnSetConstants(irr::video::IMaterialRendererServices* services, irr::s32 userData)
	{
		// The userData parameter is set to the material type param when we set it
		// However, Irrlicht doesn't pass this automatically, so we need another approach
		// We'll pass spawn time through a global variable that gets updated before rendering
		
		// For now, let's try reading from the driver's current transformation matrix's user data
		// Actually, the simplest approach: pass it as MaterialTypeParam and read it here
		// But we need to access the current material being rendered...
		
		// WORKAROUND: We'll use a static variable updated by the rendering code
		// This is not thread-safe but Irrlicht renders on a single thread
		static float currentSpawnTime = 0.0f;
		
		// Calculate normalized burn time (0 = just spawned, 1 = fully cooled)
		float currentTime = Engine::Get()->getCurrentTime();
		float elapsed = currentTime - currentSpawnTime;
		float burnTime = std::min(1.0f, elapsed / maxLifetime);

		services->setPixelShaderConstant("uBurnTime", &burnTime, 1);

		// Set texture samplers
		irr::s32 tex0 = 0;
		services->setPixelShaderConstant("texture1", &tex0, 1);

		irr::s32 depthTex = 1;
		services->setPixelShaderConstant("uDepthTexture", &depthTex, 1);

		// Pass screen dimensions for depth sampling
		irr::core::dimension2du screenSize = RenderManager::Get()->driver()->getScreenSize();
		irr::f32 screenDims[2] = { (irr::f32)screenSize.Width, (irr::f32)screenSize.Height };
		services->setPixelShaderConstant("uScreenSize", screenDims, 2);

		// Pass camera far plane for linear depth calculation
		irr::scene::ICameraSceneNode* cam = RenderManager::Get()->sceneManager()->getActiveCamera();
		if (cam) {
			irr::f32 farDist = cam->getFarValue();
			services->setVertexShaderConstant("CamFar", &farDist, 1);
		}
	}
	
	// This will be set before each decal is rendered  
	static void setCurrentSpawnTime(float time) {
		currentSpawnTime = time;
	}
	
private:
	static float currentSpawnTime;
};
