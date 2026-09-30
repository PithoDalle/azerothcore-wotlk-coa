-- Basalthane flex health (Onyxia's Lair, entries 10185-10188).
--
-- Follows the coa_boss_flex pattern from mod-coa-raid-difficulty
-- (modules/mod-coa-raid-difficulty/data/sql/db-world/base/04_boss_flex.sql):
-- health per player per difficulty, multiplied by the players in the
-- instance on pull (clamped 10..25).
--
-- Basalthane's four difficulty entries do NOT follow the base+100000/
-- +200000/+300000 convention the classic raid bosses use, so FlexHealth's
-- `entry % 100000` base-entry lookup cannot collapse them into one row.
-- Each entry gets its own row instead, with only its own hp_dN column set
-- (the other three stay 0, meaning "no flex" for that difficulty on that
-- row -- irrelevant, since that difficulty never spawns that entry).
--
-- Per-player values (CORRECTED 2026-09-30, real combat logs found -- see below).
--
-- The original values here (421/561/845/1232) were derived from
-- HealthModifier (5355/11228/13465/15701) used as if it were the raid's
-- actual total HP anchor -- it isn't; HealthModifier is a template scalar,
-- not a measured total. Four real Basalthane kills across Normal/Heroic/
-- Mythic gave actual per-player totals ~2800x higher than that guess:
--   Normal  (24.08, 19 players): 22,343,440 total -> 1,175,972 / player
--   Heroic  (24.08, 20 players): 40,244,223 total -> 2,012,211 / player
--   Heroic  (25.08, 13 players): 24,182,108 total -> 1,860,162 / player
--   Mythic  (26.08, 19 players): 54,699,256 total -> 2,878,908 / player
-- Heroic here is the mean of its two kills (1,936,186). Mythic/Heroic
-- measures at 1.49, matching the classic-raid pattern's 1.505 almost
-- exactly -- confirms Mythic flexes per-player just like Normal/Heroic,
-- not a flat total regardless of player count. Ascended has no logged kill;
-- extrapolated the same way the classic raids' ungled difficulties are
-- (Heroic x2.19, same MC pattern) -> 4,240,248 / player.
-- Player counts are "everyone who hit him", a lower bound (~7% error),
-- same caveat as the classic-raid data this pattern comes from.

CREATE TABLE IF NOT EXISTS `coa_boss_flex` (
  `entry`    INT UNSIGNED NOT NULL COMMENT 'base creature entry',
  `hp_d0`    INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'health per player, Normal; 0 no flex',
  `hp_d1`    INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Heroic',
  `hp_d2`    INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Mythic',
  `hp_d3`    INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'Ascended',
  `comment`  VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`entry`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb4;

DELETE FROM `coa_boss_flex` WHERE `entry` IN (10185,10186,10187,10188);
INSERT INTO `coa_boss_flex` VALUES (10185, 1175972, 0, 0, 0, 'Basalthane Normal: measured, 24.08 kill, 19 players, 22,343,440 total');
INSERT INTO `coa_boss_flex` VALUES (10186, 0, 1936186, 0, 0, 'Basalthane Heroic: measured, mean of 2 kills (2,012,211 and 1,860,162 per player)');
INSERT INTO `coa_boss_flex` VALUES (10187, 0, 0, 2878908, 0, 'Basalthane Mythic: measured, 26.08 kill, 19 players, 54,699,256 total -- per-player like the others, follows MC flex pattern');
INSERT INTO `coa_boss_flex` VALUES (10188, 0, 0, 0, 4240248, 'Basalthane Ascended: not measured, extrapolated at Heroic x2.19 (MC pattern)');
