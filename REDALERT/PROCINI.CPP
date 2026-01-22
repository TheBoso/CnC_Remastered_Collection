// PROCINI.CPP
//
// Procedural Mission INI Parser for Red Alert
// Implementation of INI loading for mission rules and configuration
//

#include "function.h"
#include "procini.h"
#include "ccini.h"

extern void GlyphX_Debug_Print(const char *debug_text);


/*******************************************************************************
 * GLOBAL INSTANCE
 ******************************************************************************/

ProcMissionINIClass ProcMissionINI;


/*******************************************************************************
 * NAME LOOKUP TABLES
 ******************************************************************************/

static const char* s_HouseNames[] = {
    "Spain", "Greece", "USSR", "England", "Ukraine", "Germany", "France", "Turkey",
    "GoodGuy", "BadGuy", "Neutral", "Special",
    NULL
};

static const char* s_StructNames[] = {
    "ATEK", "IRON", "WEAP", "PDOX", "PBOX", "HBOX", "DOME", "GAP",
    "GUN", "AGUN", "FTUR", "FACT", "PROC", "SILO", "HPAD", "SAM",
    "AFLD", "POWR", "APWR", "STEK", "HOSP", "BARR", "TENT", "KENN",
    "FIX", "BIO", "MISS", "SYRD", "SPEN", "MSLO", "FCOM", "TSLA",
    "WEAF", "FACF", "SYRF", "SPENF", "DOMEF",
    "SBAG", "CYCL", "BRIK", "BARB", "WOOD", "FENC",
    "MINV", "MINP",
    NULL
};

static const StructType s_StructTypes[] = {
    STRUCT_ADVANCED_TECH, STRUCT_IRON_CURTAIN, STRUCT_WEAP, STRUCT_CHRONOSPHERE,
    STRUCT_PILLBOX, STRUCT_CAMOPILLBOX, STRUCT_RADAR, STRUCT_GAP,
    STRUCT_TURRET, STRUCT_AAGUN, STRUCT_FLAME_TURRET, STRUCT_CONST,
    STRUCT_REFINERY, STRUCT_STORAGE, STRUCT_HELIPAD, STRUCT_SAM,
    STRUCT_AIRSTRIP, STRUCT_POWER, STRUCT_ADVANCED_POWER, STRUCT_SOVIET_TECH,
    STRUCT_HOSPITAL, STRUCT_BARRACKS, STRUCT_TENT, STRUCT_KENNEL,
    STRUCT_REPAIR, STRUCT_BIO_LAB, STRUCT_MISSION, STRUCT_SHIP_YARD,
    STRUCT_SUB_PEN, STRUCT_MSLO, STRUCT_FORWARD_COM, STRUCT_TESLA,
    STRUCT_FAKEWEAP, STRUCT_FAKECONST, STRUCT_FAKE_YARD, STRUCT_FAKE_PEN, STRUCT_FAKE_RADAR,
    STRUCT_SANDBAG_WALL, STRUCT_CYCLONE_WALL, STRUCT_BRICK_WALL,
    STRUCT_BARBWIRE_WALL, STRUCT_WOOD_WALL, STRUCT_FENCE,
    STRUCT_AVMINE, STRUCT_APMINE,
    STRUCT_NONE
};

static const char* s_UnitNames[] = {
    // Standard units
    "HTNK", "3TNK", "2TNK", "1TNK", "APC", "MNLY", "JEEP", "HARV",
    "ARTY", "MRJ", "MGG", "MCV", "V2RL", "TRUK",
    // Aftermath units
    "CTNK", "TTNK", "QTNK", "DTRK", "STNK",
    NULL
};

static const UnitType s_UnitTypes[] = {
    // Standard units
    UNIT_HTANK, UNIT_MTANK, UNIT_MTANK2, UNIT_LTANK, UNIT_APC,
    UNIT_MINELAYER, UNIT_JEEP, UNIT_HARVESTER,
    UNIT_ARTY, UNIT_MRJ, UNIT_MGG, UNIT_MCV, UNIT_V2_LAUNCHER, UNIT_TRUCK,
    // Aftermath units
    UNIT_CHRONOTANK, UNIT_TESLATANK, UNIT_MAD, UNIT_DEMOTRUCK, UNIT_PHASE,
    UNIT_NONE
};

static const char* s_InfantryNames[] = {
    "E1", "E2", "E3", "E4", "E6", "E7", "SPY", "THF", "MEDI", "GNRL", "DOG",
    "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10",
    "EINSTEIN", "DELPHI", "CHAN",
    NULL
};

static const InfantryType s_InfantryTypes[] = {
    INFANTRY_E1, INFANTRY_E2, INFANTRY_E3, INFANTRY_E4,
    INFANTRY_RENOVATOR, INFANTRY_TANYA, INFANTRY_SPY, INFANTRY_THIEF,
    INFANTRY_MEDIC, INFANTRY_GENERAL, INFANTRY_DOG,
    INFANTRY_C1, INFANTRY_C2, INFANTRY_C3, INFANTRY_C4, INFANTRY_C5,
    INFANTRY_C6, INFANTRY_C7, INFANTRY_C8, INFANTRY_C9, INFANTRY_C10,
    INFANTRY_EINSTEIN, INFANTRY_DELPHI, INFANTRY_CHAN,
    INFANTRY_NONE
};

