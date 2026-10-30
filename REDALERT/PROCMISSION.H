// PROCMISSION.H
//
// Procedural Mission Generator for Red Alert (skirmish).
//
// A skirmish map with "Procedural=yes" in its [Basic] section is turned into a
// generated mission when it is started from the skirmish lobby:
//
//   - Your country decides your side (Allied / Soviet).
//   - AI players on your lobby team are your allies.
//   - AI players on any other team are enemies. Each one gets a pre-built base.
//   - The lobby tech level is respected (or overridden per map).
//
// The content (wave teams, base layouts, objectives, difficulty tables) lives in
// the preset library INI files, loaded by ProcPresetLibrary (PROCINI.CPP):
//
//   PROCMISSION.INI     generator settings and difficulty tables
//   PROCTEAMS.INI       wave / guard team presets
//   PROCBASES.INI       base layout presets
//   PROCOBJECTIVES.INI  objective (task) presets
//
// A map can override any [Generator] setting in its own [Procedural] section.
//

#ifndef PROCMISSION_H
#define PROCMISSION_H

#include "defines.h"
#include "house.h"
#include "teamtype.h"


/*******************************************************************************
 * LIMITS
 ******************************************************************************/

#define PROC_NAME_LEN                32
#define PROC_TEXT_LEN                160

#define PROC_MAX_TEAM_PRESETS        80
#define PROC_MAX_BASE_PRESETS        32
#define PROC_MAX_OBJECTIVE_PRESETS   32

#define PROC_MAX_MEMBERS             5       // Matches TeamTypeClass::MAX_TEAM_CLASSCOUNT
#define PROC_MAX_TEAM_MISSIONS       8
#define PROC_MAX_BASE_ENTRIES        24
#define PROC_MAX_BASE_GUARDS         8
#define PROC_MAX_TARGET_TYPES        6

#define PROC_MAX_HOUSES              MAX_PLAYERS
#define PROC_MAX_OBJECTIVES          4       // Active objectives per mission (1 primary + secondaries)
#define PROC_MAX_WAVE_TYPES          24      // TeamTypes the director may own at once
#define PROC_MAX_SQUAD_CHOICES       4       // Squad= presets per objective (one per side is typical)
#define PROC_MAX_PATROLS             8       // Patrol teams per mission
#define PROC_MAX_ANCHORS             12      // Places patrols guard (targets, outposts, enemy bases)
#define PROC_FIRST_WAYPOINT          60      // Waypoints 60-97 are used for generated patrol routes


/*******************************************************************************
 * ENUMERATIONS
 ******************************************************************************/

typedef enum ProcSideType : unsigned char {
    PROC_SIDE_ANY = 0,
    PROC_SIDE_ALLIED,
    PROC_SIDE_SOVIET,
    PROC_SIDE_COUNT
} ProcSideType;

typedef enum ProcThreatType : unsigned char {
    PROC_THREAT_SCOUT = 0,
    PROC_THREAT_LIGHT,
    PROC_THREAT_MEDIUM,
    PROC_THREAT_HEAVY,
    PROC_THREAT_OVERWHELMING,
    PROC_THREAT_COUNT
} ProcThreatType;

// What a team preset is used for.
typedef enum ProcTeamRoleType : unsigned char {
    PROC_TEAM_ATTACK = 0,           // Director attack waves
    PROC_TEAM_DEFENSE,              // Garrisons for objectives
    PROC_TEAM_PATROL,               // Walk a loop around targets / outposts / bases (squad missions)
    PROC_TEAM_SQUAD,                // The player's starting squad in squad missions
    PROC_TEAM_ROLE_COUNT
} ProcTeamRoleType;

typedef enum ProcBaseRoleType : unsigned char {
    PROC_BASE_MAIN = 0,             // Full enemy base
    PROC_BASE_OUTPOST,              // Smaller enemy base
    PROC_BASE_DEFEND,               // Pre-built base for the player (survival style objectives)
    PROC_BASE_ROLE_COUNT
} ProcBaseRoleType;

