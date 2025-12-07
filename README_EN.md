# mod-real-online

### 🇬🇧 [English version](README_EN.md)

## Description (EN)  
- Characters on the same account and in the same faction share a common bank. The faction restriction can optionally be enabled/disabled in the configuration.  
- Gossip shows a list of all items in the bags.  
- Integration with professions.  
- Command to summon a temporary banker at the player’s position. Configurable command cooldown and the time after which the banker despawns.  

---

### Installation / Requirements  
The module includes an autoupdater, so there is no need to import .sql files manually.  
For the autoupdater to work correctly, make sure that the database user from `(WorldDatabaseInfo) – "127.0.0.1;3306;acore;acore;acore_world"`  
also has permissions on the new `customs` database:


```
GRANT CREATE ON *.* TO 'acore'@'127.0.0.1';
GRANT ALL PRIVILEGES ON customs.* TO 'acore'@'127.0.0.1';
FLUSH PRIVILEGES;
```  


**Optional:**  
- Add this line to worldserver.conf:  
  Logger.gv.customs=3,Console Server
  
---

### Commands
.bank  
➝ Summons a temporary banker at the player’s position  


