#pragma once
#include "GameModifier.h"

class DuplicateObjectsGameModifier : public GameModifier
{
public:
    const float COOLDOWN = 54000.0f;
    
    DuplicateObjectsGameModifier(char* modName, int multiplier = 1)
        : GameModifier(modName)
    {
        _multiplier = multiplier;
    }

    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
    bool AllowedToRun() override;

private:
    float _cachedBeginTime;
    int _multiplier = 1;
public:
    
};
