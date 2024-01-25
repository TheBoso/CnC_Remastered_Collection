#include "RandomUnitLossGameModifier.h"
#include "function.h"

void RandomUnitLossGameModifier::OnUpdate()
{
}

void RandomUnitLossGameModifier::OnBegin()
{
    //  Grab a random unit from the game

    DynamicVectorClass<TechnoClass*> potentials;
    UnitClass* unit = nullptr;
    InfantryClass* infantry = nullptr;
    int i = 0;
    for (i = 0; i < Units.Count(); i++)
    {
        unit = Units.Ptr(i);
        if (unit != NULL && unit->IsInLimbo == false && unit->IsActive)
        {
            potentials.Add(Units.Ptr(i));
        }
        
    }
    for (i = 0; i < Infantry.Count(); i++)
    {
        infantry = Infantry.Ptr(i);
        if(infantry != NULL && infantry->IsInLimbo == false && infantry->IsActive)
        {
            potentials.Add(Infantry.Ptr(i));
        }
    }
    int count = potentials.Count() - 1;
    TechnoClass* randomUnit = nullptr;
    const int maxIterations = 16;
    int currentIterations = 0;
    do
    {
        int randomElement = Random_Pick(0, count);
        randomUnit = potentials[randomElement];
        currentIterations++;
    }
    while ((randomUnit == nullptr || randomUnit->Owner() == HOUSE_NONE) && currentIterations < maxIterations);

    //  Check if the random unit is valid
    if (randomUnit != nullptr && randomUnit->Owner() != HOUSE_NONE)
    {
        //  It is, so make it owned by the original owners enemy
        HouseClass* owner = randomUnit->House;
        HousesType enemy = owner->Enemy;
        if (enemy == HOUSE_NONE)
        {
            enemy = HOUSE_BAD;
        }
        HouseClass* enemyHouse = HouseClass::As_Pointer(enemy);
        if(owner->CurInfantry + owner->CurUnits < 6)
        {
            //  Dont do this if we struggling.
            return;
        }
        
        randomUnit->Captured(enemyHouse);
    }
}

void RandomUnitLossGameModifier::OnEnd()
{
}

bool RandomUnitLossGameModifier::IsCompleted()
{
    return true;
}
