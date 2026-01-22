// PROCMISSION.H
//
// Procedural Mission Runtime System for Red Alert
// Runtime mission generation, TeamType pools, and AI Director
//

#ifndef PROCMISSION_H
#define PROCMISSION_H

#include "defines.h"
#include "house.h"
#include "teamtype.h"
#include "trigtype.h"
#include "tevent.h"
#include "taction.h"


/*******************************************************************************
 * CONSTANTS AND LIMITS
 ******************************************************************************/

#define PROC_MAX_BUILDINGS           50
#define PROC_MAX_UNITS               50
#define PROC_MAX_INFANTRY            100
#define PROC_MAX_ENEMY_BASES         8
#define PROC_MAX_POOL_TEAMS          20      // Max teams per pool
#define PROC_MAX_TEAM_MEMBERS        5       // Same as MAX_TEAM_CLASSCOUNT
#define PROC_MAX_TEAM_MISSIONS       20      // Same as MAX_TEAM_MISSIONS
#define PROC_MAX_SPAWN_ENTRIES       10      // Max spawn entries per region
#define PROC_MAX_DISABLED_TECH       20
#define PROC_MAX_BASE_NODES          10

#define PROC_REGION_NAME_LEN         16
#define PROC_MISSION_ID_LEN          32
#define PROC_TEAM_NAME_LEN           8


/*******************************************************************************
 * ENUMERATIONS
 ******************************************************************************/

// Mission themes
typedef enum ProcMissionThemeType : unsigned char {
    PROC_THEME_NONE = 0,
    PROC_THEME_BASE_ASSAULT,        // Destroy enemy base(s)
    PROC_THEME_SURVIVAL,            // Defend for time limit
    PROC_THEME_SABOTAGE,            // Destroy key structures with limited forces
    PROC_THEME_ESCORT,              // Protect convoy to destination
    PROC_THEME_CAPTURE,             // Capture specific building(s)
    PROC_THEME_COUNT
} ProcMissionThemeType;


// Named region identifiers that map to waypoints
typedef enum ProcRegionType : unsigned char {
    PROC_REGION_NONE = 0,
    PROC_REGION_BASE_A,             // Player starting base (Waypoint A/0)
    PROC_REGION_BASE_B,             // Primary enemy base (Waypoint B/1)
    PROC_REGION_BASE_C,             // Secondary enemy base (Waypoint C/2)
    PROC_REGION_CHOKE_1,            // Defensive chokepoint (Waypoint D/3)
    PROC_REGION_CHOKE_2,            // Secondary chokepoint (Waypoint E/4)
    PROC_REGION_EXPANSION_1,        // Resource expansion (Waypoint F/5)
    PROC_REGION_EXPANSION_2,        // Resource expansion (Waypoint G/6)
    PROC_REGION_ENTRY_NORTH,        // Reinforcement entry (Waypoint H/7)
    PROC_REGION_ENTRY_SOUTH,        // Reinforcement entry (Waypoint I/8)
    PROC_REGION_ENTRY_EAST,         // Reinforcement entry (Waypoint J/9)
    PROC_REGION_ENTRY_WEST,         // Reinforcement entry (Waypoint K/10)
    PROC_REGION_PATROL_1,           // Patrol waypoint (Waypoint L/11)
    PROC_REGION_PATROL_2,           // Patrol waypoint (Waypoint M/12)
    PROC_REGION_OBJECTIVE_1,        // Mission objective (Waypoint N/13)
    PROC_REGION_OBJECTIVE_2,        // Mission objective (Waypoint O/14)
    PROC_REGION_OBJECTIVE_3,        // Mission objective (Waypoint P/15)
    PROC_REGION_OUTPOST_1,          // Enemy outpost 1 (Waypoint Q/16)
    PROC_REGION_OUTPOST_2,          // Enemy outpost 2 (Waypoint R/17)
    PROC_REGION_NEUTRAL,            // Neutral base (Waypoint S/18)
    PROC_REGION_COUNT
} ProcRegionType;


// Enemy base activation states
typedef enum ProcBaseStateType : unsigned char {
    PROC_BASE_STATE_ACTIVE = 0,     // Immediate activation, full AI
    PROC_BASE_STATE_DORMANT,        // Waiting for trigger activation
    PROC_BASE_STATE_DELAYED,        // Time-based activation
    PROC_BASE_STATE_COUNT
} ProcBaseStateType;


