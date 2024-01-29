#include "function.h"
#include "DuplicateObjectsGameModifier.h"


void DuplicateObjectsGameModifier::OnUpdate()
{
}

void DuplicateObjectsGameModifier::OnBegin()
{
    _cachedBeginTime = Scen.Timer.Value();
    //  iterate through existing shiz
    int index = 0;
    int count = Infantry.Count();
    for (index = 0; index < count; index++)
    {
        const InfantryClass* inf = Infantry.Ptr(index);
        if (inf != NULL && inf->IsInLimbo == false && inf->IsActive == true && inf->IsDown)
        {
            for (int j = 0; j < _multiplier; j++)
            {
                // create a clone

                InfantryClass* clone = new InfantryClass(inf->Class->Type, inf->House->Class->House);
                if (clone == NULL)continue;
                if (clone->Unlimbo(Cell_Coord(inf->Nearby_Location(inf)), inf->Direction(inf)) == false)
                {
                    delete clone;
                    continue;
                }
                clone->Mission = inf->Mission;
                clone->Strength = inf->Strength;
                if(inf->Team)
                {
                    inf->Team->Add(clone);
                }
            }
        }
    }

    count = Units.Count();
    for (index = 0; index < count; index++)
    {
        const UnitClass* inf = Units.Ptr(index);
        if (inf != NULL && inf->IsInLimbo == false && inf->IsActive == true && inf->IsDown)
        {
            for (int j = 0; j < _multiplier; j++)
            {
                // create a clone

                UnitClass* clone = new UnitClass(inf->Class->Type, inf->House->Class->House);
                if (clone == NULL)continue;

                if (clone->Unlimbo(Cell_Coord(inf->Nearby_Location(inf)), inf->Direction(inf)) == false)
                {
                    delete clone;
                    continue;
                }
                clone->Mission = inf->Mission;
                clone->Strength = inf->Strength;
                if(inf->Team)
                {
                    inf->Team->Add(clone);
                }
            }
        }
    }

    count = Vessels.Count();
    for (index = 0; index < count; index++)
    {
        const VesselClass* inf = Vessels.Ptr(index);
        if (inf != NULL && inf->IsInLimbo == false && inf->IsActive == true && inf->IsDown)
        {
            for (int j = 0; j < _multiplier; j++)
            {
                // create a clone

                VesselClass* clone = new VesselClass(inf->Class->Type, inf->House->Class->House);
                if (clone == NULL)continue;

                if (clone->Unlimbo(Cell_Coord(inf->Nearby_Location(inf)), inf->Direction(inf)) == false)
                {
                    delete clone;
                    continue;
                }
                clone->Mission = inf->Mission;
                clone->Strength = inf->Strength;
                if(inf->Team)
                {
                    inf->Team->Add(clone);
                }
            }
        }
    }
}

void DuplicateObjectsGameModifier::OnEnd()
{
}

bool DuplicateObjectsGameModifier::IsCompleted()
{
    return true;
}

bool DuplicateObjectsGameModifier::AllowedToRun()
{
    return Scen.Timer.Value() > _cachedBeginTime + this->COOLDOWN;
}
