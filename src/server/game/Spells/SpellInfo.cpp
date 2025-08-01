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

#include "SpellInfo.h"
#include "Battleground.h"
#include "Corpse.h"
#include "Creature.h"
#include "DBCStores.h"
#include "FlatSet.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "Vehicle.h"

uint32 GetTargetFlagMask(SpellTargetObjectTypes objType)
{
    switch (objType)
    {
        case TARGET_OBJECT_TYPE_DEST:
            return TARGET_FLAG_DEST_LOCATION;
        case TARGET_OBJECT_TYPE_UNIT_AND_DEST:
            return TARGET_FLAG_DEST_LOCATION | TARGET_FLAG_UNIT;
        case TARGET_OBJECT_TYPE_CORPSE_ALLY:
            return TARGET_FLAG_CORPSE_ALLY;
        case TARGET_OBJECT_TYPE_CORPSE_ENEMY:
            return TARGET_FLAG_CORPSE_ENEMY;
        case TARGET_OBJECT_TYPE_CORPSE:
            return TARGET_FLAG_CORPSE_ALLY | TARGET_FLAG_CORPSE_ENEMY;
        case TARGET_OBJECT_TYPE_UNIT:
            return TARGET_FLAG_UNIT;
        case TARGET_OBJECT_TYPE_GOBJ:
            return TARGET_FLAG_GAMEOBJECT;
        case TARGET_OBJECT_TYPE_GOBJ_ITEM:
            return TARGET_FLAG_GAMEOBJECT_ITEM;
        case TARGET_OBJECT_TYPE_ITEM:
            return TARGET_FLAG_ITEM;
        case TARGET_OBJECT_TYPE_SRC:
            return TARGET_FLAG_SOURCE_LOCATION;
        default:
            return TARGET_FLAG_NONE;
    }
}

SpellImplicitTargetInfo::SpellImplicitTargetInfo(uint32 target)
{
    _target = Targets(target);
}

bool SpellImplicitTargetInfo::IsArea() const
{
    return GetSelectionCategory() == TARGET_SELECT_CATEGORY_AREA || GetSelectionCategory() == TARGET_SELECT_CATEGORY_CONE;
}

SpellTargetSelectionCategories SpellImplicitTargetInfo::GetSelectionCategory() const
{
    return _data[_target].SelectionCategory;
}

SpellTargetReferenceTypes SpellImplicitTargetInfo::GetReferenceType() const
{
    return _data[_target].ReferenceType;
}

SpellTargetObjectTypes SpellImplicitTargetInfo::GetObjectType() const
{
    return _data[_target].ObjectType;
}

SpellTargetCheckTypes SpellImplicitTargetInfo::GetCheckType() const
{
    return _data[_target].SelectionCheckType;
}

SpellTargetDirectionTypes SpellImplicitTargetInfo::GetDirectionType() const
{
    return _data[_target].DirectionType;
}

float SpellImplicitTargetInfo::CalcDirectionAngle() const
{
    switch (GetDirectionType())
    {
        case TARGET_DIR_FRONT:
            return 0.0f;
        case TARGET_DIR_BACK:
            return static_cast<float>(M_PI);
        case TARGET_DIR_RIGHT:
            return static_cast<float>(-M_PI/2);
        case TARGET_DIR_LEFT:
            return static_cast<float>(M_PI/2);
        case TARGET_DIR_FRONT_RIGHT:
            return static_cast<float>(-M_PI/4);
        case TARGET_DIR_BACK_RIGHT:
            return static_cast<float>(-3*M_PI/4);
        case TARGET_DIR_BACK_LEFT:
            return static_cast<float>(3*M_PI/4);
        case TARGET_DIR_FRONT_LEFT:
            return static_cast<float>(M_PI/4);
        case TARGET_DIR_RANDOM:
            return float(rand_norm())*static_cast<float>(2*M_PI);
        default:
            return 0.0f;
    }
}

Targets SpellImplicitTargetInfo::GetTarget() const
{
    return _target;
}

uint32 SpellImplicitTargetInfo::GetExplicitTargetMask(bool& srcSet, bool& dstSet) const
{
    uint32 targetMask = 0;
    if (GetTarget() == TARGET_DEST_TRAJ)
    {
        if (!srcSet)
            targetMask = TARGET_FLAG_SOURCE_LOCATION;
        if (!dstSet)
            targetMask |= TARGET_FLAG_DEST_LOCATION;
    }
    else
    {
        switch (GetReferenceType())
        {
            case TARGET_REFERENCE_TYPE_SRC:
                if (srcSet)
                    break;
                targetMask = TARGET_FLAG_SOURCE_LOCATION;
                break;
            case TARGET_REFERENCE_TYPE_DEST:
                if (dstSet)
                    break;
                targetMask = TARGET_FLAG_DEST_LOCATION;
                break;
            case TARGET_REFERENCE_TYPE_TARGET:
                switch (GetObjectType())
                {
                    case TARGET_OBJECT_TYPE_GOBJ:
                        targetMask = TARGET_FLAG_GAMEOBJECT;
                        break;
                    case TARGET_OBJECT_TYPE_GOBJ_ITEM:
                        targetMask = TARGET_FLAG_GAMEOBJECT_ITEM;
                        break;
                    case TARGET_OBJECT_TYPE_UNIT_AND_DEST:
                    case TARGET_OBJECT_TYPE_UNIT:
                    case TARGET_OBJECT_TYPE_DEST:
                        switch (GetCheckType())
                        {
                            case TARGET_CHECK_ENEMY:
                                targetMask = TARGET_FLAG_UNIT_ENEMY;
                                break;
                            case TARGET_CHECK_ALLY:
                                targetMask = TARGET_FLAG_UNIT_ALLY;
                                break;
                            case TARGET_CHECK_PARTY:
                                targetMask = TARGET_FLAG_UNIT_PARTY;
                                break;
                            case TARGET_CHECK_RAID:
                                targetMask = TARGET_FLAG_UNIT_RAID;
                                break;
                            case TARGET_CHECK_PASSENGER:
                                targetMask = TARGET_FLAG_UNIT_PASSENGER;
                                break;
                            case TARGET_CHECK_RAID_CLASS:
                                [[fallthrough]];
                            default:
                                targetMask = TARGET_FLAG_UNIT;
                                break;
                        }
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }

    switch (GetObjectType())
    {
        case TARGET_OBJECT_TYPE_SRC:
            srcSet = true;
            break;
        case TARGET_OBJECT_TYPE_DEST:
        case TARGET_OBJECT_TYPE_UNIT_AND_DEST:
            dstSet = true;
            break;
        default:
            break;
    }
    return targetMask;
}

struct SpellEffectInfo::ImmunityInfo
{
    ImmunityInfo() = default;
    ~ImmunityInfo() = default;

    ImmunityInfo(ImmunityInfo const&) = delete;
    ImmunityInfo(ImmunityInfo&&) noexcept = delete;
    ImmunityInfo& operator=(ImmunityInfo const&) = delete;
    ImmunityInfo& operator=(ImmunityInfo&&) noexcept = delete;

    uint32 SchoolImmuneMask = 0;
    uint32 ApplyHarmfulAuraImmuneMask = 0;
    uint32 MechanicImmuneMask = 0;
    uint32 DispelImmune = 0;
    uint32 DamageSchoolMask = 0;
    bool RemoveEffectsWithMechanic = false;

    Trinity::Containers::FlatSet<AuraType> AuraTypeImmune;
    Trinity::Containers::FlatSet<SpellEffects> SpellEffectImmune;
};

std::array<SpellImplicitTargetInfo::StaticData, TOTAL_SPELL_TARGETS> SpellImplicitTargetInfo::_data =
{ {
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        //
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 1 TARGET_UNIT_CASTER
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_NEARBY,  TARGET_CHECK_ENEMY,    TARGET_DIR_NONE},        // 2 TARGET_UNIT_NEARBY_ENEMY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_NEARBY,  TARGET_CHECK_ALLY,     TARGET_DIR_NONE},        // 3 TARGET_UNIT_NEARBY_ALLY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_NEARBY,  TARGET_CHECK_PARTY,    TARGET_DIR_NONE},        // 4 TARGET_UNIT_NEARBY_PARTY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 5 TARGET_UNIT_PET
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_ENEMY,    TARGET_DIR_NONE},        // 6 TARGET_UNIT_TARGET_ENEMY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_SRC,    TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_ENTRY,    TARGET_DIR_NONE},        // 7 TARGET_UNIT_SRC_AREA_ENTRY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_ENTRY,    TARGET_DIR_NONE},        // 8 TARGET_UNIT_DEST_AREA_ENTRY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 9 TARGET_DEST_HOME
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 10
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_SRC,    TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 11 TARGET_UNIT_SRC_AREA_UNK_11
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 12
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 13
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 14
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_SRC,    TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_ENEMY,    TARGET_DIR_NONE},        // 15 TARGET_UNIT_SRC_AREA_ENEMY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_ENEMY,    TARGET_DIR_NONE},        // 16 TARGET_UNIT_DEST_AREA_ENEMY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 17 TARGET_DEST_DB
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 18 TARGET_DEST_CASTER
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 19
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_PARTY,    TARGET_DIR_NONE},        // 20 TARGET_UNIT_CASTER_AREA_PARTY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_ALLY,     TARGET_DIR_NONE},        // 21 TARGET_UNIT_TARGET_ALLY
    {TARGET_OBJECT_TYPE_SRC,  TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 22 TARGET_SRC_CASTER
    {TARGET_OBJECT_TYPE_GOBJ, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 23 TARGET_GAMEOBJECT_TARGET
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CONE,    TARGET_CHECK_ENEMY,    TARGET_DIR_FRONT},       // 24 TARGET_UNIT_CONE_ENEMY_24
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 25 TARGET_UNIT_TARGET_ANY
    {TARGET_OBJECT_TYPE_GOBJ_ITEM, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT, TARGET_DIR_NONE},    // 26 TARGET_GAMEOBJECT_ITEM_TARGET
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 27 TARGET_UNIT_MASTER
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_ENEMY,    TARGET_DIR_NONE},        // 28 TARGET_DEST_DYNOBJ_ENEMY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_ALLY,     TARGET_DIR_NONE},        // 29 TARGET_DEST_DYNOBJ_ALLY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_SRC,    TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_ALLY,     TARGET_DIR_NONE},        // 30 TARGET_UNIT_SRC_AREA_ALLY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_ALLY,     TARGET_DIR_NONE},        // 31 TARGET_UNIT_DEST_AREA_ALLY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT_LEFT},  // 32 TARGET_DEST_CASTER_SUMMON
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_SRC,    TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_PARTY,    TARGET_DIR_NONE},        // 33 TARGET_UNIT_SRC_AREA_PARTY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_PARTY,    TARGET_DIR_NONE},        // 34 TARGET_UNIT_DEST_AREA_PARTY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_PARTY,    TARGET_DIR_NONE},        // 35 TARGET_UNIT_TARGET_PARTY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 36 TARGET_DEST_CASTER_UNK_36
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_LAST,   TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_PARTY,    TARGET_DIR_NONE},        // 37 TARGET_UNIT_LASTTARGET_AREA_PARTY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_NEARBY,  TARGET_CHECK_ENTRY,    TARGET_DIR_NONE},        // 38 TARGET_UNIT_NEARBY_ENTRY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 39 TARGET_DEST_CASTER_FISHING
    {TARGET_OBJECT_TYPE_GOBJ, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_NEARBY,  TARGET_CHECK_ENTRY,    TARGET_DIR_NONE},        // 40 TARGET_GAMEOBJECT_NEARBY_ENTRY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT_RIGHT}, // 41 TARGET_DEST_CASTER_FRONT_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK_RIGHT},  // 42 TARGET_DEST_CASTER_BACK_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK_LEFT},   // 43 TARGET_DEST_CASTER_BACK_LEFT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT_LEFT},  // 44 TARGET_DEST_CASTER_FRONT_LEFT
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_ALLY,     TARGET_DIR_NONE},        // 45 TARGET_UNIT_TARGET_CHAINHEAL_ALLY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_NEARBY,  TARGET_CHECK_ENTRY,    TARGET_DIR_NONE},        // 46 TARGET_DEST_NEARBY_ENTRY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT},       // 47 TARGET_DEST_CASTER_FRONT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK},        // 48 TARGET_DEST_CASTER_BACK
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RIGHT},       // 49 TARGET_DEST_CASTER_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_LEFT},        // 50 TARGET_DEST_CASTER_LEFT
    {TARGET_OBJECT_TYPE_GOBJ, TARGET_REFERENCE_TYPE_SRC,    TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 51 TARGET_GAMEOBJECT_SRC_AREA
    {TARGET_OBJECT_TYPE_GOBJ, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 52 TARGET_GAMEOBJECT_DEST_AREA
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_ENEMY,    TARGET_DIR_NONE},        // 53 TARGET_DEST_TARGET_ENEMY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CONE,    TARGET_CHECK_ENEMY,    TARGET_DIR_FRONT},       // 54 TARGET_UNIT_CONE_ENEMY_54
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 55 TARGET_DEST_CASTER_FRONT_LEAP
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_RAID,     TARGET_DIR_NONE},        // 56 TARGET_UNIT_CASTER_AREA_RAID
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_RAID,     TARGET_DIR_NONE},        // 57 TARGET_UNIT_TARGET_RAID
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_NEARBY,  TARGET_CHECK_RAID,     TARGET_DIR_NONE},        // 58 TARGET_UNIT_NEARBY_RAID
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CONE,    TARGET_CHECK_ALLY,     TARGET_DIR_FRONT},       // 59 TARGET_UNIT_CONE_ALLY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CONE,    TARGET_CHECK_ENTRY,    TARGET_DIR_FRONT},       // 60 TARGET_UNIT_CONE_ENTRY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_AREA,    TARGET_CHECK_RAID_CLASS, TARGET_DIR_NONE},      // 61 TARGET_UNIT_TARGET_AREA_RAID_CLASS
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 62 TARGET_UNK_62
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 63 TARGET_DEST_TARGET_ANY
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT},       // 64 TARGET_DEST_TARGET_FRONT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK},        // 65 TARGET_DEST_TARGET_BACK
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RIGHT},       // 66 TARGET_DEST_TARGET_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_LEFT},        // 67 TARGET_DEST_TARGET_LEFT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT_RIGHT}, // 68 TARGET_DEST_TARGET_FRONT_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK_RIGHT},  // 69 TARGET_DEST_TARGET_BACK_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK_LEFT},   // 70 TARGET_DEST_TARGET_BACK_LEFT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT_LEFT},  // 71 TARGET_DEST_TARGET_FRONT_LEFT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RANDOM},      // 72 TARGET_DEST_CASTER_RANDOM
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RANDOM},      // 73 TARGET_DEST_CASTER_RADIUS
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RANDOM},      // 74 TARGET_DEST_TARGET_RANDOM
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RANDOM},      // 75 TARGET_DEST_TARGET_RADIUS
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CHANNEL, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 76 TARGET_DEST_CHANNEL_TARGET
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CHANNEL, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 77 TARGET_UNIT_CHANNEL_TARGET
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT},       // 78 TARGET_DEST_DEST_FRONT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK},        // 79 TARGET_DEST_DEST_BACK
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RIGHT},       // 80 TARGET_DEST_DEST_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_LEFT},        // 81 TARGET_DEST_DEST_LEFT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT_RIGHT}, // 82 TARGET_DEST_DEST_FRONT_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK_RIGHT},  // 83 TARGET_DEST_DEST_BACK_RIGHT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_BACK_LEFT},   // 84 TARGET_DEST_DEST_BACK_LEFT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT_LEFT},  // 85 TARGET_DEST_DEST_FRONT_LEFT
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RANDOM},      // 86 TARGET_DEST_DEST_RANDOM
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 87 TARGET_DEST_DEST
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 88 TARGET_DEST_DYNOBJ_NONE
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_TRAJ,    TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 89 TARGET_DEST_TRAJ
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 90 TARGET_UNIT_TARGET_MINIPET
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_RANDOM},      // 91 TARGET_DEST_DEST_RADIUS
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 92 TARGET_UNIT_SUMMONER
    {TARGET_OBJECT_TYPE_CORPSE, TARGET_REFERENCE_TYPE_SRC,   TARGET_SELECT_CATEGORY_AREA,     TARGET_CHECK_ENEMY,    TARGET_DIR_NONE},      // 93 TARGET_CORPSE_SRC_AREA_ENEMY
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 94 TARGET_UNIT_VEHICLE
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_TARGET, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_PASSENGER, TARGET_DIR_NONE},       // 95 TARGET_UNIT_TARGET_PASSENGER
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 96 TARGET_UNIT_PASSENGER_0
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 97 TARGET_UNIT_PASSENGER_1
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 98 TARGET_UNIT_PASSENGER_2
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 99 TARGET_UNIT_PASSENGER_3
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 100 TARGET_UNIT_PASSENGER_4
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 101 TARGET_UNIT_PASSENGER_5
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 102 TARGET_UNIT_PASSENGER_6
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_DEFAULT, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 103 TARGET_UNIT_PASSENGER_7
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CONE,    TARGET_CHECK_ENEMY,    TARGET_DIR_FRONT},       // 104 TARGET_UNIT_CONE_ENEMY_104
    {TARGET_OBJECT_TYPE_UNIT, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 105 TARGET_UNIT_UNK_105
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CHANNEL, TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 106 TARGET_DEST_CHANNEL_CASTER
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_DEST,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 107 TARGET_UNK_DEST_AREA_UNK_107
    {TARGET_OBJECT_TYPE_GOBJ, TARGET_REFERENCE_TYPE_CASTER, TARGET_SELECT_CATEGORY_CONE,    TARGET_CHECK_DEFAULT,  TARGET_DIR_FRONT},       // 108 TARGET_GAMEOBJECT_CONE
    {TARGET_OBJECT_TYPE_NONE, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_DEFAULT,  TARGET_DIR_NONE},        // 109
    {TARGET_OBJECT_TYPE_DEST, TARGET_REFERENCE_TYPE_NONE,   TARGET_SELECT_CATEGORY_NYI,     TARGET_CHECK_ENTRY,    TARGET_DIR_NONE},        // 110 TARGET_UNIT_CONE_ENTRY_110
} };

SpellEffectInfo::SpellEffectInfo() : _spellInfo(nullptr), EffectIndex(EFFECT_0), Effect(SPELL_EFFECT_NONE), ApplyAuraName(SPELL_AURA_NONE),
    Amplitude(0), DieSides(0), RealPointsPerLevel(0), BasePoints(0), PointsPerComboPoint(0), ValueMultiplier(0), DamageMultiplier(0),
    BonusMultiplier(0), MiscValue(0), MiscValueB(0), Mechanic(MECHANIC_NONE), RadiusEntry(nullptr), ChainTarget(0), ItemType(0),
    TriggerSpell(0), ImplicitTargetConditions(nullptr), _immunityInfo(nullptr)
{
}

SpellEffectInfo::SpellEffectInfo(SpellEntry const* spellEntry, SpellInfo const* spellInfo, uint8 effIndex)
{
    _spellInfo = spellInfo;
    EffectIndex = SpellEffIndex(effIndex);
    Effect = SpellEffects(spellEntry->Effect[effIndex]);
    ApplyAuraName = AuraType(spellEntry->EffectAura[effIndex]);
    Amplitude = spellEntry->EffectAuraPeriod[effIndex];
    DieSides = spellEntry->EffectDieSides[effIndex];
    RealPointsPerLevel = spellEntry->EffectRealPointsPerLevel[effIndex];
    BasePoints = spellEntry->EffectBasePoints[effIndex];
    PointsPerComboPoint = spellEntry->EffectPointsPerCombo[effIndex];
    ValueMultiplier = spellEntry->EffectAmplitude[effIndex];
    DamageMultiplier = spellEntry->EffectChainAmplitude[effIndex];
    BonusMultiplier = spellEntry->EffectBonusCoefficient[effIndex];
    MiscValue = spellEntry->EffectMiscValue[effIndex];
    MiscValueB = spellEntry->EffectMiscValueB[effIndex];
    Mechanic = Mechanics(spellEntry->EffectMechanic[effIndex]);
    TargetA = SpellImplicitTargetInfo(spellEntry->EffectImplicitTargetA[effIndex]);
    TargetB = SpellImplicitTargetInfo(spellEntry->EffectImplicitTargetB[effIndex]);
    RadiusEntry = spellEntry->EffectRadiusIndex[effIndex] ? sSpellRadiusStore.LookupEntry(spellEntry->EffectRadiusIndex[effIndex]) : nullptr;
    ChainTarget = spellEntry->EffectChainTargets[effIndex];
    ItemType = spellEntry->EffectItemType[effIndex];
    TriggerSpell = spellEntry->EffectTriggerSpell[effIndex];
    SpellClassMask = spellEntry->EffectSpellClassMask[effIndex];
    ImplicitTargetConditions = nullptr;
    _immunityInfo = nullptr;
}

