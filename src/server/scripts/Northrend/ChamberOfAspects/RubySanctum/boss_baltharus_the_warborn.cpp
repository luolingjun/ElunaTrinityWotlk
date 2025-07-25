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
#include "InstanceScript.h"
#include "ruby_sanctum.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

enum BaltharusTexts
{
    SAY_BALTHARUS_INTRO         = 0,    // Your power wanes, ancient one.... Soon you will join your friends.
    SAY_AGGRO                   = 1,    // Ah, the entertainment has arrived.
    SAY_KILL                    = 2,    // Baltharus leaves no survivors! - This world has enough heroes.
    SAY_CLONE                   = 3,    // Twice the pain and half the fun.
    SAY_DEATH                   = 4    // I... didn't see that coming....
};

enum BaltharusSpells
{
    SPELL_BARRIER_CHANNEL       = 76221,
    SPELL_ENERVATING_BRAND      = 74502,
    SPELL_SIPHONED_MIGHT        = 74507,
    SPELL_CLEAVE                = 40504,
    SPELL_BLADE_TEMPEST         = 75125,
    SPELL_CLONE                 = 74511,
    SPELL_REPELLING_WAVE        = 74509,
    SPELL_CLEAR_DEBUFFS         = 34098,
    SPELL_SPAWN_EFFECT          = 64195
};

enum BaltharusEvents
{
    EVENT_BLADE_TEMPEST         = 1,
    EVENT_CLEAVE,
    EVENT_ENERVATING_BRAND,
    EVENT_INTRO_TALK,
    EVENT_SUMMONS_ATTACK,
    EVENT_CLONE
};

enum BaltharusActions
{
    ACTION_CLONE                = 1
};

enum BaltharusPhases
{
    PHASE_ALL                   = 0,
    PHASE_INTRO,
    PHASE_COMBAT
};

// 39751 - Baltharus the Warborn
struct boss_baltharus_the_warborn : public BossAI
{
    boss_baltharus_the_warborn(Creature* creature) : BossAI(creature, DATA_BALTHARUS_THE_WARBORN), _cloneCount(0) { }

    void Reset() override
    {
        _Reset();
        events.SetPhase(PHASE_INTRO);
        instance->SetData(DATA_BALTHARUS_SHARED_HEALTH, me->GetMaxHealth());
        if (Creature* channelTarget = instance->GetCreature(DATA_CRYSTAL_CHANNEL_TARGET))
            DoCast(channelTarget, SPELL_BARRIER_CHANNEL);
        _cloneCount = 0;
    }

    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_INTRO_BALTHARUS:
                me->setActive(true);
                me->SetFarVisible(true);
                events.ScheduleEvent(EVENT_INTRO_TALK, 7s, 0, PHASE_INTRO);
                break;
            case ACTION_CLONE:
            {
                DoCastSelf(SPELL_CLEAR_DEBUFFS, true);
                DoCastSelf(SPELL_CLONE, true);
                DoCastSelf(SPELL_REPELLING_WAVE);
                Talk(SAY_CLONE);
                break;
            }
            default:
                break;
        }
    }

    void JustEngagedWith(Unit* who) override
    {
        me->InterruptNonMeleeSpells(false);
        BossAI::JustEngagedWith(who);
        events.Reset();
        events.SetPhase(PHASE_COMBAT);
        events.ScheduleEvent(EVENT_CLEAVE, 13s, 0, PHASE_COMBAT);
        events.ScheduleEvent(EVENT_ENERVATING_BRAND, 13s, 0, PHASE_COMBAT);
        events.ScheduleEvent(EVENT_BLADE_TEMPEST, 18s, 0, PHASE_COMBAT);
        Talk(SAY_AGGRO);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
        if (Creature* xerestrasza = instance->GetCreature(DATA_XERESTRASZA))
            xerestrasza->AI()->DoAction(ACTION_BALTHARUS_DEATH);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
        summon->SetHealth(me->GetHealth());
        events.ScheduleEvent(EVENT_SUMMONS_ATTACK, 2s);
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (GetDifficulty() == RAID_DIFFICULTY_10MAN_NORMAL)
        {
            if (me->HealthBelowPctDamaged(50, damage) && _cloneCount == 0)
            {
                ++_cloneCount;
                events.ScheduleEvent(EVENT_CLONE, 1ms);
            }
        }
        else
        {
            if (me->HealthBelowPctDamaged(66, damage) && _cloneCount == 0)
            {
                ++_cloneCount;
                events.ScheduleEvent(EVENT_CLONE, 1ms);
            }
            else if (me->HealthBelowPctDamaged(33, damage) && _cloneCount == 1)
            {
                ++_cloneCount;
                events.ScheduleEvent(EVENT_CLONE, 1ms);
            }
        }

        if (me->GetHealth() > damage)
            instance->SetData(DATA_BALTHARUS_SHARED_HEALTH, me->GetHealth() - damage);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        summons.DespawnAll();
        events.Reset();
        _DespawnAtEvade();
    }

    void UpdateAI(uint32 diff) override
    {

        if (!events.IsInPhase(PHASE_INTRO) && !UpdateVictim())
            return;

        if (!events.IsInPhase(PHASE_INTRO))
            me->SetHealth(instance->GetData(DATA_BALTHARUS_SHARED_HEALTH));

        events.Update(diff);

        if (!events.IsInPhase(PHASE_INTRO) && me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_INTRO_TALK:
                    Talk(SAY_BALTHARUS_INTRO);
                    break;
                case EVENT_CLEAVE:
                    DoCastVictim(SPELL_CLEAVE);
                    events.Repeat(20s, 24s);
                    break;
                case EVENT_BLADE_TEMPEST:
                    DoCastSelf(SPELL_BLADE_TEMPEST);
                    events.Repeat(24s);
                    break;
                case EVENT_ENERVATING_BRAND:
                    for (uint8 i = 0; i < RAID_MODE<uint8>(2, 4, 2, 4); i++)
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 45.0f, true, false, -SPELL_ENERVATING_BRAND))
                            DoCast(target, SPELL_ENERVATING_BRAND);
                    events.Repeat(26s);
                    break;
                case EVENT_SUMMONS_ATTACK:
                    summons.DoZoneInCombat(NPC_BALTHARUS_THE_WARBORN_CLONE);
                    break;
                case EVENT_CLONE:
                    DoAction(ACTION_CLONE);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    uint8 _cloneCount;
};

