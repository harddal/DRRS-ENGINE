#include "Game/AI/AICoordinator.h"

#include <cmath>

#include "Engine/Navigation/CrowdManager.h"
#include "Engine/Navigation/NavigationManager.h"
#include "Engine/Renderer/RenderManager.h"
#include "Engine/World/Components/DescriptorComponent.h"
#include "Engine/World/Components/TransformComponent.h"
#include "Utility/Utility.h"

using namespace irr::core;

namespace
{
    // Eight bearings. More than this and adjacent slots are closer together
    // than an NPC is wide, so the assignment starts thrashing between them.
    const int k_slotCount = 8;

    // A slot whose navmesh projection moved it further than this is discarded.
    // This is the single check that separates "they spread out and circled"
    // from "six of them walked into a wall because their slot was in the next
    // room".
    const float k_slotProjectTolerance = 1.5f;

    // Minimum time a token must be held before it can be taken away. Without
    // it, two NPCs at nearly equal distance trade the token every pass and
    // visibly stutter between closing and circling.
    const float k_minTokenHoldMs = 1500.0f;

    // How long LOS must stay broken before a holder gives its token up. Short
    // breaks (a pillar, another NPC crossing) must not cost the token.
    const float k_losGraceMs = 1000.0f;

    const float k_passIntervalMs = 50.0f;   // 20Hz

    const vector3df k_eyeOffset(0.0f, 1.5f, 0.0f);
}

// ---------------------------------------------------------------------------