SpellEffectInfo::SpellEffectInfo(SpellEffectInfo&&) noexcept = default;
SpellEffectInfo& SpellEffectInfo::operator=(SpellEffectInfo&&) noexcept = default;

SpellEffectInfo::~SpellEffectInfo() = default;

bool SpellEffectInfo::IsEffect() const
{
    return Effect != 0;
}

bool SpellEffectInfo::IsEffect(SpellEffects effectName) const
{
    return Effect == effectName;
}

bool SpellEffectInfo::IsAura() const
{
    return (IsUnitOwnedAuraEffect() || Effect == SPELL_EFFECT_PERSISTENT_AREA_AURA) && ApplyAuraName != 0;
}

bool SpellEffectInfo::IsAura(AuraType aura) const
{
    return IsAura() && ApplyAuraName == aura;
}

bool SpellEffectInfo::IsTargetingArea() const
{
    return TargetA.IsArea() || TargetB.IsArea();
}

bool SpellEffectInfo::IsAreaAuraEffect() const
{
    if (Effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY    ||
        Effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID     ||
        Effect == SPELL_EFFECT_APPLY_AREA_AURA_FRIEND   ||
        Effect == SPELL_EFFECT_APPLY_AREA_AURA_ENEMY    ||
        Effect == SPELL_EFFECT_APPLY_AREA_AURA_PET      ||
        Effect == SPELL_EFFECT_APPLY_AREA_AURA_OWNER)
        return true;
    return false;
}

bool SpellEffectInfo::IsUnitOwnedAuraEffect() const
{
    return IsAreaAuraEffect() || Effect == SPELL_EFFECT_APPLY_AURA;
}

int32 SpellEffectInfo::CalcValue(WorldObject const* caster /*= nullptr*/, int32 const* bp /*= nullptr*/) const
{
    float basePointsPerLevel = RealPointsPerLevel;
    int32 basePoints = bp ? *bp : BasePoints;
    int32 randomPoints = int32(DieSides);

    Unit const* casterUnit = nullptr;
    if (caster)
        casterUnit = caster->ToUnit();

    // base amount modification based on spell lvl vs caster lvl
    if (casterUnit && basePointsPerLevel != 0.0f)
    {
        int32 level = int32(casterUnit->GetLevel());
        if (level > int32(_spellInfo->MaxLevel) && _spellInfo->MaxLevel > 0)
            level = int32(_spellInfo->MaxLevel);
        else if (level < int32(_spellInfo->BaseLevel))
            level = int32(_spellInfo->BaseLevel);

        // if base level is greater than spell level, reduce by base level (eg. pilgrims foods)
        level -= int32(std::max(_spellInfo->BaseLevel, _spellInfo->SpellLevel));
        basePoints += int32(level * basePointsPerLevel);
    }

    // roll in a range <1;EffectDieSides> as of patch 3.3.3
    switch (randomPoints)
    {
        case 0: break;
        case 1: basePoints += 1; break;                     // range 1..1
        default:
            // range can have positive (1..rand) and negative (rand..1) values, so order its for irand
            int32 randvalue = (randomPoints >= 1)
                ? irand(1, randomPoints)
                : irand(randomPoints, 1);

            basePoints += randvalue;
            break;
    }

    float value = float(basePoints);

    // random damage
    if (casterUnit)
    {
        // bonus amount from combo points
        if (uint8 comboPoints = casterUnit->GetComboPoints())
            value += PointsPerComboPoint * comboPoints;
    }

    if (caster)
        value = caster->ApplyEffectModifiers(_spellInfo, EffectIndex, value);

    if (casterUnit)
    {
        // amount multiplication based on caster's level
        if (!casterUnit->IsControlledByPlayer() &&
            _spellInfo->SpellLevel && _spellInfo->SpellLevel != casterUnit->GetLevel() &&
            !basePointsPerLevel && _spellInfo->HasAttribute(SPELL_ATTR0_LEVEL_DAMAGE_CALCULATION))
        {
            bool canEffectScale = false;
            switch (Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_DUMMY:
                case SPELL_EFFECT_POWER_DRAIN:
                case SPELL_EFFECT_HEALTH_LEECH:
                case SPELL_EFFECT_HEAL:
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_POWER_BURN:
                case SPELL_EFFECT_SCRIPT_EFFECT:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                case SPELL_EFFECT_FORCE_CAST_WITH_VALUE:
                case SPELL_EFFECT_TRIGGER_SPELL_WITH_VALUE:
                case SPELL_EFFECT_TRIGGER_MISSILE_SPELL_WITH_VALUE:
                    canEffectScale = true;
                    break;
                default:
                    break;
            }

            switch (ApplyAuraName)
            {
                case SPELL_AURA_PERIODIC_DAMAGE:
                case SPELL_AURA_DUMMY:
                case SPELL_AURA_PERIODIC_HEAL:
                case SPELL_AURA_DAMAGE_SHIELD:
                case SPELL_AURA_PROC_TRIGGER_DAMAGE:
                case SPELL_AURA_PERIODIC_LEECH:
                case SPELL_AURA_PERIODIC_MANA_LEECH:
                case SPELL_AURA_SCHOOL_ABSORB:
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE:
                    canEffectScale = true;
                    break;
                default:
                    break;
            }

            if (canEffectScale)
            {
                GtNPCManaCostScalerEntry const* spellScaler = sGtNPCManaCostScalerStore.LookupEntry(_spellInfo->SpellLevel - 1);
                GtNPCManaCostScalerEntry const* casterScaler = sGtNPCManaCostScalerStore.LookupEntry(casterUnit->GetLevel() - 1);
                if (spellScaler && casterScaler)
                    value *= casterScaler->Data / spellScaler->Data;
            }
        }
    }

    return int32(value);
}

int32 SpellEffectInfo::CalcBaseValue(int32 value) const
{
    if (DieSides == 0)
        return value;
    else
        return value - 1;
}

float SpellEffectInfo::CalcValueMultiplier(WorldObject* caster, Spell* spell /*= nullptr*/) const
{
    float multiplier = ValueMultiplier;
    if (Player* modOwner = (caster ? caster->GetSpellModOwner() : nullptr))
        modOwner->ApplySpellMod(_spellInfo->Id, SPELLMOD_VALUE_MULTIPLIER, multiplier, spell);

    return multiplier;
}

float SpellEffectInfo::CalcDamageMultiplier(WorldObject* caster, Spell* spell /*= nullptr*/) const
{
    float multiplierPercent = DamageMultiplier * 100.0f;
    if (Player* modOwner = (caster ? caster->GetSpellModOwner() : nullptr))
        modOwner->ApplySpellMod(_spellInfo->Id, SPELLMOD_DAMAGE_MULTIPLIER, multiplierPercent, spell);

    return multiplierPercent / 100.0f;
}

bool SpellEffectInfo::HasRadius() const
{
    return RadiusEntry != nullptr;
}

float SpellEffectInfo::CalcRadius(WorldObject* caster /*= nullptr*/, Spell* spell /*= nullptr*/) const
{
    if (!HasRadius())
        return 0.0f;

    float radius = RadiusEntry->Radius;
    if (caster)
    {
        if (Unit* casterUnit = caster->ToUnit())
            radius += RadiusEntry->RadiusPerLevel * casterUnit->GetLevel();

        radius = std::min(radius, RadiusEntry->RadiusMax);

        if (Player* modOwner = caster->GetSpellModOwner())
            modOwner->ApplySpellMod(_spellInfo->Id, SPELLMOD_RADIUS, radius, spell);
    }

    return radius;
}

uint32 SpellEffectInfo::GetProvidedTargetMask() const
{
    return GetTargetFlagMask(TargetA.GetObjectType()) | GetTargetFlagMask(TargetB.GetObjectType());
}

uint32 SpellEffectInfo::GetMissingTargetMask(bool srcSet /*= false*/, bool dstSet /*= false*/, uint32 mask /*=0*/) const
{
    uint32 effImplicitTargetMask = GetTargetFlagMask(GetUsedTargetObjectType());
    uint32 providedTargetMask = GetProvidedTargetMask() | mask;

    // remove all flags covered by effect target mask
    if (providedTargetMask & TARGET_FLAG_UNIT_MASK)
        effImplicitTargetMask &= ~(TARGET_FLAG_UNIT_MASK);
    if (providedTargetMask & TARGET_FLAG_CORPSE_MASK)
        effImplicitTargetMask &= ~(TARGET_FLAG_UNIT_MASK | TARGET_FLAG_CORPSE_MASK);
    if (providedTargetMask & TARGET_FLAG_GAMEOBJECT_ITEM)
        effImplicitTargetMask &= ~(TARGET_FLAG_GAMEOBJECT_ITEM | TARGET_FLAG_GAMEOBJECT | TARGET_FLAG_ITEM);
    if (providedTargetMask & TARGET_FLAG_GAMEOBJECT)
        effImplicitTargetMask &= ~(TARGET_FLAG_GAMEOBJECT | TARGET_FLAG_GAMEOBJECT_ITEM);
    if (providedTargetMask & TARGET_FLAG_ITEM)
        effImplicitTargetMask &= ~(TARGET_FLAG_ITEM | TARGET_FLAG_GAMEOBJECT_ITEM);
    if (dstSet || providedTargetMask & TARGET_FLAG_DEST_LOCATION)
        effImplicitTargetMask &= ~(TARGET_FLAG_DEST_LOCATION);
    if (srcSet || providedTargetMask & TARGET_FLAG_SOURCE_LOCATION)
        effImplicitTargetMask &= ~(TARGET_FLAG_SOURCE_LOCATION);

    return effImplicitTargetMask;
}

SpellEffectImplicitTargetTypes SpellEffectInfo::GetImplicitTargetType() const
{
    return _data[Effect].ImplicitTargetType;
}

SpellTargetObjectTypes SpellEffectInfo::GetUsedTargetObjectType() const
{
    return _data[Effect].UsedTargetObjectType;
}

std::array<SpellEffectInfo::StaticData, TOTAL_SPELL_EFFECTS> SpellEffectInfo::_data =
{ {
    // implicit target type           used target object type
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 0
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 1 SPELL_EFFECT_INSTAKILL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 2 SPELL_EFFECT_SCHOOL_DAMAGE
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 3 SPELL_EFFECT_DUMMY
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 4 SPELL_EFFECT_PORTAL_TELEPORT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT_AND_DEST}, // 5 SPELL_EFFECT_TELEPORT_UNITS
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 6 SPELL_EFFECT_APPLY_AURA
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 7 SPELL_EFFECT_ENVIRONMENTAL_DAMAGE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 8 SPELL_EFFECT_POWER_DRAIN
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 9 SPELL_EFFECT_HEALTH_LEECH
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 10 SPELL_EFFECT_HEAL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 11 SPELL_EFFECT_BIND
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 12 SPELL_EFFECT_PORTAL
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 13 SPELL_EFFECT_RITUAL_BASE
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 14 SPELL_EFFECT_RITUAL_SPECIALIZE
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 15 SPELL_EFFECT_RITUAL_ACTIVATE_PORTAL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 16 SPELL_EFFECT_QUEST_COMPLETE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 17 SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_CORPSE_ALLY}, // 18 SPELL_EFFECT_RESURRECT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 19 SPELL_EFFECT_ADD_EXTRA_ATTACKS
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 20 SPELL_EFFECT_DODGE
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 21 SPELL_EFFECT_EVADE
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 22 SPELL_EFFECT_PARRY
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 23 SPELL_EFFECT_BLOCK
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 24 SPELL_EFFECT_CREATE_ITEM
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 25 SPELL_EFFECT_WEAPON
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 26 SPELL_EFFECT_DEFENSE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 27 SPELL_EFFECT_PERSISTENT_AREA_AURA
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 28 SPELL_EFFECT_SUMMON
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT_AND_DEST}, // 29 SPELL_EFFECT_LEAP
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 30 SPELL_EFFECT_ENERGIZE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 31 SPELL_EFFECT_WEAPON_PERCENT_DAMAGE
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 32 SPELL_EFFECT_TRIGGER_MISSILE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_GOBJ_ITEM}, // 33 SPELL_EFFECT_OPEN_LOCK
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 34 SPELL_EFFECT_SUMMON_CHANGE_ITEM
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 35 SPELL_EFFECT_APPLY_AREA_AURA_PARTY
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 36 SPELL_EFFECT_LEARN_SPELL
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 37 SPELL_EFFECT_SPELL_DEFENSE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 38 SPELL_EFFECT_DISPEL
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 39 SPELL_EFFECT_LANGUAGE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 40 SPELL_EFFECT_DUAL_WIELD
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 41 SPELL_EFFECT_JUMP
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_DEST}, // 42 SPELL_EFFECT_JUMP_DEST
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT_AND_DEST}, // 43 SPELL_EFFECT_TELEPORT_UNITS_FACE_CASTER
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 44 SPELL_EFFECT_SKILL_STEP
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 45 SPELL_EFFECT_ADD_HONOR
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 46 SPELL_EFFECT_SPAWN
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 47 SPELL_EFFECT_TRADE_SKILL
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 48 SPELL_EFFECT_STEALTH
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 49 SPELL_EFFECT_DETECT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 50 SPELL_EFFECT_TRANS_DOOR
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 51 SPELL_EFFECT_FORCE_CRITICAL_HIT
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 52 SPELL_EFFECT_GUARANTEE_HIT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_ITEM}, // 53 SPELL_EFFECT_ENCHANT_ITEM
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_ITEM}, // 54 SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 55 SPELL_EFFECT_TAMECREATURE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 56 SPELL_EFFECT_SUMMON_PET
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 57 SPELL_EFFECT_LEARN_PET_SPELL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 58 SPELL_EFFECT_WEAPON_DAMAGE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 59 SPELL_EFFECT_CREATE_RANDOM_ITEM
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 60 SPELL_EFFECT_PROFICIENCY
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 61 SPELL_EFFECT_SEND_EVENT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 62 SPELL_EFFECT_POWER_BURN
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 63 SPELL_EFFECT_THREAT
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 64 SPELL_EFFECT_TRIGGER_SPELL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 65 SPELL_EFFECT_APPLY_AREA_AURA_RAID
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 66 SPELL_EFFECT_CREATE_MANA_GEM
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 67 SPELL_EFFECT_HEAL_MAX_HEALTH
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 68 SPELL_EFFECT_INTERRUPT_CAST
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT_AND_DEST}, // 69 SPELL_EFFECT_DISTRACT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 70 SPELL_EFFECT_PULL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 71 SPELL_EFFECT_PICKPOCKET
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 72 SPELL_EFFECT_ADD_FARSIGHT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 73 SPELL_EFFECT_UNTRAIN_TALENTS
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 74 SPELL_EFFECT_APPLY_GLYPH
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 75 SPELL_EFFECT_HEAL_MECHANICAL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 76 SPELL_EFFECT_SUMMON_OBJECT_WILD
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 77 SPELL_EFFECT_SCRIPT_EFFECT
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 78 SPELL_EFFECT_ATTACK
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 79 SPELL_EFFECT_SANCTUARY
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 80 SPELL_EFFECT_ADD_COMBO_POINTS
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 81 SPELL_EFFECT_CREATE_HOUSE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 82 SPELL_EFFECT_BIND_SIGHT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT_AND_DEST}, // 83 SPELL_EFFECT_DUEL
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 84 SPELL_EFFECT_STUCK
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 85 SPELL_EFFECT_SUMMON_PLAYER
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_GOBJ}, // 86 SPELL_EFFECT_ACTIVATE_OBJECT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_GOBJ}, // 87 SPELL_EFFECT_GAMEOBJECT_DAMAGE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_GOBJ}, // 88 SPELL_EFFECT_GAMEOBJECT_REPAIR
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_GOBJ}, // 89 SPELL_EFFECT_GAMEOBJECT_SET_DESTRUCTION_STATE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 90 SPELL_EFFECT_KILL_CREDIT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 91 SPELL_EFFECT_THREAT_ALL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 92 SPELL_EFFECT_ENCHANT_HELD_ITEM
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 93 SPELL_EFFECT_FORCE_DESELECT
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 94 SPELL_EFFECT_SELF_RESURRECT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 95 SPELL_EFFECT_SKINNING
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 96 SPELL_EFFECT_CHARGE
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 97 SPELL_EFFECT_CAST_BUTTON
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 98 SPELL_EFFECT_KNOCK_BACK
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_ITEM}, // 99 SPELL_EFFECT_DISENCHANT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 100 SPELL_EFFECT_INEBRIATE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_ITEM}, // 101 SPELL_EFFECT_FEED_PET
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 102 SPELL_EFFECT_DISMISS_PET
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 103 SPELL_EFFECT_REPUTATION
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 104 SPELL_EFFECT_SUMMON_OBJECT_SLOT1
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 105 SPELL_EFFECT_SUMMON_OBJECT_SLOT2
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 106 SPELL_EFFECT_SUMMON_OBJECT_SLOT3
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 107 SPELL_EFFECT_SUMMON_OBJECT_SLOT4
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 108 SPELL_EFFECT_DISPEL_MECHANIC
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 109 SPELL_EFFECT_SUMMON_DEAD_PET
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 110 SPELL_EFFECT_DESTROY_ALL_TOTEMS
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 111 SPELL_EFFECT_DURABILITY_DAMAGE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 112 SPELL_EFFECT_112
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_CORPSE_ALLY}, // 113 SPELL_EFFECT_RESURRECT_NEW
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 114 SPELL_EFFECT_ATTACK_ME
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 115 SPELL_EFFECT_DURABILITY_DAMAGE_PCT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_CORPSE_ENEMY}, // 116 SPELL_EFFECT_SKIN_PLAYER_CORPSE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 117 SPELL_EFFECT_SPIRIT_HEAL
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 118 SPELL_EFFECT_SKILL
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 119 SPELL_EFFECT_APPLY_AREA_AURA_PET
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 120 SPELL_EFFECT_TELEPORT_GRAVEYARD
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 121 SPELL_EFFECT_NORMALIZED_WEAPON_DMG
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 122 SPELL_EFFECT_122
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 123 SPELL_EFFECT_SEND_TAXI
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 124 SPELL_EFFECT_PULL_TOWARDS
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 125 SPELL_EFFECT_MODIFY_THREAT_PERCENT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 126 SPELL_EFFECT_STEAL_BENEFICIAL_BUFF
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_ITEM}, // 127 SPELL_EFFECT_PROSPECTING
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 128 SPELL_EFFECT_APPLY_AREA_AURA_FRIEND
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 129 SPELL_EFFECT_APPLY_AREA_AURA_ENEMY
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 130 SPELL_EFFECT_REDIRECT_THREAT
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 131 SPELL_EFFECT_PLAY_SOUND
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 132 SPELL_EFFECT_PLAY_MUSIC
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 133 SPELL_EFFECT_UNLEARN_SPECIALIZATION
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 134 SPELL_EFFECT_KILL_CREDIT2
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 135 SPELL_EFFECT_CALL_PET
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 136 SPELL_EFFECT_HEAL_PCT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 137 SPELL_EFFECT_ENERGIZE_PCT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 138 SPELL_EFFECT_LEAP_BACK
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 139 SPELL_EFFECT_CLEAR_QUEST
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 140 SPELL_EFFECT_FORCE_CAST
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 141 SPELL_EFFECT_FORCE_CAST_WITH_VALUE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 142 SPELL_EFFECT_TRIGGER_SPELL_WITH_VALUE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 143 SPELL_EFFECT_APPLY_AREA_AURA_OWNER
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT_AND_DEST}, // 144 SPELL_EFFECT_KNOCK_BACK_DEST
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT_AND_DEST}, // 145 SPELL_EFFECT_PULL_TOWARDS_DEST
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 146 SPELL_EFFECT_ACTIVATE_RUNE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 147 SPELL_EFFECT_QUEST_FAIL
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 148 SPELL_EFFECT_TRIGGER_MISSILE_SPELL_WITH_VALUE
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_DEST}, // 149 SPELL_EFFECT_CHARGE_DEST
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 150 SPELL_EFFECT_QUEST_START
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 151 SPELL_EFFECT_TRIGGER_SPELL_2
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_NONE}, // 152 SPELL_EFFECT_SUMMON_RAF_FRIEND
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 153 SPELL_EFFECT_CREATE_TAMED_PET
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 154 SPELL_EFFECT_DISCOVER_TAXI
    {EFFECT_IMPLICIT_TARGET_NONE,     TARGET_OBJECT_TYPE_UNIT}, // 155 SPELL_EFFECT_TITAN_GRIP
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_ITEM}, // 156 SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 157 SPELL_EFFECT_CREATE_ITEM_2
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_ITEM}, // 158 SPELL_EFFECT_MILLING
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 159 SPELL_EFFECT_ALLOW_RENAME_PET
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 160 SPELL_EFFECT_FORCE_CAST_2
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 161 SPELL_EFFECT_TALENT_SPEC_COUNT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 162 SPELL_EFFECT_TALENT_SPEC_SELECT
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 163 SPELL_EFFECT_163
    {EFFECT_IMPLICIT_TARGET_EXPLICIT, TARGET_OBJECT_TYPE_UNIT}, // 164 SPELL_EFFECT_REMOVE_AURA
} };

