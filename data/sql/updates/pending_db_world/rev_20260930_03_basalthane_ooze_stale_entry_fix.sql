-- Molten Blood ooze (310189) SmartAI rows still reference boss entry 10185 --
-- stale since rev_20260925_34 renamed Basalthane to 10189-10192. Entry 10185
-- no longer exists, so these have been dead/broken since the rename:
--   id=1 (native SMART_ACTION_FOLLOW): target_type=19 (SMART_TARGET_CLOSEST_CREATURE)
--        target_param1=10185 -- can never resolve a creature to follow, so the
--        ooze never moves at all once native follow is the active movement path
--        (as opposed to the custom C++ movement class, which doesn't use this
--        row and so never surfaced this bug while it was the active path).
--   id=2/3 (become-aggressive / stop-following near boss): event_param2/
--        target_param1=10185 -- SMART_EVENT_DISTANCE_CREATURE never finds a
--        match, so these never fire.

UPDATE `smart_scripts` SET `action_param3` = 10189, `target_param1` = 10189
WHERE `entryorguid` = 310189 AND `source_type` = 0 AND `id` = 1 AND `target_param1` = 10185;

UPDATE `smart_scripts` SET `event_param2` = 10189, `target_param1` = 10189
WHERE `entryorguid` = 310189 AND `source_type` = 0 AND `id` IN (2,3) AND `event_param2` = 10185;