// 39899 - Baltharus the Warborn
struct npc_baltharus_the_warborn_clone : public BossAI
{
    npc_baltharus_the_warborn_clone(Creature* creature) : BossAI(creature, DATA_BALTHARUS_CLONE) { }

    void Reset() override
    {
        DoCastSelf(SPELL_SPAWN_EFFECT);
        me->SetReactState(REACT_DEFENSIVE);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();
        events.Reset();
        events.ScheduleEvent(EVENT_CLEAVE, 11s);
        events.ScheduleEvent(EVENT_BLADE_TEMPEST, 15s);
        events.ScheduleEvent(EVENT_ENERVATING_BRAND, 10s);
    }

    void EnterEvadeMode(EvadeReason /*why*/) override { }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // Setting DATA_BALTHARUS_SHARED_HEALTH to 0 when killed would bug the boss.
        if (me->GetHealth() > damage)
            instance->SetData(DATA_BALTHARUS_SHARED_HEALTH, me->GetHealth() - damage);
    }

    void JustDied(Unit* killer) override
    {
        // This is here because DamageTaken wont trigger if the damage is deadly.
        if (Creature* baltharus = instance->GetCreature(DATA_BALTHARUS_THE_WARBORN))
            Unit::Kill(killer, baltharus);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        me->SetHealth(instance->GetData(DATA_BALTHARUS_SHARED_HEALTH));

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CLEAVE:
                    DoCastVictim(SPELL_CLEAVE);
                    events.Repeat(20s, 24s);
                    break;
                case EVENT_BLADE_TEMPEST:
                    DoCastVictim(SPELL_BLADE_TEMPEST);
                    events.Repeat(24s);
                    break;
                case EVENT_ENERVATING_BRAND:
                    for (uint8 i = 0; i < RAID_MODE<uint8>(2, 4, 2, 4); i++)
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 45.0f, true, false, -SPELL_ENERVATING_BRAND))
                            DoCast(target, SPELL_ENERVATING_BRAND);
                    events.Repeat(26s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }
};

// 74505 - Enervating Brand
class spell_baltharus_enervating_brand_trigger : public SpellScript
{
    PrepareSpellScript(spell_baltharus_enervating_brand_trigger);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SIPHONED_MIGHT });
    }

    void HandleSiphonedMight()
    {
        if (SpellInfo const* spellInfo = GetTriggeringSpell())
            if (Aura* triggerAura = GetCaster()->GetAura(spellInfo->Id))
                if (Unit* caster = triggerAura->GetCaster())
                    GetHitUnit()->CastSpell(caster, SPELL_SIPHONED_MIGHT, true);
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_baltharus_enervating_brand_trigger::HandleSiphonedMight);
    }
};

void AddSC_boss_baltharus_the_warborn()
{
    RegisterRubySanctumCreatureAI(boss_baltharus_the_warborn);
    RegisterRubySanctumCreatureAI(npc_baltharus_the_warborn_clone);
    RegisterSpellScript(spell_baltharus_enervating_brand_trigger);
}
