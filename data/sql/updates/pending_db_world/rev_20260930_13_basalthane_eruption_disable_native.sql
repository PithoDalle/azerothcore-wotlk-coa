-- Eruption is now scheduled entirely from C++ (see spell_basalthane.cpp's
-- eruptionNextCast/ERUPTION_INITIAL_CAST_MS) so it can exclude both the tank
-- AND the off-tank -- native SmartAI's target_type=6
-- (SMART_TARGET_HOSTILE_RANDOM_NOT_TOP) only excludes the single top-threat
-- unit, there's no native target type that excludes a second, separately-
-- tracked off-tank too. Disabled the native rows (id=2, id=22) so they don't
-- also fire and double-cast Eruption alongside the C++ path.

UPDATE `smart_scripts` SET `event_chance` = 0
WHERE `entryorguid` = 10189 AND `source_type` = 0 AND `action_param1` = 2108227;
