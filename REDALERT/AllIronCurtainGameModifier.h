#pragma once
#include "GameModifier.h"

class AllIronCurtainGameModifier : public GameModifier
{
public:
    AllIronCurtainGameModifier(char* name) : GameModifier(name)
    {
        
    }
    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
    void RandomizeWeapon();
};