// Team pool categories
typedef enum ProcTeamPoolType : unsigned char {
    PROC_POOL_SOVIET_ATTACK = 0,    // Soviet offensive teams
    PROC_POOL_SOVIET_PATROL,        // Soviet patrol/scout teams
    PROC_POOL_SOVIET_DEFENSE,       // Soviet defensive/counter-attack teams
    PROC_POOL_ALLIED_ATTACK,        // Allied offensive teams
    PROC_POOL_ALLIED_PATROL,        // Allied patrol/scout teams
    PROC_POOL_ALLIED_DEFENSE,       // Allied defensive teams
    PROC_POOL_SPECIAL,              // Special/unique teams for specific missions
    PROC_POOL_COUNT
} ProcTeamPoolType;


// Team threat level for scaling
typedef enum ProcThreatLevel : unsigned char {
    PROC_THREAT_SCOUT = 0,          // Light reconnaissance
    PROC_THREAT_LIGHT,              // Light attack force
    PROC_THREAT_MEDIUM,             // Medium attack force
    PROC_THREAT_HEAVY,              // Heavy attack force
    PROC_THREAT_OVERWHELMING,       // Maximum force
    PROC_THREAT_COUNT
} ProcThreatLevel;


/*******************************************************************************
 * BUILDING/UNIT PLACEMENT STRUCTURES
 ******************************************************************************/

struct ProcBuildingDef {
    StructType      Type;
    short           OffsetX;
    short           OffsetY;
    unsigned char   Count;
    unsigned char   Priority;       // Base node priority (0 = not a node)

    ProcBuildingDef() : Type(STRUCT_NONE), OffsetX(0), OffsetY(0), Count(1), Priority(0) {}
};


struct ProcUnitDef {
    UnitType        Type;
    unsigned char   Count;
    MissionType     Mission;

    ProcUnitDef() : Type(UNIT_NONE), Count(1), Mission(MISSION_GUARD) {}
};


struct ProcInfantryDef {
    InfantryType    Type;
    unsigned char   Count;
    MissionType     Mission;

    ProcInfantryDef() : Type(INFANTRY_NONE), Count(1), Mission(MISSION_GUARD) {}
};


/*******************************************************************************
 * HANDICAP AND MODIFIER STRUCTURES
 ******************************************************************************/

struct ProcHandicaps {
    fixed           FirepowerBias;
    fixed           ArmorBias;
    fixed           CostBias;
    fixed           BuildSpeedBias;
    fixed           GroundspeedBias;
    fixed           ROFBias;

    ProcHandicaps() :
        FirepowerBias(1), ArmorBias(1), CostBias(1),
        BuildSpeedBias(1), GroundspeedBias(1), ROFBias(1) {}
};


struct ProcDifficultyModifiers {
    // Player modifiers
    fixed           PlayerCreditMult;
    fixed           PlayerCostBias;
    fixed           PlayerArmorBias;
    fixed           PlayerFirepowerBias;

    // Enemy modifiers
    fixed           EnemyCreditMult;
    int             EnemyTechLevelCap;
    fixed           EnemyArmorBias;
    fixed           EnemyFirepowerBias;
    int             EnemyIQLevel;

    // Director modifiers
    fixed           TeamQuantityScale;      // Team size multiplier
    fixed           SpawnRateScale;         // How fast teams spawn (lower = faster)
    int             MaxConcurrentTeams;     // Max active teams at once
    int             AntiRushPeriod;         // Build-up protection time (1/10 min)

    ProcDifficultyModifiers();
};


/*******************************************************************************
 * PLAYER CONFIGURATION
 ******************************************************************************/

struct ProcPlayerConfig {
    ProcRegionType  Region;
    int             TechLevel;
    long            Credits;
    unsigned int    MaxUnits;
    unsigned int    MaxBuildings;
    unsigned int    MaxInfantry;

    ProcHandicaps   Handicaps;

    // Disabled technology
    StructType      DisabledBuildings[PROC_MAX_DISABLED_TECH];
    int             DisabledBuildingCount;

    // Initial assets
    ProcBuildingDef Buildings[PROC_MAX_BUILDINGS];
    int             BuildingCount;