SpellInfo::SpellInfo(SpellEntry const* spellEntry)
{
    Id = spellEntry->ID;
    CategoryEntry = spellEntry->Category ? sSpellCategoryStore.LookupEntry(spellEntry->Category) : nullptr;
    Dispel = spellEntry->DispelType;
    Mechanic = spellEntry->Mechanic;
    Attributes = spellEntry->Attributes;
    AttributesEx = spellEntry->AttributesEx;
    AttributesEx2 = spellEntry->AttributesExB;
    AttributesEx3 = spellEntry->AttributesExC;
    AttributesEx4 = spellEntry->AttributesExD;
    AttributesEx5 = spellEntry->AttributesExE;
    AttributesEx6 = spellEntry->AttributesExF;
    AttributesEx7 = spellEntry->AttributesExG;
    AttributesCu = 0;
    Stances = MAKE_PAIR64(spellEntry->ShapeshiftMask[0], spellEntry->ShapeshiftMask[1]);
    StancesNot = MAKE_PAIR64(spellEntry->ShapeshiftExclude[0], spellEntry->ShapeshiftExclude[1]);
    Targets = spellEntry->Targets;
    TargetCreatureType = spellEntry->TargetCreatureType;
    RequiresSpellFocus = spellEntry->RequiresSpellFocus;
    FacingCasterFlags = spellEntry->FacingCasterFlags;
    CasterAuraState = spellEntry->CasterAuraState;
    TargetAuraState = spellEntry->TargetAuraState;
    CasterAuraStateNot = spellEntry->ExcludeCasterAuraState;
    TargetAuraStateNot = spellEntry->ExcludeTargetAuraState;
    CasterAuraSpell = spellEntry->CasterAuraSpell;
    TargetAuraSpell = spellEntry->TargetAuraSpell;
    ExcludeCasterAuraSpell = spellEntry->ExcludeCasterAuraSpell;
    ExcludeTargetAuraSpell = spellEntry->ExcludeTargetAuraSpell;
    CastTimeEntry = spellEntry->CastingTimeIndex ? sSpellCastTimesStore.LookupEntry(spellEntry->CastingTimeIndex) : nullptr;
    RecoveryTime = spellEntry->RecoveryTime;
    CategoryRecoveryTime = spellEntry->CategoryRecoveryTime;
    StartRecoveryCategory = spellEntry->StartRecoveryCategory;
    StartRecoveryTime = spellEntry->StartRecoveryTime;
    InterruptFlags = spellEntry->InterruptFlags;
    AuraInterruptFlags = spellEntry->AuraInterruptFlags;
    ChannelInterruptFlags = spellEntry->ChannelInterruptFlags;
    ProcFlags = spellEntry->ProcTypeMask;
    ProcChance = spellEntry->ProcChance;
    ProcCharges = spellEntry->ProcCharges;
    MaxLevel = spellEntry->MaxLevel;
    BaseLevel = spellEntry->BaseLevel;
    SpellLevel = spellEntry->SpellLevel;
    DurationEntry = spellEntry->DurationIndex ? sSpellDurationStore.LookupEntry(spellEntry->DurationIndex) : nullptr;
    PowerType = static_cast<Powers>(spellEntry->PowerType);
    ManaCost = spellEntry->ManaCost;
    ManaCostPerlevel = spellEntry->ManaCostPerLevel;
    ManaPerSecond = spellEntry->ManaPerSecond;
    ManaPerSecondPerLevel = spellEntry->ManaPerSecondPerLevel;
    ManaCostPercentage = spellEntry->ManaCostPct;
    RuneCostID = spellEntry->RuneCostID;
    RangeEntry = spellEntry->RangeIndex ? sSpellRangeStore.LookupEntry(spellEntry->RangeIndex) : nullptr;
    Speed = spellEntry->Speed;
    StackAmount = spellEntry->CumulativeAura;
    Totem = spellEntry->Totem;
    Reagent = spellEntry->Reagent;
    ReagentCount = spellEntry->ReagentCount;
    EquippedItemClass = spellEntry->EquippedItemClass;
    EquippedItemSubClassMask = spellEntry->EquippedItemSubclass;
    EquippedItemInventoryTypeMask = spellEntry->EquippedItemInvTypes;
    TotemCategory = spellEntry->RequiredTotemCategoryID;
    SpellVisual = spellEntry->SpellVisualID;
    SpellIconID = spellEntry->SpellIconID;
    ActiveIconID = spellEntry->ActiveIconID;
    Priority = spellEntry->SpellPriority;
    SpellName = spellEntry->Name;
    Rank = spellEntry->NameSubtext;
    MaxTargetLevel = spellEntry->MaxTargetLevel;
    MaxAffectedTargets = spellEntry->MaxTargets;
    SpellFamilyName = spellEntry->SpellClassSet;
    SpellFamilyFlags = spellEntry->SpellClassMask;
    DmgClass = spellEntry->DefenseType;
    PreventionType = spellEntry->PreventionType;
    AreaGroupId = spellEntry->RequiredAreasID;
    SchoolMask = spellEntry->SchoolMask;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        _effects[i] = SpellEffectInfo(spellEntry, this, i);

    ChainEntry = nullptr;
    ExplicitTargetMask = 0;

    _spellSpecific = SPELL_SPECIFIC_NORMAL;
    _auraState = AURA_STATE_NONE;

    _allowedMechanicMask = 0;
}

SpellInfo::~SpellInfo()
{
    _UnloadImplicitTargetConditionLists();
}

uint32 SpellInfo::GetCategory() const
{
    return CategoryEntry ? CategoryEntry->ID : 0;
}

bool SpellInfo::HasEffect(SpellEffects effect) const
{
    for (SpellEffectInfo const& eff : GetEffects())
        if (eff.IsEffect(effect))
            return true;

    return false;
}

bool SpellInfo::HasAura(AuraType aura) const
{
    for (SpellEffectInfo const& effect : GetEffects())
        if (effect.IsAura(aura))
            return true;

    return false;
}

bool SpellInfo::HasAreaAuraEffect() const
{
    for (SpellEffectInfo const& effect : GetEffects())
        if (effect.IsAreaAuraEffect())
            return true;

    return false;
}

bool SpellInfo::HasOnlyDamageEffects() const
{
    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (effect.IsEffect())
        {
            switch (effect.Effect)
            {
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_ENVIRONMENTAL_DAMAGE:
                case SPELL_EFFECT_HEALTH_LEECH:
                    continue;
                default:
                    return false;
            }
        }
    }

    return true;
}

bool SpellInfo::IsExplicitDiscovery() const
{
    return ((GetEffect(EFFECT_0).Effect == SPELL_EFFECT_CREATE_RANDOM_ITEM
        || GetEffect(EFFECT_0).Effect == SPELL_EFFECT_CREATE_ITEM_2)
        && GetEffect(EFFECT_1).Effect == SPELL_EFFECT_SCRIPT_EFFECT)
        || Id == 64323;
}

bool SpellInfo::IsLootCrafting() const
{
    return (GetEffect(EFFECT_0).Effect == SPELL_EFFECT_CREATE_RANDOM_ITEM ||
        // different random cards from Inscription (121==Virtuoso Inking Set category) r without explicit item
        (GetEffect(EFFECT_0).Effect == SPELL_EFFECT_CREATE_ITEM_2 &&
        ((TotemCategory[0] != 0 || (Totem[0] != 0 && SpellIconID == 1)) || GetEffect(EFFECT_0).ItemType == 0)));
}

bool SpellInfo::IsProfessionOrRiding() const
{
    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (effect.Effect == SPELL_EFFECT_SKILL)
        {
            uint32 skill = effect.MiscValue;

            if (IsProfessionOrRidingSkill(skill))
                return true;
        }
    }
    return false;
}

bool SpellInfo::IsProfession() const
{
    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (effect.Effect == SPELL_EFFECT_SKILL)
        {
            uint32 skill = effect.MiscValue;

            if (IsProfessionSkill(skill))
                return true;
        }
    }
    return false;
}

bool SpellInfo::IsPrimaryProfession() const
{
    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (effect.Effect == SPELL_EFFECT_SKILL)
        {
            uint32 skill = effect.MiscValue;

            if (IsPrimaryProfessionSkill(skill))
                return true;
        }
    }
    return false;
}

bool SpellInfo::IsPrimaryProfessionFirstRank() const
{
    return IsPrimaryProfession() && GetRank() == 1;
}

bool SpellInfo::IsAbilityLearnedWithProfession() const
{
    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(Id);

    for (SkillLineAbilityMap::const_iterator _spell_idx = bounds.first; _spell_idx != bounds.second; ++_spell_idx)
    {
        SkillLineAbilityEntry const* pAbility = _spell_idx->second;
        if (!pAbility || pAbility->AcquireMethod != SKILL_LINE_ABILITY_LEARNED_ON_SKILL_VALUE)
            continue;

        if (pAbility->MinSkillLineRank > 0)
            return true;
    }

    return false;
}

bool SpellInfo::IsAbilityOfSkillType(uint32 skillType) const
{
    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(Id);

    for (SkillLineAbilityMap::const_iterator _spell_idx = bounds.first; _spell_idx != bounds.second; ++_spell_idx)
        if (_spell_idx->second->SkillLine == uint32(skillType))
            return true;

    return false;
}

bool SpellInfo::IsAffectingArea() const
{
    for (SpellEffectInfo const& effect : GetEffects())
        if (effect.IsEffect() && (effect.IsTargetingArea() || effect.IsEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA) || effect.IsAreaAuraEffect()))
            return true;
    return false;
}

// checks if spell targets are selected from area, doesn't include spell effects in check (like area wide auras for example)
bool SpellInfo::IsTargetingArea() const
{
    for (SpellEffectInfo const& effect : GetEffects())
        if (effect.IsEffect() && effect.IsTargetingArea())
            return true;
    return false;
}

bool SpellInfo::NeedsExplicitUnitTarget() const
{
    return (GetExplicitTargetMask() & TARGET_FLAG_UNIT_MASK) != 0;
}

bool SpellInfo::NeedsToBeTriggeredByCaster(SpellInfo const* triggeringSpell) const
{
    if (NeedsExplicitUnitTarget())
        return true;

    /*
    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (effect.IsEffect())
        {
            if (effect.TargetA.GetSelectionCategory() == TARGET_SELECT_CATEGORY_CHANNEL
                || effect.TargetB.GetSelectionCategory() == TARGET_SELECT_CATEGORY_CHANNEL)
                return true;
        }
    }
    */

    if (triggeringSpell->IsChanneled())
    {
        uint32 mask = 0;
        for (SpellEffectInfo const& effect : GetEffects())
        {
            if (effect.TargetA.GetTarget() != TARGET_UNIT_CASTER && effect.TargetA.GetTarget() != TARGET_DEST_CASTER
                && effect.TargetB.GetTarget() != TARGET_UNIT_CASTER && effect.TargetB.GetTarget() != TARGET_DEST_CASTER)
            {
                mask |= effect.GetProvidedTargetMask();
            }
        }

        if (mask & TARGET_FLAG_UNIT_MASK)
            return true;
    }

    return false;
}

bool SpellInfo::IsSelfCast() const
{
    for (SpellEffectInfo const& effect : GetEffects())
        if (effect.Effect && effect.TargetA.GetTarget() != TARGET_UNIT_CASTER)
            return false;
    return true;
}

bool SpellInfo::IsPassive() const
{
    return HasAttribute(SPELL_ATTR0_PASSIVE);
}

bool SpellInfo::IsAutocastable() const
{
    if (IsPassive())
        return false;
    if (HasAttribute(SPELL_ATTR1_UNAUTOCASTABLE_BY_PET))
        return false;
    return true;
}

bool SpellInfo::IsStackableWithRanks() const
{
    if (IsPassive())
        return false;
    if (PowerType != POWER_MANA && PowerType != POWER_HEALTH)
        return false;
    if (IsProfessionOrRiding())
        return false;

    if (IsAbilityLearnedWithProfession())
        return false;

    // All stance spells. if any better way, change it.
    switch (SpellFamilyName)
    {
        case SPELLFAMILY_PALADIN:
            // Paladin aura Spell
            if (HasEffect(SPELL_EFFECT_APPLY_AREA_AURA_RAID))
                return false;
            // Seal of Righteousness
            if (SpellFamilyFlags[1] & 0x20000000)
                return false;
            break;
        case SPELLFAMILY_DRUID:
            // Druid form Spell
            if (HasAura(SPELL_AURA_MOD_SHAPESHIFT))
                return false;
            break;
    }

    return true;
}

bool SpellInfo::IsPassiveStackableWithRanks() const
{
    return IsPassive() && !HasEffect(SPELL_EFFECT_APPLY_AURA);
}

bool SpellInfo::IsMultiSlotAura() const
{
    return IsPassive() || Id == 55849 || Id == 40075 || Id == 44413; // Power Spark, Fel Flak Fire, Incanter's Absorption
}

bool SpellInfo::IsStackableOnOneSlotWithDifferentCasters() const
{
    /// TODO: Re-verify meaning of SPELL_ATTR3_STACK_FOR_DIFF_CASTERS and update conditions here
    return StackAmount > 1 && !IsChanneled() && !HasAttribute(SPELL_ATTR3_STACK_FOR_DIFF_CASTERS);
}

bool SpellInfo::IsCooldownStartedOnEvent() const
{
    if (HasAttribute(SPELL_ATTR0_DISABLED_WHILE_ACTIVE))
        return true;

    return CategoryEntry && CategoryEntry->Flags & SPELL_CATEGORY_FLAG_COOLDOWN_STARTS_ON_EVENT;
}

bool SpellInfo::IsDeathPersistent() const
{
    return HasAttribute(SPELL_ATTR3_DEATH_PERSISTENT);
}

bool SpellInfo::IsRequiringDeadTarget() const
{
    return HasAttribute(SPELL_ATTR3_ONLY_TARGET_GHOSTS);
}

bool SpellInfo::IsAllowingDeadTarget() const
{
    return HasAttribute(SPELL_ATTR2_CAN_TARGET_DEAD) || Targets & (TARGET_FLAG_CORPSE_ALLY | TARGET_FLAG_CORPSE_ENEMY | TARGET_FLAG_UNIT_DEAD);
}

bool SpellInfo::IsGroupBuff() const
{
    for (SpellEffectInfo const& effect : GetEffects())
    {
        switch (effect.TargetA.GetCheckType())
        {
            case TARGET_CHECK_PARTY:
            case TARGET_CHECK_RAID:
            case TARGET_CHECK_RAID_CLASS:
                return true;
            default:
                break;
        }
    }

    return false;
}

bool SpellInfo::CanBeUsedInCombat() const
{
    return !HasAttribute(SPELL_ATTR0_CANT_USED_IN_COMBAT);
}

bool SpellInfo::IsPositive() const
{
    return !HasAttribute(SPELL_ATTR0_CU_NEGATIVE);
}

bool SpellInfo::IsPositiveEffect(uint8 effIndex) const
{
    switch (effIndex)
    {
        default:
        case 0:
            return !HasAttribute(SPELL_ATTR0_CU_NEGATIVE_EFF0);
        case 1:
            return !HasAttribute(SPELL_ATTR0_CU_NEGATIVE_EFF1);
        case 2:
            return !HasAttribute(SPELL_ATTR0_CU_NEGATIVE_EFF2);
    }
}

bool SpellInfo::IsChanneled() const
{
    return HasAttribute(SpellAttr1(SPELL_ATTR1_CHANNELED_1 | SPELL_ATTR1_CHANNELED_2));
}

bool SpellInfo::IsMoveAllowedChannel() const
{
    return IsChanneled() && (HasAttribute(SPELL_ATTR5_CAN_CHANNEL_WHEN_MOVING) || (!(ChannelInterruptFlags & (AURA_INTERRUPT_FLAG_MOVE | AURA_INTERRUPT_FLAG_TURNING))));
}

bool SpellInfo::NeedsComboPoints() const
{
    return HasAttribute(SpellAttr1(SPELL_ATTR1_REQ_COMBO_POINTS1 | SPELL_ATTR1_REQ_COMBO_POINTS2));
}

bool SpellInfo::IsNextMeleeSwingSpell() const
{
    return HasAttribute(SpellAttr0(SPELL_ATTR0_ON_NEXT_SWING | SPELL_ATTR0_ON_NEXT_SWING_2));
}

bool SpellInfo::IsBreakingStealth() const
{
    return !HasAttribute(SPELL_ATTR1_NOT_BREAK_STEALTH);
}

bool SpellInfo::IsRangedWeaponSpell() const
{
    return (SpellFamilyName == SPELLFAMILY_HUNTER && !(SpellFamilyFlags[1] & 0x10000000)) // for 53352, cannot find better way
        || (EquippedItemSubClassMask & ITEM_SUBCLASS_MASK_WEAPON_RANGED)
        || (Attributes & SPELL_ATTR0_REQ_AMMO);
}

bool SpellInfo::IsAutoRepeatRangedSpell() const
{
    return HasAttribute(SPELL_ATTR2_AUTOREPEAT_FLAG);
}

bool SpellInfo::HasInitialAggro() const
{
    return !(HasAttribute(SPELL_ATTR1_NO_THREAT) || HasAttribute(SPELL_ATTR3_NO_INITIAL_AGGRO));
}

WeaponAttackType SpellInfo::GetAttackType() const
{
    WeaponAttackType result;
    switch (DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MELEE:
            if (HasAttribute(SPELL_ATTR3_REQ_OFFHAND))
                result = OFF_ATTACK;
            else
                result = BASE_ATTACK;
            break;
        case SPELL_DAMAGE_CLASS_RANGED:
            result = IsRangedWeaponSpell() ? RANGED_ATTACK : BASE_ATTACK;
            break;
        default:
            // Wands
            if (IsAutoRepeatRangedSpell())
                result = RANGED_ATTACK;
            else
                result = BASE_ATTACK;
            break;
    }

    return result;
}

bool SpellInfo::IsItemFitToSpellRequirements(Item const* item) const
{
    // item neutral spell
    if (EquippedItemClass == -1)
        return true;

    // item dependent spell
    if (item && item->IsFitToSpellRequirements(this))
        return true;

    return false;
}

bool SpellInfo::IsAffected(uint32 familyName, flag96 const& familyFlags) const
{
    if (!familyName)
        return true;

    if (familyName != SpellFamilyName)
        return false;

    if (familyFlags && !(familyFlags & SpellFamilyFlags))
        return false;

    return true;
}

bool SpellInfo::IsAffectedBySpellMods() const
{
    return !HasAttribute(SPELL_ATTR3_NO_DONE_BONUS);
}

bool SpellInfo::IsAffectedBySpellMod(SpellModifier const* mod) const
{
    if (!IsAffectedBySpellMods())
        return false;

    SpellInfo const* affectSpell = sSpellMgr->GetSpellInfo(mod->spellId);
    if (!affectSpell)
        return false;

    return IsAffected(affectSpell->SpellFamilyName, mod->mask);
}

bool SpellInfo::CanPierceImmuneAura(SpellInfo const* auraSpellInfo) const
{
    // aura can't be pierced
    if (!auraSpellInfo || auraSpellInfo->HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))
        return false;

    // these spells pierce all available spells (Resurrection Sickness for example)
    if (HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))
        return true;

    // these spells (Cyclone for example) can pierce all...
    if (HasAttribute(SPELL_ATTR1_UNAFFECTED_BY_SCHOOL_IMMUNE) || HasAttribute(SPELL_ATTR2_UNAFFECTED_BY_AURA_SCHOOL_IMMUNE))
    {
        // ...but not these (Divine shield, Ice block, Cyclone and Banish for example)
        if (auraSpellInfo->Mechanic != MECHANIC_IMMUNE_SHIELD &&
               auraSpellInfo->Mechanic != MECHANIC_INVULNERABILITY &&
               auraSpellInfo->Mechanic != MECHANIC_BANISH)
            return true;
    }

    // Dispels other auras on immunity, check if this spell makes the unit immune to aura
    if (HasAttribute(SPELL_ATTR1_DISPEL_AURAS_ON_IMMUNITY) && CanSpellProvideImmunityAgainstAura(auraSpellInfo))
        return true;

    return false;
}

