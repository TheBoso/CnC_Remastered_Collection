#pragma once
#include "GameModifier.h"

class BaseSwapGameModifier : public GameModifier
{
public:
    explicit BaseSwapGameModifier(char* modName)
        : GameModifier(modName)
    {
    }

    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
};
