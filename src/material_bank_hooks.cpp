#include "ScriptMgr.h"
#include "AllSpellScript.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"

#include "material_bank.h"

namespace
{
    static bool SpellHasReagents(SpellInfo const* info)
    {
        if (!info)
            return false;

        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            if (info->Reagent[i] > 0 && info->ReagentCount[i] > 0)
                return true;
        }
        return false;
    }

    class MaterialBank_AllSpell : public AllSpellScript
    {
    public:
        MaterialBank_AllSpell() : AllSpellScript("MaterialBank_AllSpell") { }

        void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& /*res*/) override
        {
            if (!spell)
                return;

            Unit* caster = spell->GetCaster();
            if (!caster)
                return;

            Player* pl = caster->ToPlayer();
            if (!pl)
                return;

            SpellInfo const* info = spell->m_spellInfo;
            if (!info)
                return;

            if (!SpellHasReagents(info))
                return;

            MaterialBank::EnsureReagentsFromAccountBank(pl, info);
        }
    };
}

void RegisterMaterialBankHooks()
{
    new MaterialBank_AllSpell();
}
