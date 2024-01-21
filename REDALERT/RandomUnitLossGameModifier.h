#pragma once
#include "GameModifier.h"

class RandomUnitLossGameModifier : public GameModifier
{
public:
    RandomUnitLossGameModifier(char* name) : GameModifier(name)
    {
        
    }
    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
};
