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
-- Per-player values: current total HealthModifier (5355/11228/13465/15701,
-- confirmed live 2026-09-25) has no combat-log baseline like the classic
-- raids do, so Heroic (11228) is taken as the 20-player anchor and the
-- other three follow upstream's measured ratio pattern:
--   Normal = Heroic x0.75, Mythic = Heroic x1.505, Ascended = Heroic x2.195
-- 11228 / 20 = 561.4 per player on Heroic.

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
INSERT INTO `coa_boss_flex` VALUES (10185, 421, 0, 0, 0, 'Basalthane Normal: Heroic x0.750 (pattern), anchor 11228 total / 20 players');
INSERT INTO `coa_boss_flex` VALUES (10186, 0, 561, 0, 0, 'Basalthane Heroic: 11228 total / 20 players (anchor)');
INSERT INTO `coa_boss_flex` VALUES (10187, 0, 0, 845, 0, 'Basalthane Mythic: Heroic x1.505 (pattern)');
INSERT INTO `coa_boss_flex` VALUES (10188, 0, 0, 0, 1232, 'Basalthane Ascended: Heroic x2.195 (pattern)');