static const char* s_MissionNames[] = {
    "SLEEP", "ATTACK", "MOVE", "QMOVE", "RETREAT", "GUARD", "STICKY",
    "ENTER", "CAPTURE", "HARVEST", "GUARD_AREA", "RETURN", "STOP",
    "AMBUSH", "HUNT", "UNLOAD", "SABOTAGE", "CONSTRUCTION",
    "DECONSTRUCTION", "REPAIR", "RESCUE", "MISSILE", "HARMLESS",
    NULL
};

static const char* s_QuarryNames[] = {
    "NONE", "ANYTHING", "BUILDINGS", "HARVESTERS", "INFANTRY",
    "VEHICLES", "VESSELS", "FACTORIES", "DEFENSE", "THREAT", "POWER", "FAKES",
    NULL
};

// Team mission names for INI parsing
static const char* s_TeamMissionNames[] = {
    "ATTACK", "ATT_WAYPT", "FORMATION", "MOVE", "MOVECELL", "GUARD",
    "LOOP", "PATROL", "SET_GLOBAL", "INVULNERABLE", "LOAD", "SPY",
    "UNLOAD", "DO", "DEPLOY", "ATTACKTARCOM", "HOUND_DOG",
    NULL
};

static const TeamMissionType s_TeamMissionTypes[] = {
    TMISSION_ATTACK, TMISSION_ATT_WAYPT, TMISSION_FORMATION, TMISSION_MOVE,
    TMISSION_MOVECELL, TMISSION_GUARD, TMISSION_LOOP, TMISSION_PATROL,
    TMISSION_SET_GLOBAL, TMISSION_INVULNERABLE, TMISSION_LOAD, TMISSION_SPY,
    TMISSION_UNLOAD, TMISSION_DO, TMISSION_DEPLOY, TMISSION_ATTACKTARCOM,
    TMISSION_HOUND_DOG,
    TMISSION_NONE
};

// Vessel type names
static const char* s_VesselNames[] = {
    "SS", "DD", "CA", "LST", "PT", "MSUB", "CARR",
    NULL
};

static const VesselType s_VesselTypes[] = {
    VESSEL_SS, VESSEL_DD, VESSEL_CA, VESSEL_TRANSPORT, VESSEL_PT,
    VESSEL_MISSILESUB, VESSEL_CARRIER,
    VESSEL_NONE
};


/*******************************************************************************
 * CONSTRUCTOR/DESTRUCTOR
 ******************************************************************************/

ProcMissionINIClass::ProcMissionINIClass()
{
}

ProcMissionINIClass::~ProcMissionINIClass()
{
}


/*******************************************************************************
 * TYPE CONVERSION HELPERS
 ******************************************************************************/

HousesType ProcMissionINIClass::House_From_Name(const char* name)
{
    if (!name || !*name) return HOUSE_NONE;

    for (int i = 0; s_HouseNames[i]; i++) {
        if (stricmp(name, s_HouseNames[i]) == 0) {
            return (HousesType)i;
        }
    }

    if (stricmp(name, "None") == 0) return HOUSE_NONE;
    if (stricmp(name, "Player") == 0) return HOUSE_GREECE;
    if (stricmp(name, "Allies") == 0) return HOUSE_GREECE;
    if (stricmp(name, "Soviet") == 0) return HOUSE_USSR;

    return HOUSE_NONE;
}

StructType ProcMissionINIClass::Struct_From_Name(const char* name)
{
    if (!name || !*name) return STRUCT_NONE;

    for (int i = 0; s_StructNames[i]; i++) {
        if (stricmp(name, s_StructNames[i]) == 0) {
            return s_StructTypes[i];
        }
    }

    return STRUCT_NONE;
}

UnitType ProcMissionINIClass::Unit_From_Name(const char* name)
{
    if (!name || !*name) return UNIT_NONE;

    for (int i = 0; s_UnitNames[i]; i++) {
        if (stricmp(name, s_UnitNames[i]) == 0) {
            return s_UnitTypes[i];
        }
    }

    return UNIT_NONE;
}

InfantryType ProcMissionINIClass::Infantry_From_Name(const char* name)
{
    if (!name || !*name) return INFANTRY_NONE;

    for (int i = 0; s_InfantryNames[i]; i++) {
        if (stricmp(name, s_InfantryNames[i]) == 0) {
            return s_InfantryTypes[i];
        }
    }

    return INFANTRY_NONE;
}

