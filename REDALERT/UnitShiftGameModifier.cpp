#include "UnitShiftGameModifier.h"

#include "function.h"


InfantryType UnitShiftGameModifier::GetInfantryTransformation(InfantryType curr)
{
    const int min = INFANTRY_E1;
    const int max = INFANTRY_COUNT - 1;
    InfantryType ret = static_cast<InfantryType>(Random_Pick(min, max));
    return ret;
}

UnitType UnitShiftGameModifier::GetUnitTransformation(UnitType curr)
{
    const int min = UNIT_HTANK;
    const int max = UNIT_COUNT - 1;
    UnitType ret = static_cast<UnitType>(Random_Pick(min, max));
    return ret;
}


void UnitShiftGameModifier::TransformUnits()
{
    int i = 0;

    //  infantry
    for (i = 0; i < Infantry.Count(); i++)
    {
        InfantryClass* inf = Infantry.Ptr(i);
        InfantryType infType = inf->Class->Type;
        InfantryType newType = GetInfantryTransformation(infType);
        InfantryClass* newInf = new InfantryClass(newType, inf->House->Class->House);
        newInf->Unlimbo(inf->Coord, inf->Direction(inf));
        delete inf;
    }

    //  units
    for (i = 0; i < Units.Count(); i++)
    {
        UnitClass* unit = Units.Ptr(i);
        UnitType unitType = unit->Class->Type;
        UnitType newType = GetUnitTransformation(unitType);
        UnitClass* newUnit = new UnitClass(newType, unit->House->Class->House);
        newUnit->Unlimbo(unit->Coord, unit->Direction(unit));
        delete unit;
    }
    //  vessels
}

void UnitShiftGameModifier::OnUpdate()
{
}

void UnitShiftGameModifier::OnBegin()
{
    TransformUnits();
}

void UnitShiftGameModifier::OnEnd()
{
}

bool UnitShiftGameModifier::IsCompleted()
{
    return true;
}