typedef enum ProcObjectiveKindType : unsigned char {
    PROC_OBJ_DESTROY_BASES = 0,     // Defeat every enemy house
    PROC_OBJ_SURVIVE,               // Survive until the timer runs out
    PROC_OBJ_DESTROY,               // Destroy a specific structure
    PROC_OBJ_CAPTURE,               // Capture a specific structure
    PROC_OBJ_PROTECT,               // Keep a specific structure alive until the timer runs out
    PROC_OBJ_KIND_COUNT
} ProcObjectiveKindType;

typedef enum ProcObjectiveSlotType : unsigned char {
    PROC_SLOT_PRIMARY = 0,          // Can only be the win condition
    PROC_SLOT_SECONDARY,            // Can only be a bonus task
    PROC_SLOT_EITHER,
    PROC_SLOT_COUNT
} ProcObjectiveSlotType;

// Who owns an objective's target structure.
typedef enum ProcOwnerType : unsigned char {
    PROC_OWNER_ENEMY = 0,           // Placed inside an enemy base
    PROC_OWNER_NEUTRAL,             // Placed somewhere neutral, away from all bases
    PROC_OWNER_PLAYER,              // Placed in the player's base
    PROC_OWNER_OUTPOST,             // Placed in a small enemy outpost generated away from all bases
    PROC_OWNER_COUNT
} ProcOwnerType;

typedef enum ProcObjectiveStateType : unsigned char {
    PROC_STATE_ACTIVE = 0,
    PROC_STATE_COMPLETE,
    PROC_STATE_FAILED
} ProcObjectiveStateType;

// How a player-team house starts the mission.
typedef enum ProcStartType : unsigned char {
    PROC_START_MCV = 0,             // Normal skirmish start (MCV + lobby units)
    PROC_START_BASE,                // Pre-built base from a Defend preset
    PROC_START_RANDOM,              // Base with PlayerBaseChance %, otherwise MCV
    PROC_START_SAME,                // Allies only: start the same way the player does
    PROC_START_COUNT
} ProcStartType;

typedef enum ProcSpawnModeType : unsigned char {
    PROC_SPAWN_BASE = 0,            // Waves appear next to the owning house's base
    PROC_SPAWN_EDGE,                // Waves drive in from the map edge nearest the owning base
    PROC_SPAWN_COUNT
} ProcSpawnModeType;


/*******************************************************************************
 * PRESETS (loaded from the INI library)
 ******************************************************************************/

struct ProcTechnoCount {
    TechnoTypeClass const * Type;
    int                     Count;

    ProcTechnoCount() : Type(NULL), Count(0) {}
};

struct ProcTeamMission {
    TeamMissionType         Mission;
    int                     Data;

    ProcTeamMission() : Mission(TMISSION_NONE), Data(0) {}
};

struct ProcTeamPreset {
    char                    Name[PROC_NAME_LEN];
    ProcSideType            Side;
    ProcTeamRoleType        Role;
    ProcThreatType          Threat;
    int                     Weight;

    ProcTechnoCount         Members[PROC_MAX_MEMBERS];
    int                     MemberCount;

    ProcTeamMission         Missions[PROC_MAX_TEAM_MISSIONS];
    int                     MissionCount;

    bool                    IsGathering;    // Rally at home, then stage outside the target base
    bool                    IsSuicide;
    bool                    IsRoundAbout;

    ProcTeamPreset();
};

struct ProcBaseEntry {
    StructType              Type;
    int                     Count;
    int                     MinRadius;
    int                     MaxRadius;
    bool                    IsDefense;      // Scaled by the difficulty DefenseScale

    ProcBaseEntry() : Type(STRUCT_NONE), Count(1), MinRadius(3), MaxRadius(10), IsDefense(false) {}
};

struct ProcBasePreset {
    char                    Name[PROC_NAME_LEN];
    ProcSideType            Side;
    ProcBaseRoleType        Role;
    int                     MinTech;
    int                     Weight;