bool SpellInfo::CanDispelAura(SpellInfo const* auraSpellInfo) const
{
    // These auras (like Divine Shield) can't be dispelled
    if (auraSpellInfo->HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))
        return false;

    // These spells (like Mass Dispel) can dispel all auras
    if (HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))
        return true;

    // These auras (Cyclone for example) are not dispelable
    if ((auraSpellInfo->HasAttribute(SPELL_ATTR1_UNAFFECTED_BY_SCHOOL_IMMUNE) && auraSpellInfo->Mechanic != MECHANIC_NONE)
        || auraSpellInfo->HasAttribute(SPELL_ATTR2_UNAFFECTED_BY_AURA_SCHOOL_IMMUNE))
        return false;

    return true;
}

bool SpellInfo::IsSingleTarget() const
{
    // all other single target spells have if it has AttributesEx5
    if (HasAttribute(SPELL_ATTR5_SINGLE_TARGET_SPELL))
        return true;

    return false;
}

bool SpellInfo::IsAuraExclusiveBySpecificWith(SpellInfo const* spellInfo) const
{
    SpellSpecificType spellSpec1 = GetSpellSpecific();
    SpellSpecificType spellSpec2 = spellInfo->GetSpellSpecific();
    switch (spellSpec1)
    {
        case SPELL_SPECIFIC_TRACKER:
        case SPELL_SPECIFIC_WARLOCK_ARMOR:
        case SPELL_SPECIFIC_MAGE_ARMOR:
        case SPELL_SPECIFIC_ELEMENTAL_SHIELD:
        case SPELL_SPECIFIC_MAGE_POLYMORPH:
        case SPELL_SPECIFIC_PRESENCE:
        case SPELL_SPECIFIC_CHARM:
        case SPELL_SPECIFIC_SCROLL:
        case SPELL_SPECIFIC_WARRIOR_ENRAGE:
        case SPELL_SPECIFIC_MAGE_ARCANE_BRILLANCE:
        case SPELL_SPECIFIC_PRIEST_DIVINE_SPIRIT:
            return spellSpec1 == spellSpec2;
        case SPELL_SPECIFIC_FOOD:
            return spellSpec2 == SPELL_SPECIFIC_FOOD
                || spellSpec2 == SPELL_SPECIFIC_FOOD_AND_DRINK;
        case SPELL_SPECIFIC_DRINK:
            return spellSpec2 == SPELL_SPECIFIC_DRINK
                || spellSpec2 == SPELL_SPECIFIC_FOOD_AND_DRINK;
        case SPELL_SPECIFIC_FOOD_AND_DRINK:
            return spellSpec2 == SPELL_SPECIFIC_FOOD
                || spellSpec2 == SPELL_SPECIFIC_DRINK
                || spellSpec2 == SPELL_SPECIFIC_FOOD_AND_DRINK;
        default:
            return false;
    }
}

bool SpellInfo::IsAuraExclusiveBySpecificPerCasterWith(SpellInfo const* spellInfo) const
{
    SpellSpecificType spellSpec = GetSpellSpecific();
    switch (spellSpec)
    {
        case SPELL_SPECIFIC_SEAL:
        case SPELL_SPECIFIC_HAND:
        case SPELL_SPECIFIC_AURA:
        case SPELL_SPECIFIC_STING:
        case SPELL_SPECIFIC_CURSE:
        case SPELL_SPECIFIC_ASPECT:
        case SPELL_SPECIFIC_JUDGEMENT:
        case SPELL_SPECIFIC_WARLOCK_CORRUPTION:
            return spellSpec == spellInfo->GetSpellSpecific();
        default:
            return false;
    }
}

SpellCastResult SpellInfo::CheckShapeshift(uint32 form) const
{
    // talents that learn spells can have stance requirements that need ignore
    // (this requirement only for client-side stance show in talent description)
    if (GetTalentSpellCost(Id) > 0 && HasEffect(SPELL_EFFECT_LEARN_SPELL))
        return SPELL_CAST_OK;

    uint64 stanceMask = (form ? UI64LIT(1) << (form - 1) : 0);

    if (stanceMask & StancesNot)                 // can explicitly not be cast in this stance
        return SPELL_FAILED_NOT_SHAPESHIFT;

    if (stanceMask & Stances)                    // can explicitly be cast in this stance
        return SPELL_CAST_OK;

    bool actAsShifted = false;
    SpellShapeshiftFormEntry const* shapeInfo = nullptr;
    if (form > 0)
    {
        shapeInfo = sSpellShapeshiftFormStore.LookupEntry(form);
        if (!shapeInfo)
        {
            TC_LOG_ERROR("spells", "GetErrorAtShapeshiftedCast: unknown shapeshift {}", form);
            return SPELL_CAST_OK;
        }
        actAsShifted = !(shapeInfo->Flags & 1);            // shapeshift acts as normal form for spells
    }

    if (actAsShifted)
    {
        if (HasAttribute(SPELL_ATTR0_NOT_SHAPESHIFT)) // not while shapeshifted
            return SPELL_FAILED_NOT_SHAPESHIFT;
        else if (Stances != 0)                   // needs other shapeshift
            return SPELL_FAILED_ONLY_SHAPESHIFT;
    }
    else
    {
        // needs shapeshift
        if (!HasAttribute(SPELL_ATTR2_NOT_NEED_SHAPESHIFT) && Stances != 0)
            return SPELL_FAILED_ONLY_SHAPESHIFT;
    }

    // Check if stance disables cast of not-stance spells
    // Example: cannot cast any other spells in zombie or ghoul form
    /// @todo Find a way to disable use of these spells clientside
    if (shapeInfo && shapeInfo->Flags & 0x400)
    {
        if (!(stanceMask & Stances))
            return SPELL_FAILED_ONLY_SHAPESHIFT;
    }

    return SPELL_CAST_OK;
}

SpellCastResult SpellInfo::CheckLocation(uint32 map_id, uint32 zone_id, uint32 area_id, Player const* player /*= nullptr*/, bool strict /*= true*/) const
{
    // normal case
    if (AreaGroupId > 0)
    {
        bool found = false;
        AreaGroupEntry const* groupEntry = sAreaGroupStore.LookupEntry(AreaGroupId);
        while (groupEntry)
        {
            for (uint8 i = 0; i < MAX_GROUP_AREA_IDS; ++i)
                if (groupEntry->AreaID[i] == zone_id || groupEntry->AreaID[i] == area_id)
                    found = true;
            if (found || !groupEntry->NextAreaID)
                break;
            // Try search in next group
            groupEntry = sAreaGroupStore.LookupEntry(groupEntry->NextAreaID);
        }

        if (!found)
            return SPELL_FAILED_INCORRECT_AREA;
    }

    // continent limitation (virtual continent)
    if (HasAttribute(SPELL_ATTR4_CAST_ONLY_IN_OUTLAND))
    {
        if (strict)
        {
            AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(area_id);
            if (!areaEntry)
                areaEntry = sAreaTableStore.LookupEntry(zone_id);

            if (!areaEntry || !areaEntry->IsFlyable() || !player->CanFlyInZone(map_id, zone_id, this))
                return SPELL_FAILED_INCORRECT_AREA;
        }
        else
        {
            uint32 const v_map = GetVirtualMapForMapAndZone(map_id, zone_id);
            MapEntry const* mapEntry = sMapStore.LookupEntry(v_map);
            if (!mapEntry || mapEntry->Expansion() < 1 || !mapEntry->IsContinent())
                return SPELL_FAILED_INCORRECT_AREA;
        }
    }

    // raid instance limitation
    if (HasAttribute(SPELL_ATTR6_NOT_IN_RAID_INSTANCE))
    {
        MapEntry const* mapEntry = sMapStore.LookupEntry(map_id);
        if (!mapEntry || mapEntry->IsRaid())
            return SPELL_FAILED_NOT_IN_RAID_INSTANCE;
    }

    // DB base check (if non empty then must fit at least single for allow)
    SpellAreaMapBounds saBounds = sSpellMgr->GetSpellAreaMapBounds(Id);
    if (saBounds.first != saBounds.second)
    {
        for (SpellAreaMap::const_iterator itr = saBounds.first; itr != saBounds.second; ++itr)
        {
            if (itr->second.IsFitToRequirements(player, zone_id, area_id))
                return SPELL_CAST_OK;
        }
        return SPELL_FAILED_INCORRECT_AREA;
    }

    // bg spell checks
    switch (Id)
    {
        case 23333:                                         // Warsong Flag
        case 23335:                                         // Silverwing Flag
            return map_id == 489 && player && player->InBattleground() ? SPELL_CAST_OK : SPELL_FAILED_REQUIRES_AREA;
        case 34976:                                         // Netherstorm Flag
            return map_id == 566 && player && player->InBattleground() ? SPELL_CAST_OK : SPELL_FAILED_REQUIRES_AREA;
        case 2584:                                          // Waiting to Resurrect
        case 22011:                                         // Spirit Heal Channel
        case 22012:                                         // Spirit Heal
        case 42792:                                         // Recently Dropped Flag
        case 43681:                                         // Inactive
        case 44535:                                         // Spirit Heal (mana)
        {
            MapEntry const* mapEntry = sMapStore.LookupEntry(map_id);
            if (!mapEntry)
                return SPELL_FAILED_INCORRECT_AREA;

            return zone_id == AREA_WINTERGRASP || (mapEntry->IsBattleground() && player && player->InBattleground()) ? SPELL_CAST_OK : SPELL_FAILED_REQUIRES_AREA;
        }
        case 44521:                                         // Preparation
        {
            if (!player)
                return SPELL_FAILED_REQUIRES_AREA;

            MapEntry const* mapEntry = sMapStore.LookupEntry(map_id);
            if (!mapEntry)
                return SPELL_FAILED_INCORRECT_AREA;

            if (!mapEntry->IsBattleground())
                return SPELL_FAILED_REQUIRES_AREA;

            Battleground* bg = player->GetBattleground();
            return bg && bg->GetStatus() == STATUS_WAIT_JOIN ? SPELL_CAST_OK : SPELL_FAILED_REQUIRES_AREA;
        }
        case 32724:                                         // Gold Team (Alliance)
        case 32725:                                         // Green Team (Alliance)
        case 35774:                                         // Gold Team (Horde)
        case 35775:                                         // Green Team (Horde)
        {
            MapEntry const* mapEntry = sMapStore.LookupEntry(map_id);
            if (!mapEntry)
                return SPELL_FAILED_INCORRECT_AREA;

            return mapEntry->IsBattleArena() && player && player->InBattleground() ? SPELL_CAST_OK : SPELL_FAILED_REQUIRES_AREA;
        }
        case 32727:                                         // Arena Preparation
        {
            if (!player)
                return SPELL_FAILED_REQUIRES_AREA;

            MapEntry const* mapEntry = sMapStore.LookupEntry(map_id);
            if (!mapEntry)
                return SPELL_FAILED_INCORRECT_AREA;

            if (!mapEntry->IsBattleArena())
                return SPELL_FAILED_REQUIRES_AREA;

            Battleground* bg = player->GetBattleground();
            return bg && bg->GetStatus() == STATUS_WAIT_JOIN ? SPELL_CAST_OK : SPELL_FAILED_REQUIRES_AREA;
        }
    }

    return SPELL_CAST_OK;
}

SpellCastResult SpellInfo::CheckTarget(WorldObject const* caster, WorldObject const* target, bool implicit /*= true*/) const
{
    if (HasAttribute(SPELL_ATTR1_CANT_TARGET_SELF) && caster == target)
        return SPELL_FAILED_BAD_TARGETS;

    // check visibility - ignore invisibility/stealth for implicit (area) targets
    if (!HasAttribute(SPELL_ATTR6_CAN_TARGET_INVISIBLE) && !caster->CanSeeOrDetect(target, implicit))
        return SPELL_FAILED_BAD_TARGETS;

    Unit const* unitTarget = target->ToUnit();

    // creature/player specific target checks
    if (unitTarget)
    {
        // spells cannot be cast if target has a pet in combat either
        if (HasAttribute(SPELL_ATTR1_CANT_TARGET_IN_COMBAT) && (unitTarget->IsInCombat() || unitTarget->HasUnitFlag(UNIT_FLAG_PET_IN_COMBAT)))
            return SPELL_FAILED_TARGET_AFFECTING_COMBAT;

        // only spells with SPELL_ATTR3_ONLY_TARGET_GHOSTS can target ghosts
        if (HasAttribute(SPELL_ATTR3_ONLY_TARGET_GHOSTS) != unitTarget->HasAuraType(SPELL_AURA_GHOST))
        {
            if (HasAttribute(SPELL_ATTR3_ONLY_TARGET_GHOSTS))
                return SPELL_FAILED_TARGET_NOT_GHOST;
            else
                return SPELL_FAILED_BAD_TARGETS;
        }

        if (caster != unitTarget)
        {
            if (caster->GetTypeId() == TYPEID_PLAYER)
            {
                // Do not allow these spells to target creatures not tapped by us (Banish, Polymorph, many quest spells)
                if (HasAttribute(SPELL_ATTR2_CANT_TARGET_TAPPED))
                    if (Creature const* targetCreature = unitTarget->ToCreature())
                        if (targetCreature->hasLootRecipient() && !targetCreature->isTappedBy(caster->ToPlayer()))
                            return SPELL_FAILED_CANT_CAST_ON_TAPPED;

                if (HasAttribute(SPELL_ATTR0_CU_PICKPOCKET))
                {
                     if (unitTarget->GetTypeId() == TYPEID_PLAYER)
                         return SPELL_FAILED_BAD_TARGETS;
                     else if ((unitTarget->GetCreatureTypeMask() & CREATURE_TYPEMASK_HUMANOID_OR_UNDEAD) == 0)
                         return SPELL_FAILED_TARGET_NO_POCKETS;
                }

                // Not allow disarm unarmed player
                if (Mechanic == MECHANIC_DISARM)
                {
                    if (unitTarget->GetTypeId() == TYPEID_PLAYER)
                    {
                        Player const* player = unitTarget->ToPlayer();
                        if (!player->GetWeaponForAttack(BASE_ATTACK) || !player->IsUseEquipedWeapon(true))
                            return SPELL_FAILED_TARGET_NO_WEAPONS;
                    }
                    else if (!unitTarget->GetVirtualItemId(0))
                        return SPELL_FAILED_TARGET_NO_WEAPONS;
                }
            }
        }
    }
    // corpse specific target checks
    else if (Corpse const* corpseTarget = target->ToCorpse())
    {
        // cannot target bare bones
        if (corpseTarget->GetType() == CORPSE_BONES)
            return SPELL_FAILED_BAD_TARGETS;
        // we have to use owner for some checks (aura preventing resurrection for example)
        if (Player* owner = ObjectAccessor::FindPlayer(corpseTarget->GetOwnerGUID()))
            unitTarget = owner;
        // we're not interested in corpses without owner
        else
            return SPELL_FAILED_BAD_TARGETS;
    }
    // other types of objects - always valid
    else return SPELL_CAST_OK;

    // corpseOwner and unit specific target checks
    if (HasAttribute(SPELL_ATTR3_ONLY_TARGET_PLAYERS) && unitTarget->GetTypeId() != TYPEID_PLAYER)
       return SPELL_FAILED_TARGET_NOT_PLAYER;

    if (!IsAllowingDeadTarget() && !unitTarget->IsAlive())
       return SPELL_FAILED_TARGETS_DEAD;

    // check this flag only for implicit targets (chain and area), allow to explicitly target units for spells like Shield of Righteousness
    if (implicit && HasAttribute(SPELL_ATTR6_CANT_TARGET_CROWD_CONTROLLED) && !unitTarget->CanFreeMove())
       return SPELL_FAILED_BAD_TARGETS;

    // checked in Unit::IsValidAttack/AssistTarget, shouldn't be checked for ENTRY targets
    //if (!HasAttribute(SPELL_ATTR6_CAN_TARGET_UNTARGETABLE) && target->HasUnitFlag(UNIT_FLAG_UNINTERACTIBLE))
    //    return SPELL_FAILED_BAD_TARGETS;

    //if (!HasAttribute(SPELL_ATTR6_CAN_TARGET_POSSESSED_FRIENDS))

    if (!CheckTargetCreatureType(unitTarget))
    {
        if (target->GetTypeId() == TYPEID_PLAYER)
            return SPELL_FAILED_TARGET_IS_PLAYER;
        else
            return SPELL_FAILED_BAD_TARGETS;
    }

    // check GM mode and GM invisibility - only for player casts (npc casts are controlled by AI) and negative spells
    if (unitTarget != caster && (caster->GetAffectingPlayer() || !IsPositive()) && unitTarget->GetTypeId() == TYPEID_PLAYER)
    {
        if (!unitTarget->ToPlayer()->IsVisible())
            return SPELL_FAILED_BM_OR_INVISGOD;

        if (unitTarget->ToPlayer()->IsGameMaster())
            return SPELL_FAILED_BM_OR_INVISGOD;
    }

    // not allow casting on flying player
    if (unitTarget->HasUnitState(UNIT_STATE_IN_FLIGHT) && !HasAttribute(SPELL_ATTR0_CU_ALLOW_INFLIGHT_TARGET))
        return SPELL_FAILED_BAD_TARGETS;

    /* TARGET_UNIT_MASTER gets blocked here for passengers, because the whole idea of this check is to
    not allow passengers to be implicitly hit by spells, however this target type should be an exception,
    if this is left it kills spells that award kill credit from vehicle to master (few spells),
    the use of these 2 covers passenger target check, logically, if vehicle cast this to master it should always hit
    him, because it would be it's passenger, there's no such case where this gets to fail legitimacy, this problem
    cannot be solved from within the check in other way since target type cannot be called for the spell currently
    Spell examples: [ID - 52864 Devour Water, ID - 52862 Devour Wind, ID - 49370 Wyrmrest Defender: Destabilize Azure Dragonshrine Effect] */
    if (Unit const* unitCaster = caster->ToUnit())
    {
        if (!unitCaster->IsVehicle() && !(unitCaster->GetCharmerOrOwner() == target))
        {
            if (TargetAuraState && !unitTarget->HasAuraState(AuraStateType(TargetAuraState), this, unitCaster))
                return SPELL_FAILED_TARGET_AURASTATE;

            if (TargetAuraStateNot && unitTarget->HasAuraState(AuraStateType(TargetAuraStateNot), this, unitCaster))
                return SPELL_FAILED_TARGET_AURASTATE;
        }
    }

    if (TargetAuraSpell && !unitTarget->HasAura(sSpellMgr->GetSpellIdForDifficulty(TargetAuraSpell, caster)))
        return SPELL_FAILED_TARGET_AURASTATE;

    if (ExcludeTargetAuraSpell && unitTarget->HasAura(sSpellMgr->GetSpellIdForDifficulty(ExcludeTargetAuraSpell, caster)))
        return SPELL_FAILED_TARGET_AURASTATE;

    if (unitTarget->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION) && !HasAttribute(SPELL_ATTR7_BYPASS_NO_RESURRECT_AURA))
        if (HasEffect(SPELL_EFFECT_SELF_RESURRECT) || HasEffect(SPELL_EFFECT_RESURRECT) || HasEffect(SPELL_EFFECT_RESURRECT_NEW))
            return SPELL_FAILED_TARGET_CANNOT_BE_RESURRECTED;

    return SPELL_CAST_OK;
}

