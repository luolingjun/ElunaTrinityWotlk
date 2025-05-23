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

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "scholomance.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

enum GandlingTexts
{
    SAY_SUMMONED                 = 0
};

enum GandlingSpells
{
    SPELL_ARCANE_MISSILES       = 15790,
    SPELL_SHADOW_SHIELD         = 12040,
    SPELL_CURSE                 = 18702,
    SPELL_SHADOW_PORTAL         = 17950
};

enum GandlingEvents
{
    EVENT_ARCANE_MISSILES = 1,
    EVENT_SHADOW_SHIELD,
    EVENT_CURSE,
    EVENT_SHADOW_PORTAL
};

// 1853 - Darkmaster Gandling
struct boss_darkmaster_gandling : public BossAI
{
    boss_darkmaster_gandling(Creature* creature) : BossAI(creature, DATA_DARKMASTER_GANDLING) { }

    void Reset() override
    {
        _Reset();
        if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_GANDLING)))
            gate->SetGoState(GO_STATE_ACTIVE);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_GANDLING)))
            gate->SetGoState(GO_STATE_ACTIVE);
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_ARCANE_MISSILES, 4500ms);
        events.ScheduleEvent(EVENT_SHADOW_SHIELD, 12s);
        events.ScheduleEvent(EVENT_CURSE, 2s);
        events.ScheduleEvent(EVENT_SHADOW_PORTAL, 15s);

        if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_GANDLING)))
            gate->SetGoState(GO_STATE_READY);
    }

    void IsSummonedBy(WorldObject* /*summoner*/) override
    {
        Talk(SAY_SUMMONED);
        me->GetMotionMaster()->MoveRandom(5);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_ARCANE_MISSILES:
                    DoCastVictim(SPELL_ARCANE_MISSILES);
                    events.Repeat(8s);
                    break;
                case EVENT_SHADOW_SHIELD:
                    DoCastSelf(SPELL_SHADOW_SHIELD);
                    events.Repeat(14s, 28s);
                    break;
                case EVENT_CURSE:
                    DoCastVictim(SPELL_CURSE);
                    events.Repeat(15s, 27s);
                    break;
                case EVENT_SHADOW_PORTAL:
                    if (HealthAbovePct(3))
                    {
                        DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_SHADOW_PORTAL);
                        events.Repeat(17s, 27s);
                    }
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }
};

enum Rooms
{
    ROOM_HALL_OF_SECRETS        = 0,
    ROOM_HALL_OF_THE_DAMNED     = 1,
    ROOM_THE_COVEN              = 2,
    ROOM_THE_SHADOW_VAULT       = 3,
    ROOM_BAROV_FAMILY_VAULT     = 4,
    ROOM_VAULT_OF_THE_RAVENIAN  = 5
};

enum SPSpells
{
    SPELL_SHADOW_PORTAL_HALLOFSECRETS          = 17863,
    SPELL_SHADOW_PORTAL_HALLOFTHEDAMNED        = 17939,
    SPELL_SHADOW_PORTAL_THECOVEN               = 17943,
    SPELL_SHADOW_PORTAL_THESHADOWVAULT         = 17944,
    SPELL_SHADOW_PORTAL_BAROVFAMILYVAULT       = 17946,
    SPELL_SHADOW_PORTAL_VAULTOFTHERAVENIAN     = 17948
};

// 17950 - Shadow Portal
class spell_shadow_portal : public SpellScript
{
    PrepareSpellScript(spell_shadow_portal);

    bool Load() override
    {
        _instance = GetCaster()->GetInstanceScript();
        return InstanceHasScript(GetCaster(), ScholomanceScriptName);
    }

    void HandleCast(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        uint8 attempts = 0;
        uint32 spellId = 0;

        while (!spellId)
        {
            if (attempts++ >= 6) break;

            switch (urand(0, 5))
            {
                case ROOM_HALL_OF_SECRETS:
                    if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_RAVENIAN)))
                        if (go->GetGoState() == GO_STATE_ACTIVE)
                            spellId = SPELL_SHADOW_PORTAL_HALLOFSECRETS;
                    break;
                case ROOM_HALL_OF_THE_DAMNED:
                    if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_THEOLEN)))
                        if (go->GetGoState() == GO_STATE_ACTIVE)
                            spellId = SPELL_SHADOW_PORTAL_HALLOFTHEDAMNED;
                    break;
                case ROOM_THE_COVEN:
                    if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_MALICIA)))
                        if (go->GetGoState() == GO_STATE_ACTIVE)
                            spellId = SPELL_SHADOW_PORTAL_THECOVEN;
                    break;
                case ROOM_THE_SHADOW_VAULT:
                    if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_ILLUCIA)))
                        if (go->GetGoState() == GO_STATE_ACTIVE)
                            spellId = SPELL_SHADOW_PORTAL_THESHADOWVAULT;
                    break;
                case ROOM_BAROV_FAMILY_VAULT:
                    if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_BAROV)))
                        if (go->GetGoState() == GO_STATE_ACTIVE)
                            spellId = SPELL_SHADOW_PORTAL_BAROVFAMILYVAULT;
                    break;
                case ROOM_VAULT_OF_THE_RAVENIAN:
                    if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_POLKELT)))
                        if (go->GetGoState() == GO_STATE_ACTIVE)
                            spellId = SPELL_SHADOW_PORTAL_VAULTOFTHERAVENIAN;
                    break;
            }

            if (spellId)
                GetHitUnit()->CastSpell(GetHitUnit(), spellId);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_shadow_portal::HandleCast, EFFECT_0, SPELL_EFFECT_DUMMY);
    }

    InstanceScript* _instance = nullptr;
};

