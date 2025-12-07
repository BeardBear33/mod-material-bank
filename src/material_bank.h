#pragma once

#include "Player.h"
#include "SpellInfo.h"

namespace MaterialBank
{
    uint64 GetAccountItemCount(uint32 accountId, uint32 itemEntry);

    uint64 GetAccountItemCount(uint32 accountId, uint8 teamId, uint32 itemEntry);

    void AddToAccountBank(uint32 accountId, uint32 itemEntry, uint64 count);

    void AddToAccountBank(uint32 accountId, uint8 teamId, uint32 itemEntry, uint64 count);

    uint64 RemoveFromAccountBank(uint32 accountId, uint32 itemEntry, uint64 count);

    uint64 RemoveFromAccountBank(uint32 accountId, uint8 teamId, uint32 itemEntry, uint64 count);

    uint64 MoveFromBankToBags(Player* player, uint32 itemEntry, uint64 missing);

    uint64 GetBankItemCount(Player* player, uint32 itemEntry);

    void EnsureReagentsFromAccountBank(Player* player, SpellInfo const* spellInfo);

    uint8 GetBankTeamId(Player* player);
}