SpellCastResult SpellInfo::CheckExplicitTarget(WorldObject const* caster, WorldObject const* target, Item const* itemTarget /*= nullptr*/) const
{
    uint32 neededTargets = GetExplicitTargetMask();
    if (!target)
    {
        if (neededTargets & (TARGET_FLAG_UNIT_MASK | TARGET_FLAG_GAMEOBJECT_MASK | TARGET_FLAG_CORPSE_MASK))
            if (!(neededTargets & TARGET_FLAG_GAMEOBJECT_ITEM) || !itemTarget)
                return SPELL_FAILED_BAD_TARGETS;
        return SPELL_CAST_OK;
    }

    if (Unit const* unitTarget = target->ToUnit())
    {
        if (neededTargets & (TARGET_FLAG_UNIT_ENEMY | TARGET_FLAG_UNIT_ALLY | TARGET_FLAG_UNIT_RAID | TARGET_FLAG_UNIT_PARTY | TARGET_FLAG_UNIT_MINIPET | TARGET_FLAG_UNIT_PASSENGER))
        {
            Unit const* unitCaster = caster->ToUnit();
            if (neededTargets & TARGET_FLAG_UNIT_ENEMY)
                if (caster->IsValidAttackTarget(unitTarget, this))
                    return SPELL_CAST_OK;
            if ((neededTargets & TARGET_FLAG_UNIT_ALLY)
                || ((neededTargets & TARGET_FLAG_UNIT_PARTY) && unitCaster && unitCaster->IsInPartyWith(unitTarget))
                || ((neededTargets & TARGET_FLAG_UNIT_RAID) && unitCaster && unitCaster->IsInRaidWith(unitTarget)))
                    if (caster->IsValidAssistTarget(unitTarget, this))
                        return SPELL_CAST_OK;
            if ((neededTargets & TARGET_FLAG_UNIT_MINIPET) && unitCaster)
                if (unitTarget->GetGUID() == unitCaster->GetCritterGUID())
                    return SPELL_CAST_OK;
            if ((neededTargets & TARGET_FLAG_UNIT_PASSENGER) && unitCaster)
                if (unitTarget->IsOnVehicle(unitCaster))
                    return SPELL_CAST_OK;
            return SPELL_FAILED_BAD_TARGETS;
        }
    }
    return SPELL_CAST_OK;
}

SpellCastResult SpellInfo::CheckVehicle(Unit const* caster) const
{
    // All creatures should be able to cast as passengers freely, restriction and attribute are only for players
    if (caster->GetTypeId() != TYPEID_PLAYER)
        return SPELL_CAST_OK;

    Vehicle* vehicle = caster->GetVehicle();
    if (vehicle)
    {
        uint16 checkMask = 0;
        for (SpellEffectInfo const& effect : GetEffects())
        {
            if (effect.IsAura(SPELL_AURA_MOD_SHAPESHIFT))
            {
                SpellShapeshiftFormEntry const* shapeShiftEntry = sSpellShapeshiftFormStore.LookupEntry(effect.MiscValue);
                if (shapeShiftEntry && (shapeShiftEntry->Flags & 1) == 0)  // unk flag
                    checkMask |= VEHICLE_SEAT_FLAG_UNCONTROLLED;
                break;
            }
        }

        if (HasAura(SPELL_AURA_MOUNTED))
            checkMask |= VEHICLE_SEAT_FLAG_CAN_CAST_MOUNT_SPELL;

        if (!checkMask)
            checkMask = VEHICLE_SEAT_FLAG_CAN_ATTACK;

        VehicleSeatEntry const* vehicleSeat = vehicle->GetSeatForPassenger(caster);
        if (!HasAttribute(SPELL_ATTR6_CASTABLE_WHILE_ON_VEHICLE) && !HasAttribute(SPELL_ATTR0_CASTABLE_WHILE_MOUNTED)
            && (vehicleSeat->Flags & checkMask) != checkMask)
            return SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW;

        // Can only summon uncontrolled minions/guardians when on controlled vehicle
        if (vehicleSeat->Flags & (VEHICLE_SEAT_FLAG_CAN_CONTROL | VEHICLE_SEAT_FLAG_UNK2))
        {
            for (SpellEffectInfo const& effect : GetEffects())
            {
                if (!effect.IsEffect(SPELL_EFFECT_SUMMON))
                    continue;

                SummonPropertiesEntry const* props = sSummonPropertiesStore.LookupEntry(effect.MiscValueB);
                if (props && props->Control != SUMMON_CATEGORY_WILD)
                    return SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW;
            }
        }
    }

    return SPELL_CAST_OK;
}

bool SpellInfo::CheckTargetCreatureType(Unit const* target) const
{
    // Curse of Doom & Exorcism: not find another way to fix spell target check :/
    if (SpellFamilyName == SPELLFAMILY_WARLOCK && GetCategory() == 1179)
    {
        // not allow cast at player
        if (target->GetTypeId() == TYPEID_PLAYER)
            return false;
        else
            return true;
    }

    // if target is magnet (i.e Grounding Totem) the check is skipped
    if (target->IsMagnet())
        return true;

    uint32 creatureType = target->GetCreatureTypeMask();
    return !TargetCreatureType || !creatureType || (creatureType & TargetCreatureType);
}

SpellSchoolMask SpellInfo::GetSchoolMask() const
{
    return SpellSchoolMask(SchoolMask);
}

uint32 SpellInfo::GetAllEffectsMechanicMask() const
{
    uint32 mask = 0;
    if (Mechanic)
        mask |= 1 << Mechanic;

    for (SpellEffectInfo const& effect : GetEffects())
        if (effect.IsEffect() && effect.Mechanic)
            mask |= 1 << effect.Mechanic;

    return mask;
}

uint32 SpellInfo::GetEffectMechanicMask(SpellEffIndex effIndex) const
{
    uint32 mask = 0;
    if (Mechanic)
        mask |= 1 << Mechanic;

    if (GetEffect(effIndex).IsEffect() && GetEffect(effIndex).Mechanic)
        mask |= 1 << GetEffect(effIndex).Mechanic;

    return mask;
}

uint32 SpellInfo::GetSpellMechanicMaskByEffectMask(uint32 effectMask) const
{
    uint32 mask = 0;
    if (Mechanic)
        mask |= 1 << Mechanic;

    for (SpellEffectInfo const& effect : GetEffects())
        if ((effectMask & (1 << effect.EffectIndex)) && effect.Mechanic)
            mask |= 1 << effect.Mechanic;

    return mask;
}

Mechanics SpellInfo::GetEffectMechanic(SpellEffIndex effIndex) const
{
    if (GetEffect(effIndex).IsEffect() && GetEffect(effIndex).Mechanic)
        return GetEffect(effIndex).Mechanic;

    if (Mechanic)
        return Mechanics(Mechanic);

    return MECHANIC_NONE;
}

uint32 SpellInfo::GetDispelMask() const
{
    return GetDispelMask(DispelType(Dispel));
}

uint32 SpellInfo::GetDispelMask(DispelType type)
{
    // If dispel all
    if (type == DISPEL_ALL)
        return DISPEL_ALL_MASK;
    else
        return uint32(1 << type);
}

uint32 SpellInfo::GetExplicitTargetMask() const
{
    return ExplicitTargetMask;
}

AuraStateType SpellInfo::GetAuraState() const
{
    return _auraState;
}

void SpellInfo::_LoadAuraState()
{
    _auraState = [this]() -> AuraStateType
    {
        // Seals
        if (GetSpellSpecific() == SPELL_SPECIFIC_SEAL)
            return AURA_STATE_JUDGEMENT;

        // Conflagrate aura state on Immolate and Shadowflame
        if (SpellFamilyName == SPELLFAMILY_WARLOCK &&
            // Immolate
            ((SpellFamilyFlags[0] & 4) ||
                // Shadowflame
            (SpellFamilyFlags[2] & 2)))
            return AURA_STATE_CONFLAGRATE;

        // Faerie Fire (druid versions)
        if (SpellFamilyName == SPELLFAMILY_DRUID && SpellFamilyFlags[0] & 0x400)
            return AURA_STATE_FAERIE_FIRE;

        // Sting (hunter's pet ability)
        if (GetCategory() == 1133)
            return AURA_STATE_FAERIE_FIRE;

        // Victorious
        if (SpellFamilyName == SPELLFAMILY_WARRIOR &&  SpellFamilyFlags[1] & 0x00040000)
            return AURA_STATE_WARRIOR_VICTORY_RUSH;

        // Swiftmend state on Regrowth & Rejuvenation
        if (SpellFamilyName == SPELLFAMILY_DRUID && SpellFamilyFlags[0] & 0x50)
            return AURA_STATE_SWIFTMEND;

        // Deadly poison aura state
        if (SpellFamilyName == SPELLFAMILY_ROGUE && SpellFamilyFlags[0] & 0x10000)
            return AURA_STATE_DEADLY_POISON;

        // Enrage aura state
        if (Dispel == DISPEL_ENRAGE)
            return AURA_STATE_ENRAGE;

        // Bleeding aura state
        if (GetAllEffectsMechanicMask() & 1 << MECHANIC_BLEED)
            return AURA_STATE_BLEEDING;

        if (GetSchoolMask() & SPELL_SCHOOL_MASK_FROST)
            for (SpellEffectInfo const& effect : GetEffects())
                if (effect.IsAura(SPELL_AURA_MOD_STUN) || effect.IsAura(SPELL_AURA_MOD_ROOT))
                    return AURA_STATE_FROZEN;

        switch (Id)
        {
            case 71465: // Divine Surge
            case 50241: // Evasive Charges
                return AURA_STATE_UNKNOWN22;
            case 9991:  // Touch of Zanzil
            case 35325: // Glowing Blood
            case 35328: // Lambent Blood
            case 35329: // Vibrant Blood
            case 35331: // Black Blood
            case 49163: // Perpetual Instability
                return AURA_STATE_FAERIE_FIRE;
            default:
                break;
        }

        return AURA_STATE_NONE;
    }();
}

SpellSpecificType SpellInfo::GetSpellSpecific() const
{
    return _spellSpecific;
}

void SpellInfo::_LoadSpellSpecific()
{
    _spellSpecific = [this]() -> SpellSpecificType
    {
        switch (SpellFamilyName)
        {
            case SPELLFAMILY_GENERIC:
            {
                // Food / Drinks (mostly)
                if (AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_SEATED)
                {
                    bool food = false;
                    bool drink = false;
                    for (SpellEffectInfo const& effect : GetEffects())
                    {
                        if (!effect.IsAura())
                            continue;
                        switch (effect.ApplyAuraName)
                        {
                            // Food
                            case SPELL_AURA_MOD_REGEN:
                            case SPELL_AURA_OBS_MOD_HEALTH:
                                food = true;
                                break;
                                // Drink
                            case SPELL_AURA_MOD_POWER_REGEN:
                            case SPELL_AURA_OBS_MOD_POWER:
                                drink = true;
                                break;
                            default:
                                break;
                        }
                    }

                    if (food && drink)
                        return SPELL_SPECIFIC_FOOD_AND_DRINK;
                    else if (food)
                        return SPELL_SPECIFIC_FOOD;
                    else if (drink)
                        return SPELL_SPECIFIC_DRINK;
                }
                // scrolls effects
                else
                {
                    SpellInfo const* firstRankSpellInfo = GetFirstRankSpell();
                    switch (firstRankSpellInfo->Id)
                    {
                        case 8118: // Strength
                        case 8099: // Stamina
                        case 8112: // Spirit
                        case 8096: // Intellect
                        case 8115: // Agility
                        case 8091: // Armor
                            return SPELL_SPECIFIC_SCROLL;
                        default:
                            break;
                    }

                    switch (Id)
                    {
                        case 12880: // Enrage (Enrage)
                        case 14201:
                        case 14202:
                        case 14203:
                        case 14204:
                        case 57518: // Enrage (Wrecking Crew)
                        case 57519:
                        case 57520:
                        case 57521:
                        case 57522:
                        case 57514: // Enrage (Imp. Defensive Stance)
                        case 57516:
                            return SPELL_SPECIFIC_WARRIOR_ENRAGE;
                        default:
                            break;
                    }
                }
                break;
            }
            case SPELLFAMILY_MAGE:
            {
                // family flags 18(Molten), 25(Frost/Ice), 28(Mage)
                if (SpellFamilyFlags[0] & 0x12040000)
                    return SPELL_SPECIFIC_MAGE_ARMOR;

                // Arcane brillance and Arcane intelect (normal check fails because of flags difference)
                if (SpellFamilyFlags[0] & 0x400)
                    return SPELL_SPECIFIC_MAGE_ARCANE_BRILLANCE;

                if ((SpellFamilyFlags[0] & 0x1000000) && GetEffect(EFFECT_0).IsAura(SPELL_AURA_MOD_CONFUSE))
                    return SPELL_SPECIFIC_MAGE_POLYMORPH;

                break;
            }
            case SPELLFAMILY_WARRIOR:
            {
                if (Id == 12292) // Death Wish
                    return SPELL_SPECIFIC_WARRIOR_ENRAGE;

                break;
            }
            case SPELLFAMILY_WARLOCK:
            {
                // only warlock curses have this
                if (Dispel == DISPEL_CURSE)
                    return SPELL_SPECIFIC_CURSE;

                // Warlock (Demon Armor | Demon Skin | Fel Armor)
                if (SpellFamilyFlags[1] & 0x20000020 || SpellFamilyFlags[2] & 0x00000010)
                    return SPELL_SPECIFIC_WARLOCK_ARMOR;

                //seed of corruption and corruption
                if (SpellFamilyFlags[1] & 0x10 || SpellFamilyFlags[0] & 0x2)
                    return SPELL_SPECIFIC_WARLOCK_CORRUPTION;
                break;
            }
            case SPELLFAMILY_PRIEST:
            {
                // Divine Spirit and Prayer of Spirit
                if (SpellFamilyFlags[0] & 0x20)
                    return SPELL_SPECIFIC_PRIEST_DIVINE_SPIRIT;

                break;
            }
            case SPELLFAMILY_HUNTER:
            {
                // only hunter stings have this
                if (Dispel == DISPEL_POISON)
                    return SPELL_SPECIFIC_STING;

                // only hunter aspects have this (but not all aspects in hunter family)
                if (SpellFamilyFlags.HasFlag(0x00380000, 0x00440000, 0x00001010))
                    return SPELL_SPECIFIC_ASPECT;

                break;
            }
            case SPELLFAMILY_PALADIN:
            {
                // Collection of all the seal family flags. No other paladin spell has any of those.
                if (SpellFamilyFlags[1] & 0x26000C00
                    || SpellFamilyFlags[0] & 0x0A000000)
                    return SPELL_SPECIFIC_SEAL;

                if (SpellFamilyFlags[0] & 0x00002190)
                    return SPELL_SPECIFIC_HAND;

                // Judgement of Wisdom, Judgement of Light, Judgement of Justice
                if (Id == 20184 || Id == 20185 || Id == 20186)
                    return SPELL_SPECIFIC_JUDGEMENT;

                // only paladin auras have this (for palaldin class family)
                if (SpellFamilyFlags[2] & 0x00000020)
                    return SPELL_SPECIFIC_AURA;

                break;
            }
            case SPELLFAMILY_SHAMAN:
            {
                // family flags 10 (Lightning), 42 (Earth), 37 (Water), proc shield from T2 8 pieces bonus
                if (SpellFamilyFlags[1] & 0x420
                    || SpellFamilyFlags[0] & 0x00000400
                    || Id == 23552)
                    return SPELL_SPECIFIC_ELEMENTAL_SHIELD;

                break;
            }
            case SPELLFAMILY_DEATHKNIGHT:
                if (Id == 48266 || Id == 48263 || Id == 48265)
                    return SPELL_SPECIFIC_PRESENCE;
                break;
        }

        for (SpellEffectInfo const& effect : GetEffects())
        {
            if (effect.IsEffect(SPELL_EFFECT_APPLY_AURA))
            {
                switch (effect.ApplyAuraName)
                {
                    case SPELL_AURA_MOD_CHARM:
                    case SPELL_AURA_MOD_POSSESS_PET:
                    case SPELL_AURA_MOD_POSSESS:
                    case SPELL_AURA_AOE_CHARM:
                        return SPELL_SPECIFIC_CHARM;
                    case SPELL_AURA_TRACK_CREATURES:
                        /// @workaround For non-stacking tracking spells (We need generic solution)
                        if (Id == 30645) // Gas Cloud Tracking
                            return SPELL_SPECIFIC_NORMAL;
                        [[fallthrough]];
                    case SPELL_AURA_TRACK_RESOURCES:
                    case SPELL_AURA_TRACK_STEALTHED:
                        return SPELL_SPECIFIC_TRACKER;
                    default:
                        break;
                }
            }
        }

        return SPELL_SPECIFIC_NORMAL;
    }();
}

