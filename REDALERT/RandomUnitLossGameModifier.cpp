#include "RandomUnitLossGameModifier.h"
#include "function.h"

void RandomUnitLossGameModifier::OnUpdate()
{
}

void RandomUnitLossGameModifier::OnBegin()
{
    //  Grab a random unit from the game
    int count = Units.Count();
    int randomElement = Random_Pick(0, count);
    UnitClass* randomUnit = Units.Ptr(randomElement);

    //  Check if the random unit is valid
    if (randomUnit != nullptr)
    {
        //  It is, so make it owned by the original owners enemy
        HouseClass* owner = randomUnit->House;
        HousesType enemy = owner->Enemy;
        randomUnit->Captured(HouseClass::As_Pointer(enemy));
        
    }
}

void RandomUnitLossGameModifier::OnEnd()
{
}

bool RandomUnitLossGameModifier::IsCompleted()
{
    return true;
}
