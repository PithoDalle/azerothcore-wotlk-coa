-- Mythic and Ascended are 25-man-locked difficulties for Basalthane, not
-- dynamic flex like Normal/Heroic: total HP is always per-player x 25,
-- regardless of how many players actually show up. Normal/Heroic keep
-- flexing dynamically with the instance's actual headcount (clamped 10..25).
--
-- FlexHealth.cpp (modules/mod-coa-raid-difficulty) was extended to support
-- this: a NEGATIVE hp_dN value now means "always multiply by
-- FLEX_MAX_PLAYERS (25)" instead of the instance's actual player count.
-- Positive values behave exactly as before (dynamic 10-25), so no other
-- boss already using this table is affected.

ALTER TABLE `coa_boss_flex`
    MODIFY `hp_d0` INT NOT NULL DEFAULT 0 COMMENT 'health per player, Normal; 0 no flex',
    MODIFY `hp_d1` INT NOT NULL DEFAULT 0 COMMENT 'Heroic',
    MODIFY `hp_d2` INT NOT NULL DEFAULT 0 COMMENT 'Mythic; negative = 25-man-locked (always x25)',
    MODIFY `hp_d3` INT NOT NULL DEFAULT 0 COMMENT 'Ascended; negative = 25-man-locked (always x25)';

UPDATE `coa_boss_flex` SET `hp_d2` = -2878908,
    `comment` = 'Basalthane Mythic: measured, 26.08 kill, 19 players, 54,699,256 total -- 25-man-locked, always x25 regardless of actual headcount'
WHERE `entry` = 10191;

UPDATE `coa_boss_flex` SET `hp_d3` = -4240248,
    `comment` = 'Basalthane Ascended: not measured, extrapolated at Heroic x2.19 (MC pattern) -- 25-man-locked, always x25 regardless of actual headcount'
WHERE `entry` = 10192;