void SpellInfo::_LoadSpellDiminishInfo()
{
    auto diminishingGroupCompute = [this](bool triggered) -> DiminishingGroup
    {
        if (IsPositive())
            return DIMINISHING_NONE;

        if (HasAura(SPELL_AURA_MOD_TAUNT))
            return DIMINISHING_TAUNT;

        // Explicit Diminishing Groups
        switch (SpellFamilyName)
        {
            case SPELLFAMILY_GENERIC:
            {
                // Pet charge effects (Infernal Awakening, Demon Charge)
                if (SpellVisual[0] == 2816 && SpellIconID == 15)
                    return DIMINISHING_CONTROLLED_STUN;
                // Frost Tomb
                else if (Id == 48400)
                    return DIMINISHING_NONE;
                // Gnaw
                else if (Id == 47481)
                    return DIMINISHING_CONTROLLED_STUN;
                // ToC Icehowl Arctic Breath
                else if (SpellVisual[0] == 14153)
                    return DIMINISHING_NONE;
                // Black Plague
                else if (Id == 64155)
                    return DIMINISHING_NONE;
                // Screams of the Dead (King Ymiron)
                else if (Id == 51750)
                    return DIMINISHING_NONE;
                // Crystallize (Keristrasza heroic)
                else if (Id == 48179)
                    return DIMINISHING_NONE;
                break;
            }
            // Event spells
            case SPELLFAMILY_UNK1:
                return DIMINISHING_NONE;
            case SPELLFAMILY_MAGE:
            {
                // Frostbite
                if (SpellFamilyFlags[1] & 0x80000000)
                    return DIMINISHING_ROOT;
                // Shattered Barrier
                else if (SpellVisual[0] == 12297)
                    return DIMINISHING_ROOT;
                // Deep Freeze
                else if (SpellIconID == 2939 && SpellVisual[0] == 9963)
                    return DIMINISHING_CONTROLLED_STUN;
                // Frost Nova / Freeze (Water Elemental)
                else if (SpellIconID == 193)
                    return DIMINISHING_CONTROLLED_ROOT;
                // Dragon's Breath
                else if (SpellFamilyFlags[0] & 0x800000)
                    return DIMINISHING_DRAGONS_BREATH;
                break;
            }
            case SPELLFAMILY_WARRIOR:
            {
                // Hamstring - limit duration to 10s in PvP
                if (SpellFamilyFlags[0] & 0x2)
                    return DIMINISHING_LIMITONLY;
                // Charge Stun (own diminishing)
                else if (SpellFamilyFlags[0] & 0x01000000)
                    return DIMINISHING_CHARGE;
                break;
            }
            case SPELLFAMILY_WARLOCK:
            {
                // Curses/etc
                if ((SpellFamilyFlags[0] & 0x80000000) || (SpellFamilyFlags[1] & 0x200))
                    return DIMINISHING_LIMITONLY;
                // Seduction
                else if (SpellFamilyFlags[1] & 0x10000000)
                    return DIMINISHING_FEAR;
                break;
            }
            case SPELLFAMILY_DRUID:
            {
                // Pounce
                if (SpellFamilyFlags[0] & 0x20000)
                    return DIMINISHING_OPENING_STUN;
                // Cyclone
                else if (SpellFamilyFlags[1] & 0x20)
                    return DIMINISHING_CYCLONE;
                // Entangling Roots
                // Nature's Grasp
                else if (SpellFamilyFlags[0] & 0x00000200)
                    return DIMINISHING_CONTROLLED_ROOT;
                // Faerie Fire
                else if (SpellFamilyFlags[0] & 0x400)
                    return DIMINISHING_LIMITONLY;
                break;
            }
            case SPELLFAMILY_ROGUE:
            {
                // Gouge
                if (SpellFamilyFlags[0] & 0x8)
                    return DIMINISHING_DISORIENT;
                // Blind
                else if (SpellFamilyFlags[0] & 0x1000000)
                    return DIMINISHING_FEAR;
                // Cheap Shot
                else if (SpellFamilyFlags[0] & 0x400)
                    return DIMINISHING_OPENING_STUN;
                // Crippling poison - Limit to 10 seconds in PvP (No SpellFamilyFlags)
                else if (SpellIconID == 163)
                    return DIMINISHING_LIMITONLY;
                break;
            }
            case SPELLFAMILY_HUNTER:
            {
                // Hunter's Mark
                if ((SpellFamilyFlags[0] & 0x400) && SpellIconID == 538)
                    return DIMINISHING_LIMITONLY;
                // Scatter Shot (own diminishing)
                else if ((SpellFamilyFlags[0] & 0x40000) && SpellIconID == 132)
                    return DIMINISHING_SCATTER_SHOT;
                // Entrapment (own diminishing)
                else if (SpellVisual[0] == 7484 && SpellIconID == 20)
                    return DIMINISHING_ENTRAPMENT;
                // Wyvern Sting mechanic is MECHANIC_SLEEP but the diminishing is DIMINISHING_DISORIENT
                else if ((SpellFamilyFlags[1] & 0x1000) && SpellIconID == 1721)
                    return DIMINISHING_DISORIENT;
                // Freezing Arrow
                else if (SpellFamilyFlags[0] & 0x8)
                    return DIMINISHING_DISORIENT;
                break;
            }
            case SPELLFAMILY_PALADIN:
            {
                // Judgement of Justice - limit duration to 10s in PvP
                if (SpellFamilyFlags[0] & 0x100000)
                    return DIMINISHING_LIMITONLY;
                // Turn Evil
                else if ((SpellFamilyFlags[1] & 0x804000) && SpellIconID == 309)
                    return DIMINISHING_FEAR;
                break;
            }
            case SPELLFAMILY_SHAMAN:
            {
                // Storm, Earth and Fire - Earthgrab
                if (SpellFamilyFlags[2] & 0x4000)
                    return DIMINISHING_NONE;
                break;
            }
            case SPELLFAMILY_DEATHKNIGHT:
            {
                // Hungering Cold (no flags)
                if (SpellIconID == 2797)
                    return DIMINISHING_DISORIENT;
                // Mark of Blood
                else if ((SpellFamilyFlags[0] & 0x10000000) && SpellIconID == 2285)
                    return DIMINISHING_LIMITONLY;
                break;
            }
            default:
                break;
        }

        // Lastly - Set diminishing depending on mechanic
        uint32 mechanic = GetAllEffectsMechanicMask();
        if (mechanic & (1 << MECHANIC_CHARM))
            return DIMINISHING_MIND_CONTROL;
        if (mechanic & (1 << MECHANIC_SILENCE))
            return DIMINISHING_SILENCE;
        if (mechanic & (1 << MECHANIC_SLEEP))
            return DIMINISHING_SLEEP;
        if (mechanic & ((1 << MECHANIC_SAPPED) | (1 << MECHANIC_POLYMORPH) | (1 << MECHANIC_SHACKLE)))
            return DIMINISHING_DISORIENT;
        // Mechanic Knockout, except Blast Wave
        if (mechanic & (1 << MECHANIC_KNOCKOUT) && SpellIconID != 292)
            return DIMINISHING_DISORIENT;
        if (mechanic & (1 << MECHANIC_DISARM))
            return DIMINISHING_DISARM;
        if (mechanic & (1 << MECHANIC_FEAR))
            return DIMINISHING_FEAR;
        if (mechanic & (1 << MECHANIC_STUN))
            return triggered ? DIMINISHING_STUN : DIMINISHING_CONTROLLED_STUN;
        if (mechanic & (1 << MECHANIC_BANISH))
            return DIMINISHING_BANISH;
        if (mechanic & (1 << MECHANIC_ROOT))
            return triggered ? DIMINISHING_ROOT : DIMINISHING_CONTROLLED_ROOT;
        if (mechanic & (1 << MECHANIC_HORROR))
            return DIMINISHING_HORROR;

        return DIMINISHING_NONE;
    };

    auto diminishingTypeCompute = [](DiminishingGroup group) -> DiminishingReturnsType
    {
        switch (group)
        {
            case DIMINISHING_TAUNT:
            case DIMINISHING_CONTROLLED_STUN:
            case DIMINISHING_STUN:
            case DIMINISHING_OPENING_STUN:
            case DIMINISHING_CYCLONE:
            case DIMINISHING_CHARGE:
                return DRTYPE_ALL;
            case DIMINISHING_LIMITONLY:
            case DIMINISHING_NONE:
                return DRTYPE_NONE;
            default:
                return DRTYPE_PLAYER;
        }
    };

    auto diminishingMaxLevelCompute = [](DiminishingGroup group) -> DiminishingLevels
    {
        switch (group)
        {
            case DIMINISHING_TAUNT:
                return DIMINISHING_LEVEL_TAUNT_IMMUNE;
            default:
                return DIMINISHING_LEVEL_IMMUNE;
        }
    };

    auto diminishingLimitDurationCompute = [this](DiminishingGroup group) -> int32
    {
        auto isGroupDurationLimited = [group]() -> bool
        {
            switch (group)
            {
                case DIMINISHING_BANISH:
                case DIMINISHING_CONTROLLED_STUN:
                case DIMINISHING_CONTROLLED_ROOT:
                case DIMINISHING_CYCLONE:
                case DIMINISHING_DISORIENT:
                case DIMINISHING_ENTRAPMENT:
                case DIMINISHING_FEAR:
                case DIMINISHING_HORROR:
                case DIMINISHING_MIND_CONTROL:
                case DIMINISHING_OPENING_STUN:
                case DIMINISHING_ROOT:
                case DIMINISHING_STUN:
                case DIMINISHING_SLEEP:
                case DIMINISHING_LIMITONLY:
                    return true;
                default:
                    return false;
            }
        };

        if (!isGroupDurationLimited())
            return 0;

        // Explicit diminishing duration
        switch (SpellFamilyName)
        {
            case SPELLFAMILY_DRUID:
            {
                // Faerie Fire - limit to 40 seconds in PvP (3.1)
                if (SpellFamilyFlags[0] & 0x400)
                    return 40 * IN_MILLISECONDS;
                break;
            }
            case SPELLFAMILY_HUNTER:
            {
                // Wyvern Sting
                if (SpellFamilyFlags[1] & 0x1000)
                    return 6 * IN_MILLISECONDS;
                // Hunter's Mark
                if (SpellFamilyFlags[0] & 0x400)
                    return 120 * IN_MILLISECONDS;
                break;
            }
            case SPELLFAMILY_PALADIN:
            {
                // Repentance - limit to 6 seconds in PvP
                if (SpellFamilyFlags[0] & 0x4)
                    return 6 * IN_MILLISECONDS;
                break;
            }
            case SPELLFAMILY_WARLOCK:
            {
                // Banish - limit to 6 seconds in PvP
                if (SpellFamilyFlags[1] & 0x8000000)
                    return 6 * IN_MILLISECONDS;
                // Curse of Tongues - limit to 12 seconds in PvP
                else if (SpellFamilyFlags[2] & 0x800)
                    return 12 * IN_MILLISECONDS;
                // Curse of Elements - limit to 120 seconds in PvP
                else if (SpellFamilyFlags[1] & 0x200)
                    return 120 * IN_MILLISECONDS;
                break;
            }
            default:
                break;
        }

        return 10 * IN_MILLISECONDS;
    };

    SpellDiminishInfo triggeredInfo, normalInfo;
    triggeredInfo.DiminishGroup = diminishingGroupCompute(true);
    triggeredInfo.DiminishReturnType = diminishingTypeCompute(triggeredInfo.DiminishGroup);
    triggeredInfo.DiminishMaxLevel = diminishingMaxLevelCompute(triggeredInfo.DiminishGroup);
    triggeredInfo.DiminishDurationLimit = diminishingLimitDurationCompute(triggeredInfo.DiminishGroup);

    normalInfo.DiminishGroup = diminishingGroupCompute(false);
    normalInfo.DiminishReturnType = diminishingTypeCompute(normalInfo.DiminishGroup);
    normalInfo.DiminishMaxLevel = diminishingMaxLevelCompute(normalInfo.DiminishGroup);
    normalInfo.DiminishDurationLimit = diminishingLimitDurationCompute(normalInfo.DiminishGroup);

    _diminishInfoTriggered = triggeredInfo;
    _diminishInfoNonTriggered = normalInfo;
}

DiminishingGroup SpellInfo::GetDiminishingReturnsGroupForSpell(bool triggered) const
{
    return triggered ? _diminishInfoTriggered.DiminishGroup : _diminishInfoNonTriggered.DiminishGroup;
}

DiminishingReturnsType SpellInfo::GetDiminishingReturnsGroupType(bool triggered) const
{
    return triggered ? _diminishInfoTriggered.DiminishReturnType : _diminishInfoNonTriggered.DiminishReturnType;
}

DiminishingLevels SpellInfo::GetDiminishingReturnsMaxLevel(bool triggered) const
{
    return triggered ? _diminishInfoTriggered.DiminishMaxLevel : _diminishInfoNonTriggered.DiminishMaxLevel;
}

int32 SpellInfo::GetDiminishingReturnsLimitDuration(bool triggered) const
{
    return triggered ? _diminishInfoTriggered.DiminishDurationLimit : _diminishInfoNonTriggered.DiminishDurationLimit;
}

void SpellInfo::_LoadImmunityInfo()
{
    std::unique_ptr<SpellEffectInfo::ImmunityInfo> workBuffer = std::make_unique<SpellEffectInfo::ImmunityInfo>();

    for (SpellEffectInfo& effect : _GetEffects())
    {
        uint32 schoolImmunityMask = 0;
        uint32 applyHarmfulAuraImmunityMask = 0;
        uint32 mechanicImmunityMask = 0;
        uint32 dispelImmunity = 0;
        uint32 damageImmunityMask = 0;
        bool removeEffectsWithMechanic = false;

        int32 miscVal = effect.MiscValue;
        int32 amount = effect.CalcValue();

        SpellEffectInfo::ImmunityInfo& immuneInfo = *workBuffer;

        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_MECHANIC_IMMUNITY_MASK:
            {
                switch (miscVal)
                {
                    case 96:   // Free Friend, Uncontrollable Frenzy, Warlord's Presence
                    {
                        mechanicImmunityMask |= IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;

                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DECREASE_SPEED);
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_ROOT);
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_CONFUSE);
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_FEAR);
                        break;
                    }
                    case 1615: // Incite Rage, Wolf Spirit, Overload, Lightning Tendrils
                    {
                        switch (Id)
                        {
                            case 43292: // Incite Rage
                            case 49172: // Wolf Spirit
                                mechanicImmunityMask |= IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;

                                immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                                immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DECREASE_SPEED);
                                immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_ROOT);
                                immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_CONFUSE);
                                immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_FEAR);
                                [[fallthrough]];
                            case 61869: // Overload
                            case 63481:
                            case 61887: // Lightning Tendrils
                            case 63486:
                                mechanicImmunityMask |= (1 << MECHANIC_INTERRUPT) | (1 << MECHANIC_SILENCE);

                                immuneInfo.SpellEffectImmune.insert(SPELL_EFFECT_KNOCK_BACK);
                                immuneInfo.SpellEffectImmune.insert(SPELL_EFFECT_KNOCK_BACK_DEST);
                                break;
                            default:
                                break;
                        }
                        break;
                    }
                    case 679:  // Mind Control, Avenging Fury
                    {
                        if (Id == 57742) // Avenging Fury
                        {
                            mechanicImmunityMask |= IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;

                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DECREASE_SPEED);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_ROOT);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_CONFUSE);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_FEAR);
                        }
                        break;
                    }
                    case 1557: // Startling Roar, Warlord Roar, Break Bonds, Stormshield
                    {
                        if (Id == 64187) // Stormshield
                        {
                            mechanicImmunityMask |= (1 << MECHANIC_STUN);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                        }
                        else
                        {
                            mechanicImmunityMask |= IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;

                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DECREASE_SPEED);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_ROOT);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_CONFUSE);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_FEAR);
                        }
                        break;
                    }
                    case 1614: // Fixate
                    case 1694: // Fixated, Lightning Tendrils
                    {
                        immuneInfo.SpellEffectImmune.insert(SPELL_EFFECT_ATTACK_ME);
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_TAUNT);
                        break;
                    }
                    case 1630: // Fervor, Berserk
                    {
                        if (Id == 64112) // Berserk
                        {
                            immuneInfo.SpellEffectImmune.insert(SPELL_EFFECT_ATTACK_ME);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_TAUNT);
                        }
                        else
                        {
                            mechanicImmunityMask |= IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;

                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DECREASE_SPEED);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_ROOT);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_CONFUSE);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_FEAR);
                        }
                        break;
                    }
                    case 477:  // Bladestorm
                    case 1733: // Bladestorm, Killing Spree
                    {
                        if (!amount)
                        {
                            mechanicImmunityMask |= IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;

                            immuneInfo.SpellEffectImmune.insert(SPELL_EFFECT_KNOCK_BACK);
                            immuneInfo.SpellEffectImmune.insert(SPELL_EFFECT_KNOCK_BACK_DEST);

                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DECREASE_SPEED);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_ROOT);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_CONFUSE);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_FEAR);
                        }
                        break;
                    }
                    case 878: // Whirlwind, Fog of Corruption, Determination
                    {
                        if (Id == 66092) // Determination
                        {
                            mechanicImmunityMask |= (1 << MECHANIC_SNARE) | (1 << MECHANIC_STUN)
                                | (1 << MECHANIC_DISORIENTED) | (1 << MECHANIC_FREEZE);

                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                            immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DECREASE_SPEED);
                        }
                        break;
                    }
                    default:
                        break;
                }

                if (immuneInfo.AuraTypeImmune.empty())
                {
                    if (miscVal & (1 << 10))
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_STUN);
                    if (miscVal & (1 << 1))
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_TRANSFORM);

                    // These flag can be recognized wrong:
                    if (miscVal & (1 << 6))
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DECREASE_SPEED);
                    if (miscVal & (1 << 0))
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_ROOT);
                    if (miscVal & (1 << 2))
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_CONFUSE);
                    if (miscVal & (1 << 9))
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_FEAR);
                    if (miscVal & (1 << 7))
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_MOD_DISARM);
                }
                break;
            }
            case SPELL_AURA_MECHANIC_IMMUNITY:
            {
                switch (Id)
                {
                    case 42292: // PvP trinket
                    case 59752: // Every Man for Himself
                        mechanicImmunityMask |= IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;
                        immuneInfo.AuraTypeImmune.insert(SPELL_AURA_USE_NORMAL_MOVEMENT_SPEED);
                        removeEffectsWithMechanic = true;
                        break;
                    case 34471: // The Beast Within
                    case 19574: // Bestial Wrath
                    case 53490: // Bullheaded
                        mechanicImmunityMask |= IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;
                        removeEffectsWithMechanic = true;
                        break;
                    case 54508: // Demonic Empowerment
                        mechanicImmunityMask |= (1 << MECHANIC_SNARE) | (1 << MECHANIC_ROOT) | (1 << MECHANIC_STUN);
                        break;
                    default:
                        if (miscVal < 1)
                            break;

                        mechanicImmunityMask |= 1 << miscVal;
                        break;
                }

                // Special 100 ms duration spells - PvP trinket, Every Man for Himself and some other
                if (GetMaxDuration() == 100)
                    removeEffectsWithMechanic = true;

                break;
            }
            case SPELL_AURA_EFFECT_IMMUNITY:
            {
                immuneInfo.SpellEffectImmune.insert(static_cast<SpellEffects>(miscVal));
                break;
            }
            case SPELL_AURA_STATE_IMMUNITY:
            {
                immuneInfo.AuraTypeImmune.insert(static_cast<AuraType>(miscVal));
                break;
            }
            case SPELL_AURA_SCHOOL_IMMUNITY:
            {
                schoolImmunityMask |= uint32(miscVal);
                break;
            }
            case SPELL_AURA_MOD_IMMUNE_AURA_APPLY_SCHOOL:
            {
                applyHarmfulAuraImmunityMask |= uint32(miscVal);
                break;
            }
            case SPELL_AURA_DAMAGE_IMMUNITY:
            {
                damageImmunityMask |= uint32(miscVal);
                break;
            }
            case SPELL_AURA_DISPEL_IMMUNITY:
            {
                dispelImmunity = uint32(miscVal);
                break;
            }
            default:
                break;
        }

        immuneInfo.SchoolImmuneMask = schoolImmunityMask;
        immuneInfo.ApplyHarmfulAuraImmuneMask = applyHarmfulAuraImmunityMask;
        immuneInfo.MechanicImmuneMask = mechanicImmunityMask;
        immuneInfo.DispelImmune = dispelImmunity;
        immuneInfo.DamageSchoolMask = damageImmunityMask;
        immuneInfo.RemoveEffectsWithMechanic = removeEffectsWithMechanic;

        immuneInfo.AuraTypeImmune.shrink_to_fit();
        immuneInfo.SpellEffectImmune.shrink_to_fit();

        if (immuneInfo.SchoolImmuneMask
            || immuneInfo.ApplyHarmfulAuraImmuneMask
            || immuneInfo.MechanicImmuneMask
            || immuneInfo.DispelImmune
            || immuneInfo.DamageSchoolMask
            || !immuneInfo.AuraTypeImmune.empty()
            || !immuneInfo.SpellEffectImmune.empty())
        {
            effect._immunityInfo = std::move(workBuffer);
            workBuffer = std::make_unique<SpellEffectInfo::ImmunityInfo>();
        }

        _allowedMechanicMask |= immuneInfo.MechanicImmuneMask;
    }

    if (HasAttribute(SPELL_ATTR5_USABLE_WHILE_STUNNED))
    {
        switch (Id)
        {
            case 22812: // Barkskin
            case 47585: // Dispersion
                _allowedMechanicMask |=
                    (1 << MECHANIC_STUN) |
                    (1 << MECHANIC_FREEZE) |
                    (1 << MECHANIC_KNOCKOUT) |
                    (1 << MECHANIC_SLEEP);
                break;
            case 49039: // Lichborne, don't allow normal stuns
                break;
            default:
                _allowedMechanicMask |= (1 << MECHANIC_STUN);
                break;
        }
    }

    if (HasAttribute(SPELL_ATTR5_USABLE_WHILE_CONFUSED))
        _allowedMechanicMask |= (1 << MECHANIC_DISORIENTED);

    if (HasAttribute(SPELL_ATTR5_USABLE_WHILE_FEARED))
    {
        switch (Id)
        {
            case 22812: // Barkskin
            case 47585: // Dispersion
                _allowedMechanicMask |= (1 << MECHANIC_FEAR) | (1 << MECHANIC_HORROR);
                break;
            default:
                _allowedMechanicMask |= (1 << MECHANIC_FEAR);
                break;
        }
    }
}

void SpellInfo::ApplyAllSpellImmunitiesTo(Unit* target, SpellEffectInfo const& spellEffectInfo, bool apply) const
{
    SpellEffectInfo::ImmunityInfo const* immuneInfo = spellEffectInfo.GetImmunityInfo();
    if (!immuneInfo)
        return;

    if (uint32 schoolImmunity = immuneInfo->SchoolImmuneMask)
    {
        target->ApplySpellImmune(Id, IMMUNITY_SCHOOL, schoolImmunity, apply);

        if (apply && HasAttribute(SPELL_ATTR1_DISPEL_AURAS_ON_IMMUNITY))
        {
            target->RemoveAppliedAuras([this, schoolImmunity](AuraApplication const* aurApp) -> bool
            {
                SpellInfo const* auraSpellInfo = aurApp->GetBase()->GetSpellInfo();
                return ((auraSpellInfo->GetSchoolMask() & schoolImmunity) != 0 && // Check for school mask
                    CanDispelAura(auraSpellInfo) &&
                    (IsPositive() != aurApp->IsPositive()) &&                     // Check spell vs aura possitivity
                    !auraSpellInfo->IsPassive() &&                                // Don't remove passive auras
                    auraSpellInfo->Id != Id);                                     // Don't remove self
            });
        }
    }

    if (uint32 mechanicImmunity = immuneInfo->MechanicImmuneMask)
    {
        for (uint32 i = 0; i < MAX_MECHANIC; ++i)
            if (mechanicImmunity & (1 << i))
                target->ApplySpellImmune(Id, IMMUNITY_MECHANIC, i, apply);

        if (HasAttribute(SPELL_ATTR1_DISPEL_AURAS_ON_IMMUNITY))
        {
            if (apply)
                target->RemoveAurasWithMechanic(mechanicImmunity, AURA_REMOVE_BY_DEFAULT, Id, immuneInfo->RemoveEffectsWithMechanic);
            else if (!immuneInfo->RemoveEffectsWithMechanic)
            {
                std::vector<Aura*> aurasToUpdateTargets;
                target->RemoveAppliedAuras([mechanicImmunity, &aurasToUpdateTargets](AuraApplication const* aurApp)
                {
                    Aura* aura = aurApp->GetBase();
                    if (aura->GetSpellInfo()->GetAllEffectsMechanicMask() & mechanicImmunity)
                        aurasToUpdateTargets.push_back(aura);

                    // only update targets, don't remove anything
                    return false;
                });

                for (Aura* aura : aurasToUpdateTargets)
                    aura->UpdateTargetMap(aura->GetCaster());
            }
        }
    }

    if (uint32 dispelImmunity = immuneInfo->DispelImmune)
    {
        target->ApplySpellImmune(Id, IMMUNITY_DISPEL, dispelImmunity, apply);

        if (apply && HasAttribute(SPELL_ATTR1_DISPEL_AURAS_ON_IMMUNITY))
        {
            target->RemoveAppliedAuras([dispelImmunity](AuraApplication const* aurApp) -> bool
            {
                SpellInfo const* spellInfo = aurApp->GetBase()->GetSpellInfo();
                if (spellInfo->Dispel == dispelImmunity)
                    return true;

                return false;
            });
        }
    }

    if (uint32 damageImmunity = immuneInfo->DamageSchoolMask)
        target->ApplySpellImmune(Id, IMMUNITY_DAMAGE, damageImmunity, apply);

    for (AuraType auraType : immuneInfo->AuraTypeImmune)
    {
        target->ApplySpellImmune(Id, IMMUNITY_STATE, auraType, apply);
        if (apply && HasAttribute(SPELL_ATTR1_DISPEL_AURAS_ON_IMMUNITY))
            target->RemoveAurasByType(auraType, [](AuraApplication const* aurApp) -> bool
            {
                // if the aura has SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY, then it cannot be removed by immunity
                return !aurApp->GetBase()->GetSpellInfo()->HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY);
            });
    }

    for (SpellEffects effectType : immuneInfo->SpellEffectImmune)
        target->ApplySpellImmune(Id, IMMUNITY_EFFECT, effectType, apply);
}

