#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "Config.h"
#include "Player.h"
#include "Creature.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "StringFormat.h"
#include "Chat.h"

#include "material_bank.h"

#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace MaterialBank
{
    enum class Lang { CS, EN };

    static Lang LangOpt()
    {
        std::string loc = sConfigMgr->GetOption<std::string>("MaterialBank.Locale", "cs");
        std::transform(loc.begin(), loc.end(), loc.begin(), ::tolower);
        return (loc == "en" || loc == "english") ? Lang::EN : Lang::CS;
    }

    static inline char const* T(char const* cs, char const* en)
    {
        return (LangOpt() == Lang::EN) ? en : cs;
    }

    static inline uint32 GetAccountId(Player* player)
    {
        if (!player)
            return 0;
        if (WorldSession* sess = player->GetSession())
            return sess->GetAccountId();
        return 0;
    }
	
	//config klíč pro zákaz vkladu konkrátních item id (má nejvyšší prioritu)
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
	
	//config klíč pro soulbound item
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
	
	//config klíč pro quest item
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
	
	//config klíč pro soulbound quest item
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

    // barva podle kvality (pro katalog)
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

    static std::string CategoryName(uint8 categoryId, Field* f)
    {
        if (categoryId == 0)
        {
            return (LangOpt() == Lang::EN) ? "Uncategorized" : "Nezařazeno";
        }

        if (LangOpt() == Lang::EN)
            return f[2].Get<std::string>();
        else
            return f[1].Get<std::string>();
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

    static uint8 GetParentCategoryId(uint8 categoryId)
    {
        if (categoryId == 0)
            return 0;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT parent_id FROM customs.material_bank_category WHERE id={}",
                uint32(categoryId)))
        {
            Field* f = r->Fetch();
            return f[0].Get<uint8>();
        }

        return 0;
    }

    static bool HasSubcategories(uint8 categoryId)
    {
        if (categoryId == 0)
            return false;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT 1 FROM customs.material_bank_category WHERE parent_id={} LIMIT 1",
                uint32(categoryId)))
        {
            return true;
        }

        return false;
    }

    struct CategoryFilter
    {
        int8 itemClass;
        int8 itemSubClass;
    };

    static CategoryFilter GetCategoryFilter(uint8 categoryId)
    {
        CategoryFilter f;
        f.itemClass    = -1;
        f.itemSubClass = -1;

        if (categoryId == 0)
            return f;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT itemClass, itemSubClass FROM customs.material_bank_category WHERE id={}",
                uint32(categoryId)))
        {
            Field* fld = r->Fetch();
            f.itemClass    = fld[0].Get<int8>();
            f.itemSubClass = fld[1].Get<int8>();
        }

        return f;
    }

    static bool PassesFilter(ItemTemplate const* proto, CategoryFilter const& f)
    {
        if (!proto)
            return false;

        if (f.itemClass >= 0 &&
            proto->Class != uint32(f.itemClass))
            return false;

        if (f.itemSubClass >= 0 &&
            proto->SubClass != uint32(f.itemSubClass))
            return false;

        return true;
    }

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

        uint8 bestId = 0;
        bool bestIsExact = false;

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

    static bool HasStoredItemsInCategory(uint32 accountId, uint8 teamId, uint8 categoryId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT 1 FROM customs.account_material_bank "
                "WHERE accountId={} AND team={} AND categoryId={} AND totalCount>0 LIMIT 1",
                accountId, uint32(teamId), uint32(categoryId)))
        {
            return true;
        }

        return false;
    }

    static bool HasStoredItemsInCategoryTree(uint32 accountId, uint8 teamId, uint8 categoryId)
    {
        if (HasStoredItemsInCategory(accountId, teamId, categoryId))
            return true;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT id FROM customs.material_bank_category WHERE parent_id={}",
                uint32(categoryId)))
        {
            do
            {
                Field* f = r->Fetch();
                uint8 subId = f[0].Get<uint8>();
                if (HasStoredItemsInCategoryTree(accountId, teamId, subId))
                    return true;
            }
            while (r->NextRow());
        }

        return false;
    }

    class npc_account_material_bank : public CreatureScript
    {
    public:
        npc_account_material_bank() : CreatureScript("npc_account_material_bank") { }

        enum Actions : uint32
        {
            ACT_ROOT                 = 100,
            ACT_DEPOSIT              = 110,
            ACT_WITHDRAW             = 120,
            ACT_BACK_TO_ROOT         = 190,

            ACT_INFO_SHARED_ITEMS    = 200,
            ACT_INFO_SHARED_TOTAL    = 201,
            ACT_INFO_SEPARATOR_TOP   = 202
        };

        enum Senders : uint32
        {
            S_MAIN                   = GOSSIP_SENDER_MAIN,

            S_DEPOSIT_PAGE           = 2001,
            S_DEPOSIT_ITEM           = 2002,
            S_WITHDRAW_PAGE          = 2003,
            S_WITHDRAW_ITEM          = 2004,

            S_DEPOSIT_CATEGORIES     = 2005,
            S_WITHDRAW_CATEGORIES    = 2006,

            S_DEPOSIT_CATEGORY_SELECT  = 2007,
            S_WITHDRAW_CATEGORY_SELECT = 2008
        };

        static constexpr uint32 ITEMS_PER_PAGE = 12;

        static inline uint32 PackCatPage(uint8 categoryId, uint32 page)
        {
            return (uint32(categoryId) << 16) | (page & 0xFFFFu);
        }

        static inline void UnpackCatPage(uint32 packed, uint8& categoryId, uint32& page)
        {
            categoryId = uint8((packed >> 16) & 0xFFu);
            page       = packed & 0xFFFFu;
        }

        static inline uint32 PackCatItem(uint8 categoryId, uint32 itemEntry)
        {
            return (uint32(categoryId) << 24) | (itemEntry & 0x00FFFFFFu);
        }

        static inline void UnpackCatItem(uint32 packed, uint8& categoryId, uint32& itemEntry)
        {
            categoryId = uint8((packed >> 24) & 0xFFu);
            itemEntry  = packed & 0x00FFFFFFu;
        }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            ClearGossipMenuFor(player);

            uint32 accountId = GetAccountId(player);
            uint8 teamId = GetBankTeamId(player);

            uint64 sharedUnique = 0;
            uint64 sharedTotal  = 0;

            if (accountId)
            {
                if (QueryResult r = WorldDatabase.Query(
                        "SELECT COUNT(DISTINCT itemEntry) "
                        "FROM customs.account_material_bank "
                        "WHERE accountId={} AND team={} AND totalCount>0",
                        accountId, uint32(teamId)))
                {
                    Field* f = r->Fetch();
                    sharedUnique = f[0].Get<uint64>();
                }

                if (QueryResult r2 = WorldDatabase.Query(
                        "SELECT COALESCE(SUM(totalCount),0) "
                        "FROM customs.account_material_bank "
                        "WHERE accountId={} AND team={} AND totalCount>0",
                        accountId, uint32(teamId)))
                {
                    Field* f2 = r2->Fetch();
                    sharedTotal = f2[0].Get<uint64>();
                }
            }

            std::string sharedItemsText = Acore::StringFormat(
                (LangOpt() == Lang::EN)
                    ? "Shared items: {}"
                    : "Sdíleno předmětů: {}",
                sharedUnique);

            std::string sharedTotalText = Acore::StringFormat(
                (LangOpt() == Lang::EN)
                    ? "Shared total: {}"
                    : "Sdíleno celkem: {}",
                sharedTotal);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                             sharedItemsText,
                             S_MAIN, ACT_INFO_SHARED_ITEMS);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                             sharedTotalText,
                             S_MAIN, ACT_INFO_SHARED_TOTAL);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                             T("------------------------------",
                               "------------------------------"),
                             S_MAIN, ACT_INFO_SEPARATOR_TOP);

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                             T("Vložit sdílené předměty", "Deposit shared items"),
                             S_MAIN, ACT_DEPOSIT);

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                             T("Vybrat sdílené předměty", "Withdraw shared items"),
                             S_MAIN, ACT_WITHDRAW);

            SendGossipMenuFor(player, 1, creature->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
        {
            if (!player || !creature)
                return false;

            switch (sender)
            {
                case S_MAIN:
                {
                    switch (action)
                    {
                        case ACT_ROOT:
                        case ACT_BACK_TO_ROOT:
                        case ACT_INFO_SHARED_ITEMS:
                        case ACT_INFO_SHARED_TOTAL:
                        case ACT_INFO_SEPARATOR_TOP:
                            return OnGossipHello(player, creature);

                        case ACT_DEPOSIT:
                            ShowDepositCategoryMenu(player, creature);
                            return true;

                        case ACT_WITHDRAW:
                            ShowWithdrawCategoryMenu(player, creature);
                            return true;

                        default:
                            break;
                    }
                    break;
                }

                case S_DEPOSIT_CATEGORIES:
                    ShowDepositCategoryMenu(player, creature);
                    return true;

                case S_WITHDRAW_CATEGORIES:
                    ShowWithdrawCategoryMenu(player, creature);
                    return true;

                case S_DEPOSIT_CATEGORY_SELECT:
                {
                    uint8 categoryId = uint8(action & 0xFFu);

                    if (HasSubcategories(categoryId))
                        ShowDepositSubCategoryMenu(player, creature, categoryId);
                    else
                        ShowDepositList(player, creature, categoryId, 0);

                    return true;
                }

                case S_WITHDRAW_CATEGORY_SELECT:
                {
                    uint8 categoryId = uint8(action & 0xFFu);

                    if (HasSubcategories(categoryId))
                        ShowWithdrawSubCategoryMenu(player, creature, categoryId);
                    else
                        ShowWithdrawList(player, creature, categoryId, 0);

                    return true;
                }

                case S_DEPOSIT_PAGE:
                {
                    uint8 categoryId;
                    uint32 page;
                    UnpackCatPage(action, categoryId, page);
                    ShowDepositList(player, creature, categoryId, page);
                    return true;
                }

                case S_WITHDRAW_PAGE:
                {
                    uint8 categoryId;
                    uint32 page;
                    UnpackCatPage(action, categoryId, page);
                    ShowWithdrawList(player, creature, categoryId, page);
                    return true;
                }

                case S_DEPOSIT_ITEM:
                {
                    uint8 categoryId;
                    uint32 itemEntry;
                    UnpackCatItem(action, categoryId, itemEntry);
                    HandleDepositItem(player, creature, categoryId, itemEntry);
                    return true;
                }

                case S_WITHDRAW_ITEM:
                {
                    uint8 categoryId;
                    uint32 itemEntry;
                    UnpackCatItem(action, categoryId, itemEntry);
                    HandleWithdrawItem(player, creature, categoryId, itemEntry);
                    return true;
                }

                default:
                    break;
            }

            return false;
        }

    private:
        struct BagItemEntry
        {
            uint32 itemEntry;
            uint64 totalCount;
        };

        static void BuildBagItemList(Player* player, std::vector<BagItemEntry>& out)
        {
            out.clear();
            if (!player)
                return;

            std::unordered_map<uint32, uint64> counts;

            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    if (IsBlockedForDeposit(item))
                        continue;

                    uint32 entry = item->GetEntry();
                    uint32 count = item->GetCount();
                    if (!entry || !count)
                        continue;

                    counts[entry] += count;
                }
            }

            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            {
                Bag* bag = player->GetBagByPos(bagSlot);
                if (!bag)
                    continue;

                uint8 slots = bag->GetBagSize();
                for (uint8 slot = 0; slot < slots; ++slot)
                {
                    Item* item = bag->GetItemByPos(slot);
                    if (!item)
                        continue;

                    if (IsBlockedForDeposit(item))
                        continue;

                    uint32 entry = item->GetEntry();
                    uint32 count = item->GetCount();
                    if (!entry || !count)
                        continue;

                    counts[entry] += count;
                }
            }

            out.reserve(counts.size());
            for (auto const& kv : counts)
            {
                BagItemEntry e;
                e.itemEntry  = kv.first;
                e.totalCount = kv.second;
                out.push_back(e);
            }

            std::sort(out.begin(), out.end(),
                      [](BagItemEntry const& a, BagItemEntry const& b)
                      {
                          return a.itemEntry < b.itemEntry;
                      });
        }

        static bool CategoryHasBagItems(uint8 categoryId, std::vector<BagItemEntry> const& list)
        {
            if (categoryId == 0)
                return !list.empty();

            CategoryFilter filter = GetCategoryFilter(categoryId);

            for (BagItemEntry const& e : list)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(e.itemEntry);
                if (!proto)
                    continue;

                if (PassesFilter(proto, filter))
                    return true;
            }

            return false;
        }

        static bool CategoryTreeHasBagItems(uint8 categoryId, std::vector<BagItemEntry> const& list)
        {
            if (CategoryHasBagItems(categoryId, list))
                return true;

            if (QueryResult r = WorldDatabase.Query(
                    "SELECT id FROM customs.material_bank_category WHERE parent_id={}",
                    uint32(categoryId)))
            {
                do
                {
                    Field* f = r->Fetch();
                    uint8 subId = f[0].Get<uint8>();
                    if (CategoryHasBagItems(subId, list))
                        return true;
                }
                while (r->NextRow());
            }

            return false;
        }

        // === DEPOSIT – hlavní menu kategorií ===
        void ShowDepositCategoryMenu(Player* player, Creature* creature)
        {
            uint32 accountId = GetAccountId(player);
            if (!accountId)
            {
                CloseGossipMenuFor(player);
                return;
            }

            std::vector<BagItemEntry> bag;
            BuildBagItemList(player, bag);

            ClearGossipMenuFor(player);

            if (bag.empty())
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("V inventáři nemáš žádné vhodné předměty k uložení.",
                      "You have no suitable items in your bags to deposit.")
                );
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Zpátky", "Back"),
                                 S_MAIN, ACT_BACK_TO_ROOT);
                SendGossipMenuFor(player, 1, creature->GetGUID());
                return;
            }

            bool anyShown = false;

            if (CategoryHasBagItems(0, bag))
            {
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                                 T("Automatické třídění", "Auto sort"),
                                 S_DEPOSIT_CATEGORY_SELECT, 0);
                anyShown = true;
            }

            QueryResult r = WorldDatabase.Query(
                "SELECT id, name_cs, name_en "
                "FROM customs.material_bank_category "
                "WHERE parent_id=0 "
                "ORDER BY id ASC");

            if (r)
            {
                do
                {
                    Field* f = r->Fetch();
                    uint8 categoryId = f[0].Get<uint8>();

                    if (!CategoryTreeHasBagItems(categoryId, bag))
                        continue;

                    std::string name = CategoryName(categoryId, f);

                    AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                                     name, S_DEPOSIT_CATEGORY_SELECT,
                                     uint32(categoryId));
                    anyShown = true;
                }
                while (r->NextRow());
            }

            if (!anyShown)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Pro žádnou kategorii nemáš vhodné předměty k uložení.",
                      "You have no suitable items for any category to deposit.")
                );
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Zpátky", "Back"),
                                 S_MAIN, ACT_BACK_TO_ROOT);
                SendGossipMenuFor(player, 1, creature->GetGUID());
                return;
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                             T("------------------------------",
                               "------------------------------"),
                             S_DEPOSIT_CATEGORIES, 0);

            AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                             T("Zpátky", "Back"),
                             S_MAIN, ACT_BACK_TO_ROOT);

            SendGossipMenuFor(player, 1, creature->GetGUID());
        }

        // === DEPOSIT – menu subkategorií ===
        void ShowDepositSubCategoryMenu(Player* player, Creature* creature, uint8 parentCategoryId)
        {
            uint32 accountId = GetAccountId(player);
            if (!accountId)
            {
                CloseGossipMenuFor(player);
                return;
            }

            std::vector<BagItemEntry> bag;
            BuildBagItemList(player, bag);

            ClearGossipMenuFor(player);

            QueryResult r = WorldDatabase.Query(
                "SELECT id, name_cs, name_en "
                "FROM customs.material_bank_category "
                "WHERE parent_id={} "
                "ORDER BY id ASC",
                uint32(parentCategoryId));

            if (!r)
            {
                ShowDepositList(player, creature, parentCategoryId, 0);
                return;
            }

            bool anyShown = false;

            do
            {
                Field* f = r->Fetch();
                uint8 categoryId = f[0].Get<uint8>();

                if (!CategoryHasBagItems(categoryId, bag))
                    continue;

                std::string name = CategoryName(categoryId, f);

                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                                 name, S_DEPOSIT_PAGE,
                                 PackCatPage(categoryId, 0));
                anyShown = true;
            }
            while (r->NextRow());

            if (!anyShown)
            {
                ShowDepositList(player, creature, parentCategoryId, 0);
                return;
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                             T("------------------------------",
                               "------------------------------"),
                             S_DEPOSIT_CATEGORY_SELECT, parentCategoryId);

            AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                             T("Zpátky", "Back"),
                             S_DEPOSIT_CATEGORIES, 0);

            SendGossipMenuFor(player, 1, creature->GetGUID());
        }

        // === WITHDRAW – hlavní menu kategorií ===
        void ShowWithdrawCategoryMenu(Player* player, Creature* creature)
        {
            uint32 accountId = GetAccountId(player);
            if (!accountId)
            {
                CloseGossipMenuFor(player);
                return;
            }

            uint8 teamId = GetBankTeamId(player);

            ClearGossipMenuFor(player);

            bool anyShown = false;

            if (HasStoredItemsInCategory(accountId, teamId, 0))
            {
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                                 T("Nezařazeno", "Uncategorized"),
                                 S_WITHDRAW_CATEGORY_SELECT, 0);
                anyShown = true;
            }

            QueryResult r = WorldDatabase.Query(
                "SELECT id, name_cs, name_en "
                "FROM customs.material_bank_category "
                "WHERE parent_id=0 "
                "ORDER BY id ASC");

            if (r)
            {
                do
                {
                    Field* f = r->Fetch();
                    uint8 categoryId = f[0].Get<uint8>();

                    if (!HasStoredItemsInCategoryTree(accountId, teamId, categoryId))
                        continue;

                    std::string name = CategoryName(categoryId, f);

                    AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                                     name, S_WITHDRAW_CATEGORY_SELECT,
                                     uint32(categoryId));
                    anyShown = true;
                }
                while (r->NextRow());
            }

            if (!anyShown)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Nemáš uložený žádný předmět.",
                      "You have no stored items.")
                );
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Zpátky", "Back"),
                                 S_MAIN, ACT_BACK_TO_ROOT);
                SendGossipMenuFor(player, 1, creature->GetGUID());
                return;
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                             T("------------------------------",
                               "------------------------------"),
                             S_WITHDRAW_CATEGORIES, 0);

            AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                             T("Zpátky", "Back"),
                             S_MAIN, ACT_BACK_TO_ROOT);

            SendGossipMenuFor(player, 1, creature->GetGUID());
        }

        // === WITHDRAW – menu subkategorií ===
        void ShowWithdrawSubCategoryMenu(Player* player, Creature* creature, uint8 parentCategoryId)
        {
            uint32 accountId = GetAccountId(player);
            if (!accountId)
            {
                CloseGossipMenuFor(player);
                return;
            }

            uint8 teamId = GetBankTeamId(player);

            ClearGossipMenuFor(player);

            QueryResult r = WorldDatabase.Query(
                "SELECT id, name_cs, name_en "
                "FROM customs.material_bank_category "
                "WHERE parent_id={} "
                "ORDER BY id ASC",
                uint32(parentCategoryId));

            if (!r)
            {
                ShowWithdrawList(player, creature, parentCategoryId, 0);
                return;
            }

            bool anyShown = false;

            do
            {
                Field* f = r->Fetch();
                uint8 categoryId = f[0].Get<uint8>();

                if (!HasStoredItemsInCategory(accountId, teamId, categoryId))
                    continue;

                std::string name = CategoryName(categoryId, f);

                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                                 name, S_WITHDRAW_PAGE,
                                 PackCatPage(categoryId, 0));
                anyShown = true;
            }
            while (r->NextRow());

            if (!anyShown)
            {
                ShowWithdrawList(player, creature, parentCategoryId, 0);
                return;
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                             T("------------------------------",
                               "------------------------------"),
                             S_WITHDRAW_CATEGORY_SELECT, parentCategoryId);

            AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                             T("Zpátky", "Back"),
                             S_WITHDRAW_CATEGORIES, 0);

            SendGossipMenuFor(player, 1, creature->GetGUID());
        }

        // === DEPOSIT – seznam itemů v kategorii (barevný název) ===
        void ShowDepositList(Player* player, Creature* creature, uint8 categoryId, uint32 page)
        {
            uint32 accountId = GetAccountId(player);
            if (!accountId)
            {
                CloseGossipMenuFor(player);
                return;
            }

            std::vector<BagItemEntry> list;
            BuildBagItemList(player, list);

            ClearGossipMenuFor(player);

            CategoryFilter filter = GetCategoryFilter(categoryId);

            std::vector<BagItemEntry> filtered;
            filtered.reserve(list.size());

            for (BagItemEntry const& e : list)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(e.itemEntry);
                if (!proto)
                    continue;

                if (!PassesFilter(proto, filter))
                    continue;

                filtered.push_back(e);
            }

			if (filtered.empty())
			{
				uint8 parentId = GetParentCategoryId(categoryId);
			
				if (parentId == 0)
				{
					ShowDepositCategoryMenu(player, creature);
				}
				else
				{
					ShowDepositSubCategoryMenu(player, creature, parentId);
				}
			
				return;
			}

            uint32 total   = filtered.size();
            uint32 maxPage = (total - 1) / ITEMS_PER_PAGE;
            if (page > maxPage)
                page = maxPage;

            uint32 start = page * ITEMS_PER_PAGE;
            uint32 end   = std::min(start + ITEMS_PER_PAGE, total);

            for (uint32 i = start; i < end; ++i)
            {
                uint32 entry = filtered[i].itemEntry;
                uint64 cnt   = filtered[i].totalCount;

                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                if (!proto)
                    continue;

                char const* hex = QualityHex(proto->Quality);
                std::string name = proto->Name1;
                std::string line = Acore::StringFormat("|c{}[{}]|r x {}", hex, name, cnt);

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, line,
                                 S_DEPOSIT_ITEM, PackCatItem(categoryId, entry));
            }

            if (end > start)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                                 T("------------------------------",
                                   "------------------------------"),
                                 S_DEPOSIT_PAGE, PackCatPage(categoryId, page));
            }

            if (page < maxPage)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Další stránka", "Next page"),
                                 S_DEPOSIT_PAGE, PackCatPage(categoryId, page + 1));
            }

            if (page > 0)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Předchozí stránka", "Previous page"),
                                 S_DEPOSIT_PAGE, PackCatPage(categoryId, page - 1));
            }

            uint8 parentId = GetParentCategoryId(categoryId);

            if (parentId == 0)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Zpátky", "Back"),
                                 S_DEPOSIT_CATEGORIES, 0);
            }
            else
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Zpátky", "Back"),
                                 S_DEPOSIT_CATEGORY_SELECT, parentId);
            }

            SendGossipMenuFor(player, 1, creature->GetGUID());
        }

        // === DEPOSIT – vložení itemu (auto-sort + neduplicitnost) ===
        void HandleDepositItem(Player* player, Creature* creature, uint8 categoryId, uint32 itemEntry)
        {
            uint32 accountId = GetAccountId(player);
            if (!accountId)
            {
                CloseGossipMenuFor(player);
                return;
            }

            uint8 teamId = GetBankTeamId(player);

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
            if (!proto)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Neplatný předmět.", "Invalid item.")
                );
                ShowDepositList(player, creature, categoryId, 0);
                return;
            }

            uint8 usedCategoryId = categoryId;
            bool redirectedExisting = false;
            bool autoSorted = false;

            uint8 existingCategoryId = 0;
            if (FindExistingItemCategory(accountId, teamId, itemEntry, existingCategoryId))
            {
                if (existingCategoryId != categoryId)
                {
                    usedCategoryId = existingCategoryId;
                    redirectedExisting = true;
                }
            }
            else if (categoryId == 0)
            {
                uint8 match = FindMatchingCategoryForItem(itemEntry);
                if (match != 0)
                {
                    usedCategoryId = match;
                    autoSorted = true;
                }
                else
                {
                    usedCategoryId = 0;
                }
            }

            uint64 totalDeposited = 0;

            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    if (item->GetEntry() != itemEntry)
                        continue;

                    if (IsBlockedForDeposit(item))
                        continue;

                    uint32 count = item->GetCount();
                    if (!count)
                        continue;

                    AddToAccountBank(accountId, teamId, usedCategoryId, itemEntry, count);
                    totalDeposited += count;

                    player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                }
            }

            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            {
                Bag* bag = player->GetBagByPos(bagSlot);
                if (!bag)
                    continue;

                uint8 slots = bag->GetBagSize();
                for (uint8 slot = 0; slot < slots; ++slot)
                {
                    Item* item = bag->GetItemByPos(slot);
                    if (!item)
                        continue;

                    if (item->GetEntry() != itemEntry)
                        continue;

                    if (IsBlockedForDeposit(item))
                        continue;

                    uint32 count = item->GetCount();
                    if (!count)
                        continue;

                    AddToAccountBank(accountId, teamId, usedCategoryId, itemEntry, count);
                    totalDeposited += count;

                    player->DestroyItem(bagSlot, slot, true);
                }
            }

            ChatHandler ch(player->GetSession());

            if (totalDeposited == 0)
            {
                ch.SendSysMessage(
                    T("U tohoto předmětu nemáš nic k uložení.",
                      "You have no such item in your bags to deposit.")
                );
            }
            else
            {
                std::string link = BuildItemLink(itemEntry);
                ch.SendSysMessage(Acore::StringFormat(
                    (LangOpt() == Lang::EN)
                        ? "Deposited {}x {} to your account storage."
                        : "Uloženo {}x {} do účtové úschovy.",
                    totalDeposited, link));

                if (redirectedExisting)
                {
                    std::string catName = GetCategoryDisplayName(usedCategoryId);
                    ch.SendSysMessage(Acore::StringFormat(
                        (LangOpt() == Lang::EN)
                            ? "Note: This item was stored in category '{}' because it already exists there."
                            : "Poznámka: Předmět byl uložen do kategorie '{}', protože se v ní již nachází stejný předmět.",
                        catName));
                }
                else if (autoSorted && usedCategoryId != 0)
                {
                    std::string catName = GetCategoryDisplayName(usedCategoryId);
                    ch.SendSysMessage(Acore::StringFormat(
                        (LangOpt() == Lang::EN)
                            ? "Item was automatically sorted into category '{}'."
                            : "Předmět byl automaticky zařazen do kategorie '{}'.",
                        catName));
                }
            }

            ShowDepositList(player, creature, categoryId, 0);
        }

        // === WITHDRAW – seznam itemů v kategorii (barevný název, max stack řeší HandleWithdrawItem) ===
        void ShowWithdrawList(Player* player, Creature* creature, uint8 categoryId, uint32 page)
        {
            uint32 accountId = GetAccountId(player);
            if (!accountId)
            {
                CloseGossipMenuFor(player);
                return;
            }

            uint8 teamId = GetBankTeamId(player);

            ClearGossipMenuFor(player);

            QueryResult r = WorldDatabase.Query(
                "SELECT itemEntry, totalCount "
                "FROM customs.account_material_bank "
                "WHERE accountId={} AND team={} AND categoryId={} AND totalCount>0 "
                "ORDER BY itemEntry ASC",
                accountId, uint32(teamId), uint32(categoryId));

			if (!r)
			{
				// Bez hlášky – jen zpátky do vyššího menu
				uint8 parentId = GetParentCategoryId(categoryId);
			
				if (parentId == 0)
				{
					ShowWithdrawCategoryMenu(player, creature);
				}
				else
				{
					ShowWithdrawSubCategoryMenu(player, creature, parentId);
				}
			
				return;
			}

            struct StoredEntry { uint32 itemEntry; uint64 total; };
            std::vector<StoredEntry> items;

            do
            {
                Field* f = r->Fetch();
                StoredEntry se;
                se.itemEntry = f[0].Get<uint32>();
                se.total     = f[1].Get<uint64>();
                items.push_back(se);
            }
            while (r->NextRow());

            CategoryFilter filter = GetCategoryFilter(categoryId);

            std::vector<StoredEntry> filtered;
            filtered.reserve(items.size());

            for (StoredEntry const& se : items)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(se.itemEntry);
                if (!proto)
                    continue;

                if (!PassesFilter(proto, filter))
                    continue;

                filtered.push_back(se);
            }

			if (filtered.empty())
			{
				uint8 parentId = GetParentCategoryId(categoryId);
			
				if (parentId == 0)
				{
					ShowWithdrawCategoryMenu(player, creature);
				}
				else
				{
					ShowWithdrawSubCategoryMenu(player, creature, parentId);
				}
			
				return;
			}

            uint32 total   = filtered.size();
            uint32 maxPage = total ? (total - 1) / ITEMS_PER_PAGE : 0;
            if (page > maxPage)
                page = maxPage;

            uint32 start = page * ITEMS_PER_PAGE;
            uint32 end   = std::min(start + ITEMS_PER_PAGE, total);

            for (uint32 i = start; i < end; ++i)
            {
                uint32 entry = filtered[i].itemEntry;
                uint64 cnt   = filtered[i].total;

                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                if (!proto)
                    continue;

                char const* hex = QualityHex(proto->Quality);
                std::string name = proto->Name1;
                std::string line = Acore::StringFormat("|c{}[{}]|r x {}", hex, name, cnt);

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, line,
                                 S_WITHDRAW_ITEM, PackCatItem(categoryId, entry));
            }

            if (end > start)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                                 T("------------------------------",
                                   "------------------------------"),
                                 S_WITHDRAW_PAGE, PackCatPage(categoryId, page));
            }

            if (page < maxPage)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Další stránka", "Next page"),
                                 S_WITHDRAW_PAGE, PackCatPage(categoryId, page + 1));
            }

            if (page > 0)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Předchozí stránka", "Previous page"),
                                 S_WITHDRAW_PAGE, PackCatPage(categoryId, page - 1));
            }

            uint8 parentId = GetParentCategoryId(categoryId);

            if (parentId == 0)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Zpátky", "Back"),
                                 S_WITHDRAW_CATEGORIES, 0);
            }
            else
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Zpátky", "Back"),
                                 S_WITHDRAW_CATEGORY_SELECT, parentId);
            }

            SendGossipMenuFor(player, 1, creature->GetGUID());
        }

        // === WITHDRAW – výběr itemu (max 1 stack) ===
        void HandleWithdrawItem(Player* player, Creature* creature, uint8 categoryId, uint32 itemEntry)
        {
            uint32 accountId = GetAccountId(player);
            if (!accountId)
            {
                CloseGossipMenuFor(player);
                return;
            }

            uint8 teamId = GetBankTeamId(player);

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
            if (!proto)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Neplatný předmět.", "Invalid item.")
                );
                ShowWithdrawList(player, creature, categoryId, 0);
                return;
            }

            uint64 total = GetAccountItemCount(accountId, teamId, categoryId, itemEntry);
            if (!total)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("U tohoto předmětu v této kategorii nemáš nic uložené.",
                      "You have no stored amount of this item in this category.")
                );
                ShowWithdrawList(player, creature, categoryId, 0);
                return;
            }

            uint32 maxStack = proto->GetMaxStackSize();
            if (!maxStack)
                maxStack = 1;

            uint64 want = (total >= maxStack) ? uint64(maxStack) : total;

            uint64 moved = MoveFromBankToBags(player, categoryId, itemEntry, want);

            if (!moved)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Inventář je plný, nelze vybrat předměty.",
                      "Your inventory is full, cannot withdraw items.")
                );
            }
            else
            {
                std::string link = BuildItemLink(itemEntry);

                ChatHandler(player->GetSession()).SendSysMessage(Acore::StringFormat(
                    (LangOpt() == Lang::EN)
                        ? "Withdrawn {}x {} from your account storage."
                        : "Vybráno {}x {} z účtové úschovy.",
                    moved, link));
            }

            ShowWithdrawList(player, creature, categoryId, 0);
        }
    };
} // namespace MaterialBank

void RegisterMaterialBankNpc()
{
    new MaterialBank::npc_account_material_bank();
}