    ProcBaseEntry           Entries[PROC_MAX_BASE_ENTRIES];
    int                     EntryCount;

    ProcTechnoCount         Guards[PROC_MAX_BASE_GUARDS];
    int                     GuardCount;

    int                     Harvesters;
    bool                    IsAutoPower;    // Add power plants until power covers drain
    bool                    IsNaval;        // Add a naval yard / sub pen if water is close
    long                    Credits;

    ProcBasePreset();
};

struct ProcObjectivePreset {
    char                    Name[PROC_NAME_LEN];
    ProcObjectiveKindType   Kind;
    ProcObjectiveSlotType   Slot;
    ProcSideType            PlayerSide;     // Only offered when the player is on this side
    int                     MinTech;
    int                     Weight;

    int                     MinMinutes;     // SURVIVE / PROTECT duration, or DESTROY / CAPTURE time limit (0 = none)
    int                     MaxMinutes;

    StructType              Targets[PROC_MAX_TARGET_TYPES];
    int                     TargetCount;
    ProcOwnerType           Owner;
    char                    Guard[PROC_NAME_LEN];       // Defense team preset placed around the target

    char                    PlayerBase[PROC_NAME_LEN];  // Primary only: DEFEND base given to the player instead of an MCV

    char                    OutpostBase[PROC_NAME_LEN]; // Owner=Outpost: outpost base preset (AUTO = random Outpost)

    long                    RewardCredits;
    ProcTechnoCount         RewardUnits[PROC_MAX_MEMBERS];
    int                     RewardUnitCount;

    /*
    **	Squad missions (primary only). The humans start with a small squad instead of an MCV
    **	or base, enemies can be made passive, and patrols guard the targets.
    */
    bool                    IsSquadStart;               // PlayerStart=Squad
    char                    Squads[PROC_MAX_SQUAD_CHOICES][PROC_NAME_LEN];  // Squad presets; the first for the player's side is used
    int                     SquadCount;
    long                    PlayerCredits;              // -1 = keep the lobby credits
    int                     WavesOverride;              // -1 = generator setting, 0 = no waves, 1 = waves
    int                     WaveDelaySeconds;           // 0 = difficulty FirstWaveSeconds
    bool                    IsEnemyProduction;          // no = enemies never build (static bases, like campaign maps)
    int                     Patrols;                    // Patrol teams to generate
    char                    Bonus[PROC_NAME_LEN];       // Secondary objective always added with this primary

    char                    Text[PROC_TEXT_LEN];        // Shown to the player; %d is replaced by minutes

    ProcObjectivePreset();
};

struct ProcDifficultyPreset {
    int                     EnemyCreditsPct;
    int                     DefenseScalePct;
    int                     GuardScalePct;
    int                     WaveSizePct;
    int                     WaveIntervalPct;    // >100 = slower waves
    int                     MaxActiveWaves;
    int                     OutpostChance;      // % chance an extra enemy uses an OUTPOST base instead of MAIN
    int                     FirstWaveSeconds;
    ProcThreatType          StartThreat;
    ProcThreatType          MaxThreat;
    int                     EscalateMinutes;    // Threat rises one step every N minutes

    ProcDifficultyPreset();
};

struct ProcSettings {
    int                     Difficulty;         // DiffType, or -1 = random
    char                    Primary[PROC_NAME_LEN];  // Objective preset name, or RANDOM
    int                     SecondaryMin;
    int                     SecondaryMax;
    int                     TechLevel;          // 0 = use the lobby tech level
    bool                    IsWaves;
    bool                    IsKeepStartUnits;   // Keep the lobby's starting units for enemies
    bool                    IsAllMaps;          // Every single-human skirmish map is procedural, flagged or not
    int                     WaveMinSeconds;
    int                     WaveMaxSeconds;
    ProcSpawnModeType       SpawnMode;
    ProcStartType           PlayerStart;        // Human players
    ProcStartType           AllyStart;          // AI players on the player's team
    int                     PlayerBaseChance;   // % used by PROC_START_RANDOM

