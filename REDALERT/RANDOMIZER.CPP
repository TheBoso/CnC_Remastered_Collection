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
 *                    File Name : RANDOMIZER.CPP                                               *
 *                                                                                             *
 *                   Programmer : Randomizer Mod                                               *
 *                                                                                             *
 *                   Start Date : 01/18/2026                                                   *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   RandomizerClass::RandomizerClass -- Constructor                                           *
 *   RandomizerClass::Load_Config -- Load configuration from RANDOMIZER.INI                   *
 *   RandomizerClass::Init -- Initialize randomizer for a scenario                            *
 *   RandomizerClass::Swap_Building -- Get randomized building type                           *
 *   RandomizerClass::Swap_Unit -- Get randomized unit type                                   *
 *   RandomizerClass::Swap_Infantry -- Get randomized infantry type                           *
 *   RandomizerClass::Swap_Vessel -- Get randomized vessel type                               *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "function.h"
#include "randomizer.h"
#include <stdio.h>
#include <direct.h>

/*
** Global randomizer instance
*/
RandomizerClass Randomizer;


/***********************************************************************************************
 * RandomizerClass::RandomizerClass -- Default constructor                                     *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
RandomizerClass::RandomizerClass() :
    ConfigLoaded(false),
    Enabled(false),
    FixedSeed(0),
    BuildingsEnabled(false),
    UnitsEnabled(false),
    InfantryEnabled(false),
    VesselsEnabled(false),
    BuildingPoolCount(0),
    UnitPoolCount(0),
    InfantryPoolCount(0),
    VesselPoolCount(0),
    RNG(0)
{
}


/***********************************************************************************************
 * RandomizerClass::Load_Config -- Load configuration from RANDOMIZER.INI                      *
 *                                                                                             *
 *    Reads the randomizer configuration from RANDOMIZER.INI file. The file should be placed   *
 *    in the game data directory or a mod directory.                                           *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  bool; true if config was loaded successfully                                       *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
bool RandomizerClass::Load_Config(void)
{
    CCINIClass ini;
    CCFileClass file("RANDOMIZER.INI");

    GlyphX_Debug_Print("Randomizer: Load_Config called");

    /*
    ** Log the current working directory for debugging
    */
    char cwd[260];
    if (_getcwd(cwd, sizeof(cwd)) != NULL) {
        char cwdMsg[300];
        sprintf(cwdMsg, "Randomizer: Current directory: %s", cwd);
        GlyphX_Debug_Print(cwdMsg);
    }

    /*
    ** Reset state
    */
    ConfigLoaded = false;
    Enabled = false;
    BuildingPoolCount = 0;
    UnitPoolCount = 0;
    InfantryPoolCount = 0;
    VesselPoolCount = 0;

    /*
    ** Check if config file exists
    */
    if (!file.Is_Available()) {
        GlyphX_Debug_Print("Randomizer: RANDOMIZER.INI not found");
        return false;
    }

    GlyphX_Debug_Print("Randomizer: RANDOMIZER.INI found");

    /*
    ** Load the INI file
    */
    if (ini.Load(file, false) == 0) {
        GlyphX_Debug_Print("Randomizer: Failed to load INI");
        return false;
    }

    GlyphX_Debug_Print("Randomizer: INI loaded successfully");
    ConfigLoaded = true;

    /*
    ** Read [Randomizer] section
    */
    static char const * const RANDOMIZER = "Randomizer";

    if (!ini.Is_Present(RANDOMIZER))
	{
        GlyphX_Debug_Print("Randomizer: [Randomizer] section not found");
        return true;
    }

    Enabled = ini.Get_Bool(RANDOMIZER, "Enabled", false);
    FixedSeed = ini.Get_Int(RANDOMIZER, "Seed", 0);

    GlyphX_Debug_Print(Enabled ? "Randomizer: Enabled=yes" : "Randomizer: Enabled=no");

    if (!Enabled) {
        return true;
    }

    /*
    ** Read building pools from multiple possible sections
    */
    static char const * const BUILDING_SECTIONS[] = {
        "DefensiveBuildings",
        "ProductionBuildings",
        "PowerBuildings",
        "Buildings",
        NULL
    };

    for (int i = 0; BUILDING_SECTIONS[i] != NULL; i++) {
        if (ini.Is_Present(BUILDING_SECTIONS[i])) {
            if (ini.Get_Bool(BUILDING_SECTIONS[i], "Enabled", true)) {
                BuildingsEnabled = true;
                Parse_Building_Section(ini, BUILDING_SECTIONS[i]);
            }
        }
    }

    /*
    ** Read unit pools
    */
    static char const * const UNIT_SECTIONS[] = {
        "Tanks",
        "Vehicles",
        "Units",
        NULL
    };

    for (int i = 0; UNIT_SECTIONS[i] != NULL; i++) {
        if (ini.Is_Present(UNIT_SECTIONS[i])) {
            if (ini.Get_Bool(UNIT_SECTIONS[i], "Enabled", true)) {
                UnitsEnabled = true;
                Parse_Unit_Section(ini, UNIT_SECTIONS[i]);
            }
        }
    }

    /*
    ** Read infantry pools
    */
    if (ini.Is_Present("Infantry")) {
        if (ini.Get_Bool("Infantry", "Enabled", true)) {
            InfantryEnabled = true;
            Parse_Infantry_Section(ini, "Infantry");
        }
    }

    /*
    ** Read vessel pools
    */
    if (ini.Is_Present("Vessels")) {
        if (ini.Get_Bool("Vessels", "Enabled", true)) {
            VesselsEnabled = true;
            Parse_Vessel_Section(ini, "Vessels");
        }
    }

    /*
    ** Log summary
    */
    char debugMsg[128];
    sprintf(debugMsg, "Randomizer: Loaded %d building, %d unit, %d infantry, %d vessel pools",
            BuildingPoolCount, UnitPoolCount, InfantryPoolCount, VesselPoolCount);
    GlyphX_Debug_Print(debugMsg);

    return true;
}


