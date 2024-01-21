#pragma once
class GameModifier
{
private:
    char* _modifierName;

public:
    
    GameModifier(char* modName)
    {
        _modifierName = modName;
    }

    virtual void OnUpdate() = 0;
    virtual void OnBegin() = 0;
    virtual void OnEnd() = 0;
    virtual bool IsCompleted() = 0;
    
    char* GetModifierName();
};
