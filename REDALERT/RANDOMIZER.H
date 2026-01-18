//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : RANDOMIZER.H                                                 *
 *                                                                                             *
 *                   Programmer : Randomizer Mod                                               *
 *                                                                                             *
 *                   Start Date : 01/18/2026                                                   *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   RandomizerClass -- Handles randomization of preplaced scenario objects                   *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef RANDOMIZER_H
#define RANDOMIZER_H

#include "random.h"
#include "defines.h"

/*
** Maximum number of entries in a swap pool
*/
#define MAX_POOL_ENTRIES 16

/*
** Maximum number of pools per category
*/
#define MAX_POOLS 64

/*
** Structure to hold a swap pool for a single type
*/
template <typename T>
struct SwapPoolType {
    T SourceType;                       // The original type to swap from
    T Pool[MAX_POOL_ENTRIES];           // Array of possible replacement types
    int PoolCount;                      // Number of valid entries in Pool

    SwapPoolType() : SourceType((T)-1), PoolCount(0) {}
};

/*
** The RandomizerClass handles randomization of preplaced objects during scenario loading.
** It reads configuration from RANDOMIZER.INI and provides swap functions for each object type.
*/
class RandomizerClass {
public:
    RandomizerClass();

    /*
    ** Load configuration from RANDOMIZER.INI
    ** Returns true if config was loaded (even if disabled), false on error
    */
    bool Load_Config(void);

    /*
    ** Initialize the randomizer for a new scenario
    ** Should be called after Load_Config and before any object placement
    */
    void Init(unsigned int scenarioSeed);

    /*
    ** Swap functions - return a random replacement type from the configured pool
    ** Returns the original type if no pool is defined or randomizer is disabled
    */
    StructType Swap_Building(StructType original);
    UnitType Swap_Unit(UnitType original);
    InfantryType Swap_Infantry(InfantryType original);
    VesselType Swap_Vessel(VesselType original);

    /*
    ** Check if randomizer is enabled
    */
    bool Is_Enabled(void) const { return Enabled && ConfigLoaded; }

private:
    /*
    ** Configuration state
    */
    bool ConfigLoaded;                  // Has config been loaded?
    bool Enabled;                       // Master enable switch
    unsigned int FixedSeed;             // 0 = use scenario seed, >0 = fixed seed

    /*
    ** Per-category enable flags
    */
    bool BuildingsEnabled;
    bool UnitsEnabled;
    bool InfantryEnabled;
    bool VesselsEnabled;

    /*
    ** Swap pools for each object type
    */
    SwapPoolType<StructType> BuildingPools[MAX_POOLS];
    int BuildingPoolCount;

    SwapPoolType<UnitType> UnitPools[MAX_POOLS];
    int UnitPoolCount;

    SwapPoolType<InfantryType> InfantryPools[MAX_POOLS];
    int InfantryPoolCount;

    SwapPoolType<VesselType> VesselPools[MAX_POOLS];
    int VesselPoolCount;

    /*
    ** Dedicated RNG instance (doesn't affect gameplay RNG)
    */
    RandomClass RNG;

    /*
    ** Helper functions for parsing INI sections
    */
    void Parse_Building_Section(class CCINIClass& ini, const char* section);
    void Parse_Unit_Section(class CCINIClass& ini, const char* section);
    void Parse_Infantry_Section(class CCINIClass& ini, const char* section);
    void Parse_Vessel_Section(class CCINIClass& ini, const char* section);

    /*
    ** Find pool for a given source type
    */
    template <typename T>
    SwapPoolType<T>* Find_Pool(SwapPoolType<T>* pools, int poolCount, T sourceType);
};

#endif
