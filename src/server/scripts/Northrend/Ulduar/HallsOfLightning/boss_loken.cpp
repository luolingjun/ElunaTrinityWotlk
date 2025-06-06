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

/* ScriptData
SDName: Boss Loken
SD%Complete: 60%
SDComment: Missing intro.
SDCategory: Halls of Lightning
EndScriptData */

#include "ScriptMgr.h"
#include "halls_of_lightning.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "SpellMgr.h"
#include "SpellScript.h"

enum LokenTexts
{
    SAY_INTRO_1                                   = 0,
    SAY_INTRO_2                                   = 1,
    SAY_AGGRO                                     = 2,
    SAY_NOVA                                      = 3,
    SAY_SLAY                                      = 4,
    SAY_75HEALTH                                  = 5,
    SAY_50HEALTH                                  = 6,
    SAY_25HEALTH                                  = 7,
    SAY_DEATH                                     = 8,
    EMOTE_NOVA                                    = 9
};

enum LokenSpells
{
    SPELL_ARC_LIGHTNING                           = 52921,
    SPELL_LIGHTNING_NOVA                          = 52960,

    SPELL_PULSING_SHOCKWAVE                       = 52961,
    SPELL_PULSING_SHOCKWAVE_AURA                  = 59414
};

enum LokenEvents
{
    EVENT_ARC_LIGHTNING = 1,
    EVENT_LIGHTNING_NOVA,
    EVENT_RESUME_PULSING_SHOCKWAVE,
    EVENT_INTRO_DIALOGUE
};

enum LokenPhases
{
    // Phases are used to allow executing the intro event while UpdateVictim() returns false and convenience.
    PHASE_INTRO = 1,
    PHASE_NORMAL
};

enum LokenMisc
{
    ACHIEV_TIMELY_DEATH_START_EVENT               = 20384
};

/*######
## Boss Loken
######*/

struct boss_loken : public BossAI
{
    boss_loken(Creature* creature) : BossAI(creature, BOSS_LOKEN)
    {
        Initialize();
        _isIntroDone = false;
    }

    void Initialize()
    {
        _healthAmountModifier = 1;
    }

    void Reset() override
    {
        Initialize();
        _Reset();
        instance->DoStopTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_TIMELY_DEATH_START_EVENT);
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        events.SetPhase(PHASE_NORMAL);
        events.ScheduleEvent(EVENT_ARC_LIGHTNING, 15s);
        events.ScheduleEvent(EVENT_LIGHTNING_NOVA, 20s);
        events.ScheduleEvent(EVENT_RESUME_PULSING_SHOCKWAVE, 1s);
        instance->DoStartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_TIMELY_DEATH_START_EVENT);
    }

    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);
        _JustDied();
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_PULSING_SHOCKWAVE_AURA, true, true);
    }

    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!_isIntroDone && me->IsValidAttackTarget(who) && me->IsWithinDistInMap(who, 40.0f))
        {
            _isIntroDone = true;
            Talk(SAY_INTRO_1);
            events.ScheduleEvent(EVENT_INTRO_DIALOGUE, 20s, 0, PHASE_INTRO);
        }
        BossAI::MoveInLineOfSight(who);
    }

    void UpdateAI(uint32 diff) override
    {
        if (events.IsInPhase(PHASE_NORMAL) && !UpdateVictim())
            return;

        events.Update(diff);

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_ARC_LIGHTNING:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_ARC_LIGHTNING);
                    events.ScheduleEvent(EVENT_ARC_LIGHTNING, 15s, 16s);
                    break;
                case EVENT_LIGHTNING_NOVA:
                    Talk(SAY_NOVA);
                    Talk(EMOTE_NOVA);
                    DoCastAOE(SPELL_LIGHTNING_NOVA);
                    me->RemoveAurasDueToSpell(sSpellMgr->GetSpellIdForDifficulty(SPELL_PULSING_SHOCKWAVE, me));
                    events.ScheduleEvent(EVENT_RESUME_PULSING_SHOCKWAVE, DUNGEON_MODE(5s, 4s)); // Pause Pulsing Shockwave aura
                    events.ScheduleEvent(EVENT_LIGHTNING_NOVA, 20s, 21s);
                    break;
                case EVENT_RESUME_PULSING_SHOCKWAVE:
                    DoCast(me, SPELL_PULSING_SHOCKWAVE_AURA, true);
                    me->ClearUnitState(UNIT_STATE_CASTING); // Workaround to allow DoMeleeAttackIfReady work
                    DoCast(me, SPELL_PULSING_SHOCKWAVE, true);
                    break;
                case EVENT_INTRO_DIALOGUE:
                    Talk(SAY_INTRO_2);
                    events.SetPhase(PHASE_NORMAL);
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (me->HealthBelowPctDamaged(100 - 25 * _healthAmountModifier, damage))
        {
            switch (_healthAmountModifier)
            {
                case 1:
                    Talk(SAY_75HEALTH);
                    break;
                case 2:
                    Talk(SAY_50HEALTH);
                    break;
                case 3:
                    Talk(SAY_25HEALTH);
                    break;
                default:
                    break;
            }
            ++_healthAmountModifier;
        }
    }

    private:
        uint32 _healthAmountModifier;
        bool _isIntroDone;
};

// 52942, 59837 - Pulsing Shockwave
class spell_loken_pulsing_shockwave : public SpellScript
{
    PrepareSpellScript(spell_loken_pulsing_shockwave);

    void CalculateDamage(SpellEffIndex /*effIndex*/)
    {
        if (!GetHitUnit())
            return;

        float distance = GetCaster()->GetDistance2d(GetHitUnit());
        if (distance > 1.0f)
            SetHitDamage(int32(GetHitDamage() * distance));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_loken_pulsing_shockwave::CalculateDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};

void AddSC_boss_loken()
{
    RegisterHallsOfLightningCreatureAI(boss_loken);
    RegisterSpellScript(spell_loken_pulsing_shockwave);
}
