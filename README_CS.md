# mod-material-bank

### 🇬🇧 [English version](README_EN.md)

## Popis (CZ)  
- Postavy na stejném účtu a ve stejné frakci vidí společnou banku. Volitelně lze frakční omezení vypnout/zapnout v konfiguraci.  
- Gossip zobrazí seznam všech předmětů v bagách.  
- Integrace s profesemi.  
- Příkaz na přivolání dočasného bankéře na pozici hráče. Nastavitelný cooldown pro příkaz a čas, kdy se bankéř despawne.  

---

### Instalace / Požadavky  
Modul obsahuje autoupdater, tudíž není potřeba ručně importovat .sql.  
Pro správnou funkčnost autoupdateru je nutné zajistit, aby uživatel databáze z `(WorldDatabaseInfo) – "127.0.0.1;3306;acore;acore;acore_world"`  
měl práva i na novou databázi `customs`:


```
GRANT CREATE ON *.* TO 'acore'@'127.0.0.1';
GRANT ALL PRIVILEGES ON customs.* TO 'acore'@'127.0.0.1';
FLUSH PRIVILEGES;
```  

**Volitelné:**
- Přidej do worldserver.conf tento řádek:  
  Logger.gv.customs=3,Console Server
  
---

### Příkazy
.bank  
➝ Přivolá dočasného bankéře na pozici hráče