/***********************************************************************************************
 * RandomizerClass::Init -- Initialize randomizer for a new scenario                           *
 *                                                                                             *
 *    Seeds the random number generator for the scenario. Should be called after Load_Config   *
 *    and before any object placement begins.                                                  *
 *                                                                                             *
 * INPUT:   scenarioSeed -- The scenario's random seed for deterministic randomization         *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
void RandomizerClass::Init(unsigned int scenarioSeed)
{
    /*
    ** Use fixed seed if specified, otherwise use scenario seed
    */
    unsigned int seed = (FixedSeed != 0) ? FixedSeed : scenarioSeed;
    RNG = RandomClass(seed);
}


/***********************************************************************************************
 * RandomizerClass::Parse_Building_Section -- Parse a building pool section from INI           *
 *                                                                                             *
 * INPUT:   ini      -- Reference to the INI database                                          *
 *          section  -- Section name to parse                                                  *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
void RandomizerClass::Parse_Building_Section(CCINIClass& ini, const char* section)
{
    char buffer[256];

    int count = ini.Entry_Count(section);
    for (int i = 0; i < count && BuildingPoolCount < MAX_POOLS; i++) {
        char const * entry = ini.Get_Entry(section, i);

        /*
        ** Skip the "Enabled" entry
        */
        if (stricmp(entry, "Enabled") == 0) {
            continue;
        }

        /*
        ** Get the source building type
        */
        StructType srcType = BuildingTypeClass::From_Name(entry);
        if (srcType == STRUCT_NONE) {
            continue;
        }

        /*
        ** Get the pool string
        */
        ini.Get_String(section, entry, "", buffer, sizeof(buffer));
        if (buffer[0] == '\0') {
            continue;
        }

        /*
        ** Parse the comma-separated pool
        */
        SwapPoolType<StructType>& pool = BuildingPools[BuildingPoolCount];
        pool.SourceType = srcType;
        pool.PoolCount = 0;

        char* token = strtok(buffer, ",");
        while (token != NULL && pool.PoolCount < MAX_POOL_ENTRIES) {
            /*
            ** Trim leading whitespace
            */
            while (*token == ' ') token++;

            StructType destType = BuildingTypeClass::From_Name(token);
            if (destType != STRUCT_NONE) {
                pool.Pool[pool.PoolCount++] = destType;
            }
            token = strtok(NULL, ",");
        }

        if (pool.PoolCount > 0) {
            BuildingPoolCount++;
        }
    }
}


