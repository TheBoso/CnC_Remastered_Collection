// PROCINI.H
//
// Procedural Mission INI Parser for Red Alert
// Loads mission rules and configuration from INI files
//

#ifndef PROCINI_H
#define PROCINI_H

#include "procmission.h"

class CCINIClass;


/*******************************************************************************
 * INI SECTION NAMES
 ******************************************************************************/

// Main sections
#define PROC_INI_MISSION        "Mission"
#define PROC_INI_PLAYER         "Player"
#define PROC_INI_HANDICAPS      "PlayerHandicaps"
#define PROC_INI_DIRECTOR       "DirectorRules"

// Dynamic sections (prefixed)
#define PROC_INI_ENEMY_BASE     "EnemyBase"     // EnemyBase1, EnemyBase2, etc.
#define PROC_INI_SPAWN_ENTRY    "SpawnEntry"    // SpawnEntry1, SpawnEntry2, etc.
#define PROC_INI_TEAM_POOL      "TeamPool"      // TeamPool section for pool configuration
#define PROC_INI_TEAM           "Team"          // Team1, Team2, etc. for team definitions
#define PROC_INI_MAP            "Map"           // Map1, Map2, etc. for map pool
#define PROC_INI_MENU_MISSION   "MenuMission"   // MenuMission1, etc. for menu entries

// Difficulty sections
#define PROC_INI_DIFF_EASY      "DifficultyEasy"
#define PROC_INI_DIFF_NORMAL    "DifficultyNormal"
#define PROC_INI_DIFF_HARD      "DifficultyHard"

// Master configuration file
#define PROC_MASTER_INI         "PROCMISSION.INI"


/*******************************************************************************
 * PROCMISSIONINICLASS
 ******************************************************************************/

/**
 * Class for loading procedural mission definitions from INI files
 *
 * INI File Format (Rules-Based):
 *
 * [Mission]
 * ID=PROC_ASSAULT_01
 * Theme=BASE_ASSAULT
 * PlayerFaction=Greece
 * EnemyFaction=USSR
 * TemplateMap=SCM01EA.INI
 *
 * [Player]
 * Region=Base_A
 * TechLevel=5
 * Credits=10000
 * Buildings=FACT:0:0,POWR:-3:2,PROC:4:0
 * Units=2TNK:3,HARV:1
 * Infantry=E1:5,E3:3
 *
 * [EnemyBase1]
 * ID=SOVIET_MAIN
 * House=USSR
 * Region=Base_B
 * State=ACTIVE
 * TechLevel=6
 * Credits=15000
 * BaseBuilding=yes
 * Autocreate=yes
 * Buildings=FACT:0:0,POWR:-4:2,WEAP:0:5
 * Defenses=FTUR:-6:0,FTUR:6:0
 * GuardUnits=HTNK:2
 * GuardInfantry=E1:6
 *
 * [DirectorRules]
 * AntiRushPeriod=300
 * InitialWaveDelay=50
 * MinSpawnInterval=120
 * MaxSpawnInterval=300
 * EscalationThreshold1=25
 * EscalationThreshold2=50
 * EscalationThreshold3=75
 * TurtleCheckTime=150
 * TurtleCreditThreshold=20000
 * MaxConcurrentTeams=2
 *
 * [SpawnEntry1]
 * Region=Entry_North
 * Pool=SOVIET_ATTACK
 * MinThreat=LIGHT
 * MaxThreat=HEAVY
 * Quarry=BUILDINGS
 *
 * [SpawnEntry2]
 * Region=Entry_East
 * Pool=SOVIET_ATTACK
 * MinThreat=SCOUT
 * MaxThreat=MEDIUM
 * Quarry=ANYTHING
 *
 * [DifficultyEasy]
 * PlayerCreditMult=125
 * EnemyTechCap=6
 * TeamQuantityScale=60
 * SpawnRateScale=150
 * MaxConcurrentTeams=1
 *
 * Note: Triggers and TeamTypes are NOT defined in the INI.
 * The Director creates them at runtime based on the rules and team pools.
 */
class ProcMissionINIClass {
public:
    ProcMissionINIClass();
    ~ProcMissionINIClass();

    // Load mission from INI file
    bool Load(const char* filename, ProcMissionDef& mission);

    // Load mission from INI class
    bool Load(CCINIClass& ini, ProcMissionDef& mission);

    // Save mission to INI file (for editors)
    bool Save(const char* filename, const ProcMissionDef& mission);

    // Load master configuration (team pools, map pools, menu entries)
    bool Load_Master_Config(const char* filename);
    bool Load_Master_Config(CCINIClass& ini);

    // Load team pools from INI
    bool Load_Team_Pools(CCINIClass& ini);

    // Load map pool from INI
    bool Load_Map_Pool(CCINIClass& ini);

    // Load mission menu entries from INI
    bool Load_Menu_Missions(CCINIClass& ini);

private:
    // Section parsers
    bool Parse_Mission_Section(CCINIClass& ini, ProcMissionDef& mission);
    bool Parse_Player_Section(CCINIClass& ini, ProcPlayerConfig& config);
    bool Parse_Handicaps_Section(CCINIClass& ini, ProcHandicaps& handicaps);
    bool Parse_Enemy_Base(CCINIClass& ini, const char* section, ProcEnemyBase& base);
    bool Parse_Director_Rules(CCINIClass& ini, ProcDirectorRules& rules);
    bool Parse_Spawn_Entry(CCINIClass& ini, const char* section, ProcSpawnEntry& entry);
    bool Parse_Difficulty(CCINIClass& ini, const char* section, ProcDifficultyModifiers& mods);

    // Pool/Map/Menu parsers
    bool Parse_Team(CCINIClass& ini, const char* section, ProcPoolTeam& team);
    bool Parse_Map_Entry(CCINIClass& ini, const char* section, ProcMapEntry& map);
    bool Parse_Menu_Mission(CCINIClass& ini, const char* section, ProcMenuMission& mission);

    // Value parsers
    bool Parse_Building_List(const char* str, ProcBuildingDef* buildings, int maxCount, int& count);
    bool Parse_Unit_List(const char* str, ProcUnitDef* units, int maxCount, int& count);
    bool Parse_Infantry_List(const char* str, ProcInfantryDef* infantry, int maxCount, int& count);
    bool Parse_Team_Composition(const char* str, ProcPoolMember* members, int maxCount, int& count);
    bool Parse_Team_Missions(const char* str, ProcPoolMission* missions, int maxCount, int& count);

    // Type conversion helpers
    HousesType House_From_Name(const char* name);
    StructType Struct_From_Name(const char* name);
    UnitType Unit_From_Name(const char* name);
    InfantryType Infantry_From_Name(const char* name);
    VesselType Vessel_From_Name(const char* name);
    MissionType Mission_From_Name(const char* name);
    QuarryType Quarry_From_Name(const char* name);
    TeamMissionType TeamMission_From_Name(const char* name);
};


/*******************************************************************************
 * GLOBAL ACCESS
 ******************************************************************************/

extern ProcMissionINIClass ProcMissionINI;


#endif // PROCINI_H