MissionType ProcMissionINIClass::Mission_From_Name(const char* name)
{
    if (!name || !*name) return MISSION_NONE;

    for (int i = 0; s_MissionNames[i]; i++) {
        if (stricmp(name, s_MissionNames[i]) == 0) {
            return (MissionType)i;
        }
    }

    return MISSION_NONE;
}

QuarryType ProcMissionINIClass::Quarry_From_Name(const char* name)
{
    if (!name || !*name) return QUARRY_NONE;

    for (int i = 0; s_QuarryNames[i]; i++) {
        if (stricmp(name, s_QuarryNames[i]) == 0) {
            return (QuarryType)i;
        }
    }

    return QUARRY_NONE;
}

VesselType ProcMissionINIClass::Vessel_From_Name(const char* name)
{
    if (!name || !*name) return VESSEL_NONE;

    for (int i = 0; s_VesselNames[i]; i++) {
        if (stricmp(name, s_VesselNames[i]) == 0) {
            return s_VesselTypes[i];
        }
    }

    return VESSEL_NONE;
}

TeamMissionType ProcMissionINIClass::TeamMission_From_Name(const char* name)
{
    if (!name || !*name) return TMISSION_NONE;

    for (int i = 0; s_TeamMissionNames[i]; i++) {
        if (stricmp(name, s_TeamMissionNames[i]) == 0) {
            return s_TeamMissionTypes[i];
        }
    }

    return TMISSION_NONE;
}


/*******************************************************************************
 * LOAD FUNCTIONS
 ******************************************************************************/

bool ProcMissionINIClass::Load(const char* filename, ProcMissionDef& mission)
{
    GlyphX_Debug_Print("ProcINI: Loading mission file...");
    GlyphX_Debug_Print(filename);

    CCFileClass file(filename);
    if (!file.Is_Available()) {
        GlyphX_Debug_Print("ProcINI: File not available!");
        return false;
    }

    GlyphX_Debug_Print("ProcINI: File found, parsing...");
    CCINIClass ini;
    ini.Load(file, false);

    return Load(ini, mission);
}

bool ProcMissionINIClass::Load(CCINIClass& ini, ProcMissionDef& mission)
{
    mission = ProcMissionDef();

    // Parse main mission section
    GlyphX_Debug_Print("ProcINI: Parsing [Mission] section...");
    if (!Parse_Mission_Section(ini, mission)) {
        GlyphX_Debug_Print("ProcINI: [Mission] section not found or invalid!");
        return false;
    }
    GlyphX_Debug_Print("ProcINI: [Mission] parsed OK");

    // Parse player section
    GlyphX_Debug_Print("ProcINI: Parsing [Player] section...");
    Parse_Player_Section(ini, mission.PlayerConfig);

    char debugBuf[128];
    sprintf(debugBuf, "ProcINI: Player region=%d, buildings=%d, units=%d, infantry=%d",
        (int)mission.PlayerConfig.Region,
        mission.PlayerConfig.BuildingCount,
        mission.PlayerConfig.UnitCount,
        mission.PlayerConfig.InfantryCount);
    GlyphX_Debug_Print(debugBuf);

    // Parse player handicaps
    Parse_Handicaps_Section(ini, mission.PlayerConfig.Handicaps);

    // Parse enemy bases
    char sectionName[32];
    for (int i = 1; i <= PROC_MAX_ENEMY_BASES; i++) {
        sprintf(sectionName, "%s%d", PROC_INI_ENEMY_BASE, i);
        if (ini.Is_Present(sectionName)) {
            if (mission.EnemyBaseCount < PROC_MAX_ENEMY_BASES) {
                Parse_Enemy_Base(ini, sectionName, mission.EnemyBases[mission.EnemyBaseCount]);
                mission.EnemyBaseCount++;
            }
        }
    }

    // Parse director rules
    Parse_Director_Rules(ini, mission.DirectorRules);

    // Parse spawn entries
    for (int i = 1; i <= PROC_MAX_SPAWN_ENTRIES; i++) {
        sprintf(sectionName, "%s%d", PROC_INI_SPAWN_ENTRY, i);
        if (ini.Is_Present(sectionName)) {
            if (mission.DirectorRules.SpawnEntryCount < PROC_MAX_SPAWN_ENTRIES) {
                Parse_Spawn_Entry(ini, sectionName,
                    mission.DirectorRules.SpawnEntries[mission.DirectorRules.SpawnEntryCount]);
                mission.DirectorRules.SpawnEntryCount++;
            }
        }
    }

    // Parse difficulty settings
    Parse_Difficulty(ini, PROC_INI_DIFF_EASY, mission.EasyDifficulty);
    Parse_Difficulty(ini, PROC_INI_DIFF_NORMAL, mission.NormalDifficulty);
    Parse_Difficulty(ini, PROC_INI_DIFF_HARD, mission.HardDifficulty);

    return true;
}


/*******************************************************************************
 * SECTION PARSERS
 ******************************************************************************/

