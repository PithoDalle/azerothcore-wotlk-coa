-- Renumber Basalthane's 4 difficulty templates off entries 10185-10188.
--
-- User discovered (2026-09-25, from an Ascension client cache lookup) that
-- 10186/10187/10188 are the REAL entries for this encounter's pillar
-- creatures -- "Volatile Pillar" / "Crumbling Pillar" / "Searing Pillar",
-- all modelid1 200003 -- and Basalthane's own difficulty_entry_1/2/3
-- (assigned back on 2026-09-24, before this was known) happened to land
-- squarely on top of them.
--
-- Basalthane moves to 10189-10192 (right after the real pillar IDs, as
-- requested), freeing 10186-10188 for the real pillar creatures. The C++
-- side (ENTRY_BASALTHANE_* constants in spell_basalthane.cpp) was updated
-- to match in the same commit as this file -- requires a worldserver
-- rebuild before this migration and the binary agree with each other.
--
-- Mapping: 10185->10189 (Normal), 10186->10190 (Heroic),
--          10187->10191 (Mythic), 10188->10192 (Ascended)

UPDATE `creature_template` SET
    `entry` = CASE `entry` WHEN 10185 THEN 10189 WHEN 10186 THEN 10190 WHEN 10187 THEN 10191 WHEN 10188 THEN 10192 END,
    `difficulty_entry_1` = CASE `difficulty_entry_1` WHEN 10186 THEN 10190 ELSE `difficulty_entry_1` END,
    `difficulty_entry_2` = CASE `difficulty_entry_2` WHEN 10187 THEN 10191 ELSE `difficulty_entry_2` END,
    `difficulty_entry_3` = CASE `difficulty_entry_3` WHEN 10188 THEN 10192 ELSE `difficulty_entry_3` END
WHERE `entry` IN (10185,10186,10187,10188);

UPDATE `creature_template_model` SET
    `CreatureID` = CASE `CreatureID` WHEN 10185 THEN 10189 WHEN 10186 THEN 10190 WHEN 10187 THEN 10191 WHEN 10188 THEN 10192 END
WHERE `CreatureID` IN (10185,10186,10187,10188);

UPDATE `creature_template_movement` SET
    `CreatureId` = CASE `CreatureId` WHEN 10185 THEN 10189 WHEN 10186 THEN 10190 WHEN 10187 THEN 10191 WHEN 10188 THEN 10192 END
WHERE `CreatureId` IN (10185,10186,10187,10188);

UPDATE `smart_scripts` SET
    `entryorguid` = CASE `entryorguid` WHEN 10185 THEN 10189 WHEN 10186 THEN 10190 WHEN 10187 THEN 10191 WHEN 10188 THEN 10192 END
WHERE `entryorguid` IN (10185,10186,10187,10188) AND `source_type` = 0;

UPDATE `coa_boss_flex` SET
    `entry` = CASE `entry` WHEN 10185 THEN 10189 WHEN 10186 THEN 10190 WHEN 10187 THEN 10191 WHEN 10188 THEN 10192 END
WHERE `entry` IN (10185,10186,10187,10188);

UPDATE `creature` SET `id` = 10189 WHERE `guid` = 9650000 AND `id` = 10185;

-- The real pillar creatures (from Ascension client data, 2026-09-25).
-- Templates only for now -- not spawned, and the boss's pillar-shatter
-- mechanic (currently gameobject-based, entry 9500100) is NOT wired to
-- these yet. Follow-up work, not part of this rename.
DELETE FROM `creature_template` WHERE `entry` IN (10186,10187,10188);
INSERT INTO `creature_template`
    (`entry`, `name`, `minlevel`, `maxlevel`, `faction`, `speed_walk`, `speed_run`,
     `rank`, `unit_class`, `type`, `HealthModifier`, `ManaModifier`,
     `ArmorModifier`, `ExperienceModifier`, `RegenHealth`, `AIName`, `ScriptName`)
VALUES
    (10186, 'Volatile Pillar',  63, 63, 14, 1, 1.14286, 0, 1, 4, 1, 1, 1, 1, 1, '', ''),
    (10187, 'Crumbling Pillar', 63, 63, 14, 1, 1.14286, 0, 1, 4, 1, 1, 1, 1, 1, '', ''),
    (10188, 'Searing Pillar',   63, 63, 14, 1, 1.14286, 0, 1, 4, 1, 1, 1, 1, 1, '', '');

INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`)
VALUES (10186, 0, 200003, 1, 1), (10187, 0, 200003, 1, 1), (10188, 0, 200003, 1, 1);
