#include "AntInvasionGameModifier.h"
#include "function.h"

void AntInvasionGameModifier::OnUpdate()
{
}

void AntInvasionGameModifier::OnBegin()
{
    HouseClass* owner = HouseClass::As_Pointer(HousesType::HOUSE_JP);

    int amountToCreate = 4;
    unsigned long elapsed = Scen.Timer.Value();
    // based on game time
    if (elapsed < 600)
    {
        amountToCreate = 12;
    }
    else if (elapsed < 1600)
    {
        amountToCreate = 26;
    }
    else if (elapsed < 2600)
    {
        amountToCreate = 32;
    }
    else
    {
        amountToCreate = 46;
    }
    //  Create Ants
    for (int i = 0; i < amountToCreate; i++)
    {
        CELL cell = Map.Pick_Random_Location();
        COORDINATE coord = Cell_Coord(cell);
        UnitClass* warriors = new UnitClass(UNIT_PHASE, HousesType::HOUSE_SPAIN);
        if (warriors->Unlimbo(coord) == false)
        {
            delete warriors;
        }
        else
        {
            warriors->Mission_Guard_Area();
        }
    }
    Sound_Effect(VOC_CHRONOTANK1);
}

void AntInvasionGameModifier::OnEnd()
{
}

bool AntInvasionGameModifier::IsCompleted()
{
    return true;
}
