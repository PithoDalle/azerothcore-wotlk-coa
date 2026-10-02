-- Accursed Judge (161708): make hostile (attacks nearby players) + random walk 5 yards
UPDATE `creature_template` SET `faction` = 16 WHERE `entry` = 161708;

UPDATE `creature` SET `MovementType` = 1, `wander_distance` = 5
WHERE `id` = 161708 AND `guid` NOT IN (9780555, 9780495, 9780492, 9780484, 9780489);

UPDATE `creature` SET `MovementType` = 1, `wander_distance` = 3
WHERE `guid` IN (9780492, 9780484, 9780489);

-- Accursed Censor (161712) guid 9780418: switch to waypoint movement
UPDATE `creature` SET `MovementType` = 2, `wander_distance` = 0
WHERE `guid` = 9780418;

-- Spawn Z (60) didn't match the waypoint path's Z (~53.7), leaving it
-- floating above its own route and unable to move. Fixed to match.
UPDATE `creature` SET `position_z` = 53.7255 WHERE `guid` = 9780418;

-- WAYPOINT_MOTION_TYPE alone does not auto-use waypoint_data.id == guid;
-- AzerothCore requires an explicit creature_addon.path_id (Creature.cpp
-- only sets m_path_id from cainfo->path_id, default is 0). Without this row
-- the log showed "doesn't have waypoint path id: 0" every time it tried to move.
INSERT IGNORE INTO `creature_addon` (`guid`, `path_id`) VALUES (9780418, 9780418);
