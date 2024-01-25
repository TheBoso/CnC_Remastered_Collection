#pragma once
#include "GameModifier.h"

class HuntFrenzyGameModifier : public GameModifier
{
public:
    explicit HuntFrenzyGameModifier(char* modName)
        : GameModifier(modName)
    {
    }

    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
};