bool ProcMissionINIClass::Parse_Mission_Section(CCINIClass& ini, ProcMissionDef& mission)
{
    if (!ini.Is_Present(PROC_INI_MISSION)) {
        return false;
    }

    char buffer[256];

    ini.Get_String(PROC_INI_MISSION, "ID", "", buffer, sizeof(buffer));
    strncpy(mission.MissionID, buffer, PROC_MISSION_ID_LEN - 1);

    ini.Get_String(PROC_INI_MISSION, "Theme", "BASE_ASSAULT", buffer, sizeof(buffer));
    mission.Theme = Proc_Theme_From_Name(buffer);

    ini.Get_String(PROC_INI_MISSION, "PlayerFaction", "Greece", buffer, sizeof(buffer));
    mission.PlayerFaction = House_From_Name(buffer);

    ini.Get_String(PROC_INI_MISSION, "EnemyFaction", "USSR", buffer, sizeof(buffer));
    mission.EnemyFaction = House_From_Name(buffer);

    ini.Get_String(PROC_INI_MISSION, "TemplateMap", "", buffer, sizeof(buffer));
    strncpy(mission.TemplateMap, buffer, sizeof(mission.TemplateMap) - 1);

    // Theme-specific settings
    mission.SurvivalTime = ini.Get_Int(PROC_INI_MISSION, "SurvivalTime", 0);

    return true;
}

bool ProcMissionINIClass::Parse_Player_Section(CCINIClass& ini, ProcPlayerConfig& config)
{
    if (!ini.Is_Present(PROC_INI_PLAYER)) {
        return false;
    }

    char buffer[1024];

    ini.Get_String(PROC_INI_PLAYER, "Region", "Base_A", buffer, sizeof(buffer));
    config.Region = Proc_Region_From_Name(buffer);

    config.TechLevel = ini.Get_Int(PROC_INI_PLAYER, "TechLevel", 5);
    config.Credits = ini.Get_Int(PROC_INI_PLAYER, "Credits", 10000);
    config.MaxUnits = ini.Get_Int(PROC_INI_PLAYER, "MaxUnits", 100);
    config.MaxBuildings = ini.Get_Int(PROC_INI_PLAYER, "MaxBuildings", 50);
    config.MaxInfantry = ini.Get_Int(PROC_INI_PLAYER, "MaxInfantry", 100);

    ini.Get_String(PROC_INI_PLAYER, "Buildings", "", buffer, sizeof(buffer));
    Parse_Building_List(buffer, config.Buildings, PROC_MAX_BUILDINGS, config.BuildingCount);

    ini.Get_String(PROC_INI_PLAYER, "Units", "", buffer, sizeof(buffer));
    Parse_Unit_List(buffer, config.Units, PROC_MAX_UNITS, config.UnitCount);

    ini.Get_String(PROC_INI_PLAYER, "Infantry", "", buffer, sizeof(buffer));
    Parse_Infantry_List(buffer, config.Infantry, PROC_MAX_INFANTRY, config.InfantryCount);

    return true;
}

bool ProcMissionINIClass::Parse_Handicaps_Section(CCINIClass& ini, ProcHandicaps& handicaps)
{
    if (!ini.Is_Present(PROC_INI_HANDICAPS)) {
        return false;
    }

    int val;

    val = ini.Get_Int(PROC_INI_HANDICAPS, "Firepower", 100);
    handicaps.FirepowerBias = fixed(val, 100);

    val = ini.Get_Int(PROC_INI_HANDICAPS, "Armor", 100);
    handicaps.ArmorBias = fixed(val, 100);

    val = ini.Get_Int(PROC_INI_HANDICAPS, "Cost", 100);
    handicaps.CostBias = fixed(val, 100);

    val = ini.Get_Int(PROC_INI_HANDICAPS, "BuildSpeed", 100);
    handicaps.BuildSpeedBias = fixed(val, 100);

    return true;
}

