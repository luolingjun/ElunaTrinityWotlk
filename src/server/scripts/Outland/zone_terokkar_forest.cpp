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
#include "Containers.h"
#include "CreatureAIImpl.h"
#include "GameObject.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "Spell.h"
#include "SpellScript.h"
#include "WorldSession.h"

/*######
## npc_unkor_the_ruthless
######*/

enum UnkorTheRuthless
{
    SAY_SUBMIT              = 0,
    REQUIRED_KILL_COUNT     = 10,
    SPELL_PULVERIZE         = 2676,
    QUEST_DONTKILLTHEFATONE = 9889,
    NPC_BOULDERFIST_INVADER = 18260
};

class npc_unkor_the_ruthless : public CreatureScript
{
public:
    npc_unkor_the_ruthless() : CreatureScript("npc_unkor_the_ruthless") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_unkor_the_ruthlessAI(creature);
    }

    struct npc_unkor_the_ruthlessAI : public ScriptedAI
    {
        npc_unkor_the_ruthlessAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            CanDoQuest = false;
            UnkorUnfriendly_Timer = 0;
            Pulverize_Timer = 3000;
        }

        bool CanDoQuest;
        uint32 UnkorUnfriendly_Timer;
        uint32 Pulverize_Timer;

        void Reset() override
        {
            Initialize();
            me->SetStandState(UNIT_STAND_STATE_STAND);
            me->SetFaction(FACTION_OGRE);
        }

        void JustEngagedWith(Unit* /*who*/) override { }

        void DoNice()
        {
            Talk(SAY_SUBMIT);
            me->SetFaction(FACTION_FRIENDLY);
            me->SetStandState(UNIT_STAND_STATE_SIT);
            me->RemoveAllAuras();
            me->CombatStop(true);
            EngagementOver();
            UnkorUnfriendly_Timer = 60000;
        }

        void DamageTaken(Unit* done_by, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            if (!done_by || !me->HealthBelowPctDamaged(30, damage))
                return;

            if (Player* player = done_by->ToPlayer())
            {
                if (Group* group = player->GetGroup())
                {
                    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                    {
                        Player* groupie = itr->GetSource();
                        if (groupie && groupie->IsInMap(player) &&
                            groupie->GetQuestStatus(QUEST_DONTKILLTHEFATONE) == QUEST_STATUS_INCOMPLETE &&
                            groupie->GetReqKillOrCastCurrentCount(QUEST_DONTKILLTHEFATONE, NPC_BOULDERFIST_INVADER) == REQUIRED_KILL_COUNT)
                        {
                            groupie->AreaExploredOrEventHappens(QUEST_DONTKILLTHEFATONE);
                            if (!CanDoQuest)
                                CanDoQuest = true;
                        }
                    }
                }
                else if (player->GetQuestStatus(QUEST_DONTKILLTHEFATONE) == QUEST_STATUS_INCOMPLETE &&
                    player->GetReqKillOrCastCurrentCount(QUEST_DONTKILLTHEFATONE, NPC_BOULDERFIST_INVADER) == REQUIRED_KILL_COUNT)
                {
                    player->AreaExploredOrEventHappens(QUEST_DONTKILLTHEFATONE);
                    CanDoQuest = true;
                }
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (CanDoQuest)
            {
                if (!UnkorUnfriendly_Timer)
                {
                    //DoCast(me, SPELL_QUID9889);        //not using spell for now
                    DoNice();
                }
                else
                {
                    if (UnkorUnfriendly_Timer <= diff)
                    {
                        EnterEvadeMode();
                        return;
                    } else UnkorUnfriendly_Timer -= diff;
                }
            }

            if (!UpdateVictim())
                return;

            if (Pulverize_Timer <= diff)
            {
                DoCast(me, SPELL_PULVERIZE);
                Pulverize_Timer = 9000;
            } else Pulverize_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

// 40655 - Skyguard Flare
class spell_skyguard_flare : public SpellScript
{
    PrepareSpellScript(spell_skyguard_flare);

    void ModDestHeight(SpellDestination& dest)
    {
        dest._position.m_positionZ = GetCaster()->GetMap()->GetHeight(dest._position.GetPositionX(), dest._position.GetPositionY(), MAX_HEIGHT);
    }

    void Register() override
    {
        OnDestinationTargetSelect += SpellDestinationTargetSelectFn(spell_skyguard_flare::ModDestHeight, EFFECT_0, TARGET_DEST_TARGET_RANDOM);
    }
};

/*######
## Quest 10873: Taken in the Night
######*/

enum TakenInTheNight
{
    SPELL_FREE_WEBBED_1      = 38953,
    SPELL_FREE_WEBBED_2      = 38955,
    SPELL_FREE_WEBBED_3      = 38956,
    SPELL_FREE_WEBBED_4      = 38957,
    SPELL_FREE_WEBBED_5      = 38958,
    SPELL_FREE_WEBBED_6      = 38978
};

std::array<uint32, 5> const CocoonSummonSpells =
{
    SPELL_FREE_WEBBED_1, SPELL_FREE_WEBBED_2, SPELL_FREE_WEBBED_3, SPELL_FREE_WEBBED_4, SPELL_FREE_WEBBED_5
};

// 38949 - Terrokar Free Webbed Creature
class spell_terokkar_free_webbed : public SpellScript
{
    PrepareSpellScript(spell_terokkar_free_webbed);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_terokkar_free_webbed::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

// 38950 - Terokkar Free Webbed Creature ON QUEST
class spell_terokkar_free_webbed_on_quest : public SpellScript
{
    PrepareSpellScript(spell_terokkar_free_webbed_on_quest);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(CocoonSummonSpells) && ValidateSpellInfo({ SPELL_FREE_WEBBED_6 });
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();

        if (roll_chance_i(66))
            caster->CastSpell(caster, Trinity::Containers::SelectRandomContainerElement(CocoonSummonSpells), true);
        else
            target->CastSpell(caster, SPELL_FREE_WEBBED_6, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_terokkar_free_webbed_on_quest::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/*######
## Quest 10040 & 10041: Who Are They?
######*/

enum WhoAreThey
{
    SPELL_SHADOWY_DISGUISE          = 32756,
    SPELL_MALE_SHADOWY_DISGUISE     = 38080,
    SPELL_FEMALE_SHADOWY_DISGUISE   = 38081
};

// 48917 - Who Are They: Cast from Questgiver
class spell_terokkar_shadowy_disguise_cast_from_questgiver : public SpellScript
{
    PrepareSpellScript(spell_terokkar_shadowy_disguise_cast_from_questgiver);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SHADOWY_DISGUISE });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_SHADOWY_DISGUISE);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_terokkar_shadowy_disguise_cast_from_questgiver::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 32756 - Shadowy Disguise
class spell_terokkar_shadowy_disguise : public AuraScript
{
    PrepareAuraScript(spell_terokkar_shadowy_disguise);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_MALE_SHADOWY_DISGUISE, SPELL_FEMALE_SHADOWY_DISGUISE });
    }

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Player* target = GetTarget()->ToPlayer())
            target->CastSpell(target, target->GetNativeGender() == GENDER_MALE ? SPELL_MALE_SHADOWY_DISGUISE : SPELL_FEMALE_SHADOWY_DISGUISE);
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        target->RemoveAurasDueToSpell(SPELL_MALE_SHADOWY_DISGUISE);
        target->RemoveAurasDueToSpell(SPELL_FEMALE_SHADOWY_DISGUISE);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_terokkar_shadowy_disguise::AfterApply, EFFECT_0, SPELL_AURA_FORCE_REACTION, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectApplyFn(spell_terokkar_shadowy_disguise::AfterRemove, EFFECT_0, SPELL_AURA_FORCE_REACTION, AURA_EFFECT_HANDLE_REAL);
    }
};

