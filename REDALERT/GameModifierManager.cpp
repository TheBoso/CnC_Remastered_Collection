#include "GameModifierManager.h"

#include "AIControlledHumanGameModifier.h"
#include "AntInvasionGameModifier.h"
#include "AllIronCurtainGameModifier.h"
#include "BaseSwapGameModifier.h"
#include "ExtraMCVGameModifier.h"
#include "function.h"
#include "HuntFrenzyGameModifier.h"
#include "InstantBuildGameModifier.h"
#include "RandomUnitLossGameModifier.h"
#include "UnitShiftGameModifier.h"

GameModifierManager::GameModifierManager()
{
    _currentModifier = nullptr;
    _lastModifierChange = 0.0f;
}

GameModifierManager::~GameModifierManager()
{
    _modifiers.Clear();
    _lastModifierChange = 0.0f;
}

void GameModifierManager::Setup()
{
    //  Create Game Modifiers
    //  Skip if we have already init
    if(_modifiers.Count() == 0)
    {
        _modifiers.Add(new RandomUnitLossGameModifier("Random Unit Possessed"));
        _modifiers.Add(new AllIronCurtainGameModifier("Curtain All"));
        _modifiers.Add(new InstantBuildGameModifier("Instant Build"));
        _modifiers.Add(new HuntFrenzyGameModifier("Hunt Frenzy"));
        //_modifiers.Add(new AntInvasionGameModifier("Ant Invasion"));
        _modifiers.Add(new BaseSwapGameModifier("Base Swap"));
        _modifiers.Add(new AIControlledHumanGameModifier("AI Base"));
        _modifiers.Add(new ExtraMCVGameModifier("MCV Reinforcements"));
        _modifiers.Add(new UnitShiftGameModifier("Object Transformations", false));
    }
}

void GameModifierManager::Update()
{
        //  Run Modifiers
        if (_currentModifier == NULL && Scen.Timer.Value() > NEXT_TIMER)
        {
            if(_modifiers.Count() == 0)
            {
                return;
            }
            //  Special fallback if we are null
            const int element = Random_Pick(0, _modifiers.Count() - 1);
            _currentModifier = _modifiers[element];
            if(_currentModifier == NULL)
            {
                printf("Fallback");
                _currentModifier = _modifiers[0];
            }
            _currentModifier->OnBegin();
            Session.Messages.Add_Message(NULL, 0, _currentModifier->GetModifierName(),
                                         PlayerColorType::PCOLOR_GREY,
                                         TPF_6PT_GRAD | TPF_USE_GRAD_PAL | TPF_FULLSHADOW,
                                         120);

            _lastModifierChange = Scen.Timer.Value();

        }

        if (_currentModifier != NULL)
        {
            _currentModifier->OnUpdate();
            //  Check if our current modifier is completed and atleast NEXT_TIMER seconds has passed 
            if (_currentModifier->IsCompleted() && Scen.Timer.Value() > _lastModifierChange + NEXT_TIMER)
            {
                _lastModifierChange = Scen.Timer.Value();
                _currentModifier->OnEnd();

                //  Assign next modifier
                const int element = Random_Pick(0, _modifiers.Count() - 1);
                _currentModifier = _modifiers[element];
                _currentModifier->OnBegin();
                Session.Messages.Add_Message(NULL, 0, _currentModifier->GetModifierName(),
                                             PlayerColorType::PCOLOR_GREY,
                                             TPF_6PT_GRAD | TPF_USE_GRAD_PAL | TPF_FULLSHADOW,
                                             120);

            }
        }
}
