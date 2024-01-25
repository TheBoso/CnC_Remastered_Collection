#include "ExtraMCVGameModifier.h"
#include "function.h"

void ExtraMCVGameModifier::OnUpdate()
{
    
}

void ExtraMCVGameModifier::OnEnd()
{
}

bool ExtraMCVGameModifier::IsCompleted()
{
    return true;
}

void ExtraMCVGameModifier::CreateMCV(HouseClass* house)
{
    BuildingClass* base = house->Find_Building(STRUCT_CONST);
    if (base != NULL)
    {
        UnitClass* mcv = new UnitClass(UNIT_MCV, house->Class->House);
        if (mcv->Unlimbo(Cell_Coord(base->Nearby_Location())) == false)
        {
            delete mcv;
        }
    }
}

void ExtraMCVGameModifier::OnBegin()
{
    if (Session.Type == GAME_SKIRMISH)
    {
        
        for (HousesType house = HOUSE_MULTI1; house < (HOUSE_MULTI1 + Session.Players.Count()); house++)
        {
            HouseClass* ptr = HouseClass::As_Pointer(house);
            if(ptr->IsDefeated == false)
            {
                CreateMCV(ptr);
                if(ptr == PlayerPtr)
                {
                    Speak(VOX_REINFORCEMENTS);
                    Sound_Effect(VOC_CHRONO);
                }
            }
        }

    }
    else
    {
        CreateMCV(PlayerPtr);
        Speak(VOX_REINFORCEMENTS);
        Sound_Effect(VOC_CHRONO);
    }
    

}