    ProcSettings();
};


/*******************************************************************************
 * PRESET LIBRARY
 ******************************************************************************/

class ProcPresetLibrary {
public:
    ProcPresetLibrary();

    // Loads all library files. Missing files are skipped. Returns false if nothing usable was loaded.
    bool Load();

    // Applies [Generator] / [Procedural] style overrides from any INI on top of the given settings.
    void Read_Settings(CCINIClass & ini, char const * section, ProcSettings & settings) const;

    ProcTeamPreset const * Pick_Team(ProcSideType side, ProcTeamRoleType role, ProcThreatType minthreat, ProcThreatType maxthreat, int tech) const;
    ProcTeamPreset const * Find_Team(char const * name) const;

    ProcBasePreset const * Pick_Base(ProcSideType side, ProcBaseRoleType role, int tech) const;
    ProcBasePreset const * Find_Base(char const * name) const;

    ProcObjectivePreset const * Pick_Objective(ProcSideType playerside, bool primary, int tech, bool playerbase, bool cancapture, ProcObjectivePreset const * const * exclude, int excludecount) const;
    ProcObjectivePreset const * Find_Objective(char const * name) const;

    ProcSettings            Settings;
    ProcDifficultyPreset    Difficulty[DIFF_COUNT];

    ProcTeamPreset          Teams[PROC_MAX_TEAM_PRESETS];
    int                     TeamCount;

    ProcBasePreset          Bases[PROC_MAX_BASE_PRESETS];
    int                     BaseCount;

    ProcObjectivePreset     Objectives[PROC_MAX_OBJECTIVE_PRESETS];
    int                     ObjectiveCount;
};


/*******************************************************************************
 * RUNTIME
 ******************************************************************************/

struct ProcObjective {
    ProcObjectivePreset const * Preset;
    bool                    IsPrimary;
    ProcObjectiveStateType  State;
    TARGET                  Target;         // Target structure (DESTROY / CAPTURE / PROTECT)
    StructType              TargetType;
    long                    EndFrame;       // Timer end (0 = no timer)
    int                     Minutes;

    ProcObjective() : Preset(NULL), IsPrimary(false), State(PROC_STATE_ACTIVE), Target(TARGET_NONE),
        TargetType(STRUCT_NONE), EndFrame(0), Minutes(0) {}
};

struct ProcHouseInfo {
    HouseClass *            House;
    CELL                    Home;           // Base center chosen at generation time
    int                     InitialBuildings;

    ProcHouseInfo() : House(NULL), Home(-1), InitialBuildings(0) {}
};

class ProcMissionManager {
public:
    ProcMissionManager();

    // Called at the end of Read_Scenario_INI for skirmish maps with Procedural=yes.
    bool Generate(CCINIClass & scenario_ini);

    // Called every logic frame while a procedural mission is running.
    void AI();

    void Reset();

    bool Is_Active() const { return IsActive; }

private:
    // Setup
    void Classify_Houses();
    void Pick_Objectives();
    void Add_Objective(ProcObjectivePreset const * preset, bool primary, ProcObjectivePreset const ** chosen);
    void Build_Enemy_Bases();
    void Build_Player_Bases();
    void Place_Objective_Targets();
    void Announce_Objectives();

