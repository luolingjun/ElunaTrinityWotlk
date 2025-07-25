/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
This placeholder for the instance is needed for dungeon finding to be able
to give credit after the boss defined in lastEncounterDungeon is killed.
Without it, the party doing random dungeon won't get satchel of spoils and
gets instead the deserter debuff.
*/

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Unit.h"
#include "the_underbog.h"

class instance_the_underbog : public InstanceMapScript
{
public:
    instance_the_underbog() : InstanceMapScript(TheUndebogScriptName, 546) { }

    struct instance_the_underbog_InstanceMapScript : public InstanceScript
    {
        instance_the_underbog_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(TheUndebogDataHeader);
            SetBossNumber(TheUnderbogBossCount);
        }

        void OnUnitDeath(Unit* unit) override
        {
            switch (unit->GetEntry())
            {
                case NPC_GHAZAN:
                    SetBossState(DATA_GHAZAN, DONE);
                    break;
                default:
                    break;
            }
        }
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_the_underbog_InstanceMapScript(map);
    }
};

void AddSC_instance_the_underbog()
{
    new instance_the_underbog();
}