    ProcUnitDef     Units[PROC_MAX_UNITS];
    int             UnitCount;

    ProcInfantryDef Infantry[PROC_MAX_INFANTRY];
    int             InfantryCount;

    ProcPlayerConfig();
};


/*******************************************************************************
 * ENEMY BASE CONFIGURATION
 ******************************************************************************/

struct ProcAIFlags {
    unsigned        IsBaseBuilding:1;       // AI will construct buildings
    unsigned        IsAlerted:1;            // AI is in combat alert mode
    unsigned        IsAutocreate:1;         // AI will auto-create teams
    unsigned        ProductionEnabled:1;    // AI can produce units

    ProcAIFlags() : IsBaseBuilding(true), IsAlerted(true), IsAutocreate(true), ProductionEnabled(true) {}
};


struct ProcEnemyBase {
    char            ID[PROC_MISSION_ID_LEN];
    HousesType      House;
    ProcRegionType  Region;
    ProcBaseStateType State;
    int             TechLevel;
    long            Credits;

    ProcAIFlags     AIFlags;

    // Base structures
    ProcBuildingDef Buildings[PROC_MAX_BUILDINGS];
    int             BuildingCount;

    // Defensive structures
    ProcBuildingDef Defenses[PROC_MAX_BUILDINGS];
    int             DefenseCount;

    // Guard units (always spawned with base)
    ProcUnitDef     GuardUnits[PROC_MAX_UNITS];
    int             GuardUnitCount;

    // Guard infantry
    ProcInfantryDef GuardInfantry[PROC_MAX_INFANTRY];
    int             GuardInfantryCount;

    // Base nodes (AI building priorities)
    ProcBuildingDef BaseNodes[PROC_MAX_BASE_NODES];
    int             BaseNodeCount;

    // Activation settings
    int             ActivationDelay;        // For DELAYED state (1/10 min)

    ProcEnemyBase();
};


/*******************************************************************************
 * TEAMTYPE POOL SYSTEM
 ******************************************************************************/

// Team member specification for pool teams
struct ProcPoolMember {
    RTTIType        RTTI;
    int             TypeIndex;
    int             Quantity;

    ProcPoolMember() : RTTI(RTTI_NONE), TypeIndex(0), Quantity(0) {}
};


// Team mission specification
struct ProcPoolMission {
    TeamMissionType Mission;
    int             Data;           // Waypoint, quarry type, etc.

    ProcPoolMission() : Mission(TMISSION_NONE), Data(0) {}
};


// Single team definition in a pool
struct ProcPoolTeam {
    char            Name[PROC_TEAM_NAME_LEN];   // Short name for the team type
    ProcThreatLevel ThreatLevel;                // Team strength category
    HousesType      House;                      // Which house this team belongs to

    // Composition
    ProcPoolMember  Members[PROC_MAX_TEAM_MEMBERS];
    int             MemberCount;

    // Default missions (can be overridden by director)
    ProcPoolMission Missions[PROC_MAX_TEAM_MISSIONS];
    int             MissionCount;

    // Properties
    unsigned char   RecruitPriority;
    unsigned        IsRoundAbout:1;
    unsigned        IsSuicide:1;
    unsigned        UseGathering:1;     // If true, team gathers at friendly base then enemy base before attacking

    ProcPoolTeam();
};


// A pool of related team types
struct ProcTeamPool {
    ProcTeamPoolType PoolType;
    HousesType      DefaultHouse;

    ProcPoolTeam    Teams[PROC_MAX_POOL_TEAMS];
    int             TeamCount;

    ProcTeamPool() : PoolType(PROC_POOL_SOVIET_ATTACK), DefaultHouse(HOUSE_USSR), TeamCount(0) {}
};


/*******************************************************************************
 * DIRECTOR RULES - These control runtime behavior
 ******************************************************************************/

// Spawn rule entry - defines when and what to spawn
struct ProcSpawnEntry {
    ProcRegionType  SpawnRegion;            // Where teams spawn from
    ProcTeamPoolType PoolType;              // Which pool to select from
    ProcThreatLevel MinThreat;              // Minimum threat level to spawn
    ProcThreatLevel MaxThreat;              // Maximum threat level to spawn
    QuarryType      Quarry;                 // What these teams should attack