bool ProcMissionINIClass::Parse_Enemy_Base(CCINIClass& ini, const char* section, ProcEnemyBase& base)
{
    char buffer[1024];

    ini.Get_String(section, "ID", "", buffer, sizeof(buffer));
    strncpy(base.ID, buffer, PROC_MISSION_ID_LEN - 1);

    ini.Get_String(section, "House", "USSR", buffer, sizeof(buffer));
    base.House = House_From_Name(buffer);

    ini.Get_String(section, "Region", "Base_B", buffer, sizeof(buffer));
    base.Region = Proc_Region_From_Name(buffer);

    ini.Get_String(section, "State", "ACTIVE", buffer, sizeof(buffer));
    if (stricmp(buffer, "ACTIVE") == 0) {
        base.State = PROC_BASE_STATE_ACTIVE;
    } else if (stricmp(buffer, "DORMANT") == 0) {
        base.State = PROC_BASE_STATE_DORMANT;
    } else if (stricmp(buffer, "DELAYED") == 0) {
        base.State = PROC_BASE_STATE_DELAYED;
    }

    base.TechLevel = ini.Get_Int(section, "TechLevel", 5);
    base.Credits = ini.Get_Int(section, "Credits", 10000);

    // AI Flags - defaults to enabled for active AI
    base.AIFlags.IsBaseBuilding = ini.Get_Bool(section, "BaseBuilding", true);
    base.AIFlags.IsAlerted = ini.Get_Bool(section, "Alerted", true);
    base.AIFlags.IsAutocreate = ini.Get_Bool(section, "Autocreate", true);
    base.AIFlags.ProductionEnabled = ini.Get_Bool(section, "Production", true);

    ini.Get_String(section, "Buildings", "", buffer, sizeof(buffer));
    Parse_Building_List(buffer, base.Buildings, PROC_MAX_BUILDINGS, base.BuildingCount);

    ini.Get_String(section, "Defenses", "", buffer, sizeof(buffer));
    Parse_Building_List(buffer, base.Defenses, PROC_MAX_BUILDINGS, base.DefenseCount);

    ini.Get_String(section, "GuardUnits", "", buffer, sizeof(buffer));
    Parse_Unit_List(buffer, base.GuardUnits, PROC_MAX_UNITS, base.GuardUnitCount);

    ini.Get_String(section, "GuardInfantry", "", buffer, sizeof(buffer));
    Parse_Infantry_List(buffer, base.GuardInfantry, PROC_MAX_INFANTRY, base.GuardInfantryCount);

    base.ActivationDelay = ini.Get_Int(section, "ActivationDelay", 0);

    return true;
}

bool ProcMissionINIClass::Parse_Director_Rules(CCINIClass& ini, ProcDirectorRules& rules)
{
    if (!ini.Is_Present(PROC_INI_DIRECTOR)) {
        return false;
    }

    rules.AntiRushPeriod = ini.Get_Int(PROC_INI_DIRECTOR, "AntiRushPeriod", 300);
    rules.InitialWaveDelay = ini.Get_Int(PROC_INI_DIRECTOR, "InitialWaveDelay", 50);
    rules.MinSpawnInterval = ini.Get_Int(PROC_INI_DIRECTOR, "MinSpawnInterval", 120);
    rules.MaxSpawnInterval = ini.Get_Int(PROC_INI_DIRECTOR, "MaxSpawnInterval", 300);

    rules.EscalationThreshold1 = ini.Get_Int(PROC_INI_DIRECTOR, "EscalationThreshold1", 25);
    rules.EscalationThreshold2 = ini.Get_Int(PROC_INI_DIRECTOR, "EscalationThreshold2", 50);
    rules.EscalationThreshold3 = ini.Get_Int(PROC_INI_DIRECTOR, "EscalationThreshold3", 75);

    rules.TurtleCheckTime = ini.Get_Int(PROC_INI_DIRECTOR, "TurtleCheckTime", 150);
    rules.TurtleCreditThreshold = ini.Get_Int(PROC_INI_DIRECTOR, "TurtleCreditThreshold", 20000);
    rules.TurtleResponseDelay = ini.Get_Int(PROC_INI_DIRECTOR, "TurtleResponseDelay", 30);

    rules.MaxConcurrentTeams = ini.Get_Int(PROC_INI_DIRECTOR, "MaxConcurrentTeams", 2);
    rules.MaxTotalSpawns = ini.Get_Int(PROC_INI_DIRECTOR, "MaxTotalSpawns", 0);

    return true;
}

bool ProcMissionINIClass::Parse_Spawn_Entry(CCINIClass& ini, const char* section, ProcSpawnEntry& entry)
{
    char buffer[64];

    ini.Get_String(section, "Region", "Entry_North", buffer, sizeof(buffer));
    entry.SpawnRegion = Proc_Region_From_Name(buffer);

    ini.Get_String(section, "Pool", "SOVIET_ATTACK", buffer, sizeof(buffer));
    entry.PoolType = Proc_Pool_From_Name(buffer);

    ini.Get_String(section, "MinThreat", "LIGHT", buffer, sizeof(buffer));
    entry.MinThreat = Proc_Threat_From_Name(buffer);

    ini.Get_String(section, "MaxThreat", "HEAVY", buffer, sizeof(buffer));
    entry.MaxThreat = Proc_Threat_From_Name(buffer);

    ini.Get_String(section, "Quarry", "ANYTHING", buffer, sizeof(buffer));
    entry.Quarry = Quarry_From_Name(buffer);

    return true;
}