// 32780 - Cancel Shadowy Disguise
class spell_terokkar_cancel_shadowy_disguise : public SpellScript
{
    PrepareSpellScript(spell_terokkar_cancel_shadowy_disguise);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SHADOWY_DISGUISE });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->RemoveAurasDueToSpell(SPELL_SHADOWY_DISGUISE);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_terokkar_cancel_shadowy_disguise::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

enum Translocation
{
    SPELL_TRANSLOCATION_FIREWING_POINT_BUILDING_DOWN     = 32572,
    SPELL_TRANSLOCATION_FIREWING_POINT_BUILDING_UP       = 32568,
    SPELL_TRANSLOCATION_FIREWING_POINT_TOWER_DOWN        = 32569,
    SPELL_TRANSLOCATION_FIREWING_POINT_TOWER_UP          = 32571
};

// 25143 - Translocate
// 29128 - Translocate
// 29129 - Translocate
// 25140 - Translocate
class spell_terokkar_translocation_firewing_point : public SpellScript
{
    PrepareSpellScript(spell_terokkar_translocation_firewing_point);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ _triggeredSpellId });
    }

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetHitUnit()->CastSpell(GetHitUnit(), _triggeredSpellId);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_terokkar_translocation_firewing_point::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }

    uint32 _triggeredSpellId;

