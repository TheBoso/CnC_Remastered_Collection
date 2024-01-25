#include "AllIronCurtainGameModifier.h"
#include "function.h"

void AllIronCurtainGameModifier::OnBegin()
{
    RandomizeWeapon();
}

void AllIronCurtainGameModifier::OnEnd()
{
}

bool AllIronCurtainGameModifier::IsCompleted()
{
    return true;
}

void AllIronCurtainGameModifier::RandomizeWeapon()
{
    int i = 0;
    int ironCurtainTime = (TICKS_PER_MINUTE * fixed(CrateData[CrateType::CRATE_INVULN], 256));
    for (i = 0; i < Infantry.Count(); i++)
    {
        Infantry.Ptr(i)->IronCurtainCountDown = ironCurtainTime;
    }
    for (i = 0; i < Units.Count(); i++)
    {
        Units.Ptr(i)->IronCurtainCountDown = ironCurtainTime;
    }
    for (i = 0; i < Buildings.Count(); i++)
    {
        Buildings.Ptr(i)->IronCurtainCountDown = ironCurtainTime;
    }
    for (i = 0; i < Vessels.Count(); i++)
    {
        Vessels.Ptr(i)->IronCurtainCountDown = ironCurtainTime;
    }
}


void AllIronCurtainGameModifier::OnUpdate()
{
}
