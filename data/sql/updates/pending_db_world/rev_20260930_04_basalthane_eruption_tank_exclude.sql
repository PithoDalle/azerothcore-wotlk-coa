-- Eruption (id=2, Normal/Heroic/Mythic) used target_type=5
-- (SMART_TARGET_HOSTILE_RANDOM -- "just any random target on our threat
-- list"), which can select the main tank purely by chance since it never
-- excludes anyone. Switched to target_type=6 (SMART_TARGET_HOSTILE_RANDOM_NOT_TOP)
-- so it excludes the current top-threat unit (the tank) from the random pick.
--
-- id=22 (the Ascended/D3 override, added 2026-09-24) never had its targeting
-- set at all (target_type=0/SMART_TARGET_NONE) -- a separate oversight from
-- when this row was created, independent of the tank-exclusion fix above.
-- Given the same target_type=6 + target_param2=1 (playerOnly) as the other
-- three difficulties.

UPDATE `smart_scripts` SET `target_type` = 6
WHERE `entryorguid` = 10189 AND `source_type` = 0 AND `id` = 2 AND `action_param1` = 2108227 AND `target_type` = 5;

UPDATE `smart_scripts` SET `target_type` = 6, `target_param1` = 0, `target_param2` = 1
WHERE `entryorguid` = 10189 AND `source_type` = 0 AND `id` = 22 AND `action_param1` = 2108227;
