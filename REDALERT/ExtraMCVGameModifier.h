#pragma once
#include "GameModifier.h"
class HouseClass;
class ExtraMCVGameModifier : public GameModifier
{
public:
    explicit ExtraMCVGameModifier(char* modName)
        : GameModifier(modName)
    {
    }

    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
    void CreateMCV(HouseClass* house);
};
