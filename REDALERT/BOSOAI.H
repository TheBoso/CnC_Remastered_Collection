//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

/*
** BOSOAI.H - Adaptive AI system for Red Alert.
**
** BosoAI takes over the human player's house and uses a runtime INI-based
** learning system to improve building and combat unit production decisions
** over multiple sessions via evolutionary mutation.
**
** Activation: add BosoAI=yes (and optionally BosoAISpectator=yes) to the
** [Basic] section of any scenario or skirmish map, or set Enabled=yes in
** BOSOAI_CONFIG.INI.
*/

#ifndef BOSOAI_H
#define BOSOAI_H

/*
** Forward declarations.
*/
class HouseClass;
enum StructType : char; // underlying type matches DEFINES.H typedef

/*---------------------------------------------------------------------------
** Shared string lengths
---------------------------------------------------------------------------*/
#define BOSO_MAP_LEN            32
#define BOSO_FACTION_LEN        16
#define BOSO_UID_LEN            13   // 12 hex chars + null terminator

/*---------------------------------------------------------------------------
** Game history log
---------------------------------------------------------------------------*/
#define BOSO_MAX_GAME_HISTORY   128  // Max game entries stored in [BosoGameHistory]

/*---------------------------------------------------------------------------
** INI file names
---------------------------------------------------------------------------*/
#define BOSO_INI_FILE           "BOSOAI.INI"        // learning data (auto-generated)
#define BOSO_CONFIG_FILE        "BOSOAI_CONFIG.INI" // user settings (never overwritten)

/*---------------------------------------------------------------------------
** Building build order constants
---------------------------------------------------------------------------*/
#define BOSO_MAX_BUILD_STEPS    32      // Max buildings recorded per session per house
#define BOSO_MAX_BUILD_ORDERS   128     // Max saved building build-order snapshots
#define BOSO_BUILD_STEP_LEN     32      // Max INI name length for a building type
#define BOSO_BUILD_ORDER_WIN    5       // Score delta on win
#define BOSO_BUILD_ORDER_LOSS   3       // Score delta on loss

/*---------------------------------------------------------------------------
** Combat (unit / infantry / vessel) build order constants
---------------------------------------------------------------------------*/
#define BOSO_MAX_COMBAT_STEPS   48      // More steps since units are built throughout the game
#define BOSO_MAX_COMBAT_ORDERS  128     // Max saved combat build-order snapshots
#define BOSO_COMBAT_STEP_LEN    16      // IniName is max ~8 chars; 16 is safe
#define BOSO_COMBAT_ORDER_WIN   5       // Score delta on win
#define BOSO_COMBAT_ORDER_LOSS  3       // Score delta on loss

/*---------------------------------------------------------------------------
** Shared evolution / exploration constants
---------------------------------------------------------------------------*/
#define BOSO_EXPLOIT_CHANCE     70      // % chance to follow best known order; rest → mutate

/*---------------------------------------------------------------------------
** Unit / building limit evolution constants
---------------------------------------------------------------------------*/
#define BOSO_MAX_LIMITS_RECORDS 64

/* Starting defaults when no training data exists */
#define BOSO_LIMITS_DEF_UNIT      40
#define BOSO_LIMITS_DEF_BUILD     20
#define BOSO_LIMITS_DEF_INF       30
#define BOSO_LIMITS_DEF_VESSEL    10
#define BOSO_LIMITS_DEF_AIRCRAFT  5
#define BOSO_LIMITS_DEF_REFINERY  3
#define BOSO_LIMITS_DEF_BARRACKS  3
#define BOSO_LIMITS_DEF_WAR       2
#define BOSO_LIMITS_DEF_DEFENSE   8
#define BOSO_LIMITS_DEF_AA        4
#define BOSO_LIMITS_DEF_TESLA     3
#define BOSO_LIMITS_DEF_HELIPAD   3
#define BOSO_LIMITS_DEF_AIRSTRIP  2