AICoordinator* AICoordinator::Get()
{
    // Function-local static: constructed on first use, so there is no static
    // init order to get wrong, and the private constructor keeps it unique.
    static AICoordinator s_instance;
    return &s_instance;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

AICoordinator::Squad* AICoordinator::findSquad(entityid targetId)
{
    for (size_t i = 0; i < m_squads.size(); ++i)
        if (m_squads[i].targetId == targetId)
            return &m_squads[i];
    return nullptr;
}

AICoordinator::Squad& AICoordinator::squadFor(entityid targetId)
{
    if (Squad* existing = findSquad(targetId))
        return *existing;

    m_squads.push_back(Squad());
    m_squads.back().targetId = targetId;
    return m_squads.back();
}

AICoordinator::Member* AICoordinator::findMember(Squad& squad, entityid selfId)
{
    for (size_t i = 0; i < squad.members.size(); ++i)
        if (squad.members[i].id == selfId)
            return &squad.members[i];
    return nullptr;
}

// ---------------------------------------------------------------------------
// Perception
// ---------------------------------------------------------------------------

bool AICoordinator::hasLineOfSight(const anax::Entity& self, const vector3df& to)
{
    if (!self.isValid() || !self.hasComponent<TransformComponent>()) return false;

    const TransformComponent& tc = self.getComponent<TransformComponent>();

    vector3df eye = tc.getPosition() + k_eyeOffset;

    vector3df dir  = to - eye;
    const float dist = dir.getLength();
    if (dist < 0.001f) return true;
    dir /= dist;

    // STEP THE ORIGIN PAST OUR OWN BODY FIRST.
    //
    // raycastWorldPosition walks every node from the scene root, and an
    // animated mesh gets a BOUNDING-BOX triangle selector -- so a ray that
    // starts at this NPC's own eye starts INSIDE its own selector and comes
    // straight back as a hit on itself. RenderManager does skip a self-hit, but
    // only for ET_PLAYER (RenderManager.cpp:4133-4144); an NPC gets no such
    // treatment.
    //
    // Un-offset, this returned false for every NPC at every distance, which
    // meant no zombie was ever granted an attack token and the whole squad sat
    // on the ring. CharacterBehavior::canSee used to carry the identical bug in
    // a second copy of this function; it forwards here now, so this is the only
    // LOS test in the game and every perception gate depends on it.
    float skin = 0.6f;
    if (tc.node)
    {
        const irr::core::aabbox3df bb = tc.node->getTransformedBoundingBox();
        const vector3df extent = bb.getExtent();
        // Half the wider horizontal axis, plus a little, so we clear the box
        // whatever way the body is facing.
        skin = 0.5f * ((extent.X > extent.Z) ? extent.X : extent.Z) + 0.1f;
    }

    // Never step past the target itself -- a very close target would otherwise
    // have the ray start beyond it and report a clear line through a wall.
    if (skin > dist * 0.5f) skin = dist * 0.5f;

    eye += dir * skin;

    RaycastResultData los = RenderManager::Get()->raycastWorldPosition(eye, to, true);

    if (!los.hit) return true;

    // Something is in the way -- unless the ray effectively arrived, in which
    // case what it hit IS the target. The target position is at the feet, so a
    // clear ray lands on the floor just underneath it; this tolerance is what
    // accepts that.
    return (los.point - to).getLengthSQ() < 0.25f;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void AICoordinator::rebuildSlots(Squad& squad)
{
    if (squad.slots.size() != static_cast<size_t>(k_slotCount))
        squad.slots.assign(static_cast<size_t>(k_slotCount), Slot());

    NavigationManager* nav = NavigationManager::Get();
    const bool haveNav = nav && nav->isNavMeshBuilt();

    for (int i = 0; i < k_slotCount; ++i)
    {
        const float ang = (360.0f / static_cast<float>(k_slotCount)) * static_cast<float>(i);
        const float rad = deg2rad(ang);

        vector3df want = squad.targetPos +
            vector3df(std::sin(rad), 0.0f, std::cos(rad)) * squad.ring;

        Slot& slot = squad.slots[i];

        if (!haveNav)
        {
            // Unbaked scene. Keep the ring so the coordination still reads,
            // and let the behaviour's own straight-line fallback deal with
            // geometry -- exactly what it does today with no squads at all.
            slot.pos   = want;
            slot.valid = true;
            continue;
        }

        vector3df snapped;
        if (!nav->findNearestPoint(want, snapped))
        {
            slot.valid = false;
            continue;
        }

        vector3df drift = snapped - want;
        drift.Y = 0.0f;

        slot.valid = drift.getLength() <= k_slotProjectTolerance;
        slot.pos   = snapped;
    }
}

void AICoordinator::assignSlots(Squad& squad)
{
    // Release slots whose owner has gone, or which stopped being reachable.
    for (size_t s = 0; s < squad.slots.size(); ++s)
    {
        Slot& slot = squad.slots[s];
        if (slot.owner == _entity_null_value) continue;

        bool ownerStillHere = false;
        for (size_t m = 0; m < squad.members.size(); ++m)
        {
            if (squad.members[m].id == slot.owner &&
                squad.members[m].slot == static_cast<int>(s))
            {
                ownerStillHere = true;
                break;
            }
        }

        if (!ownerStillHere || !slot.valid)
        {
            slot.owner = _entity_null_value;
            for (size_t m = 0; m < squad.members.size(); ++m)
                if (squad.members[m].slot == static_cast<int>(s))
                    squad.members[m].slot = -1;
        }
    }

    // Give every unslotted member the nearest free, reachable slot. Keeping an
    // existing assignment is what makes this stable -- reassigning by distance
    // every pass would have NPCs swapping slots as they move.
    for (size_t m = 0; m < squad.members.size(); ++m)
    {
        Member& member = squad.members[m];
        if (member.slot >= 0) continue;

        if (!member.entity.isValid() || !member.entity.hasComponent<TransformComponent>())
            continue;

        const vector3df pos = member.entity.getComponent<TransformComponent>().getPosition();

        int   best     = -1;
        float bestDist = 1.0e18f;

        for (size_t s = 0; s < squad.slots.size(); ++s)
        {
            const Slot& slot = squad.slots[s];
            if (!slot.valid) continue;
            if (slot.owner != _entity_null_value) continue;

            vector3df d = slot.pos - pos;
            d.Y = 0.0f;
            const float d2 = d.getLengthSQ();
            if (d2 < bestDist)
            {
                bestDist = d2;
                best     = static_cast<int>(s);
            }
        }

        if (best >= 0)
        {
            member.slot                  = best;
            squad.slots[best].owner      = member.id;
        }
    }
}

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

void AICoordinator::updateTokens(Squad& squad, float dtMs)
{
    // SQUAD_APPROACH never withholds. Everyone is a holder, permanently -- the
    // ring is only choosing bearings.
    if (squad.mode == SQUAD_APPROACH)
    {
        for (size_t i = 0; i < squad.members.size(); ++i)
            squad.members[i].hasToken = true;
        return;
    }

    int cap = squad.tokenCap;
    if (cap < 0) cap = 0;

    // --- Refresh distance and LOS ------------------------------------------
    for (size_t i = 0; i < squad.members.size(); ++i)
    {
        Member& member = squad.members[i];

        if (!member.entity.isValid() || !member.entity.hasComponent<TransformComponent>())
        {
            member.distSq   = 1.0e18f;
            member.noLosFor += dtMs;
            continue;
        }

        const vector3df pos = member.entity.getComponent<TransformComponent>().getPosition();

        vector3df d = squad.targetPos - pos;
        d.Y = 0.0f;
        member.distSq = d.getLengthSQ();

        if (hasLineOfSight(member.entity, squad.targetPos))
            member.noLosFor = 0.0f;
        else
            member.noLosFor += dtMs;

        if (member.hasToken)
            member.tokenHeld += dtMs;
    }

    // --- Revoke -------------------------------------------------------------
    int holders = 0;
    for (size_t i = 0; i < squad.members.size(); ++i)
    {
        Member& member = squad.members[i];
        if (!member.hasToken) continue;

        // The hold time gate applies to BOTH revoke reasons. Without it a
        // holder that clips behind a pillar the frame after being granted hands
        // its token straight back, and the pair oscillates.
        const bool mayRevoke = member.tokenHeld >= k_minTokenHoldMs;

        if (mayRevoke && member.noLosFor > k_losGraceMs)
        {
            member.hasToken  = false;
            member.tokenHeld = 0.0f;
            continue;
        }

        ++holders;
    }

    // Cap lowered (ai_tokens, or a designer edit): drop the furthest holders
    // that are past their minimum hold.
    while (holders > cap)
    {
        int   worst     = -1;
        float worstDist = -1.0f;

        for (size_t i = 0; i < squad.members.size(); ++i)
        {
            Member& member = squad.members[i];
            if (!member.hasToken) continue;
            if (member.tokenHeld < k_minTokenHoldMs) continue;

            if (member.distSq > worstDist)
            {
                worstDist = member.distSq;
                worst     = static_cast<int>(i);
            }
        }

        if (worst < 0) break;   // everyone left is still inside its hold window

        squad.members[worst].hasToken  = false;
        squad.members[worst].tokenHeld = 0.0f;
        --holders;
    }

    // --- Grant --------------------------------------------------------------
    while (holders < cap)
    {
        int   best     = -1;
        float bestDist = 1.0e18f;

        for (size_t i = 0; i < squad.members.size(); ++i)
        {
            Member& member = squad.members[i];
            if (member.hasToken) continue;

            // Nearest claimant WITH line of sight. Handing a token to someone
            // who cannot see the target is what produces the classic "charged
            // into a wall while its friend stood in the open" look.
            if (member.noLosFor > 0.0f) continue;

            if (member.distSq < bestDist)
            {
                bestDist = member.distSq;
                best     = static_cast<int>(i);
            }
        }

        if (best < 0) break;   // nobody eligible; the token stays unspent

        squad.members[best].hasToken  = true;
        squad.members[best].tokenHeld = 0.0f;
        ++holders;
    }
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void AICoordinator::update(float dtMs)
{
    m_accum += dtMs;
    if (m_accum < k_passIntervalMs) return;

    const float elapsed = m_accum;
    m_accum = 0.0f;

    for (size_t s = m_squads.size(); s-- > 0; )
    {
        Squad& squad = m_squads[s];

        // Prune members that stopped asking (dead, idled out, retargeted) or
        // whose entity has gone. A member that is merely not asking THIS pass
        // is still dropped -- behaviours call every frame while they want
        // coordination, and the pass runs at 20Hz, so any live member has had
        // several chances to be seen.
        for (size_t m = squad.members.size(); m-- > 0; )
        {
            Member& member = squad.members[m];
            const bool gone = !member.seenThisFrame ||
                              !member.entity.isValid() ||
                              !member.entity.hasComponent<TransformComponent>();

            if (gone)
            {
                if (member.slot >= 0 && static_cast<size_t>(member.slot) < squad.slots.size())
                    squad.slots[member.slot].owner = _entity_null_value;

                squad.members.erase(squad.members.begin() + m);
            }
            else
            {
                member.seenThisFrame = false;
            }
        }

        if (squad.members.empty() || squad.mode == SQUAD_NONE)
        {
            m_squads.erase(m_squads.begin() + s);
            continue;
        }

        rebuildSlots(squad);
        assignSlots(squad);
        updateTokens(squad, elapsed);
    }
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

SquadOrder AICoordinator::requestOrder(const anax::Entity& self, entityid targetId,
                                       const vector3df& targetPos,
                                       const SquadRequest& request)
{
    SquadOrder order;
    order.goal     = targetPos;
    order.hasToken = true;

    if (request.mode == SQUAD_NONE)             return order;
    if (targetId == _entity_null_value)         return order;
    if (!self.isValid())                        return order;
    if (!self.hasComponent<DescriptorComponent>()) return order;
    if (!self.hasComponent<TransformComponent>())  return order;

    const entityid  selfId = self.getComponent<DescriptorComponent>().id;
    const vector3df selfPos = self.getComponent<TransformComponent>().getPosition();

    Squad& squad = squadFor(targetId);
    squad.targetPos = targetPos;
    squad.mode      = request.mode;
    squad.ring      = request.ring;
    squad.tokenCap  = (m_tokenOverride >= 0) ? m_tokenOverride : request.tokens;

    Member* member = findMember(squad, selfId);
    if (!member)
    {
        squad.members.push_back(Member());
        member         = &squad.members.back();
        member->id     = selfId;
        // A new member starts with a token in APPROACH (nobody ever holds back)
        // and without one in HOLD (it has to earn it, which staggers arrivals).
        member->hasToken = (squad.mode == SQUAD_APPROACH);
    }

    member->entity        = self;
    member->seenThisFrame = true;

    const bool haveSlot = member->slot >= 0 &&
                          static_cast<size_t>(member->slot) < squad.slots.size() &&
                          squad.slots[member->slot].valid;

    if (squad.mode == SQUAD_APPROACH)
    {
        order.hasToken = true;

        // Spread the bearing only while still outside the ring. Once inside,
        // this unit closes on the target and nothing diverts it -- that is the
        // whole point of the mode.
        vector3df d = targetPos - selfPos;
        d.Y = 0.0f;

        if (haveSlot && d.getLength() > squad.ring * 1.25f)
            order.goal = squad.slots[member->slot].pos;

        return order;
    }

    // SQUAD_HOLD
    order.hasToken = member->hasToken;

    // No valid slot means the ring is unreachable from here -- fall back to the
    // direct approach rather than sending the NPC at a point in a wall.
    if (!member->hasToken && haveSlot)
        order.goal = squad.slots[member->slot].pos;

    return order;
}

void AICoordinator::release(entityid selfId)
{
    for (size_t s = 0; s < m_squads.size(); ++s)
    {
        Squad& squad = m_squads[s];
        for (size_t m = 0; m < squad.members.size(); ++m)
        {
            if (squad.members[m].id != selfId) continue;

            const int slot = squad.members[m].slot;
            if (slot >= 0 && static_cast<size_t>(slot) < squad.slots.size())
                squad.slots[slot].owner = _entity_null_value;

            squad.members.erase(squad.members.begin() + m);
            return;
        }
    }
}

void AICoordinator::clear()
{
    m_squads.clear();
    m_accum = 0.0f;
}

// ---------------------------------------------------------------------------
// Debug view
// ---------------------------------------------------------------------------

void AICoordinator::drawDebug()
{
    if (m_debugLevel <= 0) return;
    if (!RenderManager::Get()) return;

    const irr::video::SColor colFree   (255,  80, 200,  80);
    const irr::video::SColor colOwned  (255, 240, 200,  60);
    const irr::video::SColor colInvalid(255, 220,  60,  60);
    const irr::video::SColor colToken  (255, 255,  80,  80);
    const irr::video::SColor colHold   (255,  90, 160, 255);
    const irr::video::SColor colLosOk     (255,  60, 255, 140);
    const irr::video::SColor colLosBlocked(255, 255,  60,  60);

    for (size_t s = 0; s < m_squads.size(); ++s)
    {
        Squad& squad = m_squads[s];

        for (size_t i = 0; i < squad.slots.size(); ++i)
        {
            const Slot& slot = squad.slots[i];

            const irr::video::SColor col =
                !slot.valid                        ? colInvalid :
                slot.owner != _entity_null_value   ? colOwned   : colFree;

            // A vertical tick plus a cross bar: readable from any angle without
            // needing a real ring mesh.
            const vector3df& p = slot.pos;
            RenderManager::Get()->renderLine3D(
                Line3D(irr::core::line3df(p, p + vector3df(0.0f, 1.0f, 0.0f)), col));
            RenderManager::Get()->renderLine3D(
                Line3D(irr::core::line3df(p + vector3df(-0.25f, 0.05f, 0.0f),
                                          p + vector3df( 0.25f, 0.05f, 0.0f)), col));
            RenderManager::Get()->renderLine3D(
                Line3D(irr::core::line3df(p + vector3df(0.0f, 0.05f, -0.25f),
                                          p + vector3df(0.0f, 0.05f,  0.25f)), col));
        }

        for (size_t m = 0; m < squad.members.size(); ++m)
        {
            Member& member = squad.members[m];
            if (!member.entity.isValid() || !member.entity.hasComponent<TransformComponent>())
                continue;

            const vector3df pos =
                member.entity.getComponent<TransformComponent>().getPosition() +
                vector3df(0.0f, 1.0f, 0.0f);

            if (member.hasToken)
            {
                RenderManager::Get()->renderLine3D(
                    Line3D(irr::core::line3df(pos, squad.targetPos + vector3df(0.0f, 1.0f, 0.0f)),
                           colToken));
            }
            else if (member.slot >= 0 && static_cast<size_t>(member.slot) < squad.slots.size())
            {
                RenderManager::Get()->renderLine3D(
                    Line3D(irr::core::line3df(pos, squad.slots[member.slot].pos), colHold));
            }

            // Level 2: the LOS verdict each member is being judged on.
            //
            // Worth its own level because LOS is the gate on every token, and a
            // silently-failing LOS test looks EXACTLY like a working squad --
            // everyone walks calmly to the ring and stops, which is what a
            // no-token squad is supposed to look like. It cost one debugging
            // session already (the ray was starting inside the NPC's own
            // bounding-box selector and hitting itself).
            if (m_debugLevel >= 2)
            {
                const bool clear = (member.noLosFor <= 0.0f);
                RenderManager::Get()->renderLine3D(
                    Line3D(irr::core::line3df(pos + vector3df(0.0f, 0.4f, 0.0f),
                                              squad.targetPos + vector3df(0.0f, 0.4f, 0.0f)),
                           clear ? colLosOk : colLosBlocked));
            }
        }
    }

    // Level 3: the layer BELOW this one -- each crowd agent's corridor and its
    // nvel. It hangs off ai_debug rather than getting its own cvar because it
    // is the same question ("why is that NPC going there?") asked one level
    // down, and you almost always want the slots drawn alongside it.
    if (m_debugLevel >= 3 && CrowdManager::Get())
        CrowdManager::Get()->drawDebug();
}
