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

#include "material_bank.h"

#include <cstdlib>
#include <string>
#include <algorithm>
#include <cctype>
#include <unordered_map>

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
            "Použití: .mb pull <itemId[:count]>, .mb sync nebo .bank",
            "Usage: .mb pull <itemId[:count]>, .mb sync or .bank"
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

                std::string line = prefix + Acore::StringFormat(
                    "SYNC item={} cat={} total={}",
                    itemEntry,
                    uint32(categoryId),
                    static_cast<unsigned long long>(totalCount));

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