/* Per-field mutation deltas (±) */
#define BOSO_LIMITS_MUT_UNIT      10
#define BOSO_LIMITS_MUT_BUILD     5
#define BOSO_LIMITS_MUT_INF       10
#define BOSO_LIMITS_MUT_VESSEL    3
#define BOSO_LIMITS_MUT_AIRCRAFT  2
#define BOSO_LIMITS_MUT_PERTYPE   2    // for all per-type building limits

/* Hard minimums per field */
#define BOSO_LIMITS_MIN_UNIT      5
#define BOSO_LIMITS_MIN_BUILD     3
#define BOSO_LIMITS_MIN_INF       5
#define BOSO_LIMITS_MIN_VESSEL    0
#define BOSO_LIMITS_MIN_AIRCRAFT  0
#define BOSO_LIMITS_MIN_PERTYPE   1    // minimum 1 of each building type

/* Hard maximums per field */
#define BOSO_LIMITS_MAX_UNIT      200
#define BOSO_LIMITS_MAX_BUILD     80
#define BOSO_LIMITS_MAX_INF       200
#define BOSO_LIMITS_MAX_VESSEL    50
#define BOSO_LIMITS_MAX_AIRCRAFT  20
#define BOSO_LIMITS_MAX_REFINERY  10
#define BOSO_LIMITS_MAX_BARRACKS  10
#define BOSO_LIMITS_MAX_WAR       8
#define BOSO_LIMITS_MAX_DEFENSE   30
#define BOSO_LIMITS_MAX_AA        15
#define BOSO_LIMITS_MAX_TESLA     10
#define BOSO_LIMITS_MAX_HELIPAD   8
#define BOSO_LIMITS_MAX_AIRSTRIP  6

/* Attack interval (seconds between AI attacks) */
#define BOSO_LIMITS_DEF_ATTACK    120   // 2 minutes
#define BOSO_LIMITS_MIN_ATTACK    30    // 30 seconds
#define BOSO_LIMITS_MAX_ATTACK    600   // 10 minutes
#define BOSO_LIMITS_MUT_ATTACK    30    // ±30 seconds per mutation

/* BaseSizeAdd - how many buildings the build order is allowed to place before
**               defenses take over via vanilla urgency logic (not a total cap) */
#define BOSO_LIMITS_DEF_BASESIZE  30
#define BOSO_LIMITS_MIN_BASESIZE  10
#define BOSO_LIMITS_MAX_BASESIZE  80
#define BOSO_LIMITS_MUT_BASESIZE  5

/* BaseSpacing - how many of the 4 cardinal sides must have a 2-cell-wide clear corridor (2-4) */
#define BOSO_LIMITS_DEF_SPACING   3
#define BOSO_LIMITS_MIN_SPACING   2
#define BOSO_LIMITS_MAX_SPACING   4
#define BOSO_LIMITS_MUT_SPACING   1

/* Win/loss score deltas */
#define BOSO_LIMITS_WIN           5
#define BOSO_LIMITS_LOSS          3

/*---------------------------------------------------------------------------
** Time bonus — extra score added to winning records based on game speed.
** Linear decay: full bonus at or below MIN_SECS, zero at or above CAP_SECS.
---------------------------------------------------------------------------*/
#define BOSO_TIME_BONUS_MAX       10   // Extra points for fastest possible win
#define BOSO_TIME_BONUS_MIN_SECS  300  // ≤ 5 min  → full bonus
#define BOSO_TIME_BONUS_CAP_SECS  1800 // ≥ 30 min → no bonus

/*---------------------------------------------------------------------------
** Anti-air threat detection
---------------------------------------------------------------------------*/
#define BOSO_AA_LOSS_THRESHOLD  5       // Aircraft kills before forcing AA priority

/*---------------------------------------------------------------------------
** Stalemate detection — all-house economic deadlock.
** When every active BosoAI house is broke and missing either harvesters or a
** refinery for SECS consecutive seconds, the game is restarted
** without scoring (the data is discarded as uninformative).
---------------------------------------------------------------------------*/
#define BOSO_STALEMATE_SECS       60    // seconds of confirmed deadlock before restart
#define BOSO_STALEMATE_MONEY_CAP  500   // credits at/below which a house is "broke"