    ProcSpawnEntry() :
        SpawnRegion(PROC_REGION_ENTRY_NORTH), PoolType(PROC_POOL_SOVIET_ATTACK),
        MinThreat(PROC_THREAT_LIGHT), MaxThreat(PROC_THREAT_HEAVY),
        Quarry(QUARRY_ANYTHING) {}
};


// Director rules - control how the director behaves
struct ProcDirectorRules {
    // Timing (all times in 1/10 minutes)
    int             AntiRushPeriod;         // No spawns during this initial period
    int             InitialWaveDelay;       // Time until first attack wave
    int             MinSpawnInterval;       // Minimum time between spawns
    int             MaxSpawnInterval;       // Maximum time between spawns

    // Escalation thresholds (percentage of enemy destroyed)
    int             EscalationThreshold1;   // When to increase threat (e.g., 25%)
    int             EscalationThreshold2;   // Second escalation (e.g., 50%)
    int             EscalationThreshold3;   // Third escalation (e.g., 75%)

    // Turtle detection
    int             TurtleCheckTime;        // Time to check for turtling (1/10 min)
    long            TurtleCreditThreshold;  // Credits threshold indicating turtling
    int             TurtleResponseDelay;    // Delay before responding to turtle

    // Limits
    int             MaxConcurrentTeams;     // Maximum teams active at once
    int             MaxTotalSpawns;         // Maximum total spawns (0 = unlimited)

    // Spawn entries - defines where and what can spawn
    ProcSpawnEntry  SpawnEntries[PROC_MAX_SPAWN_ENTRIES];
    int             SpawnEntryCount;

    ProcDirectorRules();
};


/*******************************************************************************
 * COMPLETE MISSION DEFINITION (Rules-based)
 ******************************************************************************/

// Outpost base configuration (lighter than main base)
struct ProcOutpostConfig {
    HousesType          House;              // Which house owns this outpost
    ProcRegionType      Region;             // Where to spawn (OUTPOST_1, OUTPOST_2, etc.)
    bool                IsEnabled;          // Whether to spawn this outpost
    int                 DefenseCount;       // Number of defenses (scaled by difficulty)
    int                 GuardCount;         // Number of guard units

    ProcOutpostConfig() : House(HOUSE_UKRAINE), Region(PROC_REGION_OUTPOST_1),
        IsEnabled(false), DefenseCount(3), GuardCount(2) {}
};

// Neutral base configuration
struct ProcNeutralConfig {
    HousesType          House;              // Neutral house (HOUSE_NEUTRAL, HOUSE_SPECIAL, etc.)
    ProcRegionType      Region;             // Where to spawn
    bool                IsEnabled;          // Whether to spawn neutral base
    bool                IsHostileToAll;     // If true, attacks both player and enemy
    int                 DefenseCount;       // Number of defenses
    int                 GuardCount;         // Number of guard units

    ProcNeutralConfig() : House(HOUSE_NEUTRAL), Region(PROC_REGION_NEUTRAL),
        IsEnabled(false), IsHostileToAll(true), DefenseCount(4), GuardCount(3) {}
};

#define PROC_MAX_OUTPOSTS           4

struct ProcMissionDef {
    // Mission identity
    char                MissionID[PROC_MISSION_ID_LEN];
    ProcMissionThemeType Theme;
    HousesType          PlayerFaction;
    HousesType          EnemyFaction;
    char                TemplateMap[_MAX_FNAME + _MAX_EXT];

    // Player configuration
    ProcPlayerConfig    PlayerConfig;

    // Enemy bases (placed at mission start)
    ProcEnemyBase       EnemyBases[PROC_MAX_ENEMY_BASES];
    int                 EnemyBaseCount;

    // Outpost bases (smaller enemy bases under different houses, allied with main enemy)
    ProcOutpostConfig   Outposts[PROC_MAX_OUTPOSTS];
    int                 OutpostCount;

    // Neutral base (optional, hostile to everyone)
    ProcNeutralConfig   NeutralBase;

    // Director rules (control runtime spawning)
    ProcDirectorRules   DirectorRules;

    // Theme-specific settings
    int                 SurvivalTime;       // SURVIVAL: seconds to survive (0 = use default 10 min)

    // Difficulty settings
    ProcDifficultyModifiers EasyDifficulty;
    ProcDifficultyModifiers NormalDifficulty;
    ProcDifficultyModifiers HardDifficulty;

