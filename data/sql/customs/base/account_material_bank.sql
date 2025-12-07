-- Exportování struktury pro tabulka customs.account_material_bank
CREATE TABLE IF NOT EXISTS `customs`.`account_material_bank` (
  `accountId` int unsigned NOT NULL,
  `team` tinyint unsigned NOT NULL DEFAULT '0',
  `itemEntry` int unsigned NOT NULL,
  `totalCount` bigint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`accountId`,`team`,`itemEntry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