bool ProcMissionINIClass::Parse_Difficulty(CCINIClass& ini, const char* section, ProcDifficultyModifiers& mods)
{
    if (!ini.Is_Present(section)) {
        return false;
    }

    int val;

    val = ini.Get_Int(section, "PlayerCreditMult", 100);
    mods.PlayerCreditMult = fixed(val, 100);

    val = ini.Get_Int(section, "PlayerCostBias", 100);
    mods.PlayerCostBias = fixed(val, 100);

    val = ini.Get_Int(section, "PlayerArmorBias", 100);
    mods.PlayerArmorBias = fixed(val, 100);

    val = ini.Get_Int(section, "PlayerFirepowerBias", 100);
    mods.PlayerFirepowerBias = fixed(val, 100);

    val = ini.Get_Int(section, "EnemyCreditMult", 100);
    mods.EnemyCreditMult = fixed(val, 100);

    mods.EnemyTechLevelCap = ini.Get_Int(section, "EnemyTechCap", 10);

    val = ini.Get_Int(section, "EnemyArmorBias", 100);
    mods.EnemyArmorBias = fixed(val, 100);

    val = ini.Get_Int(section, "EnemyFirepowerBias", 100);
    mods.EnemyFirepowerBias = fixed(val, 100);

    mods.EnemyIQLevel = ini.Get_Int(section, "EnemyIQ", 2);

    val = ini.Get_Int(section, "TeamQuantityScale", 100);
    mods.TeamQuantityScale = fixed(val, 100);

    val = ini.Get_Int(section, "SpawnRateScale", 100);
    mods.SpawnRateScale = fixed(val, 100);

    mods.MaxConcurrentTeams = ini.Get_Int(section, "MaxConcurrentTeams", 2);
    mods.AntiRushPeriod = ini.Get_Int(section, "AntiRushPeriod", 300);

    return true;
}


/*******************************************************************************
 * LIST PARSERS
 ******************************************************************************/

bool ProcMissionINIClass::Parse_Building_List(const char* str, ProcBuildingDef* buildings, int maxCount, int& count)
{
    count = 0;
    if (!str || !*str) return false;

    char buffer[1024];
    strncpy(buffer, str, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, ",");
    while (token && count < maxCount) {
        char* parts[3] = {NULL, NULL, NULL};
        int partCount = 0;

        char* p = token;
        while (*p && partCount < 3) {
            parts[partCount++] = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
        }

        if (partCount >= 1) {
            while (*parts[0] == ' ') parts[0]++;

            buildings[count].Type = Struct_From_Name(parts[0]);
            buildings[count].OffsetX = (partCount >= 2) ? atoi(parts[1]) : 0;
            buildings[count].OffsetY = (partCount >= 3) ? atoi(parts[2]) : 0;
            buildings[count].Count = 1;

            if (buildings[count].Type != STRUCT_NONE) {
                count++;
            }
        }

        token = strtok(NULL, ",");
    }

    return count > 0;
}

bool ProcMissionINIClass::Parse_Unit_List(const char* str, ProcUnitDef* units, int maxCount, int& count)
{
    count = 0;
    if (!str || !*str) return false;

    char buffer[1024];
    strncpy(buffer, str, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, ",");
    while (token && count < maxCount) {
        char* parts[3] = {NULL, NULL, NULL};
        int partCount = 0;

        char* p = token;
        while (*p && partCount < 3) {
            parts[partCount++] = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
        }

        if (partCount >= 1) {
            while (*parts[0] == ' ') parts[0]++;

            units[count].Type = Unit_From_Name(parts[0]);
            units[count].Count = (partCount >= 2) ? atoi(parts[1]) : 1;
            units[count].Mission = (partCount >= 3) ? Mission_From_Name(parts[2]) : MISSION_GUARD;

            if (units[count].Type != UNIT_NONE) {
                count++;
            }
        }

        token = strtok(NULL, ",");
    }

    return count > 0;
}

bool ProcMissionINIClass::Parse_Infantry_List(const char* str, ProcInfantryDef* infantry, int maxCount, int& count)
{
    count = 0;
    if (!str || !*str) return false;

    char buffer[1024];
    strncpy(buffer, str, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, ",");
    while (token && count < maxCount) {
        char* parts[3] = {NULL, NULL, NULL};
        int partCount = 0;

        char* p = token;
        while (*p && partCount < 3) {
            parts[partCount++] = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
        }

        if (partCount >= 1) {
            while (*parts[0] == ' ') parts[0]++;

            infantry[count].Type = Infantry_From_Name(parts[0]);
            infantry[count].Count = (partCount >= 2) ? atoi(parts[1]) : 1;
            infantry[count].Mission = (partCount >= 3) ? Mission_From_Name(parts[2]) : MISSION_GUARD;

            if (infantry[count].Type != INFANTRY_NONE) {
                count++;
            }
        }

        token = strtok(NULL, ",");
    }

    return count > 0;
}

bool ProcMissionINIClass::Save(const char* filename, const ProcMissionDef& mission)
{
    // Save implementation for editor support
    return false;
}


/*******************************************************************************
 * MASTER CONFIG LOADING
 ******************************************************************************/

bool ProcMissionINIClass::Load_Master_Config(const char* filename)
{
    CCFileClass file(filename);
    if (!file.Is_Available()) {
        return false;
    }

    CCINIClass ini;
    ini.Load(file, false);

    return Load_Master_Config(ini);
}