// Script for Shadow Portal spells 17863, 17939, 17943, 17944, 17946, 17948
Position const SummonPos[18] =
{
    // Hall of Secrects

    // The Hall of the damned
    { 177.9624f, -68.23893f, 84.95197f, 3.228859f },
    { 183.7705f, -61.43489f, 84.92424f, 5.148721f },
    { 184.7035f, -77.74805f, 84.92424f, 4.660029f },
    // The Coven
    { 111.7203f, -1.105035f, 85.45985f, 3.961897f },
    { 118.0079f, 6.430664f, 85.31169f, 2.408554f },
    { 120.0276f, -7.496636f, 85.31169f, 2.984513f },
    // The Shadow Vault
    { 245.3716f, 0.628038f, 72.73877f, 0.01745329f },
    { 240.9920f, 3.405653f, 72.73877f, 6.143559f },
    { 240.9543f, -3.182943f, 72.73877f, 0.2268928f },
    // Barov Family Vault
    { 181.8245f, -42.58117f, 75.4812f, 4.660029f },
    { 177.7456f, -42.74745f, 75.4812f, 4.886922f },
    { 185.6157f, -42.91200f, 75.4812f, 4.45059f },
    // Vault of the Ravenian

};

enum Creatures
{
    NPC_RISEN_GUARDIAN                  = 11598
};

enum ScriptEventId
{
    SPELL_EVENT_HALLOFSECRETS          = 5618,
    SPELL_EVENT_HALLOFTHEDAMNED        = 5619,
    SPELL_EVENT_THECOVEN               = 5620,
    SPELL_EVENT_THESHADOWVAULT         = 5621,
    SPELL_EVENT_BAROVFAMILYVAULT       = 5622,
    SPELL_EVENT_VAULTOFTHERAVENIAN     = 5623
};

// 17863, 17939, 17943, 17944, 17946, 17948 - Shadow Portal
class spell_shadow_portal_rooms : public SpellScript
{
    PrepareSpellScript(spell_shadow_portal_rooms);

    bool Load() override
    {
        _instance = GetCaster()->GetInstanceScript();
        return InstanceHasScript(GetCaster(), ScholomanceScriptName);
    }

    void HandleSendEvent(SpellEffIndex /*effIndex*/)
    {
        // If only one player in threat list fail spell

        Unit* caster = GetCaster();

        int8 pos_to_summon = 0;
        int8 phase_to_set = 0;
        int32 gate_to_close = 0;

        switch (GetEffectInfo().MiscValue)
        {
            case SPELL_EVENT_HALLOFSECRETS:
                pos_to_summon = 0; // Not yet spawned
                phase_to_set = 1;
                gate_to_close = GO_GATE_RAVENIAN;
                break;
            case SPELL_EVENT_HALLOFTHEDAMNED:
                pos_to_summon = 0;
                phase_to_set = 2;
                gate_to_close = GO_GATE_THEOLEN;
                break;
            case SPELL_EVENT_THECOVEN:
                pos_to_summon = 3;
                phase_to_set = 3;
                gate_to_close = GO_GATE_MALICIA;
                break;
            case SPELL_EVENT_THESHADOWVAULT:
                pos_to_summon = 6;
                phase_to_set = 4;
                gate_to_close = GO_GATE_ILLUCIA;
                break;
            case SPELL_EVENT_BAROVFAMILYVAULT:
                pos_to_summon = 9;
                phase_to_set = 5;
                gate_to_close = GO_GATE_BAROV;
                break;
            case SPELL_EVENT_VAULTOFTHERAVENIAN:
                pos_to_summon = 0; // Not yet spawned
                phase_to_set = 6;
                gate_to_close = GO_GATE_POLKELT;
                break;
            default:
                break;
        }

        if (gate_to_close)
        {
            for (uint8 i = 0; i < 3; ++i)
            {
                if (Creature* Summoned = caster->SummonCreature(NPC_RISEN_GUARDIAN, SummonPos[pos_to_summon++], TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 2min))
                {
                    Summoned->GetMotionMaster()->MoveRandom(5);
                    Summoned->AI()->SetData(0, phase_to_set);
                }
            }

            if (GameObject* gate = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(gate_to_close)))
                gate->SetGoState(GO_STATE_READY);
        }
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_shadow_portal_rooms::HandleSendEvent, EFFECT_1, SPELL_EFFECT_SEND_EVENT);
    }

    InstanceScript* _instance = nullptr;
};

void AddSC_boss_darkmaster_gandling()
{
    RegisterScholomanceCreatureAI(boss_darkmaster_gandling);
    RegisterSpellScript(spell_shadow_portal);
    RegisterSpellScript(spell_shadow_portal_rooms);
}
