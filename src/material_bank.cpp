#include "material_bank.h"

#include "DatabaseEnv.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "StringFormat.h"
#include "Log.h"
#include "WorldSession.h"
#include "Config.h"

#include <algorithm>

namespace MaterialBank
{
    namespace
    {
        static constexpr uint8 DEFAULT_CATEGORY = 0;

        static uint32 GetAccountId(Player* player)
        {
            if (!player)
                return 0;

            if (WorldSession* session = player->GetSession())
                return session->GetAccountId();

            return 0;
        }

        static bool IsSharedAcrossFactions()
        {
            return sConfigMgr->GetOption<bool>("MaterialBank.SharedAcrossFactions", false);
        }
    }

    uint8 GetBankTeamId(Player* player)
    {
        if (!player)
            return 0;

        if (IsSharedAcrossFactions())
        {
            return 0;
        }

        return static_cast<uint8>(player->GetTeamId());
    }

    uint64 GetAccountItemCount(uint32 accountId, uint8 teamId, uint8 categoryId, uint32 itemEntry)
    {
        if (!accountId || !itemEntry)
            return 0;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT totalCount FROM customs.account_material_bank "
                "WHERE accountId={} AND team={} AND categoryId={} AND itemEntry={}",
                accountId, uint32(teamId), uint32(categoryId), itemEntry))
        {
            Field* f = r->Fetch();
            return f[0].Get<uint64>();
        }

        return 0;
    }

    uint64 GetAccountItemCount(uint32 accountId, uint8 teamId, uint32 itemEntry)
    {
        return GetAccountItemCount(accountId, teamId, DEFAULT_CATEGORY, itemEntry);
    }

    uint64 GetAccountItemCount(uint32 accountId, uint32 itemEntry)
    {
        return GetAccountItemCount(accountId, 0, DEFAULT_CATEGORY, itemEntry);
    }

    void AddToAccountBank(uint32 accountId, uint8 teamId, uint8 categoryId, uint32 itemEntry, uint64 count)
    {
        if (!accountId || !itemEntry || !count)
            return;

        WorldDatabase.DirectExecute(Acore::StringFormat(
            "INSERT INTO customs.account_material_bank (accountId, team, categoryId, itemEntry, totalCount) "
            "VALUES ({}, {}, {}, {}, {}) "
            "ON DUPLICATE KEY UPDATE totalCount = totalCount + {}",
            accountId, uint32(teamId), uint32(categoryId), itemEntry, count, count).c_str());
    }

    void AddToAccountBank(uint32 accountId, uint8 teamId, uint32 itemEntry, uint64 count)
    {
        AddToAccountBank(accountId, teamId, DEFAULT_CATEGORY, itemEntry, count);
    }

    void AddToAccountBank(uint32 accountId, uint32 itemEntry, uint64 count)
    {
        AddToAccountBank(accountId, 0, DEFAULT_CATEGORY, itemEntry, count);
    }

    uint64 RemoveFromAccountBank(uint32 accountId, uint8 teamId, uint8 categoryId, uint32 itemEntry, uint64 count)
    {
        if (!accountId || !itemEntry || !count)
            return 0;

        uint64 have = GetAccountItemCount(accountId, teamId, categoryId, itemEntry);
        if (!have)
            return 0;

        if (count > have)
            count = have;

        uint64 newCount = have - count;

        if (newCount == 0)
        {
            WorldDatabase.DirectExecute(Acore::StringFormat(
                "DELETE FROM customs.account_material_bank "
                "WHERE accountId={} AND team={} AND categoryId={} AND itemEntry={}",
                accountId, uint32(teamId), uint32(categoryId), itemEntry).c_str());
        }
        else
        {
            WorldDatabase.DirectExecute(Acore::StringFormat(
                "UPDATE customs.account_material_bank "
                "SET totalCount = {} "
                "WHERE accountId={} AND team={} AND categoryId={} AND itemEntry={}",
                newCount, accountId, uint32(teamId), uint32(categoryId), itemEntry).c_str());
        }

        return count;
    }

    uint64 RemoveFromAccountBank(uint32 accountId, uint8 teamId, uint32 itemEntry, uint64 count)
    {
        return RemoveFromAccountBank(accountId, teamId, DEFAULT_CATEGORY, itemEntry, count);
    }

    uint64 RemoveFromAccountBank(uint32 accountId, uint32 itemEntry, uint64 count)
    {
        return RemoveFromAccountBank(accountId, 0, DEFAULT_CATEGORY, itemEntry, count);
    }


    uint64 MoveFromBankToBags(Player* player, uint8 categoryId, uint32 itemEntry, uint64 missing)
    {
        if (!player || !itemEntry || !missing)
            return 0;

        uint32 accountId = GetAccountId(player);
        if (!accountId)
            return 0;

        uint8 teamId = GetBankTeamId(player);

        uint64 bankCount = GetAccountItemCount(accountId, teamId, categoryId, itemEntry);
        if (!bankCount)
            return 0;

        if (missing > bankCount)
            missing = bankCount;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
        if (!proto)
            return 0;

        uint32 maxStack = proto->GetMaxStackSize();
        if (!maxStack)
            maxStack = 1;

        uint64 remaining  = missing;
        uint64 addedTotal = 0;

        while (remaining > 0)
        {
            uint32 batch = static_cast<uint32>(std::min<uint64>(remaining, maxStack));

            if (Item* item = Item::CreateItem(itemEntry, batch, player))
            {
                ItemPosCountVec dest;

                if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK)
                {
                    player->StoreItem(dest, item, true);
                    remaining  -= batch;
                    addedTotal += batch;
                }
                else
                {
                    delete item;
                    break;
                }
            }
            else
            {
                break;
            }
        }

        if (!addedTotal)
            return 0;

        uint64 removed = RemoveFromAccountBank(accountId, teamId, categoryId, itemEntry, addedTotal);
        if (removed < addedTotal)
        {
            LOG_WARN("module", "MaterialBank: removed {} < added {} for account {} team {} category {} item {}",
                     removed, addedTotal, accountId, uint32(teamId), uint32(categoryId), itemEntry);
        }

        return removed;
    }

    uint64 MoveFromBankToBags(Player* player, uint32 itemEntry, uint64 missing)
    {
        if (!player || !itemEntry || !missing)
            return 0;

        uint32 accountId = GetAccountId(player);
        if (!accountId)
            return 0;

        uint8 teamId = GetBankTeamId(player);

        QueryResult r = WorldDatabase.Query(
            "SELECT categoryId, totalCount FROM customs.account_material_bank "
            "WHERE accountId={} AND team={} AND itemEntry={} AND totalCount>0 "
            "ORDER BY categoryId ASC",
            accountId, uint32(teamId), itemEntry);

        if (!r)
            return 0;

        uint64 remaining  = missing;
        uint64 movedTotal = 0;

        do
        {
            Field* f = r->Fetch();
            uint8 categoryId = f[0].Get<uint8>();

            if (!remaining)
                break;

            uint64 moved = MoveFromBankToBags(player, categoryId, itemEntry, remaining);
            if (!moved)
            {
                break;
            }

            movedTotal += moved;
            if (movedTotal >= missing)
                break;

            remaining = missing - movedTotal;
        }
        while (r->NextRow());

        return movedTotal;
    }

    uint64 GetBankItemCount(Player* player, uint8 categoryId, uint32 itemEntry)
    {
        if (!player || !itemEntry)
            return 0;

        uint32 accountId = GetAccountId(player);
        if (!accountId)
            return 0;

        uint8 teamId = GetBankTeamId(player);
        return GetAccountItemCount(accountId, teamId, categoryId, itemEntry);
    }

    uint64 GetBankItemCount(Player* player, uint32 itemEntry)
    {
        if (!player || !itemEntry)
            return 0;

        uint32 accountId = GetAccountId(player);
        if (!accountId)
            return 0;

        uint8 teamId = GetBankTeamId(player);

        if (QueryResult r = WorldDatabase.Query(
                "SELECT SUM(totalCount) FROM customs.account_material_bank "
                "WHERE accountId={} AND team={} AND itemEntry={}",
                accountId, uint32(teamId), itemEntry))
        {
            Field* f = r->Fetch();
            if (f[0].IsNull())
                return 0;

            return f[0].Get<uint64>();
        }

        return 0;
    }

    void EnsureReagentsFromAccountBank(Player* player, SpellInfo const* spellInfo)
    {
        if (!player || !spellInfo)
            return;

        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            int32 reagentEntry = spellInfo->Reagent[i];
            int32 reagentCount = spellInfo->ReagentCount[i];

            if (reagentEntry <= 0 || reagentCount <= 0)
                continue;

            uint32 itemEntry = static_cast<uint32>(reagentEntry);
            uint32 needed    = static_cast<uint32>(reagentCount);

            uint32 haveInBags = player->GetItemCount(itemEntry, false);

            if (haveInBags >= needed)
                continue;

            uint64 missing = static_cast<uint64>(needed - haveInBags);

            MoveFromBankToBags(player, DEFAULT_CATEGORY, itemEntry, missing);
        }
    }
}
