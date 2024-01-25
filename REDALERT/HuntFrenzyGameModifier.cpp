#include "HuntFrenzyGameModifier.h"
#include "function.h"

bool HuntFrenzyGameModifier::IsCompleted()
{
    return true;
}

void HuntFrenzyGameModifier::OnBegin()
{
    for (int i = 0; i < Houses.Count(); i++)
    {
        HouseClass* house = Houses.Ptr(i);
        fixed firepower = house->FirepowerBias;
       house->FirepowerBias = firepower * fixed(CrateData[CrateType::CRATE_FIREPOWER], 256);

    }

    Speak(VOX_UPGRADE_FIREPOWER);
}

void HuntFrenzyGameModifier::OnEnd()
{
    for (int i = 0; i < Houses.Count(); i++)
    {
        Houses.Ptr(i)->FirepowerBias = 1;
    }
}

void HuntFrenzyGameModifier::OnUpdate()
{
}
