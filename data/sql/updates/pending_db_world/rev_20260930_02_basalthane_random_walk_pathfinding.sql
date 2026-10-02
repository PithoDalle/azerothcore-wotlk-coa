-- Basalthane (guid 9500000): 8 yard random walk
UPDATE `creature` SET `MovementType` = 1, `wander_distance` = 8 WHERE `guid` = 9500000;

-- Turn off the CREATURE_FLAG_EXTRA_IGNORE_PATHFINDING fix (0x20000000) that was
-- applied to all 4 Basalthane difficulty templates for this broken-mesh room.
UPDATE `creature_template` SET `flags_extra` = `flags_extra` & ~0x20000000
WHERE `entry` IN (10189, 10190, 10191, 10192);
