#pragma once
class GameModifier;
#include "vector.h"

class GameModifierManager
{
public:
    const float NEXT_TIMER = 15.0f;

protected:
    DynamicVectorClass<GameModifier*> _modifiers;
    GameModifier* _currentModifier;
    float _lastModifierChange;
public:
    GameModifierManager(void);
    ~GameModifierManager();
    void Setup();
    void Update();
};
