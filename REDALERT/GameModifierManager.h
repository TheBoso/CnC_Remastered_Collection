#pragma once
#include "function.h"
#include "vector.h"
class GameModifier;

class GameModifierManager
{
public:
    const float NEXT_TIMER = 800.0f;

protected:
    DynamicVectorClass<GameModifier*> _modifiers;
    GameModifier* _currentModifier;
    long _lastModifierChange;
public:
    GameModifierManager(void);
    ~GameModifierManager();
    void Setup();
    void Update();
};