/*---------------------------------------------------------------------------
** BosoLimitsRecord - evolved unit/building count limits for one faction.
** Stored in [BosoLimits] section of BOSOAI.INI.
** Global counts are applied directly to HouseClass::Control.Max*.
** Per-type limits replace the vanilla Rule.*Limit caps in AI_Building.
---------------------------------------------------------------------------*/
struct BosoLimitsRecord {
    /* Global hard caps */
    int  MaxUnit;
    int  MaxBuilding;
    int  MaxInfantry;
    int  MaxVessel;
    int  MaxAircraft;
    /* Per-type building caps (replace Rule.*Limit for BosoAI houses) */
    int  MaxRefinery;
    int  MaxBarracks;
    int  MaxWarFactory;
    int  MaxDefense;
    int  MaxAA;
    int  MaxTesla;
    int  MaxHelipad;
    int  MaxAirstrip;
    /* Attack behaviour */
    int  AttackSecs;    // seconds between AI attack waves (applied as house->Attack)
    /* Base size cap */
    int  BaseSizeAdd;   // max buildings the AI will build in total
    /* Base spacing — how many sides (2-4) must have a 2-cell clear corridor */
    int  BaseSpacing;

    char Faction[BOSO_FACTION_LEN];
    int  WinScore;
    int  GamesPlayed;
    char LastGameUID[BOSO_UID_LEN]; // UID of the most recent game this record was scored in
    int  LastGameSecs;              // length of the most recent scored game (seconds)
    bool Active;
};

/*---------------------------------------------------------------------------
** BosoGameHistoryEntry - one entry per completed game.
** Stored in [BosoGameHistory] section of BOSOAI.INI.
---------------------------------------------------------------------------*/
struct BosoGameHistoryEntry {
    char UID[BOSO_UID_LEN];       // Unique game identifier (12 hex chars)
    int  GameSecs;                 // Game length in seconds
    char Outcome[4];               // "W" = win, "L" = loss, "S" = stalemate
    char Faction[BOSO_FACTION_LEN];
    char Map[BOSO_MAP_LEN];
};

/*---------------------------------------------------------------------------
** BosoBuildOrderRecord - snapshotted building construction sequence.
** Stored in [BosoBuildOrders] section of BOSOAI.INI.
** Steps are INI names of BuildingTypeClass (e.g. "POWR", "PROC").
---------------------------------------------------------------------------*/
struct BosoBuildOrderRecord {
    char Steps[BOSO_MAX_BUILD_STEPS][BOSO_BUILD_STEP_LEN];
    int  StepCount;
    char Faction[BOSO_FACTION_LEN];
    int  WinScore;      // positive = net wins, negative = net losses
    int  GamesPlayed;   // total games this order has been scored (for normalisation)
    char LastGameUID[BOSO_UID_LEN]; // UID of the most recent game this order was scored in
    int  LastGameSecs;              // length of the most recent scored game (seconds)
    bool Active;
};

/*---------------------------------------------------------------------------
** BosoCombatOrderRecord - snapshotted combat unit production sequence.
** Stored in [BosoCombatOrders] section of BOSOAI.INI.
** Steps are INI names of UnitTypeClass / InfantryTypeClass / VesselTypeClass
** (e.g. "MTNK", "E1", "SS").
---------------------------------------------------------------------------*/
struct BosoCombatOrderRecord {
    char Steps[BOSO_MAX_COMBAT_STEPS][BOSO_COMBAT_STEP_LEN];
    int  StepCount;
    char Faction[BOSO_FACTION_LEN];
    int  WinScore;
    int  GamesPlayed;   // total games this order has been scored (for normalisation)
    char LastGameUID[BOSO_UID_LEN]; // UID of the most recent game this order was scored in
    int  LastGameSecs;              // length of the most recent scored game (seconds)
    bool Active;
};