    ProcMissionDef();
};


/*******************************************************************************
 * MISSION DIRECTOR CLASS - Runtime AI that generates content
 ******************************************************************************/

class ProcMissionDirector {
public:
    // State tracking
    enum DirectorState {
        DIRECTOR_IDLE = 0,
        DIRECTOR_ANTI_RUSH,         // Waiting for anti-rush period
        DIRECTOR_ACTIVE,            // Normal operation
        DIRECTOR_ESCALATED,         // Increased threat
        DIRECTOR_ENDGAME            // Maximum pressure
    };

    ProcMissionDirector();
    ~ProcMissionDirector();

    // Initialize with mission rules
    void Initialize(const ProcDirectorRules& rules, DiffType difficulty);

    // Called every game tick to update director state
    void AI();

    // State queries
    DirectorState Get_State() const { return State; }
    int Get_Active_Team_Count() const { return ActiveTeamCount; }
    int Get_Current_Threat_Level() const { return CurrentThreatLevel; }
    bool Is_Active() const { return State != DIRECTOR_IDLE; }

    // Manual controls
    void Force_Spawn(ProcTeamPoolType pool, ProcThreatLevel threat, ProcRegionType region);
    void Set_Escalation_Level(int level);
    void Pause() { IsPaused = true; }
    void Resume() { IsPaused = false; }

    // Reset state
    void Reset();

private:
    // Internal state
    DirectorState   State;
    DiffType        Difficulty;
    bool            IsPaused;

    // Timing
    long            GameStartFrame;
    long            LastSpawnFrame;
    long            NextSpawnFrame;
    int             AntiRushEndFrame;

    // Tracking
    int             ActiveTeamCount;
    int             TotalSpawns;
    int             CurrentThreatLevel;     // 0-4 matching ProcThreatLevel
    int             EscalationLevel;        // 0-3

    // Cached rules
    ProcDirectorRules Rules;
    fixed           QuantityScale;
    fixed           SpawnRateScale;
    int             MaxConcurrentTeams;

    // Internal functions
    void Update_Escalation();
    void Check_Turtle_Detection();
    void Try_Spawn_Team();
    void Select_And_Spawn(const ProcSpawnEntry& entry);
    TeamTypeClass* Create_Team_From_Pool(ProcTeamPoolType pool, ProcThreatLevel threat, HousesType house, ProcRegionType spawnRegion);
    TriggerTypeClass* Create_Spawn_Trigger(TeamTypeClass* team);

    int Calculate_Next_Spawn_Delay();
    int Get_Enemy_Destruction_Percentage();
    bool Can_Spawn_Team();
};


/*******************************************************************************
 * PROCEDURAL MISSION MANAGER CLASS
 ******************************************************************************/

class ProcMissionManager {
public:
    static ProcMissionManager& Instance();

    // Mission loading (for INI-defined missions)
    bool Load_Mission_Definition(const char* filename);
    bool Load_Mission_From_Memory(const ProcMissionDef& mission);

    // Auto-generation (randomly generates theme and content)
    bool Auto_Generate_And_Inject(DiffType difficulty);

    // Runtime injection (call after template map is loaded)
    bool Inject_Mission(DiffType difficulty);

    // Director access
    ProcMissionDirector& Get_Director() { return Director; }

    // Region to waypoint mapping
    CELL Get_Region_Cell(ProcRegionType region) const;
    int Get_Region_Waypoint(ProcRegionType region) const;

    // Query current mission state
    bool Is_Mission_Loaded() const { return IsMissionLoaded; }
    const ProcMissionDef* Get_Current_Mission() const { return IsMissionLoaded ? &CurrentMission : NULL; }

    // Cleanup
    void Reset();

    // Call this each game tick
    void AI();

private:
    ProcMissionManager();
    ~ProcMissionManager();
    ProcMissionManager(const ProcMissionManager&);
    ProcMissionManager& operator=(const ProcMissionManager&);

    // Internal injection functions
    bool Initialize_Houses(DiffType difficulty);
    bool Spawn_Player_Assets();
    bool Spawn_Enemy_Bases();
    bool Initialize_Team_Pools();
    bool Setup_Victory_Defeat_Triggers();