bool SpellInfo::CanSpellProvideImmunityAgainstAura(SpellInfo const* auraSpellInfo) const
{
    if (!auraSpellInfo)
        return false;

    for (SpellEffectInfo const& effectInfo : _effects)
    {
        if (!effectInfo.IsEffect())
            continue;

        SpellEffectInfo::ImmunityInfo const* immuneInfo = effectInfo.GetImmunityInfo();
        if (!immuneInfo)
            continue;

        if (!auraSpellInfo->HasAttribute(SPELL_ATTR1_UNAFFECTED_BY_SCHOOL_IMMUNE) && !auraSpellInfo->HasAttribute(SPELL_ATTR2_UNAFFECTED_BY_AURA_SCHOOL_IMMUNE))
        {
            if (uint32 schoolImmunity = immuneInfo->SchoolImmuneMask)
                if ((auraSpellInfo->SchoolMask & schoolImmunity) != 0)
                    return true;
        }

        if (uint32 mechanicImmunity = immuneInfo->MechanicImmuneMask)
            if ((mechanicImmunity & (1 << auraSpellInfo->Mechanic)) != 0)
                return true;

        if (uint32 dispelImmunity = immuneInfo->DispelImmune)
            if (auraSpellInfo->Dispel == dispelImmunity)
                return true;

        bool immuneToAllEffects = true;
        for (SpellEffectInfo const& auraSpellEffectInfo : auraSpellInfo->GetEffects())
        {
            if (!auraSpellEffectInfo.IsEffect())
                continue;

            auto spellImmuneItr = immuneInfo->SpellEffectImmune.find(auraSpellEffectInfo.Effect);
            if (spellImmuneItr == immuneInfo->SpellEffectImmune.end())
            {
                immuneToAllEffects = false;
                break;
            }

            if (uint32 mechanic = auraSpellEffectInfo.Mechanic)
            {
                if (!(immuneInfo->MechanicImmuneMask & (1 << mechanic)))
                {
                    immuneToAllEffects = false;
                    break;
                }
            }

            if (!auraSpellInfo->HasAttribute(SPELL_ATTR3_IGNORE_HIT_RESULT))
            {
                if (AuraType auraName = auraSpellEffectInfo.ApplyAuraName)
                {
                    bool isImmuneToAuraEffectApply = false;
                    auto auraImmuneItr = immuneInfo->AuraTypeImmune.find(auraName);
                    if (auraImmuneItr != immuneInfo->AuraTypeImmune.end())
                        isImmuneToAuraEffectApply = true;

                    if (!isImmuneToAuraEffectApply && !auraSpellInfo->IsPositiveEffect(auraSpellEffectInfo.EffectIndex) && !auraSpellInfo->HasAttribute(SPELL_ATTR2_UNAFFECTED_BY_AURA_SCHOOL_IMMUNE))
                    {
                        if (uint32 applyHarmfulAuraImmunityMask = immuneInfo->ApplyHarmfulAuraImmuneMask)
                            if ((auraSpellInfo->GetSchoolMask() & applyHarmfulAuraImmunityMask) != 0)
                                isImmuneToAuraEffectApply = true;
                    }

                    if (!isImmuneToAuraEffectApply)
                    {
                        immuneToAllEffects = false;
                        break;
                    }
                }
            }
        }

        if (immuneToAllEffects)
            return true;
    }

    return false;
}

// based on client Spell_C::CancelsAuraEffect
bool SpellInfo::SpellCancelsAuraEffect(AuraEffect const* aurEff) const
{
    if (!HasAttribute(SPELL_ATTR1_DISPEL_AURAS_ON_IMMUNITY))
        return false;

    if (aurEff->GetSpellInfo()->HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))
        return false;

    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (!effect.IsEffect(SPELL_EFFECT_APPLY_AURA))
            continue;

        uint32 const miscValue = static_cast<uint32>(effect.MiscValue);
        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_STATE_IMMUNITY:
                if (miscValue != aurEff->GetAuraType())
                    continue;
                break;
            case SPELL_AURA_SCHOOL_IMMUNITY:
            case SPELL_AURA_MOD_IMMUNE_AURA_APPLY_SCHOOL:
                if (aurEff->GetSpellInfo()->HasAttribute(SPELL_ATTR2_UNAFFECTED_BY_AURA_SCHOOL_IMMUNE) || !(aurEff->GetSpellInfo()->SchoolMask & miscValue))
                    continue;
                break;
            case SPELL_AURA_DISPEL_IMMUNITY:
                if (miscValue != aurEff->GetSpellInfo()->Dispel)
                    continue;
                break;
            case SPELL_AURA_MECHANIC_IMMUNITY:
                if (miscValue != aurEff->GetSpellInfo()->Mechanic)
                {
                    if (miscValue != aurEff->GetSpellEffectInfo().Mechanic)
                        continue;
                }
                break;
            default:
                continue;
        }

        return true;
    }

    return false;
}

uint32 SpellInfo::GetAllowedMechanicMask() const
{
    return _allowedMechanicMask;
}

uint32 SpellInfo::GetMechanicImmunityMask(Unit* caster) const
{
    uint32 casterMechanicImmunityMask = caster->GetMechanicImmunityMask();
    uint32 mechanicImmunityMask = 0;

    // @todo: research other interrupt flags
    if (InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT)
    {
        if (casterMechanicImmunityMask & (1 << MECHANIC_SILENCE))
            mechanicImmunityMask |= (1 << MECHANIC_SILENCE);

        if (casterMechanicImmunityMask & (1 << MECHANIC_INTERRUPT))
            mechanicImmunityMask |= (1 << MECHANIC_INTERRUPT);
    }

    return mechanicImmunityMask;
}

float SpellInfo::GetMinRange(bool positive /*= false*/) const
{
    if (!RangeEntry)
        return 0.0f;
    if (positive)
        return RangeEntry->RangeMin[1];
    return RangeEntry->RangeMin[0];
}

float SpellInfo::GetMaxRange(bool positive /*= false*/, WorldObject* caster /*= nullptr*/, Spell* spell /*= nullptr*/) const
{
    if (!RangeEntry)
        return 0.0f;
    float range;
    if (positive)
        range = RangeEntry->RangeMax[1];
    else
        range = RangeEntry->RangeMax[0];
    if (caster)
        if (Player* modOwner = caster->GetSpellModOwner())
            modOwner->ApplySpellMod(Id, SPELLMOD_RANGE, range, spell);

    return range;
}

int32 SpellInfo::GetDuration() const
{
    if (!DurationEntry)
        return IsPassive() ? -1 : 0;
    return (DurationEntry->Duration == -1) ? -1 : abs(DurationEntry->Duration);
}

int32 SpellInfo::GetMaxDuration() const
{
    if (!DurationEntry)
        return IsPassive() ? -1 : 0;
    return (DurationEntry->MaxDuration == -1) ? -1 : abs(DurationEntry->MaxDuration);
}

uint32 SpellInfo::CalcCastTime(Spell* spell /*= nullptr*/) const
{
    // not all spells have cast time index and this is all is pasiive abilities
    if (!CastTimeEntry)
        return 0;

    int32 castTime = CastTimeEntry->Base;

    if (spell)
        spell->GetCaster()->ModSpellCastTime(this, castTime, spell);

    if (HasAttribute(SPELL_ATTR0_REQ_AMMO) && (!IsAutoRepeatRangedSpell()))
        castTime += 500;

    return (castTime > 0) ? uint32(castTime) : 0;
}

