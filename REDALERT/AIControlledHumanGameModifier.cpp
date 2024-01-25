#include "AIControlledHumanGameModifier.h"
#include "function.h"

void AIControlledHumanGameModifier::OnUpdate()
{
}

void AIControlledHumanGameModifier::OnBegin()
{
    HouseClass* human = PlayerPtr;

    human->IQ = 5;
    human->IsPlayerControl = false;
    human->IsBaseBuilding = true;
    human->IsHuman = false;
    human->WasHuman = true;
}

void AIControlledHumanGameModifier::OnEnd()
{
    HouseClass* human = PlayerPtr;
    human->IQ = 3;
    human->IsPlayerControl = true;
    human->IsBaseBuilding = false;
    human->IsHuman = true;
    human->WasHuman = false;

}

bool AIControlledHumanGameModifier::IsCompleted()
{
    return true;
}
