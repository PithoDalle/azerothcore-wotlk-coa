/*
 * Custom script for the Basalthane encounter (Onyxia's Lair, entry 10185-10188).
 *
 * Annihilation Strike (spell 2108206) is a pure SPELL_EFFECT_DUMMY on Ascension's
 * client/server - it carries no native damage or aura effect in our Spell.dbc
 * (StackAmount = 0, no EffectApplyAura). Ascension's own server implements the
 * cleave damage and the stacking -25% Fire resistance / -25% armor debuff with
 * custom server-side logic that doesn't exist on this fork, so it's reimplemented
 * here by hand:
 *
 *  - On hit: deal 40% weapon damage (Fire school) to the target and to any other
 *    enemies within melee-cleave range of it.
 *  - Track a per-target stack count (not a native WoW aura - the spell has no
 *    aura effect to attach one to) and apply the -25%/stack Fire resistance and
 *    armor reduction directly via the unit's stat modifiers.
 *  - Stacks refresh their 30s expiry on every hit and are cleared either when
 *    they expire (checked on player update) or on logout, so nothing lingers on
 *    a player after the pull ends.
 *
 * Tanks are expected to taunt Basalthane off whoever is holding 2-3 stacks,
 * since at that point Fire damage taken is drastically increased.
 */

#include "AllCreatureScript.h"
#include "Containers.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridTerrainData.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "ThreatManager.h"
#include "Unit.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr uint32 SPELL_ANNIHILATION_STRIKE = 2108206;
    // The stacking debuff (-25% Fire resistance, -25% armor per stack) is a real native
    // aura (Effect type Apply Aura, not Dummy) - just cast it and let the game show the
    // icon and manage stacking/duration itself instead of hand-rolling it.
    constexpr uint32 SPELL_ANNIHILATION_DEBUFF = 2108211;
    // The real hit: native "Weapon % Damage 40, radius 10yd" per difficulty - the
    // engine handles the cleave itself, no manual damage calc or nearby-unit loop needed.
    constexpr uint32 SPELL_ANNIHILATION_HIT_D0 = 2108207;
    constexpr uint32 SPELL_ANNIHILATION_HIT_D1 = 2108208;
    constexpr uint32 SPELL_ANNIHILATION_HIT_D2 = 2108209;
    constexpr uint32 SPELL_ANNIHILATION_HIT_D3 = 2108210;

    // Inferno Trail (2108217) and Eruption (2108227) are, like Annihilation Strike,
    // pure SPELL_EFFECT_DUMMY with no native damage/aura in our Spell.dbc - reimplemented here.
    constexpr uint32 SPELL_INFERNO_TRAIL = 2108217;
    constexpr uint32 SPELL_INFERNO_TRAIL_PRE = 2108218; // "Inferno Trail - Hidden - Pre", real cast-start telegraph
    // Real native stacking DoT, per difficulty - just delegate to it
    constexpr uint32 SPELL_FLASH_BURN_D0 = 2108201;
    constexpr uint32 SPELL_FLASH_BURN_D1 = 2108202;
    constexpr uint32 SPELL_FLASH_BURN_D2 = 2108203;
    constexpr uint32 SPELL_FLASH_BURN_D3 = 2108204;
    // A cone, not a straight line (confirmed against a real kill recording - it fans
    // out wider the further it travels, not a constant-width strip).
    constexpr float INFERNO_TRAIL_LINE_LENGTH = 100.0f; // "Unlimited Range" per the tooltip - keeps going until it leaves the room
    // Forward spacing must be bigger than the same-row gap below - rows should not
    // overlap each other (that's what was stacking hundreds of swirls on top of one
    // another), only swirls within the same row should overlap a little.
    constexpr float INFERNO_TRAIL_VISUAL_STEP = 3.5f; // forward spacing between rows (was 1.0 - rows were overlapping each other)
    constexpr int INFERNO_TRAIL_CONE_START_SWIRLS = 2; // starts as 2 overlapping swirls right at the boss
    constexpr float INFERNO_TRAIL_GROWTH_STEP_DIST = 5.0f; // +1 swirl in the row every 5 yards traveled
    constexpr float INFERNO_TRAIL_SWIRL_OVERLAP_GAP = 1.8f; // gap between adjacent swirls in a row - small so they overlap
    // Cone stops widening past this distance (both the visual and the hitbox use it,
    // so the danger zone never grows wider than what the raid can actually see).
    constexpr float INFERNO_TRAIL_WIDTH_CAP_DIST = 40.0f;
    // Hit detection now checks "am I standing inside one of the actual swirls" rather
    // than a smooth mathematical cone - the two had drifted out of sync once the
    // visual became a discrete grid of rows/swirls, so players standing under a
    // visible swirl but off the cone's exact centerline weren't getting hit. Matches
    // the real native hit spell's own "radius 3yd" per difficulty.
    constexpr float INFERNO_TRAIL_HIT_RADIUS = 3.0f;
    // Real native "School Damage, radius 3yd" per difficulty - casting a chain of these
    // along the line both deals the damage and (since each one carries its own real
    // spell visual) renders the actual chain-of-swirls trail, instead of one cast-start marker.
    constexpr uint32 SPELL_INFERNO_TRAIL_HIT_D0 = 2108219;
    constexpr uint32 SPELL_INFERNO_TRAIL_HIT_D1 = 2108220;
    constexpr uint32 SPELL_INFERNO_TRAIL_HIT_D2 = 2108221;
    constexpr uint32 SPELL_INFERNO_TRAIL_HIT_D3 = 2108222;
    constexpr uint32 SPELL_CRACKED_ARMOR = 2108234;

    // Direction the line was aimed in, captured the instant the cast started (see
    // OnAllCreatureUpdate below) - not the target's live position 2.5s later, which is
    // what makes the line dodgeable instead of a guaranteed hit.
    std::unordered_map<ObjectGuid, std::pair<float, float>> infernoTrailDirection;

    constexpr uint32 SPELL_ERUPTION = 2108227;
    constexpr uint32 SPELL_ERUPTION_PRE = 2108239; // "Eruption - Hidden - Pre", real cast-start telegraph
    // Real native periodic-damage area aura, per difficulty
    constexpr uint32 SPELL_MAGMA_POOL_D0 = 2108230;
    constexpr uint32 SPELL_MAGMA_POOL_D1 = 2108231;
    constexpr uint32 SPELL_MAGMA_POOL_D2 = 2108232;
    constexpr uint32 SPELL_MAGMA_POOL_D3 = 2108233;
    // The actual explosion: real native School Damage + Knockback + a stacking
    // "+100% Fire damage taken" debuff, found separately from the 2108xxx family
    // (D0-D3, one spell id per difficulty).
    constexpr uint32 SPELL_ERUPTION_EXPLOSION_D0 = 2105077;
    constexpr uint32 SPELL_ERUPTION_EXPLOSION_D1 = 2105078;
    constexpr uint32 SPELL_ERUPTION_EXPLOSION_D2 = 2105079;
    constexpr uint32 SPELL_ERUPTION_EXPLOSION_D3 = 2105080;
    constexpr float ERUPTION_BURST_RADIUS = 6.0f; // GUESS: still used for who "counts as hit" for the split damage
    // Magma Pool's own Spell.dbc duration is 168 hours - a failsafe cap, not the real
    // lifetime. Normal/Heroic (10185/10186): expires on its own after 20s. Mythic/Ascended
    // (10187/10188): stays down permanently until the pull ends (see the cleanup below).
    constexpr int32 MAGMA_POOL_DURATION_TIMED_MS = 20000; // GUESS
    constexpr uint32 ENTRY_BASALTHANE_NORMAL = 10185;
    constexpr uint32 ENTRY_BASALTHANE_HEROIC = 10186;
    constexpr uint32 ENTRY_BASALTHANE_MYTHIC = 10187;
    constexpr uint32 ENTRY_BASALTHANE_ASCENDED = 10188;

    // Pillar mechanic (corrected): it's not the ooze - when Basalthane lands
    // Annihilation Strike while standing near a pillar (gameobject 68371), it shatters -
    // Igneous Impact (2108212, real native School Damage AoE) goes off, and Basalthane
    // gets "caught in the blast" (2108216, real stun) as a bonus damage window.
    constexpr uint32 ENTRY_PILLAR = 68371;
    constexpr uint32 ENTRY_MOLTEN_BLOOD_OOZE = 310189; // reverted after diagnostic test 2026-09-24 confirmed entry 68 worked normally (spawned, stayed visible, despawned after the expected ~90s timer) - the bug is specific to 310189's own config, not the spawn mechanism/room/grid. Prime suspect: its model (DisplayID 60375, creature_model_info BoundingRadius 0.5/CombatReach 1.5) may be broken/invisible on this custom client.
    constexpr float ANNIHILATION_PILLAR_RANGE = 6.0f; // GUESS
    constexpr uint32 SPELL_IGNEOUS_IMPACT = 2108212;
    constexpr uint32 SPELL_CAUGHT_IN_THE_BLAST = 2108216;
    // CONFIRMED from real kill combat logs: Cracked Armor lasts ~20.0s each time it's
    // applied (self-debuff on Basalthane from a pillar stun, not a tank debuff).
    constexpr uint32 CRACKED_ARMOR_DURATION_SECONDS = 20;

    // Shattered pillars are phased out (invisible/unusable) rather than despawned -
    // AC's native GO respawn scheduling is unreliable to force on demand, but we can
    // always flip our own phase mask back the instant a wipe/kill/evade happens.
    // Confirmed 2026-09-23: pillars only come back on wipe/kill/evade - no mid-fight
    // respawn timer. Don't re-add one without the user asking for it again.
    std::unordered_set<ObjectGuid> hiddenPillars;

    // Cracked Armor is self-applied to Basalthane by the pillar-stun (see
    // spell_basalthane_annihilation_strike::HandleDummy), not a tank debuff - tracked
    // here and force-removed after CRACKED_ARMOR_DURATION_SECONDS since we can't trust
    // this custom spell's own DBC duration field (same pattern as Magma Pool's bogus
    // 168h cap elsewhere in this file).
    std::unordered_map<ObjectGuid, uint32> crackedArmorUntil;

    // Tracks which Basalthane GUIDs we've already raid-wide-applied opening Flash Burn
    // to for the current pull, so the rising edge (not-in-combat -> in-combat) only
    // fires once per pull instead of every update tick.
    std::unordered_set<ObjectGuid> flashBurnOpenerApplied;

    // CONFIRMED from real kill logs (cross-checked across 2 separate kills): raid-wide
    // Flash Burn stacks reapply on a clean, fixed 15.0s cycle the whole fight - this is
    // NOT explained by Inferno Trail hits (its own cast cadence is irregular, 13-34s,
    // and doesn't line up with this). Independent periodic mechanic, first tick 15s
    // after the opener.
    constexpr uint32 FLASH_BURN_RAIDWIDE_TICK_MS = 15000;
    std::unordered_map<ObjectGuid, uint32> flashBurnNextTick;

    // Inferno Trail is scheduled from C++ (not SmartAI, unlike Fierce Blow/Annihilation
    // Strike/Eruption) so it can give those two real priority: CONFIRMED cadence from
    // real kill logs is a clean 14s cycle that sometimes stretches to 28s when it loses
    // out to one of them - implemented here as an honest "is the boss already mid-cast
    // on something (only Annihilation Strike/Eruption ever share this cast slot) right
    // when Inferno Trail is due? If so, skip this cycle entirely" check, rather than the
    // earlier randomized-interval approximation (removed 2026-09-24, user asked for the
    // real thing instead).
    constexpr uint32 INFERNO_TRAIL_CAST_INTERVAL_MS = 14000;
    std::unordered_map<ObjectGuid, uint32> infernoTrailNextCast;

    // Molten Blood ooze spawning - moved from SmartAI to C++ (2026-09-24) because the
    // boss gets dragged around this room (not tanked in the middle), so "spawn at one of
    // the 3 points farthest from the boss" has to be computed against his CURRENT
    // position every time, which a static SmartAI random-point target can't do. All 10
    // points below were walked and .gps'd by the user around the room's usable area
    // after a vmap/mmap re-extraction; every spawn picks among the 3 currently farthest
    // from Basalthane, not a fixed subset. Z values are used as-is (no ground-height
    // requery), same anti-bad-Z precaution as everywhere else in this room - see the
    // room-bug notes in memory for why that matters here.
    // CONFIRMED from real kill logs (2026-09-24): counting distinct Molten Blood GUIDs
    // per 60s bucket across two separate kills showed a roughly steady ~5-6 new oozes
    // per minute for the whole fight (not accelerating) - averages to ~10-12s between
    // spawns (193s/17 oozes = 11.4s avg in one kill, 307s/23 oozes = 13.3s avg in
    // another). Replaces the old 45s GUESS, which was far too slow. Randomized range
    // instead of a fixed value to reflect the real data's natural clustering (some
    // spawns landed within ~1-2s of each other, others 20-30s apart).
    constexpr uint32 MOLTEN_BLOOD_SPAWN_INTERVAL_MIN_MS = 8000;
    constexpr uint32 MOLTEN_BLOOD_SPAWN_INTERVAL_MAX_MS = 16000;
    constexpr uint32 MOLTEN_BLOOD_DESPAWN_MS = 90000; // matches the old SmartAI config (action_param3)
    struct MoltenBloodSpawnPoint { float x, y, z; };
    constexpr MoltenBloodSpawnPoint MOLTEN_BLOOD_SPAWN_POINTS[] = {
        { -240.27287f,  35.508114f, -78.784676f },
        { -263.4401f,   12.799386f, -78.87359f  },
        { -271.89877f, -19.737661f, -77.90959f  },
        { -237.37097f, -58.87336f,  -78.87458f  },
        { -211.72928f, -63.637604f, -78.51081f  },
        { -186.05104f, -46.86159f,  -78.688255f },
        { -175.00475f, -26.678469f, -78.85377f  },
        { -171.39236f,  -9.649658f, -78.56807f  },
        { -178.40074f,   9.963768f, -78.81197f  },
        { -195.8838f,   29.746857f, -78.93467f  },
    };
    std::unordered_map<ObjectGuid, uint32> moltenBloodNextSpawn;

    void SpawnMoltenBloodAtFarthestPoint(Creature* boss)
    {
        float bx = boss->GetPositionX();
        float by = boss->GetPositionY();

        // This custom room straddles a map grid boundary (some of the 10 points sit in
        // a different grid than Basalthane's own). CONFIRMED 2026-09-24: an ooze spawned
        // in a grid that isn't currently loaded still exists and functions server-side
        // (it kept granting Molten Blood stacks to the boss - visible as stacks climbing
        // on him) but is invisible to players, which read as "despawns instantly" until
        // the boss buff gave it away. Only consider points whose grid is actually loaded
        // right now, so we never place one somewhere players can't see it.
        std::vector<MoltenBloodSpawnPoint const*> byDistance;
        for (auto const& pt : MOLTEN_BLOOD_SPAWN_POINTS)
            if (boss->GetMap()->IsGridLoaded(pt.x, pt.y))
                byDistance.push_back(&pt);

        if (byDistance.empty())
            return;

        std::sort(byDistance.begin(), byDistance.end(), [bx, by](MoltenBloodSpawnPoint const* a, MoltenBloodSpawnPoint const* b)
        {
            float da = (a->x - bx) * (a->x - bx) + (a->y - by) * (a->y - by);
            float db = (b->x - bx) * (b->x - bx) + (b->y - by) * (b->y - by);
            return da > db;
        });

        std::vector<MoltenBloodSpawnPoint const*> farthestThree(byDistance.begin(), byDistance.begin() + std::min<size_t>(3, byDistance.size()));
        MoltenBloodSpawnPoint const* chosen = Acore::Containers::SelectRandomContainerElement(farthestThree);

        // FOUND 2026-09-24 - the real root cause of the "despawns after ~90s, no death,
        // everywhere/every-config" mystery: TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN's timer
        // (see TempSummon::Update in TemporarySummon.cpp) only counts down while the
        // summon is OUT of combat - it resets to full lifetime whenever IsInCombat() is
        // true. Since this ooze is deliberately passive and never enters combat, that
        // timer just counted down from the moment it spawned and unsummoned it ~90s
        // later no matter where it was, what model it had, or whether it could move -
        // explaining every single symptom chased over several failed diagnostics
        // (terrain, grid, gravity, Swim, SmartAI react state). TEMPSUMMON_TIMED_DESPAWN
        // counts down unconditionally regardless of combat state, and death still
        // unsummons instantly regardless of summon type (handled earlier in
        // TempSummon::Update, before the type-specific switch). This is the actual fix -
        // the SetDisableGravity/IsGridLoaded workarounds above are left in as harmless
        // extra safety nets, not because they were wrong, just not the real cause.
        if (Creature* ooze = boss->SummonCreature(ENTRY_MOLTEN_BLOOD_OOZE, chosen->x, chosen->y, chosen->z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, MOLTEN_BLOOD_DESPAWN_MS))
        {
            ooze->SetDisableGravity(true);
        }
    }

    // Tracks which Basalthane GUIDs already got their one-time Smoldering Vengeance
    // opener thrown on the off-tank for the current pull - see the apply site in
    // OnAllCreatureUpdate for why this can't just fire on the same rising edge as the
    // Flash Burn opener (the threat list needs a moment to have two players on it).
    std::unordered_set<ObjectGuid> smolderingVengeanceOpenerApplied;

    // Picks the current off-tank: the second-highest-threat PLAYER on the boss's threat
    // list (index 0 is the current tank/victim). Returns nullptr if there aren't two
    // distinct player tanks on the list yet (e.g. right at the very start of the pull).
    // TEMP: not called right now, see the TEMP TEST MODE comment at the Smoldering
    // Vengeance opener call site - [[maybe_unused]] keeps this from warning/breaking
    // the build while it's parked. Swap it back in when test mode is reverted.
    [[maybe_unused]] Player* FindOffTank(Creature* boss)
    {
        Player* found = nullptr;
        for (auto const& ref : boss->GetThreatMgr().GetSortedThreatList())
        {
            Unit* target = ref->GetVictim();
            if (!target || !target->IsPlayer())
                continue;

            if (!found)
            {
                found = target->ToPlayer();
                continue;
            }

            if (target != found)
                return target->ToPlayer();
        }
        return nullptr;
    }

    void ShatterPillar(GameObject* pillar)
    {
        pillar->SetPhaseMask(0, true);
        hiddenPillars.insert(pillar->GetGUID());
    }

    void RestorePillar(Unit* context, ObjectGuid guid)
    {
        if (GameObject* pillar = ObjectAccessor::GetGameObject(*context, guid))
            pillar->SetPhaseMask(PHASEMASK_NORMAL, true);
    }

    // Restores every currently-shattered pillar immediately (wipe/kill/evade)
    void RestoreAllPillars(Unit* context)
    {
        for (ObjectGuid const& guid : hiddenPillars)
            RestorePillar(context, guid);
        hiddenPillars.clear();
    }

    // Both real per-difficulty values pulled directly from Spell.dbc (Ascension's own
    // hidden D0-D3 helper spells for these two abilities), not guesses:
    //   Inferno Trail direct hit: 2108219-2108222 (D0-D3), EffectBasePoints 206/277/349/410
    //   Eruption base damage:     2108223-2108226 (D0-D3), EffectBasePoints 30000/40000/68000/80000
    uint32 GetDifficultyEntry(Unit* caster)
    {
        if (Creature* creature = caster->ToCreature())
            if (CreatureTemplate const* cinfo = creature->GetCreatureTemplate())
                return cinfo->Entry;
        return ENTRY_BASALTHANE_NORMAL;
    }

    uint32 InfernoTrailHitSpellFor(Unit* caster)
    {
        switch (GetDifficultyEntry(caster))
        {
            case ENTRY_BASALTHANE_HEROIC:   return SPELL_INFERNO_TRAIL_HIT_D1;
            case ENTRY_BASALTHANE_MYTHIC:   return SPELL_INFERNO_TRAIL_HIT_D2;
            case ENTRY_BASALTHANE_ASCENDED: return SPELL_INFERNO_TRAIL_HIT_D3;
            default:                        return SPELL_INFERNO_TRAIL_HIT_D0;
        }
    }

    // Flat damage values, real per-difficulty numbers from Spell.dbc - used for the
    // manual 2D damage dealing (see spell_basalthane_inferno_trail::HandleDummy).
    uint32 InfernoTrailDamageFor(Unit* caster)
    {
        switch (GetDifficultyEntry(caster))
        {
            case ENTRY_BASALTHANE_HEROIC:   return 277;
            case ENTRY_BASALTHANE_MYTHIC:   return 349;
            case ENTRY_BASALTHANE_ASCENDED: return 410;
            default:                        return 206;
        }
    }

    uint32 EruptionBaseDamageFor(Unit* caster)
    {
        switch (GetDifficultyEntry(caster))
        {
            case ENTRY_BASALTHANE_HEROIC:   return 40000;
            case ENTRY_BASALTHANE_MYTHIC:   return 68000;
            case ENTRY_BASALTHANE_ASCENDED: return 80000;
            default:                        return 30000;
        }
    }

    uint32 MagmaPoolSpellFor(Unit* caster)
    {
        switch (GetDifficultyEntry(caster))
        {
            case ENTRY_BASALTHANE_HEROIC:   return SPELL_MAGMA_POOL_D1;
            case ENTRY_BASALTHANE_MYTHIC:   return SPELL_MAGMA_POOL_D2;
            case ENTRY_BASALTHANE_ASCENDED: return SPELL_MAGMA_POOL_D3;
            default:                        return SPELL_MAGMA_POOL_D0;
        }
    }

    uint32 FlashBurnSpellFor(Unit* caster)
    {
        switch (GetDifficultyEntry(caster))
        {
            case ENTRY_BASALTHANE_HEROIC:   return SPELL_FLASH_BURN_D1;
            case ENTRY_BASALTHANE_MYTHIC:   return SPELL_FLASH_BURN_D2;
            case ENTRY_BASALTHANE_ASCENDED: return SPELL_FLASH_BURN_D3;
            default:                        return SPELL_FLASH_BURN_D0;
        }
    }

    // CONFIRMED from real kill combat logs 2026-09-24: Smoldering Vengeance is applied
    // by Basalthane himself to a single player - the off-tank - at pull, NOT by killing
    // or standing near the Molten Blood ooze (an earlier design, now removed - ooze
    // deaths in the logs checked had no correlation with when the buff was applied).
    // While it's up, the player ticks Fire damage to nearby enemies every 3s via a
    // separate, difficulty-scaled spell (2108256/57/58 - no D0 variant, Normal doesn't tick).
    constexpr uint32 SPELL_SMOLDERING_VENGEANCE = 2108235;
    constexpr uint32 SPELL_SMOLDERING_VENGEANCE_TICK_D1 = 2108256;
    constexpr uint32 SPELL_SMOLDERING_VENGEANCE_TICK_D2 = 2108257;
    constexpr uint32 SPELL_SMOLDERING_VENGEANCE_TICK_D3 = 2108258;
    constexpr uint32 SMOLDERING_VENGEANCE_TICK_MS = 3000;

    uint32 SmolderingVengeanceTickSpellFor(uint32 difficultyEntry)
    {
        switch (difficultyEntry)
        {
            case ENTRY_BASALTHANE_HEROIC:   return SPELL_SMOLDERING_VENGEANCE_TICK_D1;
            case ENTRY_BASALTHANE_MYTHIC:   return SPELL_SMOLDERING_VENGEANCE_TICK_D2;
            case ENTRY_BASALTHANE_ASCENDED: return SPELL_SMOLDERING_VENGEANCE_TICK_D3;
            default:                        return 0; // Normal doesn't tick
        }
    }

    uint32 AnnihilationHitSpellFor(Unit* caster)
    {
        switch (GetDifficultyEntry(caster))
        {
            case ENTRY_BASALTHANE_HEROIC:   return SPELL_ANNIHILATION_HIT_D1;
            case ENTRY_BASALTHANE_MYTHIC:   return SPELL_ANNIHILATION_HIT_D2;
            case ENTRY_BASALTHANE_ASCENDED: return SPELL_ANNIHILATION_HIT_D3;
            default:                        return SPELL_ANNIHILATION_HIT_D0;
        }
    }

    uint32 EruptionExplosionSpellFor(Unit* caster)
    {
        switch (GetDifficultyEntry(caster))
        {
            case ENTRY_BASALTHANE_HEROIC:   return SPELL_ERUPTION_EXPLOSION_D1;
            case ENTRY_BASALTHANE_MYTHIC:   return SPELL_ERUPTION_EXPLOSION_D2;
            case ENTRY_BASALTHANE_ASCENDED: return SPELL_ERUPTION_EXPLOSION_D3;
            default:                        return SPELL_ERUPTION_EXPLOSION_D0;
        }
    }

    // forcedDurationMs >= 0 always overrides to that exact duration (used for the early
    // cast-start telegraph). Left at -1, it falls back to the real per-difficulty rule.
    void CastMagmaPoolAt(Unit* caster, float x, float y, float z, int32 forcedDurationMs = -1)
    {
        SpellInfo const* magmaPool = sSpellMgr->GetSpellInfo(MagmaPoolSpellFor(caster));
        if (!magmaPool)
            return;

        SpellCastTargets targets;
        targets.SetDst(x, y, z, 0.0f);

        if (forcedDurationMs >= 0)
        {
            CustomSpellValues values;
            values.AddSpellMod(SPELLVALUE_AURA_DURATION, forcedDurationMs);
            caster->CastSpell(targets, magmaPool, &values, TRIGGERED_FULL_MASK);
            return;
        }

        bool permanent = false;
        if (Creature* creature = caster->ToCreature())
            if (CreatureTemplate const* cinfo = creature->GetCreatureTemplate())
                permanent = (cinfo->Entry == ENTRY_BASALTHANE_MYTHIC || cinfo->Entry == ENTRY_BASALTHANE_ASCENDED);

        if (permanent)
        {
            caster->CastSpell(targets, magmaPool, nullptr, TRIGGERED_FULL_MASK);
        }
        else
        {
            CustomSpellValues values;
            values.AddSpellMod(SPELLVALUE_AURA_DURATION, MAGMA_POOL_DURATION_TIMED_MS);
            caster->CastSpell(targets, magmaPool, &values, TRIGGERED_FULL_MASK);
        }
    }

    // Ground height varies across the cone's width on this uneven lava floor - using a
    // flat Z for every point made the side casts land in mid-air/underground relative
    // to where players actually stand, so the side hits never registered. Query the
    // real ground height per point instead, falling back to the caster's Z if the
    // query fails (matches the known vmap gaps in this room).
    float ResolveGroundZ(WorldObject* context, float x, float y, float fallbackZ)
    {
        float z = context->GetMap()->GetHeight(x, y, fallbackZ + 5.0f, true);
        return (z > INVALID_HEIGHT) ? z : fallbackZ;
    }

    // Damage is dealt manually (see spell_basalthane_inferno_trail::HandleDummy) - this
    // cast is visual-only, its School Damage effect gets stripped by
    // spell_basalthane_inferno_trail_hit_visual_only (same trick as
    // spell_basalthane_eruption_explosion suppressing Knockback below). Restores the
    // per-swirl explosion look without reintroducing double damage.
    void CastInfernoTrailHitAt(Unit* caster, float x, float y, float z)
    {
        SpellInfo const* hit = sSpellMgr->GetSpellInfo(InfernoTrailHitSpellFor(caster));
        if (!hit)
            return;

        SpellCastTargets targets;
        targets.SetDst(x, y, z, 0.0f);
        caster->CastSpell(targets, hit, nullptr, TRIGGERED_FULL_MASK);
    }

    // Single source of truth for every point along the trail - used to draw the
    // telegraph, to check who's standing in a swirl when it hits, and to play the
    // per-swirl explosion visual, so all three can never drift out of sync again.
    template <typename Callback>
    void ForEachInfernoTrailSwirl(float originX, float originY, float dirX, float dirY, Callback&& cb)
    {
        float perpX = -dirY;
        float perpY = dirX;
        for (float dist = INFERNO_TRAIL_VISUAL_STEP; dist <= INFERNO_TRAIL_LINE_LENGTH; dist += INFERNO_TRAIL_VISUAL_STEP)
        {
            float cappedDist = std::min(dist, INFERNO_TRAIL_WIDTH_CAP_DIST);
            int swirlCount = INFERNO_TRAIL_CONE_START_SWIRLS + int(cappedDist / INFERNO_TRAIL_GROWTH_STEP_DIST);
            float rowHalfWidth = (swirlCount - 1) * INFERNO_TRAIL_SWIRL_OVERLAP_GAP / 2.0f;
            for (int i = 0; i < swirlCount; ++i)
            {
                float offset = -rowHalfWidth + i * INFERNO_TRAIL_SWIRL_OVERLAP_GAP;
                float px = originX + dirX * dist + perpX * offset;
                float py = originY + dirY * dist + perpY * offset;
                cb(px, py);
            }
        }
    }

    // The actual "swirl" visual lives on 2108218 ("Pre"), not on the small hit spells -
    // repeat it at every point along the line to get the chain-of-swirls look instead
    // of a single marker at the boss. Purely cosmetic, no damage/aura of its own.
    void CastInfernoTrailSwirlAt(Unit* caster, float x, float y, float z)
    {
        SpellInfo const* swirl = sSpellMgr->GetSpellInfo(SPELL_INFERNO_TRAIL_PRE);
        if (!swirl)
            return;

        SpellCastTargets targets;
        targets.SetDst(x, y, z, 0.0f);
        caster->CastSpell(targets, swirl, nullptr, TRIGGERED_FULL_MASK);
    }

    // The real explosion: native School Damage + Knockback + stacking "+100% Fire
    // damage taken" debuff, centered on the impact point (not on Basalthane).
    void CastEruptionExplosionAt(Unit* caster, float x, float y, float z)
    {
        uint32 spellId = EruptionExplosionSpellFor(caster);
        SpellInfo const* explosion = sSpellMgr->GetSpellInfo(spellId);
        if (!explosion)
            return;

        SpellCastTargets targets;
        targets.SetDst(x, y, z, 0.0f);
        caster->CastSpell(targets, explosion, nullptr, TRIGGERED_FULL_MASK);
    }

}

