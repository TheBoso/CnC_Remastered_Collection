#include "InstantBuildGameModifier.h"
#include "function.h"
void InstantBuildGameModifier::OnBegin()
{
    for(int i = 0; i < Houses.Count(); i++)
    {
        Houses.Ptr(i)->BuildSpeedBias = 0;
    }
}

void InstantBuildGameModifier::OnEnd()
{
    for(int i = 0; i < Houses.Count(); i++)
    {
        Houses.Ptr(i)->BuildSpeedBias = 1;
    }
}

bool InstantBuildGameModifier::IsCompleted()
{
    return true;
}

void InstantBuildGameModifier::OnUpdate()
{
    
}



