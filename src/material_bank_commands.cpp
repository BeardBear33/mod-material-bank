#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "WorldSession.h"
#include "Config.h"
#include "ObjectMgr.h"
#include "ItemTemplate.h"
#include "Creature.h"
#include "GameTime.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Bag.h"

#include "material_bank.h"

#include <cstdlib>
#include <string>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

using namespace Acore::ChatCommands;

namespace
{
    // === Locale handling (cs/en) ===
    enum class Lang { CS, EN };

    static inline Lang LangOpt()
    {
        std::string loc = sConfigMgr->GetOption<std::string>("MaterialBank.Locale", "cs");
        std::transform(loc.begin(), loc.end(), loc.begin(), ::tolower);
        return (loc == "en" || loc == "english") ? Lang::EN : Lang::CS;
    }

    static inline char const* T(char const* cs, char const* en)
    {
        return (LangOpt() == Lang::EN) ? en : cs;
    }

    static inline std::string Prefix()
    {
        if (LangOpt() == Lang::EN)
            return "|cff00ff00[Banker]|r ";
        else
            return "|cff00ff00[Bankéř]|r ";
    }

    static inline std::string Trim(std::string s)
    {
        auto ns = [](int ch){ return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
        s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
        return s;
    }

    static inline void SplitFirstToken(std::string const& in, std::string& tok, std::string& rest)
    {
        std::string s = Trim(in);
        auto p = s.find(' ');
        if (p == std::string::npos)
        {
            tok = s;
            rest.clear();
        }
        else
        {
            tok  = Trim(s.substr(0, p));
            rest = Trim(s.substr(p + 1));
        }
    }

    // --- barva podle kvality ---
    static inline char const* QualityHex(uint32 q)
    {
        switch (q)
        {
            case 0: return "ff9d9d9d"; // Poor
            case 1: return "ffffffff"; // Common
            case 2: return "ff1eff00"; // Uncommon
            case 3: return "ff0070dd"; // Rare
            case 4: return "ffa335ee"; // Epic
            case 5: return "ffff8000"; // Legendary
            case 6: return "ffe6cc80"; // Artifact
            case 7: return "ffe6cc80"; // Heirloom
            default:return "ffffffff";
        }
    }

    static std::string BuildItemLink(uint32 itemId)
    {
        if (ItemTemplate const* it = sObjectMgr->GetItemTemplate(itemId))
        {
            char const* hex = QualityHex(it->Quality);

            std::string out = "|c";
            out += hex;
            out += "|Hitem:";
            out += std::to_string(itemId);
            out += ":0:0:0:0:0:0:0|h[";
            out += it->Name1;
            out += "]|h|r";
            return out;
        }

        std::string out = "|cffffffff|Hitem:";
        out += std::to_string(itemId);
        out += ":0:0:0:0:0:0:0|h[Item ";
        out += std::to_string(itemId);
        out += "]|h|r";
        return out;
    }

	static void SendUsage(ChatHandler* handler)
	{
		std::string s = Prefix();
		s += T(
			"Použití: .mb pull <itemId[:count]>, .mb push <itemId[:count]>, .mb sync nebo .bank",
			"Usage: .mb pull <itemId[:count]>, .mb push <itemId[:count]>, .mb sync or .bank"
		);
		handler->SendSysMessage(s.c_str());
	}

    static void SendNoMatch(ChatHandler* handler)
    {
        std::string s = Prefix();
        s += T("Nenalezen žádný odpovídající materiál.",
               "No matching items to pull.");
        handler->SendSysMessage(s.c_str());
    }
	
	// === Stejná logika kategorií jako u NPC ===
	
	static bool FindExistingItemCategory(uint32 accountId, uint8 teamId, uint32 itemEntry, uint8& outCategoryId)
	{
		if (QueryResult r = WorldDatabase.Query(
				"SELECT categoryId FROM customs.account_material_bank "
				"WHERE accountId={} AND team={} AND itemEntry={} AND totalCount>0 "
				"ORDER BY categoryId LIMIT 1",
				accountId, uint32(teamId), itemEntry))
		{
			Field* f = r->Fetch();
			outCategoryId = f[0].Get<uint8>();
			return true;
		}
	
		return false;
	}
	
	static uint8 FindMatchingCategoryForItem(uint32 itemEntry)
	{
		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
		if (!proto)
			return 0;
	
		uint32 itemClass    = proto->Class;
		uint32 itemSubClass = proto->SubClass;
	
		uint8  bestId     = 0;
		bool   bestIsExact = false;
	
		if (QueryResult r = WorldDatabase.Query(
				"SELECT id, itemClass, itemSubClass "
				"FROM customs.material_bank_category "
				"WHERE itemClass >= 0"))
		{
			do
			{
				Field* f = r->Fetch();
				uint8 id = f[0].Get<uint8>();
				int8 cls = f[1].Get<int8>();
				int8 sub = f[2].Get<int8>();
	
				if (cls < 0)
					continue;
	
				if (uint32(cls) != itemClass)
					continue;
	
				bool exact = (sub >= 0 && uint32(sub) == itemSubClass);
				if (sub >= 0 && uint32(sub) != itemSubClass)
					continue;
	
				if (!bestId)
				{
					bestId = id;
					bestIsExact = exact;
				}
				else if (exact && !bestIsExact)
				{
					bestId = id;
					bestIsExact = true;
				}
			}
			while (r->NextRow());
		}
	
		return bestId;
	}
	
	static std::string GetCategoryDisplayName(uint8 categoryId)
	{
		if (categoryId == 0)
			return (LangOpt() == Lang::EN) ? "Uncategorized" : "Nezařazeno";
	
		if (QueryResult r = WorldDatabase.Query(
				"SELECT name_cs, name_en FROM customs.material_bank_category WHERE id={}",
				uint32(categoryId)))
		{
			Field* f = r->Fetch();
			if (LangOpt() == Lang::EN)
				return f[1].Get<std::string>();
			else
				return f[0].Get<std::string>();
		}
	
		return (LangOpt() == Lang::EN) ? "Unknown category" : "Neznámá kategorie";
	}
	
	// === Stejná blokace vkladu jako NPC (soulbound / quest / blocklist) ===

	static std::unordered_set<uint32> s_blockDeposit;
	static std::string s_blockDepositRaw;
	
	static void RebuildBlockDepositListIfNeeded()
	{
		std::string raw = sConfigMgr->GetOption<std::string>("MaterialBank.BlockDepositItemIds", "");
	
		auto trim = [](std::string& s)
		{
			auto ns = [](int ch){ return !std::isspace(ch); };
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
			s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
		};
	
		trim(raw);
	
		if (raw == s_blockDepositRaw)
			return;
	
		s_blockDepositRaw = raw;
		s_blockDeposit.clear();
	
		uint32 val = 0;
		bool inNum = false;
	
		for (char c : raw)
		{
			if (c >= '0' && c <= '9')
			{
				inNum = true;
				val = val * 10u + uint32(c - '0');
			}
			else
			{
				if (inNum)
				{
					if (val > 0)
						s_blockDeposit.insert(val);
					val = 0;
					inNum = false;
				}
			}
		}
	
		if (inNum && val > 0)
			s_blockDeposit.insert(val);
	}
	
	static bool IsDepositBlockedById(uint32 itemEntry)
	{
		RebuildBlockDepositListIfNeeded();
		return (itemEntry != 0) && (s_blockDeposit.find(itemEntry) != s_blockDeposit.end());
	}
	
	static std::unordered_set<uint32> s_allowSoulbound;
	static std::string s_allowSoulboundRaw;
	
	static void RebuildSoulboundAllowlistIfNeeded()
	{
		std::string raw = sConfigMgr->GetOption<std::string>("MaterialBank.AllowSoulboundItemIds", "");
	
		auto trim = [](std::string& s)
		{
			auto ns = [](int ch){ return !std::isspace(ch); };
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
			s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
		};
	
		trim(raw);
	
		if (raw == s_allowSoulboundRaw)
			return;
	
		s_allowSoulboundRaw = raw;
		s_allowSoulbound.clear();
	
		uint32 val = 0;
		bool inNum = false;
	
		for (char c : raw)
		{
			if (c >= '0' && c <= '9')
			{
				inNum = true;
				val = val * 10u + uint32(c - '0');
			}
			else
			{
				if (inNum)
				{
					if (val > 0)
						s_allowSoulbound.insert(val);
					val = 0;
					inNum = false;
				}
			}
		}
	
		if (inNum && val > 0)
			s_allowSoulbound.insert(val);
	}
	
	static bool IsSoulboundAllowed(uint32 itemEntry)
	{
		RebuildSoulboundAllowlistIfNeeded();
		return (itemEntry != 0) && (s_allowSoulbound.find(itemEntry) != s_allowSoulbound.end());
	}
	
	static std::unordered_set<uint32> s_allowQuest;
	static std::string s_allowQuestRaw;
	
	static void RebuildQuestAllowlistIfNeeded()
	{
		std::string raw = sConfigMgr->GetOption<std::string>("MaterialBank.AllowQuestItemIds", "");
	
		auto trim = [](std::string& s)
		{
			auto ns = [](int ch){ return !std::isspace(ch); };
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
			s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
		};
	
		trim(raw);
	
		if (raw == s_allowQuestRaw)
			return;
	
		s_allowQuestRaw = raw;
		s_allowQuest.clear();
	
		uint32 val = 0;
		bool inNum = false;
	
		for (char c : raw)
		{
			if (c >= '0' && c <= '9')
			{
				inNum = true;
				val = val * 10u + uint32(c - '0');
			}
			else
			{
				if (inNum)
				{
					if (val > 0)
						s_allowQuest.insert(val);
					val = 0;
					inNum = false;
				}
			}
		}
	
		if (inNum && val > 0)
			s_allowQuest.insert(val);
	}
	
	static bool IsQuestAllowed(uint32 itemEntry)
	{
		RebuildQuestAllowlistIfNeeded();
		return (itemEntry != 0) && (s_allowQuest.find(itemEntry) != s_allowQuest.end());
	}
	
	static std::unordered_set<uint32> s_allowQuestSoulbound;
	static std::string s_allowQuestSoulboundRaw;
	
	static void RebuildQuestSoulboundAllowlistIfNeeded()
	{
		std::string raw = sConfigMgr->GetOption<std::string>("MaterialBank.AllowQuestSoulboundItemIds", "");
	
		auto trim = [](std::string& s)
		{
			auto ns = [](int ch){ return !std::isspace(ch); };
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
			s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
		};
	
		trim(raw);
	
		if (raw == s_allowQuestSoulboundRaw)
			return;
	
		s_allowQuestSoulboundRaw = raw;
		s_allowQuestSoulbound.clear();
	
		uint32 val = 0;
		bool inNum = false;
	
		for (char c : raw)
		{
			if (c >= '0' && c <= '9')
			{
				inNum = true;
				val = val * 10u + uint32(c - '0');
			}
			else
			{
				if (inNum)
				{
					if (val > 0)
						s_allowQuestSoulbound.insert(val);
					val = 0;
					inNum = false;
				}
			}
		}
	
		if (inNum && val > 0)
			s_allowQuestSoulbound.insert(val);
	}
	
	static bool IsQuestSoulboundAllowed(uint32 itemEntry)
	{
		RebuildQuestSoulboundAllowlistIfNeeded();
		return (itemEntry != 0) && (s_allowQuestSoulbound.find(itemEntry) != s_allowQuestSoulbound.end());
	}
	
	static bool IsBlockedForDeposit(Item* item)
	{
		if (!item)
			return true;
	
		ItemTemplate const* proto = item->GetTemplate();
		if (!proto)
			return true;
	
		uint32 entry = item->GetEntry();
	
		if (IsDepositBlockedById(entry))
			return true;
	
		if (proto->Class == ITEM_CLASS_QUEST && item->IsSoulBound())
		{
			if (IsQuestSoulboundAllowed(entry))
				return false;
		}
	
		if (proto->Class == ITEM_CLASS_QUEST)
		{
			if (!IsQuestAllowed(entry))
				return true;
		}
	
		if (item->IsSoulBound())
		{
			if (!IsSoulboundAllowed(entry))
				return true;
		}
	
		return false;
	}

    static bool ParseToken(std::string token, uint32& itemId, uint64& count)
    {
        token = Trim(token);
        if (token.empty())
            return false;

        itemId = 0;
        count  = 1;

        std::size_t colonPos = token.find(':');
        if (colonPos != std::string::npos)
        {
            std::string itemPart  = token.substr(0, colonPos);
            std::string countPart = token.substr(colonPos + 1);

            itemId = uint32(std::strtoul(itemPart.c_str(), nullptr, 10));
            if (!countPart.empty())
                count = uint64(std::strtoull(countPart.c_str(), nullptr, 10));
        }
        else
        {
            itemId = uint32(std::strtoul(token.c_str(), nullptr, 10));
        }

        if (!itemId)
            return false;

        if (count == 0)
            count = 1;

        return true;
    }

    // ===== Summon banker cooldown tracking =====
    static std::unordered_map<uint64, uint32> s_lastBankerCall;
	
	// ===== Sync cooldown per account =====
	static std::unordered_map<uint32, uint32> s_lastSyncByAccount;

    static bool CanCallBanker(Player* player, ChatHandler* handler)
    {
        // boj / instance / BG / arena
        Map* map = player->GetMap();
        if (player->IsInCombat() ||
            (map && (map->IsBattlegroundOrArena() || map->IsDungeon())))
        {
            std::string msg = Prefix();
            msg += T(
                "Nemůžeš přivolat bankéře v boji, instanci, bojišti nebo aréně.",
                "You cannot call the banker in combat, instances, battlegrounds or arenas."
            );
            handler->SendSysMessage(msg.c_str());
            return false;
        }

        // CD v sekundách (default 300 = 5 min)
        uint32 cdSec = sConfigMgr->GetOption<uint32>("MaterialBank.SummonCooldown", 300u);

        uint32 now = uint32(GameTime::GetGameTime().count());
        uint64 key = player->GetGUID().GetCounter();

        auto it = s_lastBankerCall.find(key);
        if (it != s_lastBankerCall.end())
        {
            uint32 last = it->second;
            if (now < last + cdSec)
            {
                uint32 remain = (last + cdSec) - now;

                std::string msg = Prefix();
                if (LangOpt() == Lang::EN)
                {
                    msg += Acore::StringFormat(
                        "You must wait {}s before calling the banker again.",
                        remain);
                }
                else
                {
                    msg += Acore::StringFormat(
                        "Musíš počkat ještě {} s, než znovu přivoláš bankéře.",
                        remain);
                }
                handler->SendSysMessage(msg.c_str());
                return false;
            }
        }

        s_lastBankerCall[key] = now;
        return true;
    }

    static void DoSummonBanker(Player* player, ChatHandler* handler)
    {
        if (!CanCallBanker(player, handler))
            return;

        uint32 entry = sConfigMgr->GetOption<uint32>("MaterialBank.BankerNpcEntry", 0u);
        uint32 despawnMs = sConfigMgr->GetOption<uint32>("MaterialBank.BankerDespawnMs", 5u * 60u * 1000u);

        if (!entry)
        {
            std::string msg = Prefix();
            msg += T(
                "Bankéř pro přivolání není nastaven (MaterialBank.BankerNpcEntry).",
                "Summon banker NPC is not configured (MaterialBank.BankerNpcEntry)."
            );
            handler->SendSysMessage(msg.c_str());
            return;
        }

        if (!sObjectMgr->GetCreatureTemplate(entry))
        {
            std::string msg = Prefix();
            msg += T(
                "Konfigurace bankéře je chybná (neplatný entry).",
                "Banker configuration is invalid (wrong entry)."
            );
            handler->SendSysMessage(msg.c_str());
            return;
        }

        Position pos = player->GetPosition();

        Creature* summon = player->SummonCreature(
            entry,
            pos,
            TEMPSUMMON_TIMED_DESPAWN,
            despawnMs
        );

        if (!summon)
        {
            std::string msg = Prefix();
            msg += T("Nepodařilo se přivolat bankéře.",
                     "Failed to summon banker.");
            handler->SendSysMessage(msg.c_str());
            return;
        }

        std::string msg = Prefix();
        if (LangOpt() == Lang::EN)
        {
            msg += "You have summoned the banker for 5 minutes.";
        }
        else
        {
            msg += "Přivolal jsi bankéře na 5 minut.";
        }
        handler->SendSysMessage(msg.c_str());
    }

    static std::string GetSafeItemName(uint32 itemEntry)
    {
        std::string name;

        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry))
            name = proto->Name1;

