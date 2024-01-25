#pragma once
#include "GameModifier.h"

class AIControlledHumanGameModifier : public GameModifier
{
public:
    explicit AIControlledHumanGameModifier(char* modName)
        : GameModifier(modName)
    {
    }

    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
};
