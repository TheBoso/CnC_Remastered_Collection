#pragma once
#include "GameModifier.h"

class AntInvasionGameModifier : public GameModifier
{
public:
    explicit AntInvasionGameModifier(char* modName)
        : GameModifier(modName)
    {
    }

    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
};