/***********************************************************************************************
 * RandomizerClass::Parse_Unit_Section -- Parse a unit pool section from INI                   *
 *                                                                                             *
 * INPUT:   ini      -- Reference to the INI database                                          *
 *          section  -- Section name to parse                                                  *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
void RandomizerClass::Parse_Unit_Section(CCINIClass& ini, const char* section)
{
    char buffer[256];

    int count = ini.Entry_Count(section);
    for (int i = 0; i < count && UnitPoolCount < MAX_POOLS; i++) {
        char const * entry = ini.Get_Entry(section, i);

        if (stricmp(entry, "Enabled") == 0) {
            continue;
        }

        UnitType srcType = UnitTypeClass::From_Name(entry);
        if (srcType == UNIT_NONE) {
            continue;
        }

        ini.Get_String(section, entry, "", buffer, sizeof(buffer));
        if (buffer[0] == '\0') {
            continue;
        }

        SwapPoolType<UnitType>& pool = UnitPools[UnitPoolCount];
        pool.SourceType = srcType;
        pool.PoolCount = 0;

        char* token = strtok(buffer, ",");
        while (token != NULL && pool.PoolCount < MAX_POOL_ENTRIES) {
            while (*token == ' ') token++;

            UnitType destType = UnitTypeClass::From_Name(token);
            if (destType != UNIT_NONE) {
                pool.Pool[pool.PoolCount++] = destType;
            }
            token = strtok(NULL, ",");
        }

        if (pool.PoolCount > 0) {
            UnitPoolCount++;
        }
    }
}


/***********************************************************************************************
 * RandomizerClass::Parse_Infantry_Section -- Parse an infantry pool section from INI          *
 *                                                                                             *
 * INPUT:   ini      -- Reference to the INI database                                          *
 *          section  -- Section name to parse                                                  *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
void RandomizerClass::Parse_Infantry_Section(CCINIClass& ini, const char* section)
{
    char buffer[256];

    int count = ini.Entry_Count(section);
    for (int i = 0; i < count && InfantryPoolCount < MAX_POOLS; i++) {
        char const * entry = ini.Get_Entry(section, i);

        if (stricmp(entry, "Enabled") == 0) {
            continue;
        }

        InfantryType srcType = InfantryTypeClass::From_Name(entry);
        if (srcType == INFANTRY_NONE) {
            continue;
        }

        ini.Get_String(section, entry, "", buffer, sizeof(buffer));
        if (buffer[0] == '\0') {
            continue;
        }

        SwapPoolType<InfantryType>& pool = InfantryPools[InfantryPoolCount];
        pool.SourceType = srcType;
        pool.PoolCount = 0;

        char* token = strtok(buffer, ",");
        while (token != NULL && pool.PoolCount < MAX_POOL_ENTRIES) {
            while (*token == ' ') token++;

            InfantryType destType = InfantryTypeClass::From_Name(token);
            if (destType != INFANTRY_NONE) {
                pool.Pool[pool.PoolCount++] = destType;
            }
            token = strtok(NULL, ",");
        }

        if (pool.PoolCount > 0) {
            InfantryPoolCount++;
        }
    }
}


/***********************************************************************************************
 * RandomizerClass::Parse_Vessel_Section -- Parse a vessel pool section from INI               *
 *                                                                                             *
 * INPUT:   ini      -- Reference to the INI database                                          *
 *          section  -- Section name to parse                                                  *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
void RandomizerClass::Parse_Vessel_Section(CCINIClass& ini, const char* section)
{
    char buffer[256];

    int count = ini.Entry_Count(section);
    for (int i = 0; i < count && VesselPoolCount < MAX_POOLS; i++) {
        char const * entry = ini.Get_Entry(section, i);

        if (stricmp(entry, "Enabled") == 0) {
            continue;
        }

        VesselType srcType = VesselTypeClass::From_Name(entry);
        if (srcType == VESSEL_NONE) {
            continue;
        }

        ini.Get_String(section, entry, "", buffer, sizeof(buffer));
        if (buffer[0] == '\0') {
            continue;
        }

        SwapPoolType<VesselType>& pool = VesselPools[VesselPoolCount];
        pool.SourceType = srcType;
        pool.PoolCount = 0;

        char* token = strtok(buffer, ",");
        while (token != NULL && pool.PoolCount < MAX_POOL_ENTRIES) {
            while (*token == ' ') token++;

            VesselType destType = VesselTypeClass::From_Name(token);
            if (destType != VESSEL_NONE) {
                pool.Pool[pool.PoolCount++] = destType;
            }
            token = strtok(NULL, ",");
        }

        if (pool.PoolCount > 0) {
            VesselPoolCount++;
        }
    }
}


/***********************************************************************************************
 * RandomizerClass::Find_Pool -- Find the swap pool for a given source type                    *
 *                                                                                             *
 * INPUT:   pools      -- Array of pools to search                                             *
 *          poolCount  -- Number of pools in the array                                         *
 *          sourceType -- The type to find a pool for                                          *
 *                                                                                             *
 * OUTPUT:  Pointer to the pool if found, NULL otherwise                                       *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
template <typename T>
SwapPoolType<T>* RandomizerClass::Find_Pool(SwapPoolType<T>* pools, int poolCount, T sourceType)
{
    for (int i = 0; i < poolCount; i++) {
        if (pools[i].SourceType == sourceType) {
            return &pools[i];
        }
    }
    return NULL;
}


/***********************************************************************************************
 * RandomizerClass::Swap_Building -- Get a randomized building type                            *
 *                                                                                             *
 *    Returns a random building type from the configured pool for the given original type.     *
 *    If no pool is defined or randomizer is disabled, returns the original type.             *
 *                                                                                             *
 * INPUT:   original -- The original building type from the scenario                           *
 *                                                                                             *
 * OUTPUT:  The randomized building type to use                                                *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
StructType RandomizerClass::Swap_Building(StructType original)
{
    if (!Is_Enabled() || !BuildingsEnabled || original == STRUCT_NONE) {
        return original;
    }

    /*
    ** Never swap power plants - they are critical for base power balance
    */
    if (original == STRUCT_POWER || original == STRUCT_ADVANCED_POWER) {
        return original;
    }

    SwapPoolType<StructType>* pool = Find_Pool(BuildingPools, BuildingPoolCount, original);
    if (pool == NULL || pool->PoolCount == 0) {
        return original;
    }

    /*
    ** Pick a random entry from the pool
    */
    int index = RNG(0, pool->PoolCount - 1);
    StructType result = pool->Pool[index];

    if (result != original) {
        char debugMsg[128];
        sprintf(debugMsg, "Randomizer: Swapped building %d -> %d", (int)original, (int)result);
        GlyphX_Debug_Print(debugMsg);
    }

    return result;
}