        if (name.empty())
        {
            name = "Item ";
            name += std::to_string(itemEntry);
        }

        for (char& c : name)
        {
            if (c == '"')
                c = '\'';
        }

        return name;
    }
	
	static bool CanSync(Player* player, ChatHandler* handler)
    {
        if (!player || !handler)
            return false;

        WorldSession* session = player->GetSession();
        if (!session)
            return false;

        uint32 accountId = session->GetAccountId();
        if (!accountId)
            return false;

        // cooldown v minutách (default 15)
        uint32 cdMin = sConfigMgr->GetOption<uint32>("MaterialBank.SyncCooldownMinutes", 15u);
        uint32 cdSec = cdMin * 60u;

        uint32 now = uint32(GameTime::GetGameTime().count());

        auto it = s_lastSyncByAccount.find(accountId);
        if (it != s_lastSyncByAccount.end())
        {
            uint32 last = it->second;
            if (now < last + cdSec)
            {
                uint32 remain = (last + cdSec) - now;

                std::string msg = Prefix();
                if (LangOpt() == Lang::EN)
                {
                    msg += Acore::StringFormat(
                        "You can use .mb sync again in {} seconds.",
                        remain);
                }
                else
                {
                    msg += Acore::StringFormat(
                        "Příkaz .mb sync můžeš znovu použít za {} s.",
                        remain);
                }

                handler->SendSysMessage(msg.c_str());
                return false;
            }
        }

        s_lastSyncByAccount[accountId] = now;
        return true;
    }

	
    static bool DoSync(Player* player, ChatHandler* handler)
    {
        if (!player || !handler)
            return false;

        WorldSession* session = player->GetSession();
        if (!session)
            return false;

        uint32 accountId = session->GetAccountId();
        if (!accountId)
            return false;

        if (!CanSync(player, handler))
            return true;

        uint8 teamId = MaterialBank::GetBankTeamId(player);

        QueryResult r = WorldDatabase.Query(
            "SELECT itemEntry, categoryId, totalCount "
            "FROM customs.account_material_bank "
            "WHERE accountId={} AND team={} AND totalCount>0 "
            "ORDER BY itemEntry ASC",
            accountId, uint32(teamId));

        std::string prefix = Prefix();

        std::string langCode = (LangOpt() == Lang::EN) ? "en" : "cs";
        handler->SendSysMessage((prefix + "SYNC_LANG=" + langCode).c_str());

        handler->SendSysMessage((prefix + "SYNC_BEGIN").c_str());

        if (r)
        {
            do
            {
                Field* f = r->Fetch();
                uint32 itemEntry  = f[0].Get<uint32>();
                uint8  categoryId = f[1].Get<uint8>();
                uint64 totalCount = f[2].Get<uint64>();

                std::string safeName = GetSafeItemName(itemEntry);

                std::string line = prefix + Acore::StringFormat(
                    "SYNC item={} cat={} total={} name=\"{}\"",
                    itemEntry,
                    uint32(categoryId),
                    static_cast<unsigned long long>(totalCount),
                    safeName);

                handler->SendSysMessage(line.c_str());
            }
            while (r->NextRow());
        }

        handler->SendSysMessage((prefix + "SYNC_END").c_str());

        return true;
    }	

    static void DoPullOne(Player* player, ChatHandler* handler, std::string const& token)
	{
		uint32 itemId = 0;
		uint64 count  = 1;
	
		if (!ParseToken(token, itemId, count))
		{
			SendUsage(handler);
			return;
		}
	
		uint64 bankCountBefore = MaterialBank::GetBankItemCount(player, itemId);
	
		if (bankCountBefore == 0)
		{
			std::string link = BuildItemLink(itemId);
			std::string msg  = Prefix();
	
			if (LangOpt() == Lang::EN)
			{
				msg += "You don't have ";
				msg += link;
				msg += " stored in the banker.";
			}
			else
			{
				msg += "Předmět ";
				msg += link;
				msg += " nemáš uskladněný u bankéře.";
			}
	
			handler->SendSysMessage(msg.c_str());
			return;
		}
	
		uint64 moved = MaterialBank::MoveFromBankToBags(player, itemId, count);
	
		if (moved == 0)
		{
			std::string msg = Prefix();
			if (LangOpt() == Lang::EN)
			{
				msg += "Your inventory is full, cannot withdraw items.";
			}
			else
			{
				msg += "Inventář je plný, nelze vybrat předměty.";
			}
	
			handler->SendSysMessage(msg.c_str());
			return;
		}
	
		uint64 remaining = MaterialBank::GetBankItemCount(player, itemId);
	
		std::string link = BuildItemLink(itemId);
		std::string msg  = Prefix();
	
		if (LangOpt() == Lang::EN)
		{
			msg += "Pulled ";
			msg += std::to_string((unsigned long long)moved);
			msg += "x ";
			msg += link;
	
			if (remaining > 0)
			{
				msg += " ";
				msg += std::to_string((unsigned long long)remaining);
				msg += (remaining == 1 ? " remaining." : " remaining.");
			}
			else
			{
				msg += " (no more stored).";
			}
		}
		else
		{
			msg += "Vytaženo ";
			msg += std::to_string((unsigned long long)moved);
			msg += "x ";
			msg += link;
	
			msg += " zbývá ";
			msg += std::to_string((unsigned long long)remaining);
			msg += (remaining == 1 ? " kus." : " kusů.");
		}
	
		handler->SendSysMessage(msg.c_str());
	}
	
	static void DoPushOne(Player* player, ChatHandler* handler, std::string const& token)
	{
		uint32 itemId = 0;
		uint64 count  = 1;
	
		if (!ParseToken(token, itemId, count))
		{
			SendUsage(handler);
			return;
		}
	
		if (!itemId)
		{
			SendUsage(handler);
			return;
		}
	
		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
		if (!proto)
		{
			std::string msg = Prefix();
			msg += T("Neplatný itemId: ", "Invalid itemId: ");
			msg += std::to_string(itemId);
			handler->SendSysMessage(msg.c_str());
			return;
		}
	
		WorldSession* session = player->GetSession();
		if (!session)
			return;
	
		uint32 accountId = session->GetAccountId();
		if (!accountId)
			return;
	
		uint8 teamId = MaterialBank::GetBankTeamId(player);
	
		uint8 usedCategoryId     = 0;
		bool  redirectedExisting = false;
		bool  autoSorted         = false;
	
		uint8 existingCategoryId = 0;
		if (FindExistingItemCategory(accountId, teamId, itemId, existingCategoryId))
		{
			usedCategoryId     = existingCategoryId;
			redirectedExisting = true;
		}
		else
		{
			uint8 match = FindMatchingCategoryForItem(itemId);
			if (match != 0)
			{
				usedCategoryId = match;
				autoSorted     = true;
			}
			else
			{
				usedCategoryId = 0;
			}
		}
	
		uint64 toMove = count;
		uint64 moved  = 0;
	
		// --- hlavní batoh ---
		for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END && toMove > 0; ++slot)
		{
			Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
			if (!item || item->GetEntry() != itemId)
				continue;
	
			if (IsBlockedForDeposit(item))
				continue;
	
			uint32 stackCount = item->GetCount();
			if (!stackCount)
				continue;
	
			uint64 removeHere = std::min<uint64>(stackCount, toMove);
			if (!removeHere)
				continue;
	
			MaterialBank::AddToAccountBank(accountId, teamId, usedCategoryId, itemId, removeHere);
	
			moved  += removeHere;
			toMove -= removeHere;
	
			if (removeHere == stackCount)
			{
				player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
			}
			else
			{
				uint32 newCount = stackCount - uint32(removeHere);
				item->SetCount(newCount);
				if (player->IsInWorld())
					item->SendUpdateToPlayer(player);
				item->SetState(ITEM_CHANGED, player);
			}
		}
	
		// --- ostatní bagy ---
		for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END && toMove > 0; ++bag)
		{
			Bag* container = player->GetBagByPos(bag);
			if (!container)
				continue;
	
			for (uint8 slot = 0; slot < container->GetBagSize() && toMove > 0; ++slot)
			{
				Item* item = player->GetItemByPos(bag, slot);
				if (!item || item->GetEntry() != itemId)
					continue;
	
				if (IsBlockedForDeposit(item))
					continue;
	
				uint32 stackCount = item->GetCount();
				if (!stackCount)
					continue;
	
				uint64 removeHere = std::min<uint64>(stackCount, toMove);
				if (!removeHere)
					continue;
	
				MaterialBank::AddToAccountBank(accountId, teamId, usedCategoryId, itemId, removeHere);
	
				moved  += removeHere;
				toMove -= removeHere;
	
				if (removeHere == stackCount)
				{
					player->DestroyItem(bag, slot, true);
				}
				else
				{
					uint32 newCount = stackCount - uint32(removeHere);
					item->SetCount(newCount);
					if (player->IsInWorld())
						item->SendUpdateToPlayer(player);
					item->SetState(ITEM_CHANGED, player);
				}
			}
		}
	
		// === výstupní hlášky ===
		if (moved == 0)
		{
			std::string link = BuildItemLink(itemId);
			std::string msg  = Prefix();
	
			if (LangOpt() == Lang::EN)
			{
				msg += "You don't have ";
				msg += link;
				msg += " in your inventory to deposit.";
			}
			else
			{
				msg += "V inventáři nemáš ";
				msg += link;
				msg += " k uložení do banky.";
			}
	
			handler->SendSysMessage(msg.c_str());
			return;
		}
	
		std::string link = BuildItemLink(itemId);
	
		{
			std::string msg = Prefix();
			if (LangOpt() == Lang::EN)
			{
				msg += "Deposited ";
				msg += std::to_string((unsigned long long)moved);
				msg += "x ";
				msg += link;
				msg += " to your account storage.";
			}
			else
			{
				msg += "Uloženo ";
				msg += std::to_string((unsigned long long)moved);
				msg += "x ";
				msg += link;
				msg += " do účtové úschovy.";
			}
		
			handler->SendSysMessage(msg.c_str());
		
			{
				std::string meta = "MB_UPDATE item=" + std::to_string(itemId)
					+ " cat=" + std::to_string(uint32(usedCategoryId));
				handler->SendSysMessage(meta.c_str());
			}
		}
	
		if (redirectedExisting)
		{
			std::string catName = GetCategoryDisplayName(usedCategoryId);
			std::string msg = Prefix();
			if (LangOpt() == Lang::EN)
			{
				msg += "Note: This item was stored in category '";
				msg += catName;
				msg += "' because it already exists there.";
			}
			else
			{
				msg += "Poznámka: Předmět byl uložen do kategorie '";
				msg += catName;
				msg += "', protože se v ní již nachází stejný předmět.";
			}
			handler->SendSysMessage(msg.c_str());
		}
		else if (autoSorted && usedCategoryId != 0)
		{
			std::string catName = GetCategoryDisplayName(usedCategoryId);
			std::string msg = Prefix();
			if (LangOpt() == Lang::EN)
			{
				msg += "Item was automatically sorted into category '";
				msg += catName;
				msg += "'.";
			}
			else
			{
				msg += "Předmět byl automaticky zařazen do kategorie '";
				msg += catName;
				msg += "'.";
			}
			handler->SendSysMessage(msg.c_str());
		}
	}

	
	static bool DoPush(Player* player, ChatHandler* handler, std::string const& spec)
	{
		std::string s = Trim(spec);
		if (s.empty())
		{
			SendUsage(handler);
			return true;
		}
	
		bool anyToken = false;
	
		while (!s.empty())
		{
			std::string tok, rest;
			SplitFirstToken(s, tok, rest);
			if (tok.empty())
			{
				s = rest;
				continue;
			}
	
			anyToken = true;
			DoPushOne(player, handler, tok);
			s = rest;
		}
	
		if (!anyToken)
			SendUsage(handler);
	
		return true;
	}



    static bool HandleBankerCommand(ChatHandler* handler, char const* /*args*/)
    {
        WorldSession* session = handler->GetSession();
        if (!session)
            return false;

        Player* player = session->GetPlayer();
        if (!player)
            return false;

        DoSummonBanker(player, handler);
        return true;
    }
}