uint32 SpellInfo::GetMaxTicks() const
{
    uint32 totalTicks = 0;
    int32 DotDuration = GetDuration();

    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (effect.IsEffect(SPELL_EFFECT_APPLY_AURA))
        {
            switch (effect.ApplyAuraName)
            {
                case SPELL_AURA_PERIODIC_DAMAGE:
                case SPELL_AURA_PERIODIC_DAMAGE_PERCENT:
                case SPELL_AURA_PERIODIC_HEAL:
                case SPELL_AURA_OBS_MOD_HEALTH:
                case SPELL_AURA_OBS_MOD_POWER:
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL_FROM_CLIENT:
                case SPELL_AURA_POWER_BURN:
                case SPELL_AURA_PERIODIC_LEECH:
                case SPELL_AURA_PERIODIC_MANA_LEECH:
                case SPELL_AURA_PERIODIC_ENERGIZE:
                case SPELL_AURA_PERIODIC_DUMMY:
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL:
                case SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE:
                case SPELL_AURA_PERIODIC_HEALTH_FUNNEL:
                case SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR:
                    // skip infinite periodics
                    if (effect.Amplitude > 0 && DotDuration > 0)
                    {
                        totalTicks = static_cast<uint32>(DotDuration) / effect.Amplitude;
                        if (HasAttribute(SPELL_ATTR5_START_PERIODIC_AT_APPLY))
                            ++totalTicks;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    return totalTicks;
}

uint32 SpellInfo::GetRecoveryTime() const
{
    return RecoveryTime > CategoryRecoveryTime ? RecoveryTime : CategoryRecoveryTime;
}

int32 SpellInfo::CalcPowerCost(WorldObject const* caster, SpellSchoolMask schoolMask, Spell* spell) const
{
    // gameobject casts don't use power
    Unit const* unitCaster = caster->ToUnit();
    if (!unitCaster)
        return 0;

    // Spell drain all exist power on cast (Only paladin lay of Hands)
    if (HasAttribute(SPELL_ATTR1_DRAIN_ALL_POWER))
    {
        // If power type - health drain all
        if (PowerType == POWER_HEALTH)
            return unitCaster->GetHealth();
        // Else drain all power
        if (PowerType < MAX_POWERS)
            return unitCaster->GetPower(PowerType);
        TC_LOG_ERROR("spells", "SpellInfo::CalcPowerCost: Unknown power type '{}' in spell {}", PowerType, Id);
        return 0;
    }

    // Base powerCost
    int32 powerCost = ManaCost;
    // PCT cost from total amount
    if (ManaCostPercentage)
    {
        switch (PowerType)
        {
            // health as power used
            case POWER_HEALTH:
                powerCost += int32(CalculatePct(unitCaster->GetCreateHealth(), ManaCostPercentage));
                break;
            case POWER_MANA:
                powerCost += int32(CalculatePct(unitCaster->GetCreateMana(), ManaCostPercentage));
                break;
            case POWER_RAGE:
            case POWER_FOCUS:
            case POWER_ENERGY:
            case POWER_HAPPINESS:
                powerCost += int32(CalculatePct(unitCaster->GetMaxPower(PowerType), ManaCostPercentage));
                break;
            case POWER_RUNE:
            case POWER_RUNIC_POWER:
                TC_LOG_DEBUG("spells", "CalculateManaCost: Not implemented yet!");
                break;
            default:
                TC_LOG_ERROR("spells", "CalculateManaCost: Unknown power type '{}' in spell {}", PowerType, Id);
                return 0;
        }
    }

    // Flat mod from caster auras by spell school and power type
    Unit::AuraEffectList const& auras = unitCaster->GetAuraEffectsByType(SPELL_AURA_MOD_POWER_COST_SCHOOL);
    for (Unit::AuraEffectList::const_iterator i = auras.begin(); i != auras.end(); ++i)
    {
        if (!((*i)->GetMiscValue() & schoolMask))
            continue;
        if (!((*i)->GetMiscValueB() & (1 << PowerType)))
            continue;
        powerCost += (*i)->GetAmount();
    }
    // Shiv - costs 20 + weaponSpeed*10 energy (apply only to non-triggered spell with energy cost)
    if (HasAttribute(SPELL_ATTR4_SPELL_VS_EXTEND_COST))
    {
        uint32 speed = 0;
        if (SpellShapeshiftFormEntry const* ss = sSpellShapeshiftFormStore.LookupEntry(unitCaster->GetShapeshiftForm()))
            speed = ss->CombatRoundTime;
        else
            speed = unitCaster->GetAttackTime(GetAttackType());

        powerCost += speed / 100;
    }

    // Apply cost mod by spell
    if (Player* modOwner = unitCaster->GetSpellModOwner())
        modOwner->ApplySpellMod(Id, SPELLMOD_COST, powerCost, spell);

    if (!unitCaster->IsControlledByPlayer())
    {
        if (HasAttribute(SPELL_ATTR0_LEVEL_DAMAGE_CALCULATION))
        {
            GtNPCManaCostScalerEntry const* spellScaler = sGtNPCManaCostScalerStore.LookupEntry(SpellLevel - 1);
            GtNPCManaCostScalerEntry const* casterScaler = sGtNPCManaCostScalerStore.LookupEntry(unitCaster->GetLevel() - 1);
            if (spellScaler && casterScaler)
                powerCost *= casterScaler->Data / spellScaler->Data;
        }
    }

    // PCT mod from user auras by spell school and power type
    Unit::AuraEffectList const& aurasPct = unitCaster->GetAuraEffectsByType(SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT);
    for (Unit::AuraEffectList::const_iterator i = aurasPct.begin(); i != aurasPct.end(); ++i)
    {
        if (!((*i)->GetMiscValue() & schoolMask))
            continue;
        if (!((*i)->GetMiscValueB() & (1 << PowerType)))
            continue;
        powerCost += CalculatePct(powerCost, (*i)->GetAmount());
    }
    if (powerCost < 0)
        powerCost = 0;
    return powerCost;
}

bool SpellInfo::IsRanked() const
{
    return ChainEntry != nullptr;
}

uint8 SpellInfo::GetRank() const
{
    if (!ChainEntry)
        return 1;
    return ChainEntry->rank;
}

SpellInfo const* SpellInfo::GetFirstRankSpell() const
{
    if (!ChainEntry)
        return this;
    return ChainEntry->first;
}
SpellInfo const* SpellInfo::GetLastRankSpell() const
{
    if (!ChainEntry)
        return nullptr;
    return ChainEntry->last;
}
SpellInfo const* SpellInfo::GetNextRankSpell() const
{
    if (!ChainEntry)
        return nullptr;
    return ChainEntry->next;
}
SpellInfo const* SpellInfo::GetPrevRankSpell() const
{
    if (!ChainEntry)
        return nullptr;
    return ChainEntry->prev;
}

SpellInfo const* SpellInfo::GetAuraRankForLevel(uint8 level) const
{
    // ignore passive spells
    if (IsPassive())
        return this;

    // Client ignores spell with these attributes (sub_53D9D0)
    if (HasAttribute(SPELL_ATTR0_NEGATIVE_1) || HasAttribute(SPELL_ATTR2_ALLOW_LOW_LEVEL_BUFF) || HasAttribute(SPELL_ATTR3_DRAIN_SOUL))
        return this;

    bool needRankSelection = false;
    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (IsPositiveEffect(effect.EffectIndex) &&
            (effect.Effect == SPELL_EFFECT_APPLY_AURA ||
            effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_PARTY ||
            effect.Effect == SPELL_EFFECT_APPLY_AREA_AURA_RAID))
        {
            needRankSelection = true;
            break;
        }
    }

    // not required
    if (!needRankSelection)
        return this;

    for (SpellInfo const* nextSpellInfo = this; nextSpellInfo != nullptr; nextSpellInfo = nextSpellInfo->GetPrevRankSpell())
    {
        // if found appropriate level
        if (uint32(level + 10) >= nextSpellInfo->SpellLevel)
            return nextSpellInfo;

        // one rank less then
    }

    // not found
    return nullptr;
}

bool SpellInfo::IsRankOf(SpellInfo const* spellInfo) const
{
    return GetFirstRankSpell() == spellInfo->GetFirstRankSpell();
}

bool SpellInfo::IsDifferentRankOf(SpellInfo const* spellInfo) const
{
    if (Id == spellInfo->Id)
        return false;
    return IsRankOf(spellInfo);
}

bool SpellInfo::IsHighRankOf(SpellInfo const* spellInfo) const
{
    if (ChainEntry && spellInfo->ChainEntry)
        if (ChainEntry->first == spellInfo->ChainEntry->first)
            if (ChainEntry->rank > spellInfo->ChainEntry->rank)
                return true;

    return false;
}

void SpellInfo::_InitializeExplicitTargetMask()
{
    bool srcSet = false;
    bool dstSet = false;
    uint32 targetMask = Targets;
    // prepare target mask using effect target entries
    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (!effect.IsEffect())
            continue;

        targetMask |= effect.TargetA.GetExplicitTargetMask(srcSet, dstSet);
        targetMask |= effect.TargetB.GetExplicitTargetMask(srcSet, dstSet);

        // add explicit target flags based on spell effects which have EFFECT_IMPLICIT_TARGET_EXPLICIT and no valid target provided
        if (effect.GetImplicitTargetType() != EFFECT_IMPLICIT_TARGET_EXPLICIT)
            continue;

        // extend explicit target mask only if valid targets for effect could not be provided by target types
        uint32 effectTargetMask = effect.GetMissingTargetMask(srcSet, dstSet, targetMask);

        // don't add explicit object/dest flags when spell has no max range
        if (GetMaxRange(true) == 0.0f && GetMaxRange(false) == 0.0f)
            effectTargetMask &= ~(TARGET_FLAG_UNIT_MASK | TARGET_FLAG_GAMEOBJECT | TARGET_FLAG_CORPSE_MASK | TARGET_FLAG_DEST_LOCATION);

        targetMask |= effectTargetMask;
    }

    ExplicitTargetMask = targetMask;
}

inline bool _isPositiveTarget(SpellEffectInfo const& effect)
{
    if (!effect.IsEffect())
        return true;

    return (effect.TargetA.GetCheckType() != TARGET_CHECK_ENEMY &&
        effect.TargetB.GetCheckType() != TARGET_CHECK_ENEMY);
}

bool _isPositiveEffectImpl(SpellInfo const* spellInfo, SpellEffectInfo const& effect, std::unordered_set<std::pair<uint32, SpellEffIndex>>& visited)
{
    if (!effect.IsEffect())
        return true;

    // attribute may be already set in DB
    if (!spellInfo->IsPositiveEffect(effect.EffectIndex))
        return false;

    // passive auras like talents are all positive
    if (spellInfo->IsPassive())
        return true;

    // not found a single positive spell with this attribute
    if (spellInfo->HasAttribute(SPELL_ATTR0_NEGATIVE_1))
        return false;

    visited.insert({ spellInfo->Id, effect.EffectIndex });

    //We need scaling level info for some auras that compute bp 0 or positive but should be debuffs
    float bpScalePerLevel = effect.RealPointsPerLevel;
    int32 bp = effect.CalcValue();
    switch (spellInfo->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
            switch (spellInfo->Id)
            {
                case 40268: // Spiritual Vengeance, Teron Gorefiend, Black Temple
                case 61987: // Avenging Wrath Marker
                case 61988: // Divine Shield exclude aura
                case 64412: // Phase Punch, Algalon the Observer, Ulduar
                case 72410: // Rune of Blood, Saurfang, Icecrown Citadel
                case 71204: // Touch of Insignificance, Lady Deathwhisper, Icecrown Citadel
                    return false;
                case 24732: // Bat Costume
                case 30877: // Tag Murloc
                case 61716: // Rabbit Costume
                case 61734: // Noblegarden Bunny
                case 62344: // Fists of Stone
                case 50344: // Dream Funnel
                case 61819: // Manabonked! (item)
                case 61834: // Manabonked! (minigob)
                    return true;
                default:
                    break;
            }
            break;
        case SPELLFAMILY_ROGUE:
            // Shadow of Death, Teron Gorefiend, Black Temple
            if (spellInfo->Id == 40251)
                return false;
            break;
        case SPELLFAMILY_MAGE:
            // Amplify Magic, Dampen Magic
            if (spellInfo->SpellFamilyFlags[0] == 0x00002000)
                return true;
            // Permafrost (due to zero basepoint)
            if (spellInfo->SpellFamilyFlags[2] == 0x00000010)
                return false;
            // Arcane Missiles
            if (spellInfo->SpellFamilyFlags[0] == 0x00000800)
                return false;
            break;
        case SPELLFAMILY_WARRIOR:
            // Slam, Execute
            if ((spellInfo->SpellFamilyFlags[0] & 0x20200000) != 0)
                return false;
            break;
        case SPELLFAMILY_PALADIN:
            // assortment of judgement related spells
            if (spellInfo->SpellFamilyFlags & flag96(0x208C0000, 0x00000208, 0x00000008))
                return false;
            break;
        case SPELLFAMILY_HUNTER:
            // Aspect of the Viper
            if (spellInfo->Id == 34074)
                return true;
            // Explosive Shot
            if (spellInfo->SpellFamilyFlags[1] == SPELLFAMILYFLAG1_HUNTER_EXPLOSIVE_SHOT)
                return false;
            break;
        case SPELLFAMILY_DRUID:
            // Starfall
            if (spellInfo->SpellFamilyFlags[2] == 0x00000100)
                return false;
            break;
        case SPELLFAMILY_DEATHKNIGHT:
            if (spellInfo->SpellFamilyFlags[2] == 0x00000010) // Ebon Plague
                return false;
            break;
        default:
            break;
    }

    switch (spellInfo->Mechanic)
    {
        case MECHANIC_IMMUNE_SHIELD:
            return true;
        default:
            break;
    }

    // Special case: effects which determine positivity of whole spell
    // Note: this check was written before the attribute name was discovered so it might require to be adjusted
    if (spellInfo->HasAttribute(SPELL_ATTR1_DONT_REFRESH_DURATION_ON_RECAST))
    {
        // check for targets, there seems to be an assortment of dummy triggering spells that should be negative
        for (SpellEffectInfo const& otherEffect : spellInfo->GetEffects())
            if (!_isPositiveTarget(otherEffect))
                return false;
    }

    for (SpellEffectInfo const& otherEffect : spellInfo->GetEffects())
    {
        switch (otherEffect.Effect)
        {
            case SPELL_EFFECT_HEAL:
            case SPELL_EFFECT_LEARN_SPELL:
            case SPELL_EFFECT_SKILL_STEP:
            case SPELL_EFFECT_HEAL_PCT:
                return true;
            case SPELL_EFFECT_INSTAKILL:
                if (otherEffect.EffectIndex != effect.EffectIndex &&
                    // for spells like 38044: instakill effect is negative but auras on target must count as buff
                    otherEffect.TargetA.GetTarget() == effect.TargetA.GetTarget() &&
                    otherEffect.TargetB.GetTarget() == effect.TargetB.GetTarget())
                    return false;
                break;
            default:
                break;
        }

        if (otherEffect.IsAura())
        {
            switch (otherEffect.ApplyAuraName)
            {
                case SPELL_AURA_MOD_STEALTH:
                case SPELL_AURA_MOD_UNATTACKABLE:
                    return true;
                case SPELL_AURA_SCHOOL_HEAL_ABSORB:
                case SPELL_AURA_EMPATHY:
                case SPELL_AURA_MOD_DAMAGE_FROM_CASTER:
                case SPELL_AURA_PREVENTS_FLEEING:
                    return false;
                default:
                    break;
            }
        }
    }

    switch (effect.Effect)
    {
        case SPELL_EFFECT_WEAPON_DAMAGE:
        case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
        case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
        case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
        case SPELL_EFFECT_SCHOOL_DAMAGE:
        case SPELL_EFFECT_ENVIRONMENTAL_DAMAGE:
        case SPELL_EFFECT_HEALTH_LEECH:
        case SPELL_EFFECT_INSTAKILL:
        case SPELL_EFFECT_POWER_DRAIN:
        case SPELL_EFFECT_STEAL_BENEFICIAL_BUFF:
        case SPELL_EFFECT_INTERRUPT_CAST:
        case SPELL_EFFECT_PICKPOCKET:
        case SPELL_EFFECT_GAMEOBJECT_DAMAGE:
        case SPELL_EFFECT_DURABILITY_DAMAGE:
        case SPELL_EFFECT_DURABILITY_DAMAGE_PCT:
        case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
        case SPELL_EFFECT_TAMECREATURE:
        case SPELL_EFFECT_DISTRACT:
            return false;
        case SPELL_EFFECT_ENERGIZE:
        case SPELL_EFFECT_ENERGIZE_PCT:
        case SPELL_EFFECT_HEAL_PCT:
        case SPELL_EFFECT_HEAL_MAX_HEALTH:
        case SPELL_EFFECT_HEAL_MECHANICAL:
            return true;
        case SPELL_EFFECT_KNOCK_BACK:
        case SPELL_EFFECT_CHARGE:
        case SPELL_EFFECT_PERSISTENT_AREA_AURA:
        case SPELL_EFFECT_ATTACK_ME:
        case SPELL_EFFECT_POWER_BURN:
            // check targets
            if (!_isPositiveTarget(effect))
                return false;
            break;
        case SPELL_EFFECT_DISPEL:
            // non-positive dispel
            switch (effect.MiscValue)
            {
                case DISPEL_STEALTH:
                case DISPEL_INVISIBILITY:
                case DISPEL_ENRAGE:
                    return false;
                default:
                    break;
            }

            // also check targets
            if (!_isPositiveTarget(effect))
                return false;
            break;
        case SPELL_EFFECT_DISPEL_MECHANIC:
            if (!_isPositiveTarget(effect))
            {
                // non-positive mechanic dispel on negative target
                switch (effect.MiscValue)
                {
                    case MECHANIC_BANDAGE:
                    case MECHANIC_SHIELD:
                    case MECHANIC_MOUNT:
                    case MECHANIC_INVULNERABILITY:
                        return false;
                    default:
                        break;
                }
            }
            break;
        case SPELL_EFFECT_THREAT:
        case SPELL_EFFECT_MODIFY_THREAT_PERCENT:
            // check targets AND basepoints
            if (!_isPositiveTarget(effect) && bp > 0)
                return false;
            break;
        default:
            break;
    }

    if (effect.IsAura())
    {
        // non-positive aura use
        switch (effect.ApplyAuraName)
        {
            case SPELL_AURA_MOD_STAT:                   // dependent from basepoint sign (negative -> negative)
            case SPELL_AURA_MOD_SKILL:
            case SPELL_AURA_MOD_DODGE_PERCENT:
            case SPELL_AURA_MOD_HEALING_DONE:
            case SPELL_AURA_MOD_DAMAGE_DONE_CREATURE:
            case SPELL_AURA_OBS_MOD_HEALTH:
            case SPELL_AURA_OBS_MOD_POWER:
            case SPELL_AURA_MOD_CRIT_PCT:
            case SPELL_AURA_MOD_HIT_CHANCE:
            case SPELL_AURA_MOD_SPELL_HIT_CHANCE:
            case SPELL_AURA_MOD_SPELL_CRIT_CHANCE:
            case SPELL_AURA_MOD_RANGED_HASTE:
            case SPELL_AURA_MOD_MELEE_RANGED_HASTE:
            case SPELL_AURA_MOD_CASTING_SPEED_NOT_STACK:
            case SPELL_AURA_HASTE_SPELLS:
            case SPELL_AURA_MOD_RESISTANCE_EXCLUSIVE:
            case SPELL_AURA_MOD_DETECT_RANGE:
            case SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT:
            case SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE:
            case SPELL_AURA_MOD_INCREASE_SWIM_SPEED:
            case SPELL_AURA_MOD_PERCENT_STAT:
            case SPELL_AURA_MOD_INCREASE_HEALTH:
            case SPELL_AURA_MOD_SPEED_ALWAYS:
                if (bp < 0 || bpScalePerLevel < 0) //TODO: What if both are 0? Should it be a buff or debuff?
                    return false;
                break;
            case SPELL_AURA_MOD_ATTACKSPEED:            // some buffs have negative bp, check both target and bp
            case SPELL_AURA_MOD_MELEE_HASTE:
            case SPELL_AURA_HASTE_RANGED:
            case SPELL_AURA_MOD_DAMAGE_DONE:
            case SPELL_AURA_MOD_RESISTANCE:
            case SPELL_AURA_MOD_RESISTANCE_PCT:
            case SPELL_AURA_MOD_RATING:
            case SPELL_AURA_MOD_ATTACK_POWER:
            case SPELL_AURA_MOD_RANGED_ATTACK_POWER:
            case SPELL_AURA_MOD_DAMAGE_PERCENT_DONE:
            case SPELL_AURA_MOD_SPEED_SLOW_ALL:
            case SPELL_AURA_MELEE_SLOW:
            case SPELL_AURA_MOD_ATTACK_POWER_PCT:
            case SPELL_AURA_MOD_HEALING_DONE_PERCENT:
            case SPELL_AURA_MOD_HEALING_PCT:
                if (!_isPositiveTarget(effect) || bp < 0)
                    return false;
                break;
            case SPELL_AURA_MOD_DAMAGE_TAKEN:           // dependent from basepoint sign (positive -> negative)
            case SPELL_AURA_MOD_MELEE_DAMAGE_TAKEN:
            case SPELL_AURA_MOD_MELEE_DAMAGE_TAKEN_PCT:
            case SPELL_AURA_MOD_SCHOOL_CRIT_DMG_TAKEN:
            case SPELL_AURA_MOD_ATTACKER_SPELL_CRIT_CHANCE:
            case SPELL_AURA_MOD_POWER_COST_SCHOOL:
            case SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT:
            case SPELL_AURA_MOD_MECHANIC_DAMAGE_TAKEN_PERCENT:
                if (bp > 0)
                    return false;
                break;
            case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:   // check targets and basepoints (ex Recklessness)
                if (!_isPositiveTarget(effect) && bp > 0)
                    return false;
                break;
            case SPELL_AURA_MOD_HEALTH_REGEN_PERCENT:   // check targets and basepoints (target enemy and negative bp -> negative)
                if (!_isPositiveTarget(effect) && bp < 0)
                    return false;
                break;
            case SPELL_AURA_ADD_TARGET_TRIGGER:
                return true;
            case SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE:
            case SPELL_AURA_PERIODIC_TRIGGER_SPELL_FROM_CLIENT:
                if (SpellInfo const* spellTriggeredProto = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                {
                    // negative targets of main spell return early
                    for (SpellEffectInfo const& spellTriggeredEffect : spellTriggeredProto->GetEffects())
                    {
                        // already seen this
                        if (visited.count({ spellTriggeredProto->Id, spellTriggeredEffect.EffectIndex }) > 0)
                            continue;

                        if (!spellTriggeredEffect.IsEffect())
                            continue;

                        // if non-positive trigger cast targeted to positive target this main cast is non-positive
                        // this will place this spell auras as debuffs
                        if (_isPositiveTarget(spellTriggeredEffect) && !_isPositiveEffectImpl(spellTriggeredProto, spellTriggeredEffect, visited))
                            return false;
                    }
                }
                break;
            case SPELL_AURA_PERIODIC_TRIGGER_SPELL:
            case SPELL_AURA_MOD_STUN:
            case SPELL_AURA_TRANSFORM:
            case SPELL_AURA_MOD_DECREASE_SPEED:
            case SPELL_AURA_MOD_FEAR:
            case SPELL_AURA_MOD_TAUNT:
                // special auras: they may have non negative target but still need to be marked as debuff
                // checked again after all effects (SpellInfo::_InitializeSpellPositivity)
            case SPELL_AURA_MOD_PACIFY:
            case SPELL_AURA_MOD_PACIFY_SILENCE:
            case SPELL_AURA_MOD_DISARM:
            case SPELL_AURA_MOD_DISARM_OFFHAND:
            case SPELL_AURA_MOD_DISARM_RANGED:
            case SPELL_AURA_MOD_CHARM:
            case SPELL_AURA_AOE_CHARM:
            case SPELL_AURA_MOD_POSSESS:
            case SPELL_AURA_MOD_LANGUAGE:
            case SPELL_AURA_DAMAGE_SHIELD:
            case SPELL_AURA_PROC_TRIGGER_SPELL:
            case SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE:
            case SPELL_AURA_MOD_ATTACKER_RANGED_HIT_CHANCE:
            case SPELL_AURA_MOD_ATTACKER_SPELL_HIT_CHANCE:
            case SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_CHANCE:
            case SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_CHANCE:
            case SPELL_AURA_MOD_ATTACKER_MELEE_CRIT_DAMAGE:
            case SPELL_AURA_MOD_ATTACKER_RANGED_CRIT_DAMAGE:
            case SPELL_AURA_MOD_ATTACKER_SPELL_AND_WEAPON_CRIT_CHANCE:
            case SPELL_AURA_DUMMY:
            case SPELL_AURA_PERIODIC_DUMMY:
            case SPELL_AURA_MOD_HEALING:
            case SPELL_AURA_MOD_WEAPON_CRIT_PERCENT:
            case SPELL_AURA_POWER_BURN:
            case SPELL_AURA_MOD_COOLDOWN:
            case SPELL_AURA_MOD_INCREASE_SPEED:
            case SPELL_AURA_MOD_PARRY_PERCENT:
            case SPELL_AURA_SET_VEHICLE_ID:
            case SPELL_AURA_PERIODIC_ENERGIZE:
            case SPELL_AURA_EFFECT_IMMUNITY:
            case SPELL_AURA_OVERRIDE_CLASS_SCRIPTS:
            case SPELL_AURA_MOD_SHAPESHIFT:
            case SPELL_AURA_MOD_THREAT:
            case SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE:
                // check target for positive and negative spells
                if (!_isPositiveTarget(effect))
                    return false;
                break;
            case SPELL_AURA_MOD_CONFUSE:
            case SPELL_AURA_CHANNEL_DEATH_ITEM:
            case SPELL_AURA_MOD_ROOT:
            case SPELL_AURA_MOD_SILENCE:
            case SPELL_AURA_MOD_DETAUNT:
            case SPELL_AURA_GHOST:
            case SPELL_AURA_PERIODIC_LEECH:
            case SPELL_AURA_PERIODIC_MANA_LEECH:
            case SPELL_AURA_MOD_STALKED:
            case SPELL_AURA_PREVENT_RESURRECTION:
            case SPELL_AURA_PERIODIC_DAMAGE:
            case SPELL_AURA_PERIODIC_DAMAGE_PERCENT:
            case SPELL_AURA_MELEE_ATTACK_POWER_ATTACKER_BONUS:
            case SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS:
                return false;
            case SPELL_AURA_MECHANIC_IMMUNITY:
            {
                // non-positive immunities
                switch (effect.MiscValue)
                {
                    case MECHANIC_BANDAGE:
                    case MECHANIC_SHIELD:
                    case MECHANIC_MOUNT:
                    case MECHANIC_INVULNERABILITY:
                        return false;
                    default:
                        break;
                }
                break;
            }
            case SPELL_AURA_ADD_FLAT_MODIFIER:          // mods
            case SPELL_AURA_ADD_PCT_MODIFIER:
            {
                switch (effect.MiscValue)
                {
                    case SPELLMOD_CASTING_TIME:             // dependent from basepoint sign (positive -> negative)
                    case SPELLMOD_ACTIVATION_TIME:
                    case SPELLMOD_SPELL_COST_REFUND_ON_FAIL:
                    case SPELLMOD_GLOBAL_COOLDOWN:
                        if (bp > 0)
                            return false;
                        break;
                    case SPELLMOD_COOLDOWN:
                    case SPELLMOD_COST:
                        if (!spellInfo->IsPositive() && bp > 0) // dependent on prev effects too (ex Arcane Power)
                            return false;
                        break;
                    case SPELLMOD_EFFECT1:                  // always positive
                    case SPELLMOD_EFFECT2:
                    case SPELLMOD_EFFECT3:
                    case SPELLMOD_ALL_EFFECTS:
                    case SPELLMOD_THREAT:
                    case SPELLMOD_DAMAGE_MULTIPLIER:
                    case SPELLMOD_VALUE_MULTIPLIER:
                        return true;
                    case SPELLMOD_DURATION:
                    case SPELLMOD_CRITICAL_CHANCE:
                    case SPELLMOD_DAMAGE:
                    case SPELLMOD_JUMP_TARGETS:
                        if (!spellInfo->IsPositive() && bp < 0) // dependent on prev effects too
                            return false;
                        break;
                    default:                                // dependent from basepoint sign (negative -> negative)
                        if (bp < 0)
                            return false;
                        break;
                }
                break;
            }
            default:
                break;
        }
    }

    // negative spell if triggered spell is negative
    if (!effect.ApplyAuraName && effect.TriggerSpell)
    {
        if (SpellInfo const* spellTriggeredProto = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
        {
            // spells with at least one negative effect are considered negative
            // some self-applied spells have negative effects but in self casting case negative check ignored.
            for (SpellEffectInfo const& spellTriggeredEffect : spellTriggeredProto->GetEffects())
            {
                // already seen this
                if (visited.count({ spellTriggeredProto->Id, spellTriggeredEffect.EffectIndex }) > 0)
                    continue;

                if (!spellTriggeredEffect.IsEffect())
                    continue;

                if (!_isPositiveEffectImpl(spellTriggeredProto, spellTriggeredEffect, visited))
                    return false;
            }
        }
    }

    // ok, positive
    return true;
}

void SpellInfo::_InitializeSpellPositivity()
{
    std::unordered_set<std::pair<uint32 /*spellId*/, SpellEffIndex /*effIndex*/>> visited;

    for (SpellEffectInfo const& effect : GetEffects())
        if (!_isPositiveEffectImpl(this, effect, visited))
            AttributesCu |= (SPELL_ATTR0_CU_NEGATIVE_EFF0 << effect.EffectIndex);

    // additional checks after effects marked
    for (SpellEffectInfo const& effect : GetEffects())
    {
        if (!effect.IsEffect() || !IsPositiveEffect(effect.EffectIndex))
            continue;

        switch (effect.ApplyAuraName)
        {
            // has other non positive effect?
            // then it should be marked negative if has same target as negative effect (ex 8510, 8511, 8893, 10267)
            case SPELL_AURA_DUMMY:
            case SPELL_AURA_MOD_STUN:
            case SPELL_AURA_MOD_FEAR:
            case SPELL_AURA_MOD_TAUNT:
            case SPELL_AURA_TRANSFORM:
            case SPELL_AURA_MOD_ATTACKSPEED:
            case SPELL_AURA_MOD_DECREASE_SPEED:
            {
                for (size_t j = effect.EffectIndex + 1; j < GetEffects().size(); ++j)
                    if (!IsPositiveEffect(j)
                        && effect.TargetA.GetTarget() == GetEffect(SpellEffIndex(j)).TargetA.GetTarget()
                        && effect.TargetB.GetTarget() == GetEffect(SpellEffIndex(j)).TargetB.GetTarget())
                        AttributesCu |= (SPELL_ATTR0_CU_NEGATIVE_EFF0 << effect.EffectIndex);
                break;
            }
            default:
                break;
        }
    }
}

void SpellInfo::_UnloadImplicitTargetConditionLists()
{
    // find the same instances of ConditionList and delete them.
    for (SpellEffectInfo const& effect : GetEffects())
    {
        ConditionContainer* cur = effect.ImplicitTargetConditions;
        if (!cur)
            continue;

        for (size_t j = effect.EffectIndex; j < GetEffects().size(); ++j)
            if (GetEffect(SpellEffIndex(j)).ImplicitTargetConditions == cur)
                _GetEffect(SpellEffIndex(j)).ImplicitTargetConditions = nullptr;

        delete cur;
    }
}