    // Theme-specific trigger setup
    void Setup_Base_Assault_Triggers();
    void Setup_Survival_Triggers();

    // Auto-generation helpers
    void Setup_Default_Difficulty_Modifiers();
    void Setup_Default_Director_Rules();
    void Auto_Spawn_Player_Units();
    void Auto_Spawn_Enemy_Base();
    void Auto_Spawn_Outpost_Bases();
    void Auto_Spawn_Neutral_Base();
    void Setup_Enemy_Alliances();

    // Helper functions
    bool Spawn_Building(HousesType house, StructType type, CELL cell);
    bool Spawn_Unit(HousesType house, UnitType type, CELL cell, MissionType mission);
    bool Spawn_Infantry(HousesType house, InfantryType type, CELL cell, MissionType mission);
    bool Spawn_Vessel(HousesType house, VesselType type, CELL cell, MissionType mission);

    // Fallback spawn location finders (when waypoints aren't defined)
    CELL Find_House_Building_Cell(HousesType house) const;
    CELL Find_House_Unit_Cell(HousesType house) const;
    CELL Find_Fallback_Spawn_Cell(HousesType house) const;
    CELL Find_Random_Base_Location(int minDistanceFromBases) const;
    bool Is_Valid_Spawn_Cell(CELL cell) const;
    CELL Find_Nearby_Valid_Cell(CELL startCell, int maxRadius) const;
    CELL Find_Water_Cell_Near(CELL startCell, int maxRadius) const;

    // Random building placement
    CELL Find_Random_Building_Cell(CELL centerCell, StructType type, int minRadius, int maxRadius) const;
    bool Spawn_Building_Random(HousesType house, StructType type, CELL centerCell, int minRadius, int maxRadius);

    void Apply_Difficulty_Modifiers(HouseClass* house, const ProcDifficultyModifiers& mods, bool isPlayer);

    // Mission state
    bool            IsMissionLoaded;
    ProcMissionDef  CurrentMission;
    DiffType        CurrentDifficulty;

    // Director instance
    ProcMissionDirector Director;

    // Region mapping cache
    CELL            RegionCells[PROC_REGION_COUNT];
    bool            RegionCellsValid;
};


/*******************************************************************************
 * TEAMTYPE POOL MANAGER - Manages predefined team pools
 ******************************************************************************/

class ProcTeamPoolManager {
public:
    static ProcTeamPoolManager& Instance();

    // Initialize default pools (call at game start)
    void Initialize_Default_Pools();

    // Load custom pools from INI
    bool Load_Pools_From_INI(const char* filename);

    // Get a random team from a pool matching criteria
    const ProcPoolTeam* Get_Random_Team(ProcTeamPoolType pool, ProcThreatLevel minThreat,
                                        ProcThreatLevel maxThreat, HousesType house) const;

    // Get specific team by name
    const ProcPoolTeam* Get_Team_By_Name(const char* name) const;

    // Pool access
    const ProcTeamPool& Get_Pool(ProcTeamPoolType type) const { return Pools[type]; }

    // Add team to pool (for custom missions)
    bool Add_Team_To_Pool(ProcTeamPoolType poolType, const ProcPoolTeam& team);

private:
    ProcTeamPoolManager();
    ~ProcTeamPoolManager();
    ProcTeamPoolManager(const ProcTeamPoolManager&);
    ProcTeamPoolManager& operator=(const ProcTeamPoolManager&);

    // Team pools are loaded from INI - no hardcoded setup functions

    ProcTeamPool    Pools[PROC_POOL_COUNT];
    bool            IsInitialized;
};


/*******************************************************************************
 * MAP POOL SYSTEM
 ******************************************************************************/

#define PROC_MAX_MAPS               20
#define PROC_MAP_NAME_LEN           32
#define PROC_MAP_DESC_LEN           128

// Single map entry in the pool
struct ProcMapEntry {
    char            Filename[_MAX_FNAME + _MAX_EXT];    // e.g., "SCM01EA.INI"
    char            DisplayName[PROC_MAP_NAME_LEN];     // e.g., "Coastal Assault"
    char            Description[PROC_MAP_DESC_LEN];     // Brief description
    int             MaxPlayers;                         // 2-8 players supported
    bool            HasNaval;                           // Map supports naval units
    bool            IsSymmetrical;                      // Fair for competitive play