bool ProcMissionINIClass::Load_Master_Config(CCINIClass& ini)
{
    // Load team pools
    Load_Team_Pools(ini);

    // Load map pool
    Load_Map_Pool(ini);

    // Load menu missions
    Load_Menu_Missions(ini);

    return true;
}


/*******************************************************************************
 * TEAM POOL LOADING
 ******************************************************************************/

bool ProcMissionINIClass::Load_Team_Pools(CCINIClass& ini)
{
    char sectionName[32];
    char buffer[64];

    // Look for Team1, Team2, etc. sections
    for (int i = 1; i <= 100; i++) {
        sprintf(sectionName, "%s%d", PROC_INI_TEAM, i);

        if (!ini.Is_Present(sectionName)) {
            continue;
        }

        ProcPoolTeam team;
        if (Parse_Team(ini, sectionName, team)) {
            // Determine which pool this team belongs to
            ini.Get_String(sectionName, "Pool", "SOVIET_ATTACK", buffer, sizeof(buffer));
            ProcTeamPoolType poolType = Proc_Pool_From_Name(buffer);

            // Add to appropriate pool
            ProcTeamPools.Add_Team_To_Pool(poolType, team);
        }
    }

    // Mark pools as initialized
    ProcTeamPools.Initialize_Default_Pools();

    return true;
}

bool ProcMissionINIClass::Parse_Team(CCINIClass& ini, const char* section, ProcPoolTeam& team)
{
    char buffer[512];

    // Team name (8 chars max)
    ini.Get_String(section, "Name", "", buffer, sizeof(buffer));
    strncpy(team.Name, buffer, PROC_TEAM_NAME_LEN - 1);
    team.Name[PROC_TEAM_NAME_LEN - 1] = '\0';

    // House
    ini.Get_String(section, "House", "USSR", buffer, sizeof(buffer));
    team.House = House_From_Name(buffer);

    // Threat level
    ini.Get_String(section, "ThreatLevel", "LIGHT", buffer, sizeof(buffer));
    team.ThreatLevel = Proc_Threat_From_Name(buffer);

    // Composition: "3TNK:3,1TNK:2,E1:4"
    ini.Get_String(section, "Composition", "", buffer, sizeof(buffer));
    Parse_Team_Composition(buffer, team.Members, PROC_MAX_TEAM_MEMBERS, team.MemberCount);

    // Missions: "ATTACK:BUILDINGS" or "PATROL:11,PATROL:12,LOOP:0"
    ini.Get_String(section, "Missions", "ATTACK:ANYTHING", buffer, sizeof(buffer));
    Parse_Team_Missions(buffer, team.Missions, PROC_MAX_TEAM_MISSIONS, team.MissionCount);

    // Properties
    team.RecruitPriority = ini.Get_Int(section, "Priority", 7);
    team.IsRoundAbout = ini.Get_Bool(section, "RoundAbout", false);
    team.IsSuicide = ini.Get_Bool(section, "Suicide", false);
    team.UseGathering = ini.Get_Bool(section, "Gathering", true);  // Default to true for attack teams

    return team.Name[0] != '\0' && team.MemberCount > 0;
}

bool ProcMissionINIClass::Parse_Team_Composition(const char* str, ProcPoolMember* members, int maxCount, int& count)
{
    count = 0;
    if (!str || !*str) return false;

    char buffer[512];
    strncpy(buffer, str, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, ",");
    while (token && count < maxCount) {
        // Format: TYPE:QUANTITY  e.g., "3TNK:3" or "E1:4"
        char* parts[2] = {NULL, NULL};
        int partCount = 0;

        char* p = token;
        while (*p == ' ') p++;  // Skip leading spaces

        while (*p && partCount < 2) {
            parts[partCount++] = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
        }

        if (partCount >= 1 && parts[0]) {
            // Try to identify the type
            UnitType unit = Unit_From_Name(parts[0]);
            if (unit != UNIT_NONE) {
                members[count].RTTI = RTTI_UNITTYPE;
                members[count].TypeIndex = unit;
            } else {
                InfantryType inf = Infantry_From_Name(parts[0]);
                if (inf != INFANTRY_NONE) {
                    members[count].RTTI = RTTI_INFANTRYTYPE;
                    members[count].TypeIndex = inf;
                } else {
                    VesselType vessel = Vessel_From_Name(parts[0]);
                    if (vessel != VESSEL_NONE) {
                        members[count].RTTI = RTTI_VESSELTYPE;
                        members[count].TypeIndex = vessel;
                    } else {
                        // Skip unknown type
                        token = strtok(NULL, ",");
                        continue;
                    }
                }
            }

            members[count].Quantity = (partCount >= 2) ? atoi(parts[1]) : 1;
            if (members[count].Quantity < 1) members[count].Quantity = 1;
            count++;
        }

        token = strtok(NULL, ",");
    }

    return count > 0;
}

