-- Local: Chromie (our NPC, entry 11990001) runs upstream's Destiny Weaver menu
-- (experience bonuses + open world scaling), replacing the old npc_chromie script.
-- Upstream's Destiny Weaver NPCs are not spawned in the world; only one, Galrin Olemar
-- (449357), stands on GM Island. Chromie covers the capitals and start zones.
UPDATE `creature_template` SET `ScriptName` = 'npc_destiny_weaver', `gossip_menu_id` = 0 WHERE `entry` = 11990001;

DELETE FROM `creature` WHERE `id` IN
    (SELECT `entry` FROM (SELECT `entry` FROM `creature_template`
                          WHERE `ScriptName` = 'npc_destiny_weaver' AND `entry` NOT IN (11990001, 449357)) AS weavers);

DELETE FROM `creature` WHERE `id` = 449357 AND `guid` NOT IN
    (SELECT `guid` FROM (SELECT MIN(`guid`) AS `guid` FROM `creature` WHERE `id` = 449357) AS one);
UPDATE `creature` SET `map` = 1, `zoneId` = 0, `areaId` = 0, `position_x` = 16222.1, `position_y` = 16252.1,
    `position_z` = 12.5872, `orientation` = 3.14, `Comment` = 'Destiny Weaver: Galrin Olemar (GM Island)'
WHERE `id` = 449357;

DELETE FROM `gossip_menu` WHERE `MenuID` = 790260;
DELETE FROM `npc_text` WHERE `ID` = 790260;
