#pragma once
#include "function.h"
#include "GameModifier.h"
//  Transforms all infantry, tanks and vessels into their enum value -+ 1
class UnitShiftGameModifier : public GameModifier
{
private:
    bool _isDecrement = true;
public:
    explicit UnitShiftGameModifier(char* modName, bool isDecrement)
        : GameModifier(modName)
    {
        _isDecrement = isDecrement;
    }

    static InfantryType GetInfantryTransformation(InfantryType curr);
    static UnitType GetUnitTransformation(UnitType curr);
    void TransformUnits();
    void OnUpdate() override;
    void OnBegin() override;
    void OnEnd() override;
    bool IsCompleted() override;
};