bool ProcMissionINIClass::Parse_Team_Missions(const char* str, ProcPoolMission* missions, int maxCount, int& count)
{
    count = 0;
    if (!str || !*str) return false;

    char buffer[512];
    strncpy(buffer, str, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, ",");
    while (token && count < maxCount) {
        // Format: MISSION:DATA  e.g., "ATTACK:BUILDINGS" or "PATROL:11"
        char* parts[2] = {NULL, NULL};
        int partCount = 0;

        char* p = token;
        while (*p == ' ') p++;  // Skip leading spaces

        while (*p && partCount < 2) {
            parts[partCount++] = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
        }

        if (partCount >= 1 && parts[0]) {
            missions[count].Mission = TeamMission_From_Name(parts[0]);

            if (missions[count].Mission != TMISSION_NONE) {
                // Data interpretation depends on mission type
                if (partCount >= 2 && parts[1]) {
                    // Check if it's a quarry name or a number
                    QuarryType quarry = Quarry_From_Name(parts[1]);
                    if (quarry != QUARRY_NONE) {
                        missions[count].Data = quarry;
                    } else {
                        missions[count].Data = atoi(parts[1]);
                    }
                } else {
                    missions[count].Data = 0;
                }
                count++;
            }
        }

        token = strtok(NULL, ",");
    }

    return count > 0;
}


/*******************************************************************************
 * MAP POOL LOADING
 ******************************************************************************/

bool ProcMissionINIClass::Load_Map_Pool(CCINIClass& ini)
{
    char sectionName[32];

    // Clear existing maps
    ProcMapPool.Clear();

    // Look for Map1, Map2, etc. sections
    for (int i = 1; i <= PROC_MAX_MAPS; i++) {
        sprintf(sectionName, "%s%d", PROC_INI_MAP, i);

        if (!ini.Is_Present(sectionName)) {
            continue;
        }

        ProcMapEntry map;
        if (Parse_Map_Entry(ini, sectionName, map)) {
            ProcMapPool.Add_Map(map);
        }
    }

    return true;
}

bool ProcMissionINIClass::Parse_Map_Entry(CCINIClass& ini, const char* section, ProcMapEntry& map)
{
    char buffer[256];

    // Filename (required)
    ini.Get_String(section, "Filename", "", buffer, sizeof(buffer));
    if (buffer[0] == '\0') return false;
    strncpy(map.Filename, buffer, sizeof(map.Filename) - 1);

    // Display name
    ini.Get_String(section, "Name", buffer, buffer, sizeof(buffer));
    strncpy(map.DisplayName, buffer, PROC_MAP_NAME_LEN - 1);

    // Description
    ini.Get_String(section, "Description", "", buffer, sizeof(buffer));
    strncpy(map.Description, buffer, PROC_MAP_DESC_LEN - 1);

    // Properties
    map.MaxPlayers = ini.Get_Int(section, "MaxPlayers", 2);
    map.HasNaval = ini.Get_Bool(section, "Naval", false);
    map.IsSymmetrical = ini.Get_Bool(section, "Symmetrical", false);

    return true;
}


/*******************************************************************************
 * MENU MISSION LOADING
 ******************************************************************************/

bool ProcMissionINIClass::Load_Menu_Missions(CCINIClass& ini)
{
    char sectionName[32];

    // Clear existing missions
    ProcMissionMenu.Clear();

    // Look for MenuMission1, MenuMission2, etc. sections
    for (int i = 1; i <= PROC_MAX_MENU_MISSIONS; i++) {
        sprintf(sectionName, "%s%d", PROC_INI_MENU_MISSION, i);

        if (!ini.Is_Present(sectionName)) {
            continue;
        }

        ProcMenuMission mission;
        if (Parse_Menu_Mission(ini, sectionName, mission)) {
            ProcMissionMenu.Add_Mission(mission);
        }
    }

    return true;
}

bool ProcMissionINIClass::Parse_Menu_Mission(CCINIClass& ini, const char* section, ProcMenuMission& mission)
{
    char buffer[256];

    // ID (required)
    ini.Get_String(section, "ID", "", buffer, sizeof(buffer));
    if (buffer[0] == '\0') return false;
    strncpy(mission.ID, buffer, PROC_MISSION_ID_LEN - 1);

    // Display name
    ini.Get_String(section, "Name", buffer, buffer, sizeof(buffer));
    strncpy(mission.DisplayName, buffer, PROC_MAP_NAME_LEN - 1);

    // Description
    ini.Get_String(section, "Description", "", buffer, sizeof(buffer));
    strncpy(mission.Description, buffer, PROC_MAP_DESC_LEN - 1);

    // INI file containing mission definition
    ini.Get_String(section, "INIFile", "", buffer, sizeof(buffer));
    strncpy(mission.INIFile, buffer, sizeof(mission.INIFile) - 1);

    // Player faction
    ini.Get_String(section, "Faction", "Greece", buffer, sizeof(buffer));
    mission.PlayerFaction = House_From_Name(buffer);

    // Enabled state
    mission.IsEnabled = ini.Get_Bool(section, "Enabled", true);

    return true;
}
