#pragma once
#include "GameModifier.h"

class InstantBuildGameModifier : public GameModifier
{
public:
    explicit InstantBuildGameModifier(char* modName)
        : GameModifier(modName)
    {
    }

    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
};