/*---------------------------------------------------------------------------
** BosoAIManagerClass - static singleton manager.
---------------------------------------------------------------------------*/
class BosoAIManagerClass {
public:

    /*-- State flags (set at Load_Config / On_Scenario_Load) --*/
    static bool        IsBosoActive;       // BosoAI enabled for this session
    static bool        IsSpectatorMode;    // Reveal map and block human input
    static bool        IsMultiHouseAI;     // Extend BosoAI to all skirmish AI houses
    static bool        IsAutoRestart;      // Auto-restart match on game over (for automated training)
    static bool        IsCostZero;         // Set all house CostBias=0 and give large credits (no economy)
    static bool        IsTraining;         // Training=yes: diverse orders + mutation; Training=no: all exploit best known order
    static HouseClass *BosoHouse;          // Primary managed house (= PlayerPtr at load time)

    /*-- Lifecycle --*/
    static void Init(void);
    static void Load_Config(void);
    static void On_Scenario_Load(void);
    static void On_Game_Over(bool player_won);

    /*-- Building order hooks (called from HOUSE.CPP AI_Building) --*/
    static void       On_Building_Queued(HouseClass const * house, StructType stype);
    static StructType Get_Next_Build_Order_Step(HouseClass const * house);

    /*-- Combat order hooks (called from HOUSE.CPP AI_Unit / AI_Infantry / AI_Vessel) --*/
    static void        On_Unit_Queued(HouseClass const * house, const char * ini_name);
    static const char* Get_Next_Combat_Step(HouseClass const * house);

    /*-- Stalemate detection (called from HOUSE.CPP AI_Unit, primary house only) --*/
    static void Check_Stalemate(void);

    /*-- Late registration for MultiHouseAI (called from HOUSE.CPP AI_Building) --*/
    static void Try_Register_House(HouseClass * house);

    /*-- PlayerPtr redirect: returns next alive BosoAI house when primary is defeated,
    **   or NULL if none. Called from DLLInterface.cpp after Set_Player_Context. --*/
    static HouseClass * Get_Next_Active_House(void);

    /*-- Returns true if at least one BosoAI house is still alive (not defeated).
    **   Used to determine the winner in FFA multi-house mode. --*/
    static bool Any_Boso_House_Alive(void);

    /*-- Anti-air threat tracking (called from TECHNO.CPP Record_The_Kill) --*/
    static void On_Unit_Killed_By_Aircraft(void);
    static int  AircraftLossCount;

    /*-- Naval viability cache (set once per scenario load, per house slot) --*/
    static bool NavalViable[8];

    /*-- INI persistence --*/
    static void Load_Data(void);
    static void Save_Data(void);

    /*-- Helpers --*/
    static bool Is_Naval_Viable(HouseClass * house);
    static bool Is_Spectator_Input_Blocked(void);
    static bool Is_Boso_House(HouseClass const * h);

    /*-- Limits accessor (used by HOUSE.CPP AI_Building for per-type caps) --*/
    static const BosoLimitsRecord & Get_House_Limits(HouseClass const * h);

private:

    /*-- Building order persistence --*/
    static BosoBuildOrderRecord BuildOrders[BOSO_MAX_BUILD_ORDERS];
    static int                  BuildOrderCount;

    /*-- Building order session tracking (primary house) --*/
    static char SessionBuildSteps[BOSO_MAX_BUILD_STEPS][BOSO_BUILD_STEP_LEN];
    static int  SessionBuildStepCount;
    static int  FollowBuildOrderIdx;
    static int  FollowBuildStepIdx;

    /*-- Building order session tracking (secondary houses 1-7) --*/
    static char ExtraHouseBuildSteps[7][BOSO_MAX_BUILD_STEPS][BOSO_BUILD_STEP_LEN];
    static int  ExtraHouseBuildStepCount[7];
    static int  ExtraHouseFollowBuildOrderIdx[7];
    static int  ExtraHouseFollowBuildStepIdx[7];

    /*-- Combat order persistence --*/
    static BosoCombatOrderRecord CombatOrders[BOSO_MAX_COMBAT_ORDERS];
    static int                   CombatOrderCount;

