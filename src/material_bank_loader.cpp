#include "ScriptMgr.h"

void RegisterMaterialBankCustomsUpdater();
void RegisterMaterialBankHooks();
void RegisterMaterialBankNpc();

namespace MaterialBank
{
    void RegisterMaterialBankCommands();
}

void Addmod_material_bankScripts()
{
	RegisterMaterialBankCustomsUpdater();
    RegisterMaterialBankHooks();
    RegisterMaterialBankNpc();
    MaterialBank::RegisterMaterialBankCommands();
}
