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
#include "drak_tharon_keep.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

enum NovosTexts
{
    SAY_AGGRO                       = 0,
    SAY_KILL                        = 1,
    SAY_DEATH                       = 2,
    SAY_SUMMONING_ADDS              = 3, // unused
    SAY_ARCANE_FIELD                = 4,
    EMOTE_SUMMONING_ADDS            = 5  // unused
};

enum NovosSpells
{
    SPELL_BEAM_CHANNEL              = 52106,
    SPELL_ARCANE_FIELD              = 47346,

    SPELL_SUMMON_RISEN_SHADOWCASTER = 49105,
    SPELL_SUMMON_FETID_TROLL_CORPSE = 49103,
    SPELL_SUMMON_HULKING_CORPSE     = 49104,
    SPELL_SUMMON_CRYSTAL_HANDLER    = 49179,
    SPELL_SUMMON_COPY_OF_MINIONS    = 59933,

    SPELL_ARCANE_BLAST              = 49198,
    SPELL_BLIZZARD                  = 49034,
    SPELL_FROSTBOLT                 = 49037,
    SPELL_WRATH_OF_MISERY           = 50089,
    SPELL_SUMMON_MINIONS            = 59910
};

enum NovosMisc
{
    ACTION_RESET_CRYSTALS,
    ACTION_ACTIVATE_CRYSTAL,
    ACTION_DEACTIVATE,
    EVENT_ATTACK,
    EVENT_SUMMON_MINIONS,
    DATA_NOVOS_ACHIEV
};

struct SummonerInfo
{
    uint32 data, spell, timer;
};

const SummonerInfo summoners[] =
{
    { DATA_NOVOS_SUMMONER_1, SPELL_SUMMON_RISEN_SHADOWCASTER, 15000 },
    { DATA_NOVOS_SUMMONER_2, SPELL_SUMMON_FETID_TROLL_CORPSE, 5000 },
    { DATA_NOVOS_SUMMONER_3, SPELL_SUMMON_HULKING_CORPSE, 30000 },
    { DATA_NOVOS_SUMMONER_4, SPELL_SUMMON_CRYSTAL_HANDLER, 30000 }
};

#define MAX_Y_COORD_OH_NOVOS        -771.95f

// 26631 - Novos the Summoner
struct boss_novos : public BossAI
{
    boss_novos(Creature* creature) : BossAI(creature, DATA_NOVOS)
    {
        Initialize();
        _bubbled = false;
    }

    void Initialize()
    {
        _ohNovos = true;
        _crystalHandlerCount = 0;
    }

    void Reset() override
    {
        _Reset();

        Initialize();
        SetCrystalsStatus(false);
        SetSummonerStatus(false);
        SetBubbled(false);
    }

    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);

        SetCrystalsStatus(true);
        SetSummonerStatus(true);
        SetBubbled(true);
    }

    void AttackStart(Unit* target) override
    {
        if (!target)
            return;

        if (me->Attack(target, true))
            DoStartNoMovement(target);
    }

    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim() || _bubbled)
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        if (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SUMMON_MINIONS:
                    DoCast(SPELL_SUMMON_MINIONS);
                    events.Repeat(15s);
                    break;
                case EVENT_ATTACK:
                    if (Unit* victim = SelectTarget(SelectTargetMethod::Random))
                        DoCast(victim, RAND(SPELL_ARCANE_BLAST, SPELL_BLIZZARD, SPELL_FROSTBOLT, SPELL_WRATH_OF_MISERY));
                    events.Repeat(3s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_CRYSTAL_HANDLER_DIED)
            CrystalHandlerDied();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        BossAI::MoveInLineOfSight(who);

        if (!_ohNovos || !who || who->GetTypeId() != TYPEID_UNIT || who->GetPositionY() > MAX_Y_COORD_OH_NOVOS)
            return;

        uint32 entry = who->GetEntry();
        if (entry == NPC_HULKING_CORPSE || entry == NPC_RISEN_SHADOWCASTER || entry == NPC_FETID_TROLL_CORPSE)
            _ohNovos = false;
    }

    uint32 GetData(uint32 type) const override
    {
        return type == DATA_NOVOS_ACHIEV && _ohNovos ? 1 : 0;
    }

    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
    }