namespace MaterialBank
{
    static bool DoPull(Player* player, ChatHandler* handler, std::string const& spec)
    {
        std::string s = Trim(spec);
        if (s.empty())
        {
            SendUsage(handler);
            return true;
        }

        bool anyToken = false;

        while (!s.empty())
        {
            std::string tok, rest;
            SplitFirstToken(s, tok, rest);
            if (tok.empty())
                break;

            anyToken = true;
            DoPullOne(player, handler, tok);
            s = rest;
        }

        if (!anyToken)
            SendUsage(handler);

        return true;
    }

	static bool HandleMaterialBank(ChatHandler* handler, char const* args)
	{
		WorldSession* session = handler->GetSession();
		if (!session)
			return false;
	
		Player* player = session->GetPlayer();
		if (!player)
			return false;
	
		std::string a = args ? args : "";
		a = Trim(a);
	
		if (a.empty())
		{
			SendUsage(handler);
			return true;
		}
	
		std::string tok1, rest;
		SplitFirstToken(a, tok1, rest);
	
		std::string tokLower = tok1;
		std::transform(tokLower.begin(), tokLower.end(),
					tokLower.begin(), [](unsigned char c){ return std::tolower(c); });
	
		if (tokLower == "sync" || tokLower == "export")
		{
			return DoSync(player, handler);
		}
	
		if (tokLower == "pull")
		{
			if (rest.empty())
			{
				SendUsage(handler);
				return true;
			}
	
			return DoPull(player, handler, rest);
		}
	
		if (tokLower == "push")
		{
			if (rest.empty())
			{
				SendUsage(handler);
				return true;
			}
	
			return DoPush(player, handler, rest);
		}
	
		return DoPull(player, handler, a);
	}

    class MaterialBankCommandScript : public CommandScript
    {
    public:
        MaterialBankCommandScript()
            : CommandScript("MaterialBankCommandScript") { }

        std::vector<Acore::ChatCommands::ChatCommandBuilder> GetCommands() const override
        {
            using namespace Acore::ChatCommands;

            auto& fn = HandleMaterialBank;
            ChatCommandBuilder mb("mb", fn, SEC_PLAYER, Console::No);

            ChatCommandBuilder bank("bank", HandleBankerCommand, SEC_PLAYER, Console::No);

            std::vector<ChatCommandBuilder> out;
            out.emplace_back(mb);
            out.emplace_back(bank);
            return out;
        }
    };

    class MaterialBank_ChatHook : public PlayerScript
    {
    public:
        MaterialBank_ChatHook()
            : PlayerScript("MaterialBank_ChatHook", { PLAYERHOOK_ON_CHAT }) { }

        void OnPlayerChat(Player* /*player*/, uint32 /*type*/, uint32 /*lang*/, std::string& /*msg*/) override
        {
        }
    };

    void RegisterMaterialBankCommands()
    {
        new MaterialBankCommandScript();
        new MaterialBank_ChatHook();
    }
}
