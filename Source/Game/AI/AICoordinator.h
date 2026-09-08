#pragma once

#include <vector>

#include <anax/Entity.hpp>
#include <irrlicht.h>

#include "Engine/Types.h"

// ---------------------------------------------------------------------------
// AICoordinator — ring slots and attack tokens.
//
// The cheapest trick in the book for perceived intelligence: a coordinator
// holds N attack tokens around each target. Only token-holders close in;
// everyone else takes a position on a ring and circles. It reads as flanking
// and coordination, and it is what stops twelve NPCs stacking in one doorway.
//
// IT IS A PULL SERVICE. Behaviours ask it what to do; it NEVER calls into a
// behaviour. That is the same line CharacterBehavior draws around itself, and
// for the same reason -- the moment this class starts invoking a virtual that a
// behaviour overrides to supply AI, NPCSystem has been rebuilt in a new folder.
//
// Squads are keyed by TARGET, not by faction, so cult-vs-undead infighting gets
// its own independent squad for free.
// ---------------------------------------------------------------------------

enum SQUAD_MODE
{
    // No coordination. The NPC always heads straight at its target -- exactly
    // what every behaviour did before this class existed.
    SQUAD_NONE     = 0,

    // Everyone charges; the ring only chooses which BEARING each one comes in
    // on, and is abandoned once the NPC is inside the ring. Nothing ever holds
    // back. This is the mode for a unit whose whole read is that it does not
    // stop coming -- the suicide bomber.
    SQUAD_APPROACH = 1,

    // Token-holders close; everyone else holds its ring slot and circles. The
    // doorway fix, and the mode for the zombie.
    SQUAD_HOLD     = 2
};

struct SquadRequest
{
    int   mode   = SQUAD_NONE;
    int   tokens = 3;
    float ring   = 4.0f;
};

struct SquadOrder
{
    irr::core::vector3df goal;

    // False only in SQUAD_HOLD without a token. A behaviour may use it to pick
    // a clip, but it does not have to -- the zombie needs no new state because
    // its own ranges already keep it in CHASE out at the ring.
    bool hasToken = true;
};

class AICoordinator
{
public:
    // Lazy function-local static, same as SkillSystem. It depends on nothing at
    // construction, so there is no init-order question to get wrong.
    static AICoordinator* Get();

    // Expires tokens, re-projects slots and prunes dead squads. Ticked from
    // WorldManager AFTER CrowdManager and BEFORE BehaviorSystem, so a
    // requestOrder() this frame reads assignments made this frame.
    void update(float dtMs);

    // Called every frame by a behaviour that wants coordination. Registering is
    // implicit: a member that stops asking is dropped on the next update().
    SquadOrder requestOrder(const anax::Entity& self, entityid targetId,
                            const irr::core::vector3df& targetPos,
                            const SquadRequest& request);

    // Drop a member immediately rather than waiting for it to stop asking --
    // called on death so its token frees up on the same frame it dies.
    void release(entityid selfId);

    void clear();

    // Console (ai_tokens): overrides every squad's cap. Negative disables the
    // override and behaviours' own values apply again.
    void setTokenOverride(int tokens) { m_tokenOverride = tokens; }
    int  tokenOverride() const { return m_tokenOverride; }

    // Console (ai_debug): 0 off, 1 slots + tokens, 2 adds the per-member LOS
    // verdict (green clear / red blocked) that gates every token grant.
    void setDebugLevel(int level) { m_debugLevel = level; }
    int  debugLevel() const { return m_debugLevel; }
    void drawDebug();

    int  squadCount() const { return static_cast<int>(m_squads.size()); }

    // PUBLIC, and STATIC: it depends on nothing in this class.
    //
    // Takes the ENTITY, not just a position, because it needs the node's
    // bounding box to step the ray origin past this NPC's own collision --
    // raycastWorldPosition skips a self-hit for ET_PLAYER only, so an NPC's ray
    // otherwise starts inside its own bounding-box selector and comes straight
    // back as a hit on itself.
    //
    // It was private, and CharacterBehavior::canSee was a second, BROKEN copy
    // of the same idea that had never been called. canSee now forwards here so
    // there is exactly one implementation -- if this ever needs changing, there
    // is nowhere else to change it.
    static bool hasLineOfSight(const anax::Entity& self,
                               const irr::core::vector3df& to);

private:
    AICoordinator() {}

    // A ring position. 'valid' is false when the point could not be projected
    // onto the navmesh without moving it a long way -- an unreachable slot is
    // worse than no slot, because everyone assigned to one walks into a wall.
    struct Slot
    {
        irr::core::vector3df pos;
        bool                 valid = false;
        entityid             owner = _entity_null_value;
    };

    struct Member
    {
        entityid     id = _entity_null_value;
        anax::Entity entity;

        int   slot      = -1;
        bool  hasToken  = false;
        float tokenHeld = 0.0f;   // ms since the token was granted
        float noLosFor  = 0.0f;   // ms since LOS to the target was last had

        bool  seenThisFrame = false;
        float distSq        = 0.0f;
    };

    // No handle to the target is kept. A squad exists exactly as long as
    // somebody keeps asking about it, so a dead or despawned target prunes
    // itself when its last member stops calling requestOrder.
    struct Squad
    {
        entityid             targetId = _entity_null_value;
        irr::core::vector3df targetPos;

        int   tokenCap = 3;
        float ring     = 4.0f;
        int   mode     = SQUAD_NONE;

        std::vector<Slot>   slots;
        std::vector<Member> members;
    };

    Squad*  findSquad(entityid targetId);
    Squad&  squadFor(entityid targetId);
    Member* findMember(Squad& squad, entityid selfId);

    void rebuildSlots(Squad& squad);
    void assignSlots(Squad& squad);
    void updateTokens(Squad& squad, float dtMs);

    std::vector<Squad> m_squads;
    int   m_tokenOverride = -1;
    int   m_debugLevel    = 0;

    // The heavy pass (slot projection, LOS raycasts, token arbitration) runs at
    // 20Hz, not per frame. Token hysteresis is measured in seconds, so 50ms
    // granularity is far finer than anything that depends on it, and it keeps
    // the LOS raycasts to ~20/pass instead of ~20/frame.
    float m_accum = 0.0f;
};