    /*-- Combat order session tracking (primary house) --*/
    static char SessionCombatSteps[BOSO_MAX_COMBAT_STEPS][BOSO_COMBAT_STEP_LEN];
    static int  SessionCombatStepCount;
    static int  FollowCombatOrderIdx;
    static int  FollowCombatStepIdx;

    /*-- Combat order session tracking (secondary houses 1-7) --*/
    static char ExtraHouseCombatSteps[7][BOSO_MAX_COMBAT_STEPS][BOSO_COMBAT_STEP_LEN];
    static int  ExtraHouseCombatStepCount[7];
    static int  ExtraHouseFollowCombatOrderIdx[7];
    static int  ExtraHouseFollowCombatStepIdx[7];

    /*-- Multi-house tracking (BosoHouse is always index 0) --*/
    static HouseClass * BosoHouses[8];
    static int          BosoHouseCount;

    /*-- Limit record persistence --*/
    static BosoLimitsRecord LimitsRecords[BOSO_MAX_LIMITS_RECORDS];
    static int              LimitsCount;
    /* Per-house limit selection (index into LimitsRecords[], -1 = none) */
    static int              FollowLimitsIdx[8];
    static BosoLimitsRecord HouseLimits[8];

    /*-- Game context --*/
    static char CurrentMap[BOSO_MAP_LEN];
    static char CurrentFaction[BOSO_FACTION_LEN];
    static char CurrentEnemyFaction[BOSO_FACTION_LEN];
    static bool GameOverFired;
    static long GameStartFrame;     // Frame at scenario load (for elapsed time calc)
    static int  GameTimeBonusScore; // Computed at game-over; added to winner scores
    static int  StalemateTicks;     // Consecutive seconds all houses have been deadlocked
    static long StaleCheckFrame;    // Frame of last stalemate check (throttle: once per second)

    /*-- Current game UID and length (set at game start / game over) --*/
    static char CurrentGameUID[BOSO_UID_LEN];
    static int  CurrentGameSecs;

    /*-- Game history log --*/
    static BosoGameHistoryEntry GameHistory[BOSO_MAX_GAME_HISTORY];
    static int                  GameHistoryCount;

    /*-- Internal: context detection --*/
    static void Detect_Context(HouseClass * house);

    /*-- Internal: game history --*/
    static void Record_Game_History(const char * uid, int secs, const char * outcome,
                                     const char * faction, const char * map);

    /*-- Internal: limits evolution --*/
    static void Score_Session_Limits(int slot, bool won);
    static int  Select_Limits(const char * faction, BosoLimitsRecord & out);
    static BosoLimitsRecord * Alloc_Limits(void);
    static int  Mutate_Limits(int parent_idx, const char * faction);
    static void Apply_Limits_To_House(HouseClass * house, int slot);
    static int  House_Slot(HouseClass const * h);

    /*-- Internal: building order evolution --*/
    static void Score_Session_Build_Order(bool won);
    static int  Select_Build_Order(void);
    static int  Select_Build_Order_Excluding(int * exclude, int exclude_count);
    static BosoBuildOrderRecord * Find_Build_Order(void);
    static BosoBuildOrderRecord * Alloc_Build_Order(void);
    static int  Mutate_Build_Order(int parent_idx);

    /*-- Internal: combat order evolution --*/
    static void Score_Session_Combat_Order(bool won);
    static int  Select_Combat_Order(void);
    static int  Select_Combat_Order_Excluding(int * exclude, int exclude_count);
    static BosoCombatOrderRecord * Find_Combat_Order(void);
    static BosoCombatOrderRecord * Alloc_Combat_Order(void);
    static int  Mutate_Combat_Order(int parent_idx);
};

/*---------------------------------------------------------------------------
** Global flag checked in CNC_Handle_Input to suppress orders in spectator mode.
** Lives in BOSOAI.CPP.
---------------------------------------------------------------------------*/
extern bool BosoSpectatorActive;

#endif // BOSOAI_H
