#include "GameModifierManager.h"
#include "function.h"
#include "RandomUnitLossGameModifier.h"

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
    }
}

void GameModifierManager::Update()
{
        //  Run Modifiers
        if (_currentModifier == nullptr && Score.ElapsedTime > NEXT_TIMER)
        {
            if(_modifiers.Count() == 0)
            {
                return;
            }
            //  Special fallback if we are null
            const int element = Random_Pick(0, _modifiers.Count());
            _currentModifier = _modifiers[element];
            _currentModifier->OnBegin();
            Session.Messages.Add_Message(NULL, 0, _currentModifier->GetModifierName(),
                                         PlayerColorType::PCOLOR_GREY,
                                         TPF_6PT_GRAD | TPF_USE_GRAD_PAL | TPF_FULLSHADOW,
                                         TICKS_PER_MINUTE * Rule.MessageDelay);
            printf("Modifier Changed");
        }

        if (_currentModifier != nullptr)
        {
            _currentModifier->OnUpdate();
            //  Check if our current modifier is completed and atleast NEXT_TIMER seconds has passed 
            if (_currentModifier->IsCompleted() && Score.ElapsedTime > _lastModifierChange + NEXT_TIMER)
            {
                _lastModifierChange = Score.ElapsedTime;
                _currentModifier->OnEnd();

                //  Assign next modifier
                const int element = Random_Pick(0, _modifiers.Count());
                _currentModifier = _modifiers[element];
                _currentModifier->OnBegin();
                Session.Messages.Add_Message(NULL, 0, _currentModifier->GetModifierName(),
                                             PlayerColorType::PCOLOR_GREY,
                                             TPF_6PT_GRAD | TPF_USE_GRAD_PAL | TPF_FULLSHADOW,
                                             TICKS_PER_MINUTE * Rule.MessageDelay);
                printf("Modifier Changed");
            }
        }
}