class spell_basalthane_annihilation_strike : public SpellScript
{
    PrepareSpellScript(spell_basalthane_annihilation_strike);

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        // Real native Weapon % Damage hit, per difficulty - handles the 10yd cleave itself
        caster->CastSpell(target, AnnihilationHitSpellFor(caster), true);

        // Real native stacking debuff - the game handles the icon, stacking and duration
        caster->CastSpell(target, SPELL_ANNIHILATION_DEBUFF, true);

        // Pillar mechanic: if Basalthane is standing near a pillar when he lands
        // Annihilation Strike, it shatters - Igneous Impact goes off (real native
        // room-wide AoE), he gets caught in the blast (stunned), and Cracked Armor
        // goes on HIMSELF (not the tank) as the bonus-damage vulnerability window.
        if (GameObject* pillar = caster->FindNearestGameObject(ENTRY_PILLAR, ANNIHILATION_PILLAR_RANGE))
        {
            caster->CastSpell(caster, SPELL_IGNEOUS_IMPACT, true);
            caster->CastSpell(caster, SPELL_CAUGHT_IN_THE_BLAST, true);
            caster->CastSpell(caster, SPELL_CRACKED_ARMOR, true);
            crackedArmorUntil[caster->GetGUID()] = uint32(GameTime::GetGameTimeMS().count()) + CRACKED_ARMOR_DURATION_SECONDS * 1000;
            ShatterPillar(pillar);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_basalthane_annihilation_strike::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_basalthane_inferno_trail : public SpellScript
{
    PrepareSpellScript(spell_basalthane_inferno_trail);

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        // Aim direction was captured at cast start (OnAllCreatureUpdate); fall back to
        // the resolved target's current position if it's missing for some reason.
        float dirX = 0.0f, dirY = 0.0f;
        auto itr = infernoTrailDirection.find(caster->GetGUID());
        if (itr != infernoTrailDirection.end())
        {
            dirX = itr->second.first;
            dirY = itr->second.second;
        }
        else
        {
            float dx = target->GetPositionX() - caster->GetPositionX();
            float dy = target->GetPositionY() - caster->GetPositionY();
            float len = std::sqrt(dx * dx + dy * dy);
            if (len > 0.1f)
            {
                dirX = dx / len;
                dirY = dy / len;
            }
        }

        // The swirl-chain telegraph was already shown for the whole cast (cast-start,
        // see OnAllCreatureUpdate). Damage and Flash Burn are dealt manually here, purely
        // in 2D against the actual swirl positions - the native hit-spell's Z-sensitive
        // AoE target selection was missing players due to this room's known vmap gaps.
        // Hit detection walks the same swirl list the telegraph drew (ForEachInfernoTrailSwirl)
        // and checks "is this player within INFERNO_TRAIL_HIT_RADIUS of any swirl", instead
        // of a smooth mathematical cone that could disagree with what's actually on screen.
        float originX = caster->GetPositionX();
        float originY = caster->GetPositionY();
        float originZ = caster->GetPositionZ();
        uint32 dmg = InfernoTrailDamageFor(caster);
        constexpr float hitRadiusSq = INFERNO_TRAIL_HIT_RADIUS * INFERNO_TRAIL_HIT_RADIUS;

        std::vector<Player*> hitPlayers;
        for (auto const& mapItr : caster->GetMap()->GetPlayers())
        {
            Player* player = mapItr.GetSource();
            if (player && player->IsAlive() && caster->IsValidAttackTarget(player))
                hitPlayers.push_back(player);
        }

        // Adjacent swirls in a row deliberately overlap - collect every swirl position
        // first so a player standing in that overlap is only damaged/debuffed once,
        // while the explosion visual still plays at every swirl regardless.
        std::vector<std::pair<float, float>> swirlPositions;
        ForEachInfernoTrailSwirl(originX, originY, dirX, dirY, [&](float px, float py)
        {
            swirlPositions.emplace_back(px, py);
            CastInfernoTrailHitAt(caster, px, py, ResolveGroundZ(caster, px, py, originZ));
        });

        for (Player* player : hitPlayers)
        {
            bool hit = false;
            for (auto const& pos : swirlPositions)
            {
                float dx = player->GetPositionX() - pos.first;
                float dy = player->GetPositionY() - pos.second;
                if (dx * dx + dy * dy <= hitRadiusSq)
                {
                    hit = true;
                    break;
                }
            }
            if (!hit)
                continue;

            if (dmg)
                Unit::DealDamage(caster, player, dmg, nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_FIRE, GetSpellInfo(), false);
            caster->CastSpell(player, FlashBurnSpellFor(caster), true);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_basalthane_inferno_trail::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

class spell_basalthane_eruption : public SpellScript
{
    PrepareSpellScript(spell_basalthane_eruption);

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        std::list<Unit*> nearby;
        Acore::AnyUnitInObjectRangeCheck check(target, ERUPTION_BURST_RADIUS);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(target, nearby, check);
        Cell::VisitObjects(target, searcher, ERUPTION_BURST_RADIUS);

        std::vector<Unit*> hit;
        hit.push_back(target);
        for (Unit* other : nearby)
            if (other != target && other->IsPlayer() && caster->IsValidAttackTarget(other))
                hit.push_back(other);

        uint32 perTarget = EruptionBaseDamageFor(caster) / uint32(hit.size());

        for (Unit* u : hit)
            Unit::DealDamage(caster, u, perTarget, nullptr, SPELL_DIRECT_DAMAGE, SPELL_SCHOOL_MASK_FIRE, GetSpellInfo(), false);

        // The real explosion: native School Damage + Knockback + stacking Fire-vulnerability debuff
        CastEruptionExplosionAt(caster, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());

        // Magma Pool is a real native area aura - cast it at the impact point to leave the ground hazard
        CastMagmaPoolAt(caster, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_basalthane_eruption::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// The real explosion (2105077-80) has a native Knockback effect, but it doesn't
// actually happen on the real server (confirmed against a real kill recording) -
// suppress it here while leaving the damage and debuff effects untouched.
class spell_basalthane_eruption_explosion : public SpellScript
{
    PrepareSpellScript(spell_basalthane_eruption_explosion);

    void PreventKnockback(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_basalthane_eruption_explosion::PreventKnockback, EFFECT_1, SPELL_EFFECT_KNOCK_BACK);
    }
};

// The real Inferno Trail hit spells (2108219-22) carry the actual explosion visual,
// but their School Damage effect is suppressed here - damage is already dealt
// manually in spell_basalthane_inferno_trail::HandleDummy (see CastInfernoTrailHitAt),
// so casting the real spell unmodified would double it. Same trick as
// spell_basalthane_eruption_explosion above, just stripping the other effect type.
class spell_basalthane_inferno_trail_hit_visual_only : public SpellScript
{
    PrepareSpellScript(spell_basalthane_inferno_trail_hit_visual_only);

    void PreventDamage(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_basalthane_inferno_trail_hit_visual_only::PreventDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

class playerscript_basalthane_annihilation_cleanup : public PlayerScript
{
public:
    playerscript_basalthane_annihilation_cleanup()
        : PlayerScript("playerscript_basalthane_annihilation_cleanup",
            { PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_LOGOUT })
    {
    }

    void OnPlayerUpdate(Player* player, uint32 /*p_time*/) override
    {
        if (player->HasAura(SPELL_SMOLDERING_VENGEANCE))
            TickSmolderingVengeance(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        smolderingVengeanceNextTick.erase(player->GetGUID());
    }

private:
    std::unordered_map<ObjectGuid, uint32> smolderingVengeanceNextTick;

    void TickSmolderingVengeance(Player* player)
    {
        uint32 now = uint32(GameTime::GetGameTimeMS().count());
        uint32& nextTick = smolderingVengeanceNextTick[player->GetGUID()];
        if (now < nextTick)
            return;

        nextTick = now + SMOLDERING_VENGEANCE_TICK_MS;

        Creature* boss = player->FindNearestCreature(ENTRY_BASALTHANE_NORMAL, 200.0f);
        uint32 tickSpell = SmolderingVengeanceTickSpellFor(boss ? GetDifficultyEntry(boss) : ENTRY_BASALTHANE_NORMAL);
        if (tickSpell)
            player->CastSpell(player, tickSpell, true);
    }
};

namespace
{
    void ClearAllBasalthaneDebuffs(Creature* boss)
    {
        // Everything here is a real native aura - just strip it from the raid
        for (auto const& itr : boss->GetMap()->GetPlayers())
        {
            if (Player* player = itr.GetSource())
            {
                player->RemoveAurasDueToSpell(SPELL_ANNIHILATION_DEBUFF);
                player->RemoveAurasDueToSpell(SPELL_FLASH_BURN_D0);
                player->RemoveAurasDueToSpell(SPELL_FLASH_BURN_D1);
                player->RemoveAurasDueToSpell(SPELL_FLASH_BURN_D2);
                player->RemoveAurasDueToSpell(SPELL_FLASH_BURN_D3);
                player->RemoveAurasDueToSpell(SPELL_SMOLDERING_VENGEANCE);
            }
        }

        boss->RemoveDynObject(SPELL_MAGMA_POOL_D0);
        boss->RemoveDynObject(SPELL_MAGMA_POOL_D1);
        boss->RemoveDynObject(SPELL_MAGMA_POOL_D2);
        boss->RemoveDynObject(SPELL_MAGMA_POOL_D3);
        infernoTrailDirection.erase(boss->GetGUID());

        // Cracked Armor is self-applied to the boss, not players - strip it here too.
        boss->RemoveAurasDueToSpell(SPELL_CRACKED_ARMOR);
        crackedArmorUntil.erase(boss->GetGUID());

        // Pillars only come back on wipe/evade, NOT on a kill - a dead Basalthane keeps
        // his shattered pillars shattered (confirmed 2026-09-23). Debuffs above still
        // get stripped from players either way, this is just the pillar-restore part.
        if (boss->IsAlive())
            RestoreAllPillars(boss);
    }
}

// Cast-start detection (SmartAI's own script hooks only fire after the cast bar
// finishes, so this polls instead): captures Inferno Trail's aim direction the instant
// it starts casting, fires an early ground telegraph when Eruption starts casting, and
// sweeps up every lingering debuff once the pull is over (kill, wipe, or Basalthane
// evading) - Mythic/Ascended Magma Pools don't expire on their own, and the hand-tracked
// Annihilation Strike stacks have no native aura duration to fall back on either. Pillars
// specifically are excluded from the kill case, see ClearAllBasalthaneDebuffs.
class allcreaturescript_basalthane_cleanup : public AllCreatureScript
{
public:
    allcreaturescript_basalthane_cleanup() : AllCreatureScript("allcreaturescript_basalthane_cleanup") { }

    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        if (creature->GetEntry() != 10185)
            return;

        if (!creature->IsInCombat())
        {
            ClearAllBasalthaneDebuffs(creature);
            flashBurnOpenerApplied.erase(creature->GetGUID());
            flashBurnNextTick.erase(creature->GetGUID());
            smolderingVengeanceOpenerApplied.erase(creature->GetGUID());
            infernoTrailNextCast.erase(creature->GetGUID());
            moltenBloodNextSpawn.erase(creature->GetGUID());
            return;
        }

        // Molten Blood ooze spawning (see MOLTEN_BLOOD_SPAWN_INTERVAL_MS comment) - picks
        // among the 3 points currently farthest from Basalthane every time, since he
        // moves around the room and isn't tanked in the middle.
        {
            uint32& nextMolten = moltenBloodNextSpawn[creature->GetGUID()];
            if (nextMolten == 0)
                nextMolten = uint32(GameTime::GetGameTimeMS().count()) + urand(MOLTEN_BLOOD_SPAWN_INTERVAL_MIN_MS, MOLTEN_BLOOD_SPAWN_INTERVAL_MAX_MS);
            else if (uint32(GameTime::GetGameTimeMS().count()) >= nextMolten)
            {
                SpawnMoltenBloodAtFarthestPoint(creature);
                nextMolten = uint32(GameTime::GetGameTimeMS().count()) + urand(MOLTEN_BLOOD_SPAWN_INTERVAL_MIN_MS, MOLTEN_BLOOD_SPAWN_INTERVAL_MAX_MS);
            }
        }

        // Inferno Trail scheduling (see INFERNO_TRAIL_CAST_INTERVAL_MS comment) - real
        // priority: Annihilation Strike and Eruption are still SmartAI-timed and cast
        // straight onto CURRENT_GENERIC_SPELL like Inferno Trail does, so "is the boss
        // already mid-cast on anything" is an honest proxy for "is one of the two
        // higher-priority casts happening right now" (nothing else shares that slot).
        {
            uint32& nextInferno = infernoTrailNextCast[creature->GetGUID()];
            if (nextInferno == 0)
                nextInferno = uint32(GameTime::GetGameTimeMS().count()) + INFERNO_TRAIL_CAST_INTERVAL_MS;
            else if (uint32(GameTime::GetGameTimeMS().count()) >= nextInferno)
            {
                if (creature->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                {
                    // Annihilation Strike or Eruption is casting right now - skip this
                    // cycle entirely (matches the ~28s gaps seen in real kill logs).
                    nextInferno = uint32(GameTime::GetGameTimeMS().count()) + INFERNO_TRAIL_CAST_INTERVAL_MS;
                }
                else
                {
                    std::vector<Player*> players;
                    for (auto const& itr : creature->GetMap()->GetPlayers())
                        if (Player* player = itr.GetSource())
                            players.push_back(player);

                    if (!players.empty())
                        creature->CastSpell(Acore::Containers::SelectRandomContainerElement(players), SPELL_INFERNO_TRAIL, false);

                    nextInferno = uint32(GameTime::GetGameTimeMS().count()) + INFERNO_TRAIL_CAST_INTERVAL_MS;
                }
            }
        }

        // Smoldering Vengeance opener: thrown on the off-tank at pull (not from the
        // ooze - see the constant comment above).
        //
        // TEMP TEST MODE (2026-09-24): applying to the PULLING TANK (creature->GetVictim())
        // instead of the off-tank while the user solo/small-group tests the boss - FindOffTank()
        // needs two distinct player tanks on the threat list to ever resolve, which won't
        // happen with a small test group. FindOffTank() is left intact below, unused for now.
        // SWITCH BACK to FindOffTank(creature) once the boss is done being scripted/tested -
        // the user explicitly asked for this to be temporary, don't forget to revert it.
        if (smolderingVengeanceOpenerApplied.find(creature->GetGUID()) == smolderingVengeanceOpenerApplied.end())
        {
            if (Player* pullingTank = creature->GetVictim() ? creature->GetVictim()->ToPlayer() : nullptr)
            {
                creature->CastSpell(pullingTank, SPELL_SMOLDERING_VENGEANCE, true);
                smolderingVengeanceOpenerApplied.insert(creature->GetGUID());
            }
        }

        // Opening Flash Burn: applied raid-wide the moment the pull starts, matching
        // real kill logs where every raid member got it at the same instant Basalthane
        // entered combat. Inferno Trail's own Flash Burn application (further down in
        // this file) is untouched - this is a separate mechanism.
        uint32 nowMs = uint32(GameTime::GetGameTimeMS().count());
        if (flashBurnOpenerApplied.insert(creature->GetGUID()).second)
        {
            for (auto const& itr : creature->GetMap()->GetPlayers())
                if (Player* player = itr.GetSource())
                    creature->CastSpell(player, FlashBurnSpellFor(creature), true);
            flashBurnNextTick[creature->GetGUID()] = nowMs + FLASH_BURN_RAIDWIDE_TICK_MS;
        }

        // Periodic raid-wide Flash Burn refresh (see FLASH_BURN_RAIDWIDE_TICK_MS comment) -
        // stacks onto whatever's already there for players still afflicted from the opener.
        uint32& nextFlashBurnTick = flashBurnNextTick[creature->GetGUID()];
        if (nowMs >= nextFlashBurnTick)
        {
            for (auto const& itr : creature->GetMap()->GetPlayers())
                if (Player* player = itr.GetSource())
                    creature->CastSpell(player, FlashBurnSpellFor(creature), true);
            nextFlashBurnTick = nowMs + FLASH_BURN_RAIDWIDE_TICK_MS;
        }

        // Force-expire Cracked Armor after CRACKED_ARMOR_DURATION_SECONDS - see the
        // comment on crackedArmorUntil for why this isn't left to the spell's own duration.
        auto crackedItr = crackedArmorUntil.find(creature->GetGUID());
        if (crackedItr != crackedArmorUntil.end() && uint32(GameTime::GetGameTimeMS().count()) >= crackedItr->second)
        {
            creature->RemoveAurasDueToSpell(SPELL_CRACKED_ARMOR);
            crackedArmorUntil.erase(crackedItr);
        }

        Spell* current = creature->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        Spell*& last = lastGenericSpell[creature->GetGUID()];
        if (current == last)
            return;

        last = current;
        if (!current)
            return;

        Unit* aimTarget = current->m_targets.GetUnitTarget();
        if (!aimTarget)
            return;

        uint32 spellId = current->GetSpellInfo()->Id;
        if (spellId == SPELL_INFERNO_TRAIL)
        {
            float dx = aimTarget->GetPositionX() - creature->GetPositionX();
            float dy = aimTarget->GetPositionY() - creature->GetPositionY();
            float len = std::sqrt(dx * dx + dy * dy);
            float dirX = 0.0f, dirY = 0.0f;
            if (len > 0.1f)
            {
                dirX = dx / len;
                dirY = dy / len;
                infernoTrailDirection[creature->GetGUID()] = { dirX, dirY };
            }

            // Show the full swirl-chain telegraph for the whole 2.5s cast, not just at
            // resolution - the raid needs to see it while there's still time to dodge.
            // A real cone: each row starts as 2 overlapping swirls right at the boss,
            // gaining one more swirl every INFERNO_TRAIL_GROWTH_STEP_DIST yards, spaced
            // INFERNO_TRAIL_SWIRL_OVERLAP_GAP apart so neighbors in the same row still
            // overlap - keeps the cone reading as one connected shape instead of gapping
            // out as it grows wider.
            float originX = creature->GetPositionX();
            float originY = creature->GetPositionY();
            float z = creature->GetPositionZ();
            ForEachInfernoTrailSwirl(originX, originY, dirX, dirY, [&](float px, float py)
            {
                CastInfernoTrailSwirlAt(creature, px, py, ResolveGroundZ(creature, px, py, z));
            });
        }
        else if (spellId == SPELL_ERUPTION)
        {
            creature->CastSpell(aimTarget, SPELL_ERUPTION_PRE, true);
        }
    }

private:
    std::unordered_map<ObjectGuid, Spell*> lastGenericSpell;
};

// Manually drives the ooze toward Basalthane in pure 2D, bypassing native MoveFollow.
// CONFIRMED 2026-09-24: MoveFollow + CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING on this
// room's uneven/broken mesh makes the ooze launch into the air mid-walk - the same
// class of problem Inferno Trail's damage check was rewritten to avoid. This never
// calls GetHeight/pathfinding at all - it uses Basalthane's own live Z (already a
// valid position, since he functions fine in this room) as the ooze's target Z, and
// just relocates it a short step closer every tick. Stops at OOZE_FOLLOW_STOP_DIST,
// matching the original "hold at 3yd" design (rows 2/3 on the ooze's own SmartAI still
// handle the "become aggressive within 4yd" part independently via their own distance
// checks, untouched by this).
constexpr float OOZE_FOLLOW_STOP_DIST = 3.0f;
constexpr float OOZE_FOLLOW_SPEED = 2.5f; // yd/s, roughly a normal walk speed

constexpr uint32 OOZE_FOLLOW_TICK_MS = 200; // throttle - see comment below

class allcreaturescript_basalthane_ooze_movement : public AllCreatureScript
{
public:
    allcreaturescript_basalthane_ooze_movement() : AllCreatureScript("allcreaturescript_basalthane_ooze_movement") { }

    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        if (creature->GetEntry() != ENTRY_MOLTEN_BLOOD_OOZE || !creature->IsAlive())
            return;

        // Throttled to every OOZE_FOLLOW_TICK_MS instead of every world tick -
        // NearTeleportTo is a real teleport call (grid/visibility recalculation each
        // time), not a smooth-movement API, so calling it dozens of times a second was
        // likely spamming teleports hard enough to cause a visibility/network glitch or
        // trip some anti-spam safety net that made the ooze disappear within seconds.
        uint32 now = uint32(GameTime::GetGameTimeMS().count());
        uint32& nextMove = oozeNextMoveTick[creature->GetGUID()];
        if (now < nextMove)
            return;
        nextMove = now + OOZE_FOLLOW_TICK_MS;

        Creature* boss = creature->FindNearestCreature(ENTRY_BASALTHANE_NORMAL, 200.0f);
        if (!boss)
            return;

        // Z is interpolated from a REMEMBERED spawn position/distance, never fed back
        // from the ooze's own live Z - feeding live Z into itself each tick (previous
        // version) let it drift ("flyver op i luften") if NearTeleportTo's own internal
        // position handling nudges Z at all between calls. This way Z is a pure function
        // of progress-along-the-path, immune to any such per-tick drift.
        auto& spawn = oozeSpawnInfo[creature->GetGUID()];
        if (spawn.initialDist == 0.0f)
        {
            float sdx = boss->GetPositionX() - creature->GetPositionX();
            float sdy = boss->GetPositionY() - creature->GetPositionY();
            spawn.z = creature->GetPositionZ();
            spawn.initialDist = std::sqrt(sdx * sdx + sdy * sdy);
            if (spawn.initialDist <= 0.0f)
                spawn.initialDist = 0.01f; // avoid div-by-zero if spawned exactly on the boss
        }

        float dx = boss->GetPositionX() - creature->GetPositionX();
        float dy = boss->GetPositionY() - creature->GetPositionY();
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= OOZE_FOLLOW_STOP_DIST)
            return;

        float step = OOZE_FOLLOW_SPEED * (float(OOZE_FOLLOW_TICK_MS) / 1000.0f);
        float remaining = dist - OOZE_FOLLOW_STOP_DIST;
        if (step > remaining)
            step = remaining;

        float nx = creature->GetPositionX() + (dx / dist) * step;
        float ny = creature->GetPositionY() + (dy / dist) * step;
        float facing = std::atan2(dy, dx);

        float progress = std::clamp(1.0f - (dist - step) / spawn.initialDist, 0.0f, 1.0f);
        float nz = spawn.z + (boss->GetPositionZ() - spawn.z) * progress;

        creature->NearTeleportTo(nx, ny, nz, facing);
    }

private:
    std::unordered_map<ObjectGuid, uint32> oozeNextMoveTick;

    struct OozeSpawnInfo { float z = 0.0f; float initialDist = 0.0f; };
    std::unordered_map<ObjectGuid, OozeSpawnInfo> oozeSpawnInfo;
};

void AddSC_spell_basalthane()
{
    RegisterSpellScript(spell_basalthane_annihilation_strike);
    RegisterSpellScript(spell_basalthane_inferno_trail);
    RegisterSpellScript(spell_basalthane_eruption);
    RegisterSpellScript(spell_basalthane_eruption_explosion);
    RegisterSpellScript(spell_basalthane_inferno_trail_hit_visual_only);
    new playerscript_basalthane_annihilation_cleanup();
    new allcreaturescript_basalthane_cleanup();
    // Native SmartAI MoveFollow (smart_scripts id=1 for entry 310189) was re-tested
    // 2026-09-24 after fixing the real despawn bug (id=16) and STILL launched the ooze
    // into the air - confirmed independent issue, not a symptom of the despawn bug.
    // Back to this manual movement permanently unless a real terrain/mmap fix happens.
    new allcreaturescript_basalthane_ooze_movement();
}