/***********************************************************************************************
 * RandomizerClass::Swap_Unit -- Get a randomized unit type                                    *
 *                                                                                             *
 *    Returns a random unit type from the configured pool for the given original type.         *
 *    If no pool is defined or randomizer is disabled, returns the original type.             *
 *                                                                                             *
 * INPUT:   original -- The original unit type from the scenario                               *
 *                                                                                             *
 * OUTPUT:  The randomized unit type to use                                                    *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
UnitType RandomizerClass::Swap_Unit(UnitType original)
{
    if (!Is_Enabled() || !UnitsEnabled || original == UNIT_NONE) {
        return original;
    }

    SwapPoolType<UnitType>* pool = Find_Pool(UnitPools, UnitPoolCount, original);
    if (pool == NULL || pool->PoolCount == 0) {
        return original;
    }

    int index = RNG(0, pool->PoolCount - 1);
    return pool->Pool[index];
}


/***********************************************************************************************
 * RandomizerClass::Swap_Infantry -- Get a randomized infantry type                            *
 *                                                                                             *
 *    Returns a random infantry type from the configured pool for the given original type.     *
 *    If no pool is defined or randomizer is disabled, returns the original type.             *
 *                                                                                             *
 * INPUT:   original -- The original infantry type from the scenario                           *
 *                                                                                             *
 * OUTPUT:  The randomized infantry type to use                                                *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
InfantryType RandomizerClass::Swap_Infantry(InfantryType original)
{
    if (!Is_Enabled() || !InfantryEnabled || original == INFANTRY_NONE) {
        return original;
    }

    SwapPoolType<InfantryType>* pool = Find_Pool(InfantryPools, InfantryPoolCount, original);
    if (pool == NULL || pool->PoolCount == 0) {
        return original;
    }

    int index = RNG(0, pool->PoolCount - 1);
    return pool->Pool[index];
}


/***********************************************************************************************
 * RandomizerClass::Swap_Vessel -- Get a randomized vessel type                                *
 *                                                                                             *
 *    Returns a random vessel type from the configured pool for the given original type.       *
 *    If no pool is defined or randomizer is disabled, returns the original type.             *
 *                                                                                             *
 * INPUT:   original -- The original vessel type from the scenario                             *
 *                                                                                             *
 * OUTPUT:  The randomized vessel type to use                                                  *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/18/2026 : Created.                                                                     *
 *=============================================================================================*/
VesselType RandomizerClass::Swap_Vessel(VesselType original)
{
    if (!Is_Enabled() || !VesselsEnabled || original == VESSEL_NONE) {
        return original;
    }

    SwapPoolType<VesselType>* pool = Find_Pool(VesselPools, VesselPoolCount, original);
    if (pool == NULL || pool->PoolCount == 0) {
        return original;
    }

    int index = RNG(0, pool->PoolCount - 1);
    return pool->Pool[index];
}