    ProcMapEntry() : MaxPlayers(2), HasNaval(false), IsSymmetrical(false) {
        Filename[0] = '\0';
        DisplayName[0] = '\0';
        Description[0] = '\0';
    }
};

// Map pool manager
class ProcMapPoolManager {
public:
    static ProcMapPoolManager& Instance();

    // Load maps from INI
    bool Load_From_INI(const char* filename);

    // Get random map
    const ProcMapEntry* Get_Random_Map() const;

    // Get specific map by index or name
    const ProcMapEntry* Get_Map(int index) const;
    const ProcMapEntry* Get_Map_By_Name(const char* displayName) const;

    // Query
    int Get_Map_Count() const { return MapCount; }

    // Add map (for runtime additions)
    bool Add_Map(const ProcMapEntry& map);

    // Clear all maps
    void Clear() { MapCount = 0; }

private:
    ProcMapPoolManager();
    ~ProcMapPoolManager();
    ProcMapPoolManager(const ProcMapPoolManager&);
    ProcMapPoolManager& operator=(const ProcMapPoolManager&);

    ProcMapEntry    Maps[PROC_MAX_MAPS];
    int             MapCount;
};


/*******************************************************************************
 * CUSTOM MISSION MENU ENTRIES
 ******************************************************************************/

#define PROC_MAX_MENU_MISSIONS      10

// Represents a procedural mission available in the custom missions menu
struct ProcMenuMission {
    char            ID[PROC_MISSION_ID_LEN];            // Unique identifier
    char            DisplayName[PROC_MAP_NAME_LEN];     // Shown in menu
    char            Description[PROC_MAP_DESC_LEN];     // Brief description
    char            INIFile[_MAX_FNAME + _MAX_EXT];     // Mission definition file
    HousesType      PlayerFaction;                      // HOUSE_GREECE or HOUSE_USSR
    bool            IsEnabled;                          // Available for selection

    ProcMenuMission() : PlayerFaction(HOUSE_GREECE), IsEnabled(true) {
        ID[0] = '\0';
        DisplayName[0] = '\0';
        Description[0] = '\0';
        INIFile[0] = '\0';
    }
};

// Mission menu manager
class ProcMissionMenuManager {
public:
    static ProcMissionMenuManager& Instance();

    // Load menu entries from master INI
    bool Load_From_INI(const char* filename);

    // Get missions for a specific faction
    int Get_Mission_Count(HousesType faction) const;
    const ProcMenuMission* Get_Mission(HousesType faction, int index) const;

    // Get all missions
    int Get_Total_Count() const { return MissionCount; }
    const ProcMenuMission* Get_Mission_By_ID(const char* id) const;

    // Add mission entry
    bool Add_Mission(const ProcMenuMission& mission);

    // Clear
    void Clear() { MissionCount = 0; }

private:
    ProcMissionMenuManager();
    ~ProcMissionMenuManager();
    ProcMissionMenuManager(const ProcMissionMenuManager&);
    ProcMissionMenuManager& operator=(const ProcMissionMenuManager&);

    ProcMenuMission Missions[PROC_MAX_MENU_MISSIONS];
    int             MissionCount;
};


/*******************************************************************************
 * GLOBAL ACCESS
 ******************************************************************************/

extern ProcMissionManager& ProcMission;
extern ProcTeamPoolManager& ProcTeamPools;
extern ProcMapPoolManager& ProcMapPool;
extern ProcMissionMenuManager& ProcMissionMenu;


/*******************************************************************************
 * UTILITY FUNCTIONS
 ******************************************************************************/

int Proc_Region_To_Waypoint(ProcRegionType region);
ProcRegionType Proc_Region_From_Name(const char* name);
ProcMissionThemeType Proc_Theme_From_Name(const char* name);
ProcTeamPoolType Proc_Pool_From_Name(const char* name);
ProcThreatLevel Proc_Threat_From_Name(const char* name);

// Safe cell finding for team pathfinding
// Finds a cell that is passable and away from enemy threats
CELL Proc_Find_Safe_Cell_Near(CELL centerCell, HousesType forHouse, int searchRadius);

// Find a staging cell near a target (for assault teams to gather before attacking)
CELL Proc_Find_Staging_Cell(CELL targetCell, HousesType forHouse, int distance);


#endif // PROCMISSION_H