private:
    void SetBubbled(bool state)
    {
        _bubbled = state;
        if (!state)
        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            if (me->HasUnitState(UNIT_STATE_CASTING))
                me->CastStop();
        }
        else
        {
            if (!me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            DoCast(SPELL_ARCANE_FIELD);
        }
    }

    void SetSummonerStatus(bool active)
    {
        for (uint8 i = 0; i < 4; i++)
            if (ObjectGuid guid = instance->GetGuidData(summoners[i].data))
                if (Creature* crystalChannelTarget = ObjectAccessor::GetCreature(*me, guid))
                {
                    if (active)
                        crystalChannelTarget->AI()->SetData(summoners[i].spell, summoners[i].timer);
                    else
                        crystalChannelTarget->AI()->Reset();
                }
    }

    void SetCrystalsStatus(bool active)
    {
        for (uint8 i = 0; i < 4; i++)
            if (ObjectGuid guid = instance->GetGuidData(DATA_NOVOS_CRYSTAL_1 + i))
                if (GameObject* crystal = ObjectAccessor::GetGameObject(*me, guid))
                    SetCrystalStatus(crystal, active);
    }

    void SetCrystalStatus(GameObject* crystal, bool active)
    {
        crystal->SetGoState(active ? GO_STATE_ACTIVE : GO_STATE_READY);
        if (Creature* crystalChannelTarget = crystal->FindNearestCreature(NPC_CRYSTAL_CHANNEL_TARGET, 5.0f))
        {
            if (active)
                crystalChannelTarget->CastSpell(nullptr, SPELL_BEAM_CHANNEL);
            else if (crystalChannelTarget->HasUnitState(UNIT_STATE_CASTING))
                crystalChannelTarget->CastStop();
        }
    }

    void CrystalHandlerDied()
    {
        for (uint8 i = 0; i < 4; i++)
            if (ObjectGuid guid = instance->GetGuidData(DATA_NOVOS_CRYSTAL_1 + i))
                if (GameObject* crystal = ObjectAccessor::GetGameObject(*me, guid))
                    if (crystal->GetGoState() == GO_STATE_ACTIVE)
                    {
                        SetCrystalStatus(crystal, false);
                        break;
                    }

        if (++_crystalHandlerCount >= 4)
        {
            Talk(SAY_ARCANE_FIELD);
            SetSummonerStatus(false);
            SetBubbled(false);
            events.ScheduleEvent(EVENT_ATTACK, 3s);
            if (IsHeroic())
                events.ScheduleEvent(EVENT_SUMMON_MINIONS, 15s);
        }
        else if (ObjectGuid guid = instance->GetGuidData(DATA_NOVOS_SUMMONER_4))
            if (Creature* crystalChannelTarget = ObjectAccessor::GetCreature(*me, guid))
                crystalChannelTarget->AI()->SetData(SPELL_SUMMON_CRYSTAL_HANDLER, 15000);
    }

    uint8 _crystalHandlerCount;
    bool _ohNovos;
    bool _bubbled;
};

// 26712 - Crystal Channel Target
struct npc_crystal_channel_target : public ScriptedAI
{
    npc_crystal_channel_target(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        _spell = 0;
        _timer = 0;
        _temp = 0;
    }

    void Reset() override
    {
        Initialize();
    }

    void UpdateAI(uint32 diff) override
    {
        if (_spell)
        {
            if (_temp <= diff)
            {
                DoCast(_spell);
                _temp = _timer;
            }
            else
                _temp -= diff;
        }
    }

    void SetData(uint32 id, uint32 value) override
    {
        _spell = id;
        _timer = value;
        _temp = value;
    }

    void JustSummoned(Creature* summon) override
    {
        if (InstanceScript* instance = me->GetInstanceScript())
            if (ObjectGuid guid = instance->GetGuidData(DATA_NOVOS))
                if (Creature* novos = ObjectAccessor::GetCreature(*me, guid))
                    novos->AI()->JustSummoned(summon);

        if (summon)
            summon->GetMotionMaster()->MovePath(summon->GetEntry() * 100, false);

        if (_spell == SPELL_SUMMON_CRYSTAL_HANDLER)
            Reset();
    }

private:
    uint32 _spell;
    uint32 _timer;
    uint32 _temp;
};

// 59910 - Summon Minions
class spell_novos_summon_minions : public SpellScript
{
    PrepareSpellScript(spell_novos_summon_minions);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_COPY_OF_MINIONS });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        for (uint8 i = 0; i < 2; ++i)
            GetCaster()->CastSpell(nullptr, SPELL_SUMMON_COPY_OF_MINIONS, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_novos_summon_minions::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

class achievement_oh_novos : public AchievementCriteriaScript
{
public:
    achievement_oh_novos() : AchievementCriteriaScript("achievement_oh_novos") { }

    bool OnCheck(Player* /*player*/, Unit* target) override
    {
        return target && target->GetTypeId() == TYPEID_UNIT && target->ToCreature()->AI()->GetData(DATA_NOVOS_ACHIEV);
    }
};

void AddSC_boss_novos()
{
    RegisterDrakTharonKeepCreatureAI(boss_novos);
    RegisterDrakTharonKeepCreatureAI(npc_crystal_channel_target);
    RegisterSpellScript(spell_novos_summon_minions);
    new achievement_oh_novos();
}
