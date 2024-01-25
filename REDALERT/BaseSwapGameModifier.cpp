#include "BaseSwapGameModifier.h"
#include "function.h"
#include "house.h"

void BaseSwapGameModifier::OnUpdate()
{
}

void BaseSwapGameModifier::OnBegin()
{
    /*//  Grab 2 active houses
    HouseClass* houses[2];
    DynamicVectorClass<HouseClass*> potentials;
    if(Session.Type == GAME_SKIRMISH)
    {
        
    }
    for(int i = 0; i < Houses.Count(); i++)
    {
        HouseClass* currHouse = Houses.Ptr(i);
        if(currHouse->IsActive)
        {
            potentials.Add(currHouse);
        }
    }
    
    //  Assign them
    houses[0] = potentials[Random_Pick(0, potentials.Count() - 1)];
    potentials.Delete(houses[0]);
    houses[1] = potentials[Random_Pick(0, potentials.Count() - 1)];

    DynamicVectorClass<TechnoClass*> house1Objs =  houses[0]->GetAllOwnedTechnoObjects();
    DynamicVectorClass<TechnoClass*> house2Objs =  houses[1]->GetAllOwnedTechnoObjects();

    houses[0]->ControlObjects(house2Objs);
    houses[1]->ControlObjects(house1Objs);*/

}

void BaseSwapGameModifier::OnEnd()
{
}

bool BaseSwapGameModifier::IsCompleted()
{
    return true;
}
