-- Final movement decision for the Molten Blood ooze (310189), 2026-09-30:
-- the custom C++ manual movement class (allcreaturescript_basalthane_ooze_movement,
-- NearTeleportTo every 200ms) visibly lags even after this room's terrain was
-- fixed by fresh vmaps/mmaps. Native SmartAI MoveFollow (id=1, now pointed at
-- the correct 10189 entry per rev_20260930_03) moves smoothly, but has its own
-- real problem: AzerothCore's FollowMovementGenerator applies a catch-up/
-- acceleration boost whenever the follower falls far behind its target, which
-- made the ooze move far too fast. Rather than avoid native follow entirely,
-- countered the catch-up boost by slowing the ooze's own base speed down:
-- first pass -40% (1 -> 0.4, 1.14286 -> 0.457144), then a further -10% on top
-- after live testing confirmed movement was smooth but still slightly too
-- fast (-> 0.36, 0.4114296 net, ~64% of original). Re-enabled the native
-- follow row that AddSC_spell_basalthane's custom movement class replaces
-- (event_chance back to 100) to match -- see the C++ side comment in
-- spell_basalthane.cpp's AddSC_spell_basalthane() for the disabled
-- registration line.

UPDATE `smart_scripts` SET `event_chance` = 100
WHERE `entryorguid` = 310189 AND `source_type` = 0 AND `id` = 1;

UPDATE `creature_template` SET `speed_walk` = 0.36, `speed_run` = 0.4114296
WHERE `entry` = 310189;