public:
    explicit spell_terokkar_translocation_firewing_point(Translocation triggeredSpellId) : _triggeredSpellId(triggeredSpellId) { }
};

/*######
## Quest 10929: Fumping / 10930: The Big Bone Worm
######*/

enum Fumping
{
    SPELL_SUMMON_SAND_GNOME    = 39240,
    SPELL_SUMMON_BONE_SLICER   = 39241,

    SPELL_SUMMON_SAND_GNOME_1  = 39247,
    SPELL_SUMMON_BONE_SLICER_1 = 39245,
    SPELL_SUMMON_HAISHULUD     = 39248,
    SPELL_DESPAWN_CLEFTHOOF    = 39250
};

// 39238 - Fumping
class spell_terokkar_fumping : public SpellScript
{
    PrepareSpellScript(spell_terokkar_fumping);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_SAND_GNOME, SPELL_SUMMON_BONE_SLICER });
    }

    void SetDest(SpellDestination& dest)
    {
        Position const offset = { 0.5f, 0.5f, 5.0f, 0.0f };
        dest.RelocateOffset(offset);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), RAND(SPELL_SUMMON_SAND_GNOME, SPELL_SUMMON_BONE_SLICER), true);
    }

    void Register() override
    {
        OnDestinationTargetSelect += SpellDestinationTargetSelectFn(spell_terokkar_fumping::SetDest, EFFECT_1, TARGET_DEST_CASTER);
        OnEffectHit += SpellEffectFn(spell_terokkar_fumping::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

// 39246 - Fumping
class spell_terokkar_fumping_the_big_bone_worm : public SpellScript
{
    PrepareSpellScript(spell_terokkar_fumping_the_big_bone_worm);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SUMMON_SAND_GNOME_1,
            SPELL_SUMMON_BONE_SLICER_1,
            SPELL_SUMMON_HAISHULUD,
            SPELL_DESPAWN_CLEFTHOOF
        });
    }

    void SetDest(SpellDestination& dest)
    {
        Position const offset = { 0.5f, 0.5f, 5.0f, 0.0f };
        dest.RelocateOffset(offset);
    }

    void HandleSummon(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), RAND(SPELL_SUMMON_SAND_GNOME_1, SPELL_SUMMON_BONE_SLICER_1, SPELL_SUMMON_HAISHULUD), true);
    }

    void HandleDespawn(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetCaster(), SPELL_DESPAWN_CLEFTHOOF, true);
    }

    void Register() override
    {
        OnDestinationTargetSelect += SpellDestinationTargetSelectFn(spell_terokkar_fumping_the_big_bone_worm::SetDest, EFFECT_1, TARGET_DEST_CASTER);
        OnEffectHit += SpellEffectFn(spell_terokkar_fumping_the_big_bone_worm::HandleSummon, EFFECT_1, SPELL_EFFECT_DUMMY);
        OnEffectHit += SpellEffectFn(spell_terokkar_fumping_the_big_bone_worm::HandleDespawn, EFFECT_2, SPELL_EFFECT_DUMMY);
    }
};

void AddSC_terokkar_forest()
{
    new npc_unkor_the_ruthless();
    RegisterSpellScript(spell_skyguard_flare);
    RegisterSpellScript(spell_terokkar_free_webbed);
    RegisterSpellScript(spell_terokkar_free_webbed_on_quest);
    RegisterSpellScript(spell_terokkar_shadowy_disguise_cast_from_questgiver);
    RegisterSpellScript(spell_terokkar_shadowy_disguise);
    RegisterSpellScript(spell_terokkar_cancel_shadowy_disguise);
    RegisterSpellScriptWithArgs(spell_terokkar_translocation_firewing_point, "spell_terokkar_translocation_firewing_point_building_down", SPELL_TRANSLOCATION_FIREWING_POINT_BUILDING_DOWN);
    RegisterSpellScriptWithArgs(spell_terokkar_translocation_firewing_point, "spell_terokkar_translocation_firewing_point_building_up", SPELL_TRANSLOCATION_FIREWING_POINT_BUILDING_UP);
    RegisterSpellScriptWithArgs(spell_terokkar_translocation_firewing_point, "spell_terokkar_translocation_firewing_point_tower_down", SPELL_TRANSLOCATION_FIREWING_POINT_TOWER_DOWN);
    RegisterSpellScriptWithArgs(spell_terokkar_translocation_firewing_point, "spell_terokkar_translocation_firewing_point_tower_up", SPELL_TRANSLOCATION_FIREWING_POINT_TOWER_UP);
    RegisterSpellScript(spell_terokkar_fumping);
    RegisterSpellScript(spell_terokkar_fumping_the_big_bone_worm);
}
