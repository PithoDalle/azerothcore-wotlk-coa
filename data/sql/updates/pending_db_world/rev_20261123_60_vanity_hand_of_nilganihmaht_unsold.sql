-- Read whether the realm prices its store before the DELETE: a removed row would fall back to the DBC price.
SET @vanity_priced := EXISTS (SELECT 1 FROM `coa_client_vanity_collection` WHERE `BazaarTokenCost` > 0);
DELETE FROM `coa_client_vanity_collection` WHERE `ItemID` = 1777039;
INSERT INTO `coa_client_vanity_collection` (`ItemID`, `DonationPointCost`, `SeasonalPointCost`, `BazaarTokenCost`)
SELECT `n`.`ItemID`, 0, 0, 0
FROM (VALUES ROW(1777039)) `n` (`ItemID`)
WHERE @vanity_priced = 1;