    // Base building
    bool Build_Base(HouseClass * house, ProcBasePreset const * preset, bool isplayer, CELL center = -1, bool skipconst = false);
    void Build_Squads(ProcObjectivePreset const * primary);
    ProcTeamPreset const * Find_Squad(ProcObjectivePreset const * primary, ProcSideType side) const;
    CELL Build_Outpost(HouseClass * house, ProcObjectivePreset const * preset);
    void Spawn_Patrols(int count);
    int Claim_Waypoint(CELL cell);
    void Add_Anchor(CELL cell, HouseClass * owner);
    void Clear_Start_Units(HouseClass * house, bool keepcombat);
    bool Place_Building(HouseClass * house, StructType type, CELL center, int minradius, int maxradius, TARGET * placed = NULL);
    bool Can_Place_Building(StructType type, CELL cell) const;
    bool Try_Place_Building(HouseClass * house, StructType type, CELL cell, TARGET * placed);
    void Place_Units(HouseClass * house, TechnoTypeClass const * type, int count, CELL center, MissionType mission, int minradius = 2, int maxradius = 7);
    void Place_Naval_Yard(HouseClass * house, CELL center);
    void Add_Power(HouseClass * house, CELL center);
    CELL Find_Neutral_Location(int mindistance) const;

    // Objectives
    void Update_Objectives();
    void Complete_Objective(ProcObjective & objective);
    void Fail_Objective(ProcObjective & objective);
    void Give_Reward(ProcObjectivePreset const * preset);
    void Player_Wins();
    void Player_Loses();

    // Director (attack waves)
    void Director_AI();
    void Spawn_Wave(HouseClass * house);
    TeamTypeClass * Make_Wave_Type(ProcTeamPreset const * preset, HouseClass * house, HouseClass * victim, CELL home);
    bool Deliver_Wave(TeamTypeClass * teamtype, CELL home);
    bool Spawn_Team(TeamTypeClass * teamtype, CELL center, int minradius, int maxradius);
    int Active_Waves() const;
    void Collect_Wave_Types();
    ProcThreatType Current_Threat() const;
    HouseClass * Pick_Victim(HouseClass * attacker) const;

    // Helpers
    bool Is_Player_Team(HouseClass const * house) const;
    bool Is_Alive(HouseClass const * house) const;
    CELL Home_Cell(HouseClass const * house) const;
    void Message(char const * text) const;
    void Print_Status() const;
    void Objective_Text(ProcObjective const & objective, char * buffer, int size) const;

    bool                    IsActive;
    bool                    IsAnnounced;
    bool                    IsFinished;
    bool                    IsSquad;        // Squad mission: humans start with a squad, see ProcObjectivePreset
    bool                    IsPlayerBase;   // Humans on the player's team start with a Defend base
    bool                    IsAllyBase;     // AI allies start with a Defend base

    ProcSettings            Settings;
    DiffType                Difficulty;
    ProcDifficultyPreset    Diff;
    int                     TechLevel;
    ProcSideType            PlayerSide;

    HouseClass *            Commander;      // The (first) human player
    HouseClass *            PlayerTeam[PROC_MAX_HOUSES];
    int                     PlayerTeamCount;
    ProcHouseInfo           Enemies[PROC_MAX_HOUSES];
    int                     EnemyCount;

    ProcObjective           Objectives[PROC_MAX_OBJECTIVES];
    int                     ObjectiveCount;

    long                    Elapsed;        // Logic frames since generation (own counter; Frame is not reset between games)
    long                    StartFrame;
    long                    NextWaveFrame;
    int                     NextAttacker;
    int                     WaveCounter;
    CELL                    Anchors[PROC_MAX_ANCHORS];          // Patrol centres
    HouseClass *            AnchorOwners[PROC_MAX_ANCHORS];
    int                     AnchorCount;
    int                     PatrolCount;
    TeamTypeClass *         PatrolTypes[PROC_MAX_PATROLS];
    CELL                    PatrolStart[PROC_MAX_PATROLS];
    int                     NextWaypoint;

    TeamTypeClass *         WaveTypes[PROC_MAX_WAVE_TYPES];
    long                    WaveStart[PROC_MAX_WAVE_TYPES];   // Elapsed when each wave was sent
    int                     WaveTypeCount;
};


/*******************************************************************************
 * GLOBALS
 ******************************************************************************/

extern ProcPresetLibrary ProcPresets;
extern ProcMissionManager ProcMission;

ProcSideType Proc_Side_Of(HousesType country);
ProcSideType Proc_Side_Of(HouseClass const * house);


#endif // PROCMISSION_H
