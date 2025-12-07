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

    // BEZPEČNOST: itemy, které nepůjde vložit
    static bool IsBlockedForDeposit(Item* item)
    {
        if (!item)
            return true;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return true;

        // Quest item
        if (proto->Class == ITEM_CLASS_QUEST)
            return true;

        // Soulbound
        if (item->IsSoulBound())
            return true;

        return false;
    }

    class npc_account_material_bank : public CreatureScript
    {
    public:
        npc_account_material_bank() : CreatureScript("npc_account_material_bank") { }

        // Akce pro hlavní menu
        enum Actions : uint32
        {
            ACT_ROOT         = 100,
            ACT_DEPOSIT      = 110,
            ACT_WITHDRAW     = 120,
            ACT_BACK_TO_ROOT = 190
        };

        enum Senders : uint32
        {
            S_MAIN          = GOSSIP_SENDER_MAIN,
            S_DEPOSIT_PAGE  = 2001,
            S_DEPOSIT_ITEM  = 2002,
            S_WITHDRAW_PAGE = 2003,
            S_WITHDRAW_ITEM = 2004
        };

        // kolik itemů na jednu stránku
        static constexpr uint32 ITEMS_PER_PAGE = 12;

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            ClearGossipMenuFor(player);

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                             T("Vložit předměty", "Deposit items"),
                             S_MAIN, ACT_DEPOSIT);

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                             T("Vybrat předměty", "Withdraw items"),
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
                            return OnGossipHello(player, creature);

                        case ACT_DEPOSIT:
                            ShowDepositList(player, creature, 0);
                            return true;

                        case ACT_WITHDRAW:
                            ShowWithdrawList(player, creature, 0);
                            return true;

                        default:
                            break;
                    }
                    break;
                }

                case S_DEPOSIT_PAGE:
                {
                    uint32 page = action;
                    ShowDepositList(player, creature, page);
                    return true;
                }

                case S_WITHDRAW_PAGE:
                {
                    uint32 page = action;
                    ShowWithdrawList(player, creature, page);
                    return true;
                }

                case S_DEPOSIT_ITEM:
                {
                    uint32 itemEntry = action;
                    HandleDepositItem(player, creature, itemEntry);
                    return true;
                }

                case S_WITHDRAW_ITEM:
                {
                    uint32 itemEntry = action;
                    HandleWithdrawItem(player, creature, itemEntry);
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

        // Nasbírá seznam PŘEDMĚTŮ z bagů hráče (sečtené po itemEntry)
        // – filtruje quest itemy a soulbound
        static void BuildBagItemList(Player* player, std::vector<BagItemEntry>& out)
        {
            out.clear();
            if (!player)
                return;

            std::unordered_map<uint32, uint64> counts;

            // 1) Hlavní batoh (bag 0)
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

            // 2) Ostatní bagy
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

        // =========================
        // VLOŽIT PŘEDMĚTY – výpis s pagingem
        // =========================
        void ShowDepositList(Player* player, Creature* creature, uint32 page)
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

            if (list.empty())
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

            uint32 total   = list.size();
            uint32 maxPage = (total - 1) / ITEMS_PER_PAGE;
            if (page > maxPage)
                page = maxPage;

            uint32 start = page * ITEMS_PER_PAGE;
            uint32 end   = std::min(start + ITEMS_PER_PAGE, total);

            for (uint32 i = start; i < end; ++i)
            {
                uint32 entry = list[i].itemEntry;
                uint64 cnt   = list[i].totalCount;

                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                if (!proto)
                    continue;

                std::string name = proto->Name1;
                std::string line = Acore::StringFormat("[{}] x {}", name, cnt);

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, line,
                                 S_DEPOSIT_ITEM, entry);
            }

            // Separator
            if (end > start)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                                 T("------------------------------",
                                   "------------------------------"),
                                 S_DEPOSIT_PAGE, page);
            }

            // Navigace
            if (page < maxPage)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Další stránka", "Next page"),
                                 S_DEPOSIT_PAGE, page + 1);
            }

            if (page > 0)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Předchozí stránka", "Previous page"),
                                 S_DEPOSIT_PAGE, page - 1);
            }

            AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                             T("Zpátky", "Back"),
                             S_MAIN, ACT_BACK_TO_ROOT);

            SendGossipMenuFor(player, 1, creature->GetGUID());
        }

        // Vloží VŠECHNY kusy daného itemu z bagů do účtové banky (pro aktuální frakci)
        void HandleDepositItem(Player* player, Creature* creature, uint32 itemEntry)
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
                ShowDepositList(player, creature, 0);
                return;
            }

            uint64 totalDeposited = 0;

            // 1) hlavní batoh
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

                    AddToAccountBank(accountId, teamId, itemEntry, count);
                    totalDeposited += count;

                    player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                }
            }

            // 2) ostatní bagy
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

                    AddToAccountBank(accountId, teamId, itemEntry, count);
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
                ch.SendSysMessage(Acore::StringFormat(
                    (LangOpt() == Lang::EN)
                        ? "Deposited {}x [{}] to your account storage."
                        : "Uloženo {}x [{}] do účtové úschovy.",
                    totalDeposited, proto->Name1));
            }

            ShowDepositList(player, creature, 0);
        }

        // =========================
        // SEZNAM ULOŽENÝCH PŘEDMĚTŮ – paging (pro frakci hráče)
        // =========================
        void ShowWithdrawList(Player* player, Creature* creature, uint32 page)
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
                "WHERE accountId={} AND team={} AND totalCount>0 "
                "ORDER BY itemEntry ASC",
                accountId, teamId);

            if (!r)
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

            if (items.empty())
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Nemáš uložený žádný předmět.",
                      "You have no stored items.")
                );
            }

            uint32 total   = items.size();
            uint32 maxPage = total ? (total - 1) / ITEMS_PER_PAGE : 0;
            if (page > maxPage)
                page = maxPage;

            uint32 start = page * ITEMS_PER_PAGE;
            uint32 end   = std::min(start + ITEMS_PER_PAGE, total);

            for (uint32 i = start; i < end; ++i)
            {
                uint32 entry = items[i].itemEntry;
                uint64 cnt   = items[i].total;

                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                if (!proto)
                    continue;

                std::string name = proto->Name1;
                std::string line = Acore::StringFormat("[{}] x {}", name, cnt);

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, line,
                                 S_WITHDRAW_ITEM, entry);
            }

            // Separator
            if (end > start)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                                 T("------------------------------",
                                   "------------------------------"),
                                 S_WITHDRAW_PAGE, page);
            }

            // Navigace
            if (page < maxPage)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Další stránka", "Next page"),
                                 S_WITHDRAW_PAGE, page + 1);
            }

            if (page > 0)
            {
                AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                                 T("Předchozí stránka", "Previous page"),
                                 S_WITHDRAW_PAGE, page - 1);
            }

            AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                             T("Zpátky", "Back"),
                             S_MAIN, ACT_BACK_TO_ROOT);

            SendGossipMenuFor(player, 1, creature->GetGUID());
        }

        // =========================
        // VÝBĚR KONKRÉTNÍHO PŘEDMĚTU
        // =========================
        void HandleWithdrawItem(Player* player, Creature* creature, uint32 itemEntry)
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
                ShowWithdrawList(player, creature, 0);
                return;
            }

            uint64 total = GetAccountItemCount(accountId, teamId, itemEntry);
            if (!total)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("U tohoto předmětu nemáš nic uložené.",
                      "You have no stored amount of this item.")
                );
                ShowWithdrawList(player, creature, 0);
                return;
            }

            uint64 want  = total;
            uint64 moved = MoveFromBankToBags(player, itemEntry, want);

            if (!moved)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Inventář je plný, nelze vybrat předměty.",
                      "Your inventory is full, cannot withdraw items.")
                );
            }
            else
            {
                ChatHandler(player->GetSession()).SendSysMessage(Acore::StringFormat(
                    (LangOpt() == Lang::EN)
                        ? "Withdrawn {}x [{}] from your account storage."
                        : "Vybráno {}x [{}] z účtové úschovy.",
                    moved, proto->Name1));
            }

            ShowWithdrawList(player, creature, 0);
        }
    };
} // namespace MaterialBank

void RegisterMaterialBankNpc()
{
    new MaterialBank::npc_account_material_bank();
}
